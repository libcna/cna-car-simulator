# Asset provenance

Every non-generated asset used by cna-car-simulator is recorded in `assets/manifest.json` with
its original title, author, source URL, licence (SPDX), retrieval date, the local files derived
from it (with SHA-256), the modifications applied and the attribution text required. This file
summarises the manifest for humans; `tools/validate_assets.py` checks both.

Generated assets (textures, meshes, sounds, plate faces, sign faces) are produced by project
code and are covered by the repository's MIT licence; they are not listed here.

## External assets

| Local name | Title | Author | Source | Licence | Attribution required | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| _(none yet)_ | | | | | | |

## Evaluated and rejected sources (not used)

| Source | Reason |
| --- | --- |
| Sketchfab Škoda models | account-gated downloads, licence per model unverifiable from this environment, several NC/ND |
| KhronosGroup/glTF-Sample-Assets `CarConcept` | CC-BY-4.0 but a futuristic concept car unsuited to a Czech passenger-car simulator |
| KhronosGroup/glTF-Sample-Assets `ToyCar` | CC0 but a toy car |
| Poly Haven, Quaternius, Kenney, Poly Pizza, ambientCG, OpenGameArt, Freesound, Objaverse | host unreachable from the development environment; not verified, not used |
