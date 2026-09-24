# Czech overtaking rules represented in the simulator

Sources checked 2026-09-24: [Road Traffic Act 361/2000 Sb., §17 in e-Sbírka](https://e-sbirka.gov.cz/sb/2000/361),
[implementing Decree 294/2015 Sb. in e-Sbírka](https://e-sbirka.gov.cz/sb/2015/294), and
[BESIP's overtaking guidance](https://besip.gov.cz/Clanky/Predjizdeni). The implemented model is
a conservative simulation subset, not a claim of exhaustive legal interpretation.

The law requires enough sight distance, a safe return gap, and no danger or obstruction to
oncoming traffic. It forbids overtaking on or immediately before a pedestrian crossing and a
railway crossing; junction restrictions have exceptions (including priority roads and controlled
junctions). Traffic here already uses oncoming, curvature and junction-distance checks. The AI
keeps its conservative junction veto even where a legal exception might exist. No railway
crossings are currently authored on this map.

The decree's V 1a continuous centre line cannot normally be crossed for a motor-vehicle pass.
V 3 pairs a continuous line with a broken line: only traffic on the broken-line side can cross.
`RoadSpec::centreLine` drives both `RoadMeshBuilder` and overtake initiation. The optional
`noOvertaking` road field represents a restriction independent of the paint. The existing IP 6
sign placements generate the V 7 zebra markings and now also prevent an AI car from starting a
pass that would run through a crossing. These are road-wide or point semantics; locally changing
markings and sight-distance zones are now authored as ordered `centreLineSections`, each with
`fromM`/`toM` measured along the smoothed road curve from its first node, a `centreLine` value
and an optional independent `noOvertaking` restriction. The road mesh and planner read the
same sections. A pass is rejected if any restricted interval lies within its estimated passing
and return distance. `main` now has a 2750–3090 m solid/no-overtaking section around the E3
junction approach; the fixed before/after view is in `docs/screenshots/phase14/`.
B 21a/B 21b roadside signs and more sight-distance authoring remain Phase 14 work.
