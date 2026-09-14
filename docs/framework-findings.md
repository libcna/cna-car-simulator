# Framework findings: CNA "next" and Sharp Runtime "next"

Inspection date: 2026-09-14. Checkouts: `libcna/cna` at `e05b3d0` (branch `next`),
`libcna/sharp-runtime` at `0c82d9b` (branch `next`). This document records what the frameworks
actually provide, as opposed to what XNA 4.0 / MonoGame / FNA documentation might suggest. It is
the factual basis for `docs/api-boundary.md` and for the rendering strategy in `plan.md`.

## 1. What CNA is

- A C++23 reimplementation of the XNA 4.0 programming model (`Microsoft::Xna::Framework::*`)
  on SDL3 with a pluggable renderer layer (50 public renderer identities; Linux default
  `OPENGLES3`, implemented by the `easy-gl`/`meta-gl` sibling checkouts).
- Consumed with `add_subdirectory()`; CNA exports no CMake package. Sharp Runtime is located
  through `CNA_SHARP_RUNTIME_ROOT` (default `../sharp-runtime`).
- Physically modular: `modules/<name>/{include,src,tests}`. The public XNA surface lives in
  `modules/*/include/Microsoft/Xna/Framework/...`. The umbrella target `CNA` links every module
  plus the selected renderer.
- The extended engine layer (`CNA_CNAEXT`, module `graphics-ext`, `CNA::Graphics` namespace,
  `PbrEffect`, `CascadedShadowMap`, `RenderPipeline`, instanced/LOD helpers, sky, IBL ...) is
  **OFF by default** and is not used by this project (see `docs/api-boundary.md`).

## 2. C++ conventions

- C# properties become `getXProperty()` / `setXProperty(value)`.
- Static XNA members keep their names (`Matrix::CreateLookAt`, `Color::CornflowerBlue`,
  `Vector3::Up`, `BlendState::Opaque`, `DepthStencilState::Default`,
  `RasterizerState::CullCounterClockwise`, `SamplerState::AnisotropicWrap`).
- Non-XNA additions carry the `CNAEXT` macro and/or an `EXT` suffix. Those are the markers the
  project's static checker (`scripts/check_xna_only.py`) rejects.
- Collections expose `getCountProperty()` and `operator[]`; range-for `begin()`/`end()` on
  effect pass collections is marked `CNAEXT`, so the project iterates by index.
- `Game` is heap-allocated and never freed on Emscripten; the project follows the same pattern
  everywhere (harmless on desktop).

## 3. Graphics: what the XNA-compatible surface provides

Verified by reading the headers under `modules/graphics/include/Microsoft/Xna/Framework/Graphics`.

| Area | Status | Notes |
| --- | --- | --- |
| `GraphicsDevice` | present | `Clear`, `Present`, `SetRenderTarget(s)`, `GetBackBufferData`, `SetVertexBuffer(s)`, `setIndicesProperty`, `DrawPrimitives`, `DrawIndexedPrimitives`, `DrawInstancedPrimitives`, `DrawUserPrimitives`, `DrawUserIndexedPrimitives`, blend/depth-stencil/rasterizer/sampler states, viewport, scissor. |
| `BasicEffect` | present, pixel-verified upstream | 3 directional lights, `PreferPerPixelLighting`, specular, emissive, fog, texture, vertex color. |
| `DualTextureEffect` | present | Unlit `texture1 * texture2 * 2` blend (FNA formula). Independent `TEXCOORD0`/`TEXCOORD1` fixed upstream (SAMPLE-073) on EasyGL using a **40-byte** position/normal/uv0/uv1 declaration. |
| `AlphaTestEffect` | present | All compare functions verified. |
| `EnvironmentMapEffect` | present | Cube-map reflections, Fresnel, `EnvironmentMapAmount`. |
| `SkinnedEffect` | present | Not needed by this project. |
| `Effect(GraphicsDevice&, bytes)` | present but **unusable here** | Requires compiled Direct3D 9 Effect Framework bytecode (`.fxb`/XNB) and a renderer-family compile flag (`CNA_EASYGL_COMPILED_EFFECTS`, ...). CNA embeds no HLSL compiler; `.fx` needs an external `fxc` (Wine). No compiler is available in the development environment, so **custom shaders are out of scope**. |
| `ShaderEffect`, `PbrEffect`, `SkinnedPbrEffect`, `ColorMatrixEffect` | present, **prohibited** | CNA-specific (EXT) effects; not XNA 4.0. |
| `RenderTarget2D`, `RenderTargetCube` | present, no open gaps upstream | Used for the rear-view mirror and dashboard displays. |
| `Texture2D` | present | `SetData` for `Color`, packed formats, bytes; `SaveAsPng(System::IO::Stream*, w, h)` (the filename overload is EXT). Mip levels must be supplied by the application. |
| `TextureCube` | present | `SetData(face, ...)`; used for the car-paint environment map. |
| `VertexBuffer`/`IndexBuffer`/`Dynamic*` | present | Typed `SetData<T>` templates for trivially copyable vertex structs; 16- and 32-bit indices. |
| `VertexDeclaration`/`VertexElement` | present | Constructible with `{VertexElement...}`. |
| `SpriteBatch`, `SpriteFont` | present | `SpriteFont` cannot be constructed at runtime through the XNA API; it comes from the content pipeline (`.spritefont` route needs FreeType at build time). |
| `Model`, `ModelMesh`, `ModelMeshPart`, `ModelBone` | present | `Draw`, `CopyAbsoluteBoneTransformsTo`, per-part `Effect` replacement (the standard XNA pattern). Content pipeline imports glTF into CNB Models; the runtime can also load `.gltf/.glb` directly through `ContentManager`. PBR glTF materials import as `PbrEffect` (EXT) parts, so any imported model must have its part effects replaced by stock effects in project code. |
| `BoundingFrustum`, `BoundingBox`, `BoundingSphere`, `Ray`, `Plane` | present | `Matrix::CreateShadow(lightDirection, plane)` exists (planar projected shadows). |
| `OcclusionQuery` | present | Not used. |

