#include "CarSim/Render/RoadMeshBuilder.hpp"

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

        /// Adds a quad strip between two lateral offsets over consecutive rows; heights come from `heightAt`.
        template <typename HeightFn>
        void AddStrip(MeshData& mesh, const std::vector<Row>& rows, const float latA, const float latB, const HeightFn& heightAt,
                      const float uA, const float uB, const float tileM, const float lift = 0.0f)
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
                const std::uint32_t ia = mesh.AddVertex(pa, r.up, Vector2(uA, v), kWhite);
                const std::uint32_t ib = mesh.AddVertex(pb, r.up, Vector2(uB, v), kWhite);
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

        // Paved surface: u spans the width so the asphalt tiles are roughly square.
        const float uPaved = (2.0f * hp) / kAsphaltTileM;
        AddStrip(out.paved, rows, -hp, hp, surfaceHeight, 0.0f, uPaved, kAsphaltTileM);

        // Shoulders or sidewalks per side. Sidewalks exist only on urban stretches; a piece is
        // treated as urban when most of its rows are.
        std::size_t urbanRows = 0;
        for (const auto& r : rows) urbanRows += r.urban ? 1u : 0u;
        const bool urbanPiece = urbanRows * 2 > rows.size();
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
                const float u0 = 0.0f;
                const float u1 = profile.sidewalk.width / kPavingTileM;
                if (right) {
                    AddStrip(out.sidewalk, rows, side * inner, side * outer, walkHeight, u0, u1, kPavingTileM);
                } else {
                    AddStrip(out.sidewalk, rows, side * outer, side * inner, walkHeight, u1, u0, kPavingTileM);
                }
                // Back edge: a short face down towards the terrain.
                AddVerticalFace(out.kerb, rows, side * outer, edgeY + kerbH - 0.25f, edgeY + kerbH, right, 1.0f);
            } else if (profile.shoulderWidth > 0.0f) {
                const float inner = hp;
                const float outer = hp + profile.shoulderWidth;
                const float u1 = profile.shoulderWidth / kGravelTileM;
                if (right) {
                    AddStrip(out.shoulder, rows, side * inner, side * outer, surfaceHeight, 0.0f, u1, kGravelTileM, 0.002f);
                } else {
                    AddStrip(out.shoulder, rows, side * outer, side * inner, surfaceHeight, u1, 0.0f, kGravelTileM, 0.002f);
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
            AddStrip(out.markings, sub, lat - kLineWidth * 0.5f, lat + kLineWidth * 0.5f, surfaceHeight, 0.0f, 1.0f, 1.0f, kMarkingLift);
        };
        const bool paintable = profile.surface == Sim::SurfaceType::Asphalt || profile.surface == Sim::SurfaceType::Concrete;
        if (paintable && spec.centreLine != Map::CentreLineMarking::None && profile.lanesPerDirection >= 1 && !spec.oneWay) {
            if (spec.centreLine == Map::CentreLineMarking::Solid) {
                addLine(0.0f, piece.s0, piece.s1);
            } else {
                // V 2b 3/6 m outside, V 2a 1.5/1.5 m inside built-up areas.
                float s = piece.s0 + 1.0f;
                while (s < piece.s1 - 1.0f) {
                    const bool urban = road.curve.Evaluate(s).urban;
                    const float dash = urban ? 1.5f : 3.0f;
                    const float gap = urban ? 1.5f : 6.0f;
                    addLine(0.0f, s, std::min(piece.s1 - 0.5f, s + dash));
                    s += dash + gap;
                }
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
                if (a.control == Map::ApproachControl::Stop) {
                    // V 5: 0.5 m bar across the incoming lane, 1 m before the patch.
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
}
