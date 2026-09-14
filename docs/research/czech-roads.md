# Czech road environment: research notes

These notes drive the sample map, the road geometry generator, the sign set and the traffic
rules. They combine web-search-verified facts (cited) with the project author's knowledge of
Czech roads, which is marked as such. Photographic reference sites were not reachable from the
development environment; the notes therefore describe appearance in words.

## Road classes and widths

- Czech public roads: dálnice (motorways), silnice I./II./III. třídy (state roads of classes
  I--III), místní komunikace (local roads), účelové komunikace (special-purpose roads, including
  forest roads). Class III roads have a typical carriageway of about 4--7.5 m and design speeds
  30--70 km/h (Czech Wikipedia "Silnice III. třídy" via search).
- Design standard ČSN 73 6101 (Projektování silnic a dálnic) defines categories such as
  S 7,5 (7.5 m crown: two 3.0 m lanes plus 0.25 m guide strips and shoulders) and S 9,5
  (two 3.5 m lanes) -- the common cross-sections for class II/III and class I roads
  respectively (author knowledge, consistent with the search summary that the standard defines
  element widths and folded the guide strip into the paved shoulder in its 2018 revision).
- Urban streets follow ČSN 73 6110: local streets 6.0--6.5 m between kerbs with 1.5--2.0 m
  sidewalks; kerb height about 12--15 cm; residential streets often 5.5 m.

Adopted for the sample map: main through-road S 7,5 (two 3.0 m lanes, 0.25 m edge strips,
0.5 m unpaved shoulders, 90 km/h outside town); collector streets 6.5 m (50 km/h in town);
residential streets 5.5 m; forest road 3.5--4.0 m gravel/asphalt single track with passing
places (30 km/h expected).

## Road markings (TP 133 "Zásady pro vodorovné dopravní značení")

- V 1a solid centre line, 0.125 m wide (author knowledge; TP 133 confirmed as the governing
  guideline via search).
- V 2a/V 2b dashed centre lines: V 2b pattern 3 m line / 6 m gap (1.5/1.5 on urban roads as
  V 2a) 0.125 m wide.
- V 4 edge line 0.125 m (0.25 m on motorways); on class II/III roads the edge line is common but
  many class III roads have no markings at all.
- V 5 stop line 0.5 m wide; V 6a give-way triangles; V 7 pedestrian crossing: 0.5 m white bars
  with 0.5 m gaps, 3--4 m long.
- Colours: white; yellow only for temporary works or parking prohibitions (V 12).

## Signs (vyhláška č. 294/2015 Sb., in force since 1 January 2016)

The sign catalogue lives in příloha 1 of the decree (confirmed via search). Subset needed by the
sample map (author knowledge of the catalogue):

- P 1 Křižovatka s vedlejší pozemní komunikací, P 2 Hlavní pozemní komunikace (yellow diamond),
  P 3 Konec hlavní komunikace, P 4 Dej přednost v jízdě! (inverted triangle, red border),
  P 6 Stůj, dej přednost v jízdě! (octagonal STOP).
- B 20a Nejvyšší dovolená rychlost (red-bordered circle, black digits), B 20b end.
- IZ 4a Obec (white rectangle, black town name, black frame), IZ 4b Konec obce (same with a red
  diagonal strike). The `Obec` sign implies 50 km/h.
- IS 3a--IS 3d directional signs (white background, black text for other roads; class I/II
  numbers in blue rectangles), IS 12a/12b village boundary signs.
- IP 6 Přechod pro chodce (blue square with pedestrian pictogram), IJ 4c Zastávka autobusu.
- A 22 Jiné nebezpečí, A 7a Nerovnost vozovky, A 12a Chodci, A 14 Zvířata (deer) near forests.
- Z 11a/Z 11b směrové sloupky: white delineator posts with a black band and an orange (right
  side) or white (left side) retro-reflector; spacing about 50 m on straights.

Sign geometry: standard sizes are 700 mm (triangles 900 mm, circles 700 mm) in the "basic"
size; town signs about 1000 x 500 mm. The project generates sign faces procedurally from these
shapes.

## Appearance of Czech roads (descriptive)

- Asphalt in tones from dark grey (fresh) to light grey (weathered), frequent patch repairs,
  longitudinal cracks and crack sealing; concrete kerbs in towns; gravel or grass shoulders
  outside towns with a soft edge.
- Class II/III roads outside towns are commonly lined with **avenues of fruit trees** (apple,
  cherry, plum) or lime trees planted 8--15 m apart, low grass verges, drainage ditches, and
  fields (rapeseed yellow in spring, wheat gold in summer, brown when ploughed) or meadows.
- Villages: the road narrows between front gardens and fences; family houses with **gabled or
  hipped roofs of red/brown ceramic tiles**, plastered facades in white, beige, ochre, pale
  yellow, pale green or pink; older houses have the gable towards the street; wooden or metal
  fences with gates; a bus stop shelter, a chapel or church, a small shop, a fire station
  (hasičská zbrojnice), a football pitch at the edge.
- Towns: 2--4 storey historic houses around a square (náměstí) with arcades, plastered facades
  with cornices and window surrounds, ground-floor shops; residential districts of family
  houses; prefabricated apartment blocks (paneláky) in pastel insulation colours; cobbled areas
  near the square; concrete street lamps with a single arm; overhead telephone/electric cables
  on wooden or concrete poles in villages.
- Forests: spruce and pine plantations with straight trunks and dark canopies, mixed with
  beech and oak at edges; forest roads are asphalt or compacted gravel, 3--4 m wide, with
  barrier gates (závora) at entrances and stacked timber; light dappled through the canopy.
- Traffic drives on the **right**; priority to the right at unmarked intersections; main-road
  priority marked with P 2 / P 4.

## Sample map concept (fictional): "Lipová" and surroundings

A fictional small town "Lipová" (about 1.2 km across) with a square, a through class II road,
residential streets, a short prefab estate, a bus stop and a church; the through road leaves
town past fields with a tree avenue, curves through meadows, enters a spruce forest, and a
minor forest road branches off and loops back to town via a village edge, forming a loop of
about 6 km with several intersections of different priority types.