### 3.1 Vertex layouts are selected by stride

All renderers select the GPU attribute layout from the **byte stride** of the bound vertex
buffer, not from the `VertexDeclaration` elements (`docs/vertex-format-support.md` in CNA).
Hardcoded layouts: 16 (`VertexPositionColor`), 20 (`VertexPositionTexture`),
24 (`VertexPositionColorTexture`), 32 (`VertexPositionNormalTexture`), 40 (position, normal,
two UV sets, for `DualTextureEffect`), 52 (skinned), plus glTF/PBR strides (48, 60, 68, 76, 80)
that belong to EXT effects. Any other stride falls back to position-only (EasyGL) or is skipped
(Vulkan). **Consequence:** the project uses only the four XNA vertex structs and the 40-byte
dual-texture layout. There is no lit vertex-colour layout (Position+Normal+Color+Texture = 36
bytes is unsupported), so per-vertex baked lighting goes through the second texture coordinate
of the dual-texture layout instead of through vertex colours.

### 3.2 Instancing

`DrawInstancedPrimitives` with a second `VertexBufferBinding(instanceBuffer, 0, 1)` is standard
XNA 4.0 HiDef API. CNA's stock effects consume an instance stream of four `Vector4`
`BlendWeight` elements (usage indices 0..3) forming a world matrix (see
`modules/graphics/examples/instanced_textured_draw_test.cpp`); verified upstream on EasyGL and
Vulkan. The project uses this for vegetation and small props after a runtime conformance probe.

### 3.3 Capability queries

`GraphicsDevice::SupportsCapability(CNA::GraphicsCapability)`, `GetGraphicsRendererName()` and
the capability profiles are CNA-specific. The project does not call them; it relies on the
XNA contract (`GraphicsProfile::HiDef` features) and treats a renderer that cannot fulfil it
as unsupported, reporting the exception message.

## 4. Audio

- `SoundEffect`, `SoundEffectInstance` (volume, pan, pitch in the XNA -1..1 octave range,
  looping) and `DynamicSoundEffectInstance` (`SubmitBuffer(bytes)`, `getPendingBufferCountProperty`,
  `BufferNeeded` event) are present. The float-buffer overload is EXT and not used.
- `SoundEffect` can be constructed from PCM bytes (`SoundEffect(buffer, sampleRate, channels)`),
  so procedurally synthesised sounds need no asset files.
- Audio platform is SDL3 (`CNA_AUDIO_PLATFORM`); the dummy SDL audio driver keeps headless
  runs working.

## 5. Input

`Keyboard::GetState()`, `KeyboardState::IsKeyDown/IsKeyUp/GetPressedKeys`, `Keys` (XNA
virtual-key values), `Mouse::GetState()`, `GamePad::GetState/GetCapabilities` are present. All
`*EXT` helpers (scancode names, gyro, touchpad, light bar) are ignored.

## 6. Game loop, window, content

- `Game`: `Run`, `Tick`, `Exit`, `IsFixedTimeStep`, `TargetElapsedTime`, `IsMouseVisible`,
  `Window` (`setTitleProperty`, `getClientBoundsProperty`, `setAllowUserResizingProperty`),
  `Components`, `Services`, `LoadContent/Update/Draw/UnloadContent`.
