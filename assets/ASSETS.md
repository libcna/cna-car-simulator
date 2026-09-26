# Asset provenance

Every non-generated asset used by cna-car-simulator is recorded in `assets/manifest.json` with
its original title, author, source URL, licence (SPDX), retrieval date, the local files derived
from it (with SHA-256), the modifications applied and the attribution text required. This file
summarises the manifest for humans; `scripts/check_assets.py` checks both (CTest `asset_manifest_check`).

Generated assets (textures, meshes, the remaining sound layers, plate faces and sign faces)
are produced by project code and are covered by the repository's MIT licence; they are not
listed here.

## External assets

| Local name | Title | Author | Source | Licence | Attribution required | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| `font-d-din` | D-DIN, D-DIN Bold, D-DIN Condensed Bold (TrueType) | Datto Inc. / Monotype | https://github.com/amcchord/datto-d-din @ e199c844 (2026-09-14) | SIL OFL 1.1 (`assets/external/fonts/d-din/OFL-1.1.txt`) | yes: "D-DIN by Datto Inc., SIL Open Font License 1.1" | rasterised to glyph atlases in `content/fonts/` by `tools/fontatlas.py`; used for HUD, dashboard digits and plate characters |
| `honda-civic-2012-engine` | Honda Engine Start and Rev.wav (2012 Honda Civic) | thepodcastdoctor | https://freesound.org/people/thepodcastdoctor/sounds/487553/ (2026-09-25) | CC0 1.0 | no | retained HQ MP3 preview and a checked 44.1 kHz stereo PCM16 start/idle derivative; used for startup and low RPM |
| `mini-cooper-s-engine-load` | Car Engine Contact Recording-Mini Cooper S 2019.wav | TheLittleCrow | https://freesound.org/people/TheLittleCrow/sounds/669618/ (2026-09-25) | CC0 1.0 | no | retained HQ MP3 preview and a checked four-second 44.1 kHz stereo PCM16 derivative; used for mid/high RPM load |

## Evaluated and rejected sources (not used)

| Source | Reason |
| --- | --- |
| Sketchfab Škoda models | account-gated downloads, licence per model unverifiable from this environment, several NC/ND |
| KhronosGroup/glTF-Sample-Assets `CarConcept` | CC-BY-4.0 but a futuristic concept car unsuited to a Czech passenger-car simulator |
| KhronosGroup/glTF-Sample-Assets `ToyCar` | CC0 but a toy car |
| Poly Haven, Quaternius, Kenney, Poly Pizza, ambientCG, Objaverse | host unreachable during the 2026-09-14 asset pass; not verified, not used |
| Other engine previews reviewed for Phase 14 | The Fiat Punto, Saturn Vue and other candidate recordings were rejected in listening; the selected Honda and Mini sources are listed above (see `docs/audio-previews/phase14-recorded-source-review.md`) |
