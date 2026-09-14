# API boundary: A + P only

The simulator is built strictly on:

- **Tier A** -- the XNA 4.0-compatible public API of CNA (`Microsoft::Xna::Framework::*`,
  restricted to the 544 public types listed in `scripts/xna4_types.txt`, generated from
  [libcna/xna4-spec](https://github.com/libcna/xna4-spec)), plus Sharp Runtime (`System::*`).
- **Tier P** -- project-owned code in this repository (`simulator/`, `tools/`, `tests/`).

**Tier C is prohibited**: CNAEXT (`CNA_CNAEXT`, `graphics-ext`), any `EXT`-suffixed member or
type, `CNA/*` headers (logger, capability queries, renderer selection), `Internal/` headers,
renderer implementations (EasyGL, meta-gl, RLGL, Vulkan, Direct3D, SDL_GPU, ...), platform
libraries (SDL, OpenGL, Vulkan, X11) and any query of the currently selected renderer.

## Enforcement

1. `scripts/check_xna_only.py` scans every source file under `simulator/`, `tools/` and
   `tests/` (comments and string literals stripped) and fails on:
   - includes of `CNA/`, `Internal/`, `CnaExt`, SDL, GL/GLES, Vulkan, D3D, easygl/metagl/rlgl,
     GL loaders;
   - `Microsoft/Xna/Framework/...` includes whose type is not an XNA 4.0 public type;
   - identifiers ending in `EXT`, `CNAEXT`, `CnaExt`, `CNA::`, `ShaderEffect`, `PbrEffect`,
     `SkinnedPbrEffect`, `ColorMatrixEffect`, `AnimationPlayer`, `GetGraphicsRendererName`,
     `GetGraphicsRendererType`, `SupportsCapability`, `GraphicsCapability`,
     `RendererCapabilityProfile`, `GetMaxTextureDimension`, the `SetDepthTestEnabled` family,
     `SetCurrentEffect`, renderer class names, `SDL_*`, raw `gl*(`/`vk*(` calls;
   - local includes other than `CarSim/`, `System/`, `SharpRuntime/`, `gtest/`.
   It runs as the CTest `xna_only_api_check` and can be run directly:
   `python3 scripts/check_xna_only.py --root .`
2. `CMakeLists.txt` forces `CNA_CNAEXT=OFF` and fails the configure if it is ON.
3. Code review rule: a new `Microsoft/Xna/Framework` header may be used only if the XNA 4.0
   type exists; a CNA-only convenience (even inside an XNA header) is not used. When the XNA API
   lacks a feature, it is implemented in Tier P (see `docs/framework-findings.md` section 10).

## Known accepted usages

- `Texture2D::SaveAsPng(System::IO::Stream*, int, int)` -- XNA 4.0 signature, stream from
  Sharp Runtime.
- `ContentManager::Load<Model>("name")` -- XNA 4.0 call; the fact that CNA resolves `.glb` is
  an implementation detail the project does not depend on (the model pipeline also produces
  CNB through `cna-content`).
- `DrawInstancedPrimitives` with a second vertex stream of `BlendWeight` `Vector4` elements
  -- XNA 4.0 HiDef API; the element usage is what CNA's stock effects read.
- Range-based iteration is avoided on CNA collections whose `begin()` is marked CNAEXT
  (effect passes are indexed with `getCountProperty()`/`operator[]`).