- `GraphicsDeviceManager`: preferred back-buffer size/format/depth format, full screen,
  multisampling preference, vertical retrace synchronisation, `ApplyChanges`, `ToggleFullScreen`.
- `ContentManager::Load<T>()` resolves `.xnb`, `.cnb`, `.cnj`, loose images/`.wav`, and
  `.gltf/.glb` (direct glTF loading is a CNA extension of the *content* path but is reached
  through the standard `Load<Model>` call). The project keeps its own data files (JSON maps,
  vehicle definitions, generated textures) and loads them through Sharp Runtime and its own
  loaders, using `ContentManager` only where XNA would (textures, models, sounds).
- `Texture2D::SaveAsPng` + `GraphicsDevice::GetBackBufferData` allow screenshots through the
  XNA API; this is how visual verification under Xvfb works in this project. `GetBackBufferData`
  (like render targets with depth, 32-bit indices and instancing) requires
  `GraphicsProfile::HiDef`, which the simulator requests through `GraphicsDeviceManager`; on the
  Reach default it throws "not supported by the Reach graphics profile".
- Verified 2026-09-14: the skeleton runs on `OPENGLES3` (EasyGL over Mesa llvmpipe "OpenGL ES
  3.2") under Xvfb with `SDL_AUDIODRIVER=dummy`, and the captured PNG shows the lit, textured
  test scene. Front faces are clockwise as seen by the camera (XNA convention,
  `RasterizerState::CullCounterClockwise` culls counter-clockwise triangles).

## 7. Content pipeline

`cna-content` (module `content-pipeline`) builds CNB from images, WAV, glTF, `.spritefont`
(FreeType), `.cnj`, `.xnb`, `.fxb`/`.fx` (XNB only, needs `fxc`). CMake helper
`cna_add_content()` exists. glTF import maps metallic-roughness materials to `PbrEffect`
(EXT) or to `BasicEffect` for unlit/simple materials; multi-group files need
`generateChildAssets`. The project's model import strategy therefore keeps a project-owned
step that assigns stock effects per material (`docs/asset-pipeline.md`).

## 8. Build facts (Linux)

- CMake >= 3.23 (easy-gl/meta-gl need FILE_SET); C++23 compiler (GCC 13 works).
- CNA vendors SDL3, SDL_image, SDL_mixer as submodules (`git submodule update --init
  third_party/SDL third_party/SDL_image third_party/SDL_mixer`); googletest under
  `vendor/googletest`.
- FFmpeg is optional (`CNA_ENABLE_VIDEO=AUTO/OFF`); the project turns video off.
- The five GL renderers need `../easy-gl` and `../meta-gl`. `SOFTWARE`, `HEADLESS`, `STUB`,
  `TINYGL`, `PORTABLEGL` need no GPU. Under Xvfb, Mesa llvmpipe provides OpenGL/OpenGL ES 3
  contexts, which is how the simulator is exercised in the headless development environment.
- Upstream quirk noted by cna-template (a consumer `src/` directory tripping CNA's layout
  validator) is fixed on `next` (the validator is anchored on `CNA_SOURCE_DIR`); the project
  still keeps sources under `simulator/` for clarity.

## 9. Sharp Runtime

- 41 CMake components; CNA's default closure is `Core.Base IO Collections.Core
  Collections.ObjectModel Runtime Threading Text Globalization Storage Security.Cryptography
  Xml`. The project adds `Text.Json` (`System::Text::Json::JsonDocument`, `JsonSerializer`,
  `Utf8JsonWriter`, `Nodes`).
- `System::IO::FileStream(path, FileMode)` provides the `Stream` needed by `SaveAsPng`.
- `System::TimeSpan::FromSeconds/FromMilliseconds` drive `Game::TargetElapsedTime`.
- There is no SQLite component (relevant to the map-format decision in `docs/map-format.md`).

## 10. Consequences for this project

1. Rendering uses the five stock effects, render targets, states and instancing only;
   shadows are baked (static) and planar-projected (vehicles); car paint uses the
   environment-map effect. Normal mapping is not available and is documented as a limitation.
2. Geometry is authored/generated with the four XNA vertex structs and the 40-byte dual-UV
   layout.
3. Text is drawn with a project-owned bitmap font through `SpriteBatch`, avoiding a build-time
   font pipeline dependency.
4. All engine, tyre, indicator, horn and collision sounds are synthesised in project code and
   played through `DynamicSoundEffectInstance`/`SoundEffect`.
5. Every source file is checked by `scripts/check_xna_only.py` against the XNA 4.0 type list
   generated from `libcna/xna4-spec`.
