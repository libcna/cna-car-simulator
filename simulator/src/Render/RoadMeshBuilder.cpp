#include "CarSim/Render/RoadMeshBuilder.hpp"

#include "CarSim/Core/Noise.hpp"

#include <algorithm>
#include <cmath>

namespace CarSim::Render
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;

    namespace
    {
        constexpr float kAsphaltTileM = 4.0f;    // texture repeat along the road
        constexpr float kGravelTileM = 2.0f;
        constexpr float kPavingTileM = 0.9f;
        constexpr float kGrassTileM = 7.0f;      // must match the terrain's grass tiling
        constexpr float kRowSpacingM = 2.5f;     // maximum distance between strip rows
        constexpr float kLineWidth = 0.125f;
        const Color kWhite(255, 255, 255, 255);

        struct Row
        {
            Vector3 centre;      // centreline point (crown height)
            Vector3 right;       // unit right vector in the road plane
            Vector3 up;          // surface normal (approximate)
            float s = 0.0f;
            bool urban = false;
            float crownScale = 1.0f;
        };

        using ColourFn = std::function<Color(const Row&, float)>;

        Color Grey(const float g, const int alpha = 255)
        {
            const int v = static_cast<int>(std::clamp(g, 0.0f, 1.0f) * 255.0f + 0.5f);
            return Color(v, v, v, alpha);
        }

        /// Slow along-road variation of the surface tone (-1..1), seeded per road.
        float ToneNoise(const float s, const unsigned seed)
        {
            return Core::Noise::FbmSigned(s * 0.045f, static_cast<float>(seed % 97u) * 1.7f, 3, 0.5f, 300u + seed);
        }

        /// Adds a quad strip between two lateral offsets over consecutive rows; heights come from
        /// `heightAt`, vertex colours (surface wear) from `colour` when given.
        template <typename HeightFn>
        void AddStrip(MeshData& mesh, const std::vector<Row>& rows, const float latA, const float latB, const HeightFn& heightAt,
                      const float uA, const float uB, const float tileM, const float lift = 0.0f, const ColourFn& colour = {})
        {
            if (rows.size() < 2) {
                return;
            }
            std::vector<std::uint32_t> prev(2);
            for (std::size_t i = 0; i < rows.size(); ++i) {
                const Row& r = rows[i];
                const float v = r.s / tileM;
                const Vector3 pa = r.centre + r.right * latA + Vector3(0.0f, heightAt(r, latA) - r.centre.Y + lift, 0.0f);
                const Vector3 pb = r.centre + r.right * latB + Vector3(0.0f, heightAt(r, latB) - r.centre.Y + lift, 0.0f);
                const std::uint32_t ia = mesh.AddVertex(pa, r.up, Vector2(uA, v), colour ? colour(r, latA) : kWhite);
                const std::uint32_t ib = mesh.AddVertex(pb, r.up, Vector2(uB, v), colour ? colour(r, latB) : kWhite);
                if (i > 0) {
                    // Seen from above (+y) with x to the right, z points down the screen and +s runs
                    // up it: left-bottom, right-bottom, right-top, left-top is counter-clockwise, which
                    // is the authoring convention of MeshData::AddQuad.
                    mesh.AddQuad(prev[0], prev[1], ib, ia);
                }
                prev[0] = ia;
                prev[1] = ib;
            }
        }

        /// Vertical face between two heights at one lateral offset (kerb face).
        void AddVerticalFace(MeshData& mesh, const std::vector<Row>& rows, const float lat, const float yLowAbove, const float yHighAbove,
                             const bool facesRight, const float tileM)
        {
            if (rows.size() < 2) {
                return;
            }
            std::uint32_t prevLow = 0;
            std::uint32_t prevHigh = 0;
            for (std::size_t i = 0; i < rows.size(); ++i) {
                const Row& r = rows[i];
                const Vector3 base = r.centre + r.right * lat;
                const Vector3 n = facesRight ? r.right : r.right * -1.0f;
                const float v = r.s / tileM;
                const std::uint32_t low = mesh.AddVertex(base + Vector3(0.0f, yLowAbove, 0.0f), n, Vector2(0.0f, v), kWhite);
                const std::uint32_t high = mesh.AddVertex(base + Vector3(0.0f, yHighAbove, 0.0f), n, Vector2(0.15f, v), kWhite);
                if (i > 0) {
                    if (facesRight) {
                        mesh.AddQuad(prevLow, low, high, prevHigh);
                    } else {
                        mesh.AddQuad(prevLow, prevHigh, high, low);
                    }
                }
                prevLow = low;
                prevHigh = high;
            }
        }
    }

    RoadPieceMeshes RoadMeshBuilder::BuildPiece(const Map::RoadPiece& piece) const
    {
        RoadPieceMeshes out;
        const Map::Road& road = network_.Roads()[static_cast<std::size_t>(piece.road)];
        const Map::RoadProfile& profile = road.profile;
        const auto& samples = road.curve.Samples();

        // Rows along the piece: exact ends plus interior samples.
        std::vector<float> stations;
        stations.push_back(piece.s0);
        for (std::size_t i = piece.sampleBegin; i < piece.sampleEnd; ++i) {
            if (samples[i].s > piece.s0 + 0.25f && samples[i].s < piece.s1 - 0.25f) {
                stations.push_back(samples[i].s);
            }
        }
        stations.push_back(piece.s1);
        // Rows at most kRowSpacingM apart so baked ground shadows (tree crowns, house walls)
        // survive the per-vertex colour interpolation along the road.
        {
            std::vector<float> dense;
            dense.push_back(stations.front());
            for (std::size_t i = 1; i < stations.size(); ++i) {
                const float span = stations[i] - stations[i - 1];
                const int n = std::max(1, static_cast<int>(std::ceil(span / kRowSpacingM)));
                for (int k = 1; k <= n; ++k) dense.push_back(stations[i - 1] + span * static_cast<float>(k) / static_cast<float>(n));
            }
            stations.swap(dense);
        }

        std::vector<Row> rows;
        rows.reserve(stations.size());
        for (const float s : stations) {
            const Map::RoadSample sample = road.curve.Evaluate(s);
            Row r;
            r.centre = sample.position;
            Vector3 flatTangent(sample.tangent.X, 0.0f, sample.tangent.Z);
            if (flatTangent.LengthSquared() < 1e-8f) flatTangent = Vector3(0.0f, 0.0f, -1.0f);
            flatTangent.Normalize();
            r.right = Vector3(-flatTangent.Z, 0.0f, flatTangent.X);
            r.up = Vector3::Cross(r.right, sample.tangent);
            if (r.up.Y < 0.0f) r.up = r.up * -1.0f;
            if (r.up.LengthSquared() < 1e-8f) r.up = Vector3(0.0f, 1.0f, 0.0f);
            r.up.Normalize();
            r.s = s;
            r.urban = sample.urban;
            rows.push_back(r);
        }

        const float hp = profile.HalfPavedWidth();
        const float crown = profile.crownPercent * 0.01f;
        const auto surfaceHeight = [&](const Row& r, const float lat) {
            const float a = std::fabs(lat);
            if (a <= hp) {
                return r.centre.Y - a * crown;
            }
            return r.centre.Y - hp * crown - (a - hp) * 0.04f;
        };

        // Paved surface in lateral columns so the vertex colours can carry wheel-track wear:
        // polished tracks a little lighter, the lane centre a little darker (drips), the outer
        // 0.5 m darker and crumbling, all with a slow tone variation along the road.
        const unsigned roadSeed = static_cast<unsigned>(piece.road) * 7919u + 13u;
        const bool paved = profile.surface == Sim::SurfaceType::Asphalt;
        const int lanes = std::max(1, profile.lanesPerDirection);
        const auto pavedColour = [&](const Row& r, const float lat) {
            float w = 1.0f + 0.05f * ToneNoise(r.s, roadSeed);
            if (paved) {
                const float a = std::fabs(lat);
                float track = 0.0f;
                float centre = 0.0f;
                for (int k = 0; k < lanes; ++k) {
                    const float c = (static_cast<float>(k) + 0.5f) * profile.laneWidth;
                    for (const float t : {c - 0.78f, c + 0.78f}) {
                        const float d = (a - t) / 0.26f;
                        track += std::exp(-d * d);
                    }
                    const float dc = (a - c) / 0.30f;
                    centre += std::exp(-dc * dc);
                }
                w += 0.04f * std::min(1.0f, track) - 0.03f * std::min(1.0f, centre);
                const float edge = std::clamp((a - (hp - 0.55f)) / 0.55f, 0.0f, 1.0f);
                w *= 1.0f - 0.10f * edge * edge;
            }
            return Grey(w);
        };
        // Columns every 0.25 m so the wear gradients stay smooth (no corduroy from sparse
        // interpolation), snapped to the edges.
        std::vector<float> lats;
        for (float lat = -hp; lat < hp - 0.05f; lat += 0.25f) lats.push_back(lat);
        lats.push_back(hp);
        for (std::size_t i = 0; i + 1 < lats.size(); ++i) {
            const float uA = (lats[i] + hp) / kAsphaltTileM;
            const float uB = (lats[i + 1] + hp) / kAsphaltTileM;
            AddStrip(out.paved, rows, lats[i], lats[i + 1], surfaceHeight, uA, uB, kAsphaltTileM, 0.0f, pavedColour);
        }

        // Shoulders or sidewalks per side. Sidewalks exist only on urban stretches; a piece is
        // treated as urban when most of its rows are.
        std::size_t urbanRows = 0;
        for (const auto& r : rows) urbanRows += r.urban ? 1u : 0u;
        const bool urbanPiece = urbanRows * 2 > rows.size();
        if (paved && urbanPiece) {
            // Occasional resurfaced cuts in a town lane. They share the asphalt mesh and
            // texture, so rain/puddles and lying snow cover them exactly as they cover the
            // road. A narrow dark seam surrounds the slightly fresher fill. Global road
            // stations and a road-local hash keep placement stable across piece boundaries.
            const auto patchRow = [&](const float s) {
                const Map::RoadSample sample = road.curve.Evaluate(s);
                Row r;
                r.centre = sample.position;
                Vector3 tangent(sample.tangent.X, 0.0f, sample.tangent.Z);
                if (tangent.LengthSquared() < 1e-8f) tangent = Vector3(0.0f, 0.0f, -1.0f);
                tangent.Normalize();
                r.right = Vector3(-tangent.Z, 0.0f, tangent.X);
                r.up = Vector3(0.0f, 1.0f, 0.0f);
                r.s = s;
                return r;
            };
            const auto corner = [&](const Row& r, const float lat, const Color colour) {
                const Vector3 p = r.centre + r.right * lat +
                    Vector3(0.0f, surfaceHeight(r, lat) - r.centre.Y + 0.004f, 0.0f);
                return out.paved.AddVertex(p, r.up,
                    Vector2((lat + hp) / kAsphaltTileM, r.s / kAsphaltTileM), colour);
            };
            constexpr float kSpacing = 91.0f;
            const float phase = 25.0f + static_cast<float>(roadSeed % 37u);
            const int first = std::max(0, static_cast<int>(std::floor((piece.s0 - phase) / kSpacing)) - 1);
            const int last = static_cast<int>(std::ceil((piece.s1 - phase) / kSpacing)) + 1;
            for (int k = first; k <= last; ++k) {
                const unsigned hash = (roadSeed + static_cast<unsigned>(k) * 2654435761u) * 2246822519u;
                const float s = phase + static_cast<float>(k) * kSpacing + static_cast<float>(hash % 27u) - 13.0f;
                if (s < piece.s0 + 9.0f || s > piece.s1 - 9.0f || !road.curve.Evaluate(s).urban) continue;
                const float halfLength = 1.25f + static_cast<float>((hash >> 8) % 5u) * 0.12f;
                const float halfWidth = std::min(1.05f, profile.laneWidth * 0.32f);
                const float lane = (hash & 1u ? 1.0f : -1.0f) * profile.laneWidth * 0.5f;
                const float inset = 0.075f;
                // Lying snow must cover the road only once; drawing this cut and the original
                // strip again in the snow pass would make a bright white rectangle.
                if (out.snowBase.TriangleCount() == 0) {
                    // The overlay needs the crown and bends, not the 25 cm wheel-track
                    // colour columns. Reuse the longitudinal rows with five lateral points
                    // instead of duplicating the detailed paved mesh in GPU memory.
                    const float weatherLats[] = {-hp, -profile.laneWidth, 0.0f, profile.laneWidth, hp};
                    for (int j = 0; j < 4; ++j) {
                        AddStrip(out.snowBase, rows, weatherLats[j], weatherLats[j + 1], surfaceHeight,
                                 (weatherLats[j] + hp) / kAsphaltTileM,
                                 (weatherLats[j + 1] + hp) / kAsphaltTileM, kAsphaltTileM);
                    }
                }
                const Row near = patchRow(s - halfLength), far = patchRow(s + halfLength);
                const Row nearInner = patchRow(s - halfLength + inset), farInner = patchRow(s + halfLength - inset);
                const auto outer = Grey(0.78f), inner = Grey(0.86f);
                const std::uint32_t o0 = corner(near, lane - halfWidth, outer);
                const std::uint32_t o1 = corner(near, lane + halfWidth, outer);
                const std::uint32_t o2 = corner(far, lane + halfWidth, outer);
                const std::uint32_t o3 = corner(far, lane - halfWidth, outer);
                const std::uint32_t i0 = corner(nearInner, lane - halfWidth + inset, inner);
                const std::uint32_t i1 = corner(nearInner, lane + halfWidth - inset, inner);
                const std::uint32_t i2 = corner(farInner, lane + halfWidth - inset, inner);
                const std::uint32_t i3 = corner(farInner, lane - halfWidth + inset, inner);
                out.paved.AddQuad(o0, o1, i1, i0);
                out.paved.AddQuad(i1, o1, o2, i2);
                out.paved.AddQuad(i3, i2, o2, o3);
                out.paved.AddQuad(o0, i0, i3, o3);
                out.paved.AddQuad(i0, i1, i2, i3);
            }
        }
        for (const float side : {-1.0f, 1.0f}) {
            const bool right = side > 0.0f;
            const bool sidewalk = urbanPiece && profile.sidewalk.width > 0.0f && (right ? profile.sidewalk.right : profile.sidewalk.left);
            if (sidewalk) {
                const float kerbH = profile.sidewalk.kerbHeight;
                const float inner = hp;
                const float outer = hp + profile.sidewalk.width;
                // Kerb face (vertical) at the paved edge, then the flat sidewalk at kerb height.
                const float edgeY = -hp * crown;   // relative to the centre height
                AddVerticalFace(out.kerb, rows, side * (inner + 0.01f), edgeY, edgeY + kerbH, right, 1.0f);
                const auto walkHeight = [&](const Row& r, float) { return r.centre.Y + edgeY + kerbH; };
                const auto walkColour = [&](const Row& r, float) { return Grey(0.97f + 0.04f * ToneNoise(r.s + 500.0f, roadSeed)); };
                const float u0 = 0.0f;
                const float u1 = profile.sidewalk.width / kPavingTileM;
                if (right) {
                    AddStrip(out.sidewalk, rows, side * inner, side * outer, walkHeight, u0, u1, kPavingTileM, 0.0f, walkColour);
                } else {
                    AddStrip(out.sidewalk, rows, side * outer, side * inner, walkHeight, u1, u0, kPavingTileM, 0.0f, walkColour);
                }
                // Back edge: a short face down towards the terrain.
                AddVerticalFace(out.kerb, rows, side * outer, edgeY + kerbH - 0.25f, edgeY + kerbH, right, 1.0f);
            } else {
                float outer = hp;
                if (profile.shoulderWidth > 0.0f) {
                    const float inner = hp;
                    outer = hp + profile.shoulderWidth;
                    const float u1 = profile.shoulderWidth / kGravelTileM;
                    // Gravel packed darker where it meets the asphalt, loose and lighter outward.
                    const auto shoulderColour = [&](const Row& r, const float lat) {
                        const float t = std::clamp((std::fabs(lat) - hp) / std::max(0.05f, profile.shoulderWidth), 0.0f, 1.0f);
                        return Grey(0.86f + 0.14f * t + 0.04f * ToneNoise(r.s + 900.0f, roadSeed));
                    };
                    if (right) {
                        AddStrip(out.shoulder, rows, side * inner, side * outer, surfaceHeight, 0.0f, u1, kGravelTileM, 0.002f, shoulderColour);
                    } else {
                        AddStrip(out.shoulder, rows, side * outer, side * inner, surfaceHeight, u1, 0.0f, kGravelTileM, 0.002f, shoulderColour);
                    }
                }
                // Verge: a grass strip from the shoulder edge down to the sampled terrain, trodden
                // and bare at the road (alpha 0 keeps the road tone) and blending into the
                // terrain tint at its outer edge (alpha 255).
                if (terrainHeight_) {
                    const float vergeOuter = outer + kVergeWidthM;
                    const auto vergeHeight = [&](const Row& r, const float lat) {
                        if (std::fabs(lat) <= outer + 0.01f) return surfaceHeight(r, lat) - 0.006f;
                        const Vector3 p = r.centre + r.right * lat;
                        return terrainHeight_(p.X, p.Z) + 0.05f;
                    };
                    const auto vergeColour = [&](const Row& r, const float lat) {
                        const float t = std::clamp((std::fabs(lat) - outer) / kVergeWidthM, 0.0f, 1.0f);
                        const float n = 0.05f * ToneNoise(r.s + 1300.0f, roadSeed);
                        // Bare earth tone at the road edge fading to neutral grass.
                        const float rr = 0.78f + 0.22f * t + n, gg = 0.66f + 0.34f * t + n, bb = 0.50f + 0.50f * t + n;
                        return Color(static_cast<int>(std::clamp(rr, 0.0f, 1.0f) * 255.0f), static_cast<int>(std::clamp(gg, 0.0f, 1.0f) * 255.0f),
                                     static_cast<int>(std::clamp(bb, 0.0f, 1.0f) * 255.0f), static_cast<int>(t * 255.0f));
                    };
                    const float uOuter = kVergeWidthM / kGrassTileM;
                    if (right) {
                        AddStrip(out.verge, rows, side * outer, side * vergeOuter, vergeHeight, 0.0f, uOuter, kGrassTileM, 0.0f, vergeColour);
                    } else {
                        AddStrip(out.verge, rows, side * vergeOuter, side * outer, vergeHeight, uOuter, 0.0f, kGrassTileM, 0.0f, vergeColour);
                    }
                }
            }
        }

        // Markings.
        const Map::RoadSpec& spec = *road.spec;
        const auto addLine = [&](const float lat, const float from, const float to) {
            std::vector<Row> sub;
            for (const auto& r : rows) {
                if (r.s >= from - 0.01f && r.s <= to + 0.01f) sub.push_back(r);
            }
            // Ensure exact ends.
            if (sub.empty() || sub.front().s > from + 0.05f) {
                const Map::RoadSample sm = road.curve.Evaluate(from);
                Row r = rows.front();
                r.centre = sm.position;
                r.s = from;
                Vector3 t(sm.tangent.X, 0.0f, sm.tangent.Z);
                if (t.LengthSquared() > 1e-8f) { t.Normalize(); r.right = Vector3(-t.Z, 0.0f, t.X); }
                sub.insert(sub.begin(), r);
            }
            if (sub.back().s < to - 0.05f) {
                const Map::RoadSample sm = road.curve.Evaluate(to);
                Row r = rows.back();
                r.centre = sm.position;
                r.s = to;
                Vector3 t(sm.tangent.X, 0.0f, sm.tangent.Z);
                if (t.LengthSquared() > 1e-8f) { t.Normalize(); r.right = Vector3(-t.Z, 0.0f, t.X); }
                sub.push_back(r);
            }
            const auto markingColour = [&](const Row& r, float) { return Grey(0.90f + 0.10f * ToneNoise(r.s * 3.0f + 200.0f, roadSeed)); };
            AddStrip(out.markings, sub, lat - kLineWidth * 0.5f, lat + kLineWidth * 0.5f, surfaceHeight, 0.0f, 1.0f, 1.0f, kMarkingLift, markingColour);
        };
        const bool paintable = profile.surface == Sim::SurfaceType::Asphalt || profile.surface == Sim::SurfaceType::Concrete;
        if (paintable && spec.centreLine != Map::CentreLineMarking::None && profile.lanesPerDirection >= 1 && !spec.oneWay) {
            const auto addDashed = [&](const float lateral) {
                // V 2b 3/6 m outside, V 2a 1.5/1.5 m inside built-up areas.
                float s = piece.s0 + 1.0f;
                while (s < piece.s1 - 1.0f) {
                    const bool urban = road.curve.Evaluate(s).urban;
                    const float dash = urban ? 1.5f : 3.0f;
                    const float gap = urban ? 1.5f : 6.0f;
                    addLine(lateral, s, std::min(piece.s1 - 0.5f, s + dash));
                    s += dash + gap;
                }
            };
            switch (spec.centreLine) {
                case Map::CentreLineMarking::Solid: addLine(0.0f, piece.s0, piece.s1); break;
                case Map::CentreLineMarking::Dashed: addDashed(0.0f); break;
                case Map::CentreLineMarking::SolidForward:
                    addLine(0.12f, piece.s0, piece.s1);
                    addDashed(-0.12f);
                    break;
                case Map::CentreLineMarking::SolidReverse:
                    addLine(-0.12f, piece.s0, piece.s1);
                    addDashed(0.12f);
                    break;
                case Map::CentreLineMarking::None: break;
            }
        }
        if (paintable && spec.edgeLines && !urbanPiece) {
            const float edge = static_cast<float>(profile.lanesPerDirection) * profile.laneWidth;
            addLine(-edge + kLineWidth * 0.5f, piece.s0, piece.s1);
            addLine(edge - kLineWidth * 0.5f, piece.s0, piece.s1);
        }
        // Give-way / stop lines at the piece ends that meet an intersection where this road yields.
        const auto& intersections = network_.Intersections();
        for (const int which : {0, 1}) {
            const int ii = which == 0 ? piece.startIntersection : piece.endIntersection;
            if (ii < 0) continue;
            const Map::Intersection& inter = intersections[static_cast<std::size_t>(ii)];
            for (const Map::Approach& a : inter.approaches) {
                if (a.piece != static_cast<int>(&piece - &network_.Pieces()[0])) continue;
                if (a.control == Map::ApproachControl::Priority || a.control == Map::ApproachControl::RightHandRule) continue;
                if (!paintable) continue;
                // The lane entering the intersection is on the right of +s when the approach leaves
                // forward... incoming traffic on approach `a` travels towards the node: for a forward
                // approach (road continues away from the node towards +s) incoming lanes travel -s
                // and sit on the left (negative lateral); otherwise on the right.
                const float lane0 = a.leavesForward ? -profile.laneWidth : 0.0f;
                const float lane1 = a.leavesForward ? 0.0f : profile.laneWidth;
                const float sEnd = which == 0 ? piece.s0 : piece.s1;
                const float dir = which == 0 ? 1.0f : -1.0f;   // into the piece
                if (a.control == Map::ApproachControl::Stop || a.control == Map::ApproachControl::Signal) {
                    // V 5: 0.5 m bar across the incoming lane, 1 m before the patch. A signalised
                    // approach gets the same stop line -- the lights decide who goes, so the
                    // give-way triangles below would be both wrong and contradictory. Every
                    // approach of the junction at "U kaple" was painted with them before.
                    std::vector<Row> bar;
                    for (const float off : {1.0f, 1.5f}) {
                        const float s = sEnd + dir * off;
                        const Map::RoadSample sm = road.curve.Evaluate(s);
                        Row r = rows.front();
                        r.centre = sm.position;
                        r.s = s;
                        Vector3 t(sm.tangent.X, 0.0f, sm.tangent.Z);
                        if (t.LengthSquared() > 1e-8f) { t.Normalize(); r.right = Vector3(-t.Z, 0.0f, t.X); }
                        bar.push_back(r);
                    }
                    if (bar[0].s > bar[1].s) std::swap(bar[0], bar[1]);
                    AddStrip(out.markings, bar, lane0 + 0.15f, lane1 - 0.15f, surfaceHeight, 0.0f, 1.0f, 1.0f, kMarkingLift);
                } else {
                    // V 6a: row of triangles pointing towards the driver.
                    const float width = profile.laneWidth - 0.3f;
                    const int count = std::max(2, static_cast<int>(width / 0.6f));
                    const float step = width / static_cast<float>(count);
                    const float sBase = sEnd + dir * 1.0f;
                    const float sTip = sEnd + dir * 1.6f;
                    const Map::RoadSample base = road.curve.Evaluate(sBase);
                    const Map::RoadSample tip = road.curve.Evaluate(sTip);
                    Vector3 t(base.tangent.X, 0.0f, base.tangent.Z);
                    if (t.LengthSquared() > 1e-8f) t.Normalize();
                    const Vector3 right(-t.Z, 0.0f, t.X);
                    for (int k = 0; k < count; ++k) {
                        const float l0 = lane0 + 0.15f + step * static_cast<float>(k);
                        const float l1 = l0 + step * 0.8f;
                        const float lm = 0.5f * (l0 + l1);
                        Row rb; rb.centre = base.position; rb.right = right;
                        Row rt; rt.centre = tip.position; rt.right = right;
                        const Vector3 p0 = base.position + right * l0 + Vector3(0.0f, surfaceHeight(rb, l0) - base.position.Y + kMarkingLift, 0.0f);
                        const Vector3 p1 = base.position + right * l1 + Vector3(0.0f, surfaceHeight(rb, l1) - base.position.Y + kMarkingLift, 0.0f);
                        const Vector3 p2 = tip.position + right * lm + Vector3(0.0f, surfaceHeight(rt, lm) - tip.position.Y + kMarkingLift, 0.0f);
                        const Vector3 up(0.0f, 1.0f, 0.0f);
                        const std::uint32_t i0 = out.markings.AddVertex(p0, up, Vector2(0, 0), kWhite);
                        const std::uint32_t i1 = out.markings.AddVertex(p1, up, Vector2(1, 0), kWhite);
                        const std::uint32_t i2 = out.markings.AddVertex(p2, up, Vector2(0.5f, 1), kWhite);
                        // Counter-clockwise seen from above.
                        const Vector3 n = Vector3::Cross(p1 - p0, p2 - p0);
                        if (n.Y >= 0.0f) out.markings.AddTriangle(i0, i1, i2); else out.markings.AddTriangle(i0, i2, i1);
                    }
                }
            }
        }
        return out;
    }

    void RoadMeshBuilder::BuildIntersection(const Map::Intersection& inter, MeshData& paved, MeshData& markings) const
    {
        (void)markings;
        if (inter.patch.size() < 3) {
            return;
        }
        const Vector3 up(0.0f, 1.0f, 0.0f);
        const Vector2 c2(inter.center.X, inter.center.Z);
        const std::uint32_t centre = paved.AddVertex(Vector3(c2.X, inter.PlaneHeight(c2), c2.Y), up,
                                                     Vector2(c2.X / kAsphaltTileM, c2.Y / kAsphaltTileM), kWhite);
        std::vector<std::uint32_t> ring;
        for (const auto& p : inter.patch) {
            ring.push_back(paved.AddVertex(Vector3(p.X, inter.PlaneHeight(p), p.Y), up, Vector2(p.X / kAsphaltTileM, p.Y / kAsphaltTileM), kWhite));
        }
        // The patch polygon is stored with positive PolygonArea (x/z), which is clockwise seen
        // from above (+y) because z points south; reverse for the counter-clockwise convention.
        const std::size_t n = ring.size();
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint32_t a = ring[i];
            const std::uint32_t b = ring[(i + 1) % n];
            paved.AddTriangle(centre, b, a);
        }
    }

    bool RoadMeshBuilder::BuildCrossing(const Vector2& position, MeshData& markings) const
    {
        Map::RoadHit hit;
        if (!network_.NearestRoad(position, 14.0f, hit)) {
            return false;
        }
        const Map::Road& road = network_.Roads()[static_cast<std::size_t>(hit.road)];
        const float hp = road.profile.HalfPavedWidth();
        const float crown = road.profile.crownPercent * 0.01f;
        const float barLength = 4.0f;      // along the road
        const float barWidth = 0.5f;       // across the road (V 7: 0.5 m bars, 0.5 m gaps)
        const int bars = std::max(2, static_cast<int>((2.0f * hp - 0.3f) / (2.0f * barWidth)));
        const float span = static_cast<float>(bars) * 2.0f * barWidth - barWidth;
        const float s0 = hit.s - barLength * 0.5f;
        const float s1 = hit.s + barLength * 0.5f;
        std::vector<Row> rows;
        for (const float s : {s0, s1}) {
            const Map::RoadSample sm = road.curve.Evaluate(std::clamp(s, 0.0f, road.curve.Length()));
            Row r;
            r.centre = sm.position;
            Vector3 t(sm.tangent.X, 0.0f, sm.tangent.Z);
            if (t.LengthSquared() < 1e-8f) t = Vector3(0.0f, 0.0f, -1.0f);
            t.Normalize();
            r.right = Vector3(-t.Z, 0.0f, t.X);
            r.up = Vector3(0.0f, 1.0f, 0.0f);
            r.s = s;
            rows.push_back(r);
        }
        const auto surfaceHeight = [&](const Row& r, const float lat) {
            const float a = std::fabs(lat);
            return a <= hp ? r.centre.Y - a * crown : r.centre.Y - hp * crown - (a - hp) * 0.04f;
        };
        for (int i = 0; i < bars; ++i) {
            const float latA = -span * 0.5f + static_cast<float>(i) * 2.0f * barWidth;
            const float latB = latA + barWidth;
            AddStrip(markings, rows, latA, latB, surfaceHeight, 0.0f, 1.0f, 1.0f, kMarkingLift);
        }
        return true;
    }
}
