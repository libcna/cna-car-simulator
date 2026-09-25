# Czech overtaking rules represented in the simulator

Sources checked 2026-09-25: [Road Traffic Act 361/2000 Sb., §17 in e-Sbírka](https://e-sbirka.gov.cz/sb/2000/361),
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
pass that would run through a crossing. A sign can name its road with `"road"` where two nearby
roads make a nearest-road lookup ambiguous. The zebra geometry, pedestrian crossing and passing veto share that binding;
an unknown road ID fails map validation. The two existing IP 6 signs on `main` use an explicit
binding, and a parallel-road regression checks that a crossing on the side road does not block
the main road.

Local marking and sight-distance zones are authored as ordered `centreLineSections`, each with
`fromM`/`toM` measured along the smoothed road curve from its first node, a `centreLine` value
and an optional independent `noOvertaking` restriction. The road mesh and planner read the
same sections. A pass is rejected if any restricted interval lies within its estimated passing
and return distance. `main` now has a 2750–3090 m solid/no-overtaking section around the E3
junction approach; the fixed before/after view is in `docs/screenshots/phase14/`.
For a sign or crest applying to one approach only, a section can use
`noOvertakingForward` or `noOvertakingReverse`. These flags leave the centre-line paint unchanged
and restrict only the named direction. Parser and traffic regressions cover a dashed road with
one restricted direction while a car in the opposite direction can still pass. Four deterministic
oncoming interruptions now check both completing ahead and aborting behind the lorry, a settled
return to the lane, no return-state oscillation and no vehicle-body overlap over 30 seconds.
The decree calls B 21a "Zákaz předjíždění" and B 21b "Konec zákazu předjíždění".
The simulator paints both faces procedurally. The passing planner also projects B 21a/B 21b
onto the bound or nearest road, checks which direction the plate faces, and treats B 21a as a
ban until B 21b or the next junction. A repeat B 21a after a junction starts a new ban.
Six roadside plates bracket the E3 restriction:
one start, one repeat after the junction and one end for each direction of travel. The repeat
follows the decree's general rule that a sign prohibition ends at the nearest junction.
Their map positions are checked against the restricted curve interval in `SampleMap` tests.
The independent interval is still enforced even without a vertical sign. B 21a's motorcycle
exception is irrelevant to the current AI population, which has no motorcycles.

The pass planner now also traces a driver-height line of sight over sampled road elevations
for the whole estimated passing path. A crest blocks initiation when the road surface hides the
far point, while a flat road remains passable. The authored elevation nodes and generated heights
already give the sample map both open and crest-limited 180 m windows; synthetic flat/crest
traffic regressions verify the planner outcome. Curvature, weather visibility and oncoming
clearance remain separate checks.
