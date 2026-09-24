#include "CarSim/Render/ProceduralCar.hpp"

#include "CarBody.hpp"
#include "CarSim/Sim/Units.hpp"

#include "Microsoft/Xna/Framework/Quaternion.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <numbers>

namespace CarSim::Render
{
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;

    using namespace CarBody;

    // ================================================================== body shape

    namespace
    {
        struct Shape
        {
            CarStyle s;
            float zF = 0, zR = 0, zFA = 0, zRA = 0;
            float zCowl = 0, zRoofFront = 0, zRoofRear = 0, zRearWindowBottom = 0;
            float zDoorFront = 0, zB = 0, zDoorRear = 0, zSideGlassRear = 0, zFrontBumper = 0, zRearBumper = 0;
            float ybot = 0, sillTop = 0, cabinFloor = 0;
            float rearFaceGlassBottom = -1.0f;   // estate/van: window on the tail face above this height
            SmoothCurve top, belt, hwBelt, hwSill, hwRoof, corner;
            float archR = 0.0f;
            float xInner = 0.0f;
            float wheelY = 0.0f;

            [[nodiscard]] float Top(const float z) const { return top.Evaluate(z); }
            [[nodiscard]] float Belt(const float z) const
            {
                return std::max(sillTop + 0.22f, std::min(belt.Evaluate(z), Top(z) - 0.07f));
            }
            [[nodiscard]] float HwBelt(const float z) const { return hwBelt.Evaluate(z); }
            [[nodiscard]] float HwSill(const float z) const { return std::min(hwSill.Evaluate(z), HwBelt(z) - 0.02f); }
            [[nodiscard]] float HwRoof(const float z) const
            {
                const float curve = hwRoof.Evaluate(z);
                const float hood = HwBelt(z) - 0.07f;
                const float blend = SmoothStep(zCowl - 0.25f, zCowl + 0.05f, z);
                return std::min(HwBelt(z) - 0.03f, Lerp(hood, curve, blend));
            }
            [[nodiscard]] float Rail(const float z) const { return std::max(Belt(z) + 0.03f, Top(z) - corner.Evaluate(z)); }
            /// Height of the arch opening at z (0 when outside every arch).
            [[nodiscard]] float ArchTop(const float z) const
            {
                float best = 0.0f;
                for (const float zw : {zFA, zRA}) {
                    const float dz = z - zw;
                    if (std::fabs(dz) < archR) {
                        best = std::max(best, wheelY + std::sqrt(archR * archR - dz * dz));
                    }
                }
                return best;
            }
            [[nodiscard]] float Flare(const float z, const float y) const
            {
                float f = 0.0f;
                for (const float zw : {zFA, zRA}) {
                    const float t = (z - zw) / (archR + 0.16f);
                    f += std::max(0.0f, 1.0f - t * t);
                }
                const float vertical = std::clamp(1.0f - (y - (wheelY + 0.10f)) / 0.45f, 0.0f, 1.0f);
                return s.archFlare * std::min(1.0f, f) * vertical;
            }
            /// Half width of the door skin at height y below the belt.
            [[nodiscard]] float SideX(const float z, const float y) const
            {
                const float b = Belt(z);
                const float low = sillTop + 0.03f;
                const float hwLow = HwSill(z) + 0.035f;
                const float t = std::clamp((y - low) / std::max(0.05f, b - low), 0.0f, 1.0f);
                const float bulge = 0.02f * std::sin(t * kPi);
                return hwLow + (HwBelt(z) - hwLow) * std::pow(t, 0.8f) + bulge + Flare(z, y);
            }
        };

        Shape MakeShape(const CarStyle& s)
        {
            Shape sh;
            sh.s = s;
            sh.zF = s.FrontZ();
            sh.zR = s.RearZ();
            sh.zFA = s.FrontAxleZ();
            sh.zRA = s.RearAxleZ();
            sh.ybot = s.rideHeight;
            sh.sillTop = s.rideHeight + 0.19f;
            sh.cabinFloor = s.rideHeight + 0.05f;
            sh.zCowl = sh.zFA + s.cowlFromFrontAxle;
            sh.zRoofFront = sh.zCowl + s.windshieldLength;
            sh.zRoofRear = sh.zRA + s.roofRearFromRearAxle;
            sh.archR = s.wheelRadius + 0.075f;
            sh.xInner = s.track * 0.5f - s.tyreWidth * 0.5f - 0.03f;
            sh.wheelY = s.wheelRadius;
            const float h = s.height;
            const float n = s.noseHeight;
            const float hr = s.hoodRise;
            // Plan-view half width before the arch flares and the door bulge, so the finished
            // skin (flares included) matches the definition width.
            const float W = s.width * 0.5f - s.archFlare - 0.008f;
            const float zF = sh.zF, zR = sh.zR, zCowl = sh.zCowl, zRoofFront = sh.zRoofFront, zRoofRear = sh.zRoofRear;

            sh.zDoorFront = zCowl + (s.body == CarStyle::Body::Van ? 0.26f : 0.13f);
            const float frontDoor = s.body == CarStyle::Body::Hatchback ? 1.05f : s.body == CarStyle::Body::Van ? 1.05f : 1.08f;
            const float rearDoor = s.body == CarStyle::Body::Hatchback ? 0.95f : s.body == CarStyle::Body::Van ? 1.20f : 1.02f;
            sh.zB = sh.zDoorFront + frontDoor;
            sh.zDoorRear = sh.zB + rearDoor;
            sh.zFrontBumper = zF + 0.55f;
            sh.zRearBumper = zR - 0.45f;

            std::vector<std::pair<float, float>> top;
            top = {{zF, n - 0.06f}, {zF + 0.12f, n}, {zF + 0.55f, n + hr * 0.5f}, {zCowl - 0.06f, n + hr}, {zCowl + 0.02f, n + hr + 0.035f},
                   {zRoofFront, h - 0.045f}, {zRoofFront + 0.4f, h}, {zRoofRear - 0.35f, h - 0.012f}, {zRoofRear, h - 0.04f}};
            switch (s.body) {
                case CarStyle::Body::Hatchback:
                case CarStyle::Body::Suv: {
                    const float drop = s.rearWindowDrop;
                    sh.zRearWindowBottom = zR - s.tailRounding - 0.02f;
                    top.push_back({zRoofRear + 0.32f, h - 0.04f - drop * 0.82f});
                    top.push_back({sh.zRearWindowBottom, h - 0.04f - drop});
                    top.push_back({zR, h - 0.04f - drop - 0.05f});
                    sh.zSideGlassRear = std::min(sh.zDoorRear + 0.14f, zRoofRear - 0.02f);
                    break;
                }
                case CarStyle::Body::Sedan: {
                    const float deck = s.bootDeckHeight;
                    sh.zRearWindowBottom = zRoofRear + 0.62f;
                    top.push_back({zRoofRear + 0.45f, deck + 0.06f});
                    top.push_back({sh.zRearWindowBottom, deck + 0.005f});
                    top.push_back({zR - 0.25f, deck - 0.005f});
                    top.push_back({zR, deck - 0.05f});
                    sh.zSideGlassRear = std::min(sh.zDoorRear + 0.06f, zRoofRear - 0.02f);
                    break;
                }
                case CarStyle::Body::Estate: {
                    sh.zRearWindowBottom = zR;   // window sits on the tail face
                    top.push_back({zRoofRear + 0.10f, h - 0.09f});
                    top.push_back({zR - s.tailRounding, h - 0.20f});
                    top.push_back({zR, h - 0.28f});
                    sh.zSideGlassRear = zRoofRear - 0.06f;
                    sh.rearFaceGlassBottom = h - 0.62f;
                    break;
                }
                case CarStyle::Body::Van: {
                    sh.zRearWindowBottom = zR;
                    top = {{zF, n - 0.05f}, {zF + 0.10f, n}, {zF + 0.45f, n + hr}, {zCowl, n + hr + 0.03f}, {zRoofFront, h - 0.05f},
                           {zRoofFront + 0.3f, h}, {zRoofRear, h - 0.01f}, {zR - s.tailRounding, h - 0.06f}, {zR, h - 0.12f}};
                    sh.zSideGlassRear = zRoofRear - 0.25f;
                    sh.rearFaceGlassBottom = h - 0.85f;
                    break;
                }
                case CarStyle::Body::Bus:
                case CarStyle::Body::Truck:
                    break;   // built by GenerateHeavyVehicle, never lofted
            }
            sh.top.Set(top);

            sh.belt.Set({{zF, n - 0.12f}, {zF + 0.45f, n - 0.05f}, {zCowl - 0.12f, n + hr - 0.06f}, {zCowl + 0.05f, s.beltHeight - 0.02f},
                         {sh.zB, s.beltHeight}, {sh.zSideGlassRear, s.beltHeight + 0.03f}, {zR - 0.2f, s.beltHeight + 0.02f}, {zR, s.beltHeight - 0.02f}});
            sh.hwBelt.Set({{zF, W - 0.14f}, {zF + 0.5f, W - 0.05f}, {zCowl - 0.3f, W - 0.01f}, {sh.zB, W}, {sh.zSideGlassRear, W - 0.005f},
                           {zR - 0.35f, W - 0.035f}, {zR, W - 0.09f}});
            sh.hwSill.Set({{zF, W - 0.21f}, {zF + 0.6f, W - s.sillTuck - 0.04f}, {sh.zB, W - s.sillTuck}, {zR - 0.5f, W - s.sillTuck - 0.02f},
                           {zR, W - 0.17f}});
            sh.hwRoof.Set({{zCowl, W - 0.10f}, {zRoofFront, W - s.tumblehome}, {zRoofRear, W - s.tumblehome - 0.02f}, {zR, W - 0.15f}});
            sh.corner.Set({{zF, 0.03f}, {zCowl, 0.035f}, {zRoofFront, s.roofCrown}, {zRoofRear, s.roofCrown}, {zR, 0.035f}});
            return sh;
        }

        /// Right-half outline (27 points) of the section at station z.
        std::vector<Vector2> Outline(const Shape& sh, const float z)
        {
            std::vector<Vector2> o;
            o.reserve(Ring::kHalfPoints);
            const float top = sh.Top(z);
            const float belt = sh.Belt(z);
            const float hwS = sh.HwSill(z);
            const float hwR = sh.HwRoof(z);
            const float rail = sh.Rail(z);
            const float ybot = sh.ybot;
            const float sillTop = sh.sillTop;
            const float archTop = sh.ArchTop(z);
            const bool arch = archTop > ybot + 0.04f;

            o.emplace_back(0.0f, ybot);                                                        // 0
            if (arch) {
                const float xIn = std::min(sh.xInner, hwS - 0.05f);
                o.emplace_back(std::min(hwS - 0.12f, xIn - 0.03f), ybot);                       // 1
                o.emplace_back(xIn, ybot + 0.01f);                                              // 2
                o.emplace_back(xIn, Lerp(ybot, archTop, 0.5f));                                 // 3
                o.emplace_back(xIn, archTop);                                                   // 4
                const float lipY = std::min(archTop + 0.012f, belt - 0.08f);
                o.emplace_back(sh.SideX(z, lipY), lipY);                                        // 5
            } else {
                o.emplace_back(hwS - 0.12f, ybot);                                              // 1
                o.emplace_back(hwS, ybot + 0.05f);                                              // 2
                o.emplace_back(hwS + 0.012f, Lerp(ybot, sillTop, 0.5f));                        // 3
                o.emplace_back(hwS + 0.02f, sillTop);                                           // 4
                o.emplace_back(sh.SideX(z, sillTop + 0.03f), sillTop + 0.03f);                  // 5
            }
            const float y5 = o.back().Y;
            for (int k = 1; k <= 5; ++k) {                                                      // 6..10
                const float y = Lerp(y5, belt, static_cast<float>(k) / 6.0f);
                o.emplace_back(sh.SideX(z, y), y);
            }
            const float xBelt = sh.SideX(z, belt);
            o.emplace_back(xBelt, belt);                                                        // 11
            const Vector2 p12(xBelt - 0.018f, belt + 0.02f);
            o.push_back(p12);                                                                   // 12
            const Vector2 p19(hwR, rail);
            const Vector2 c(p12.X, p12.Y + 0.6f * (rail - p12.Y));
            for (int k = 1; k <= 6; ++k) {                                                      // 13..18
                const float t = static_cast<float>(k) / 7.0f;
                const float mt = 1.0f - t;
                o.emplace_back(mt * mt * p12.X + 2.0f * mt * t * c.X + t * t * p19.X, mt * mt * p12.Y + 2.0f * mt * t * c.Y + t * t * p19.Y);
            }
            o.push_back(p19);                                                                   // 19
            for (int k = 1; k <= 7; ++k) {                                                      // 20..26
                const float th = kPi * 0.5f * static_cast<float>(k) / 7.0f;
                o.emplace_back(hwR * std::cos(th), rail + (top - rail) * std::pow(std::sin(th), 0.75f));
            }
            o.back() = Vector2(0.0f, top);
            return o;
        }

        std::vector<Vector3> RingFromOutline(const std::vector<Vector2>& o, const float z)
        {
            std::vector<Vector3> ring;
            ring.reserve(Ring::kPoints);
            for (const auto& p : o) ring.emplace_back(p.X, p.Y, z);
            for (int i = Ring::kHalfPoints - 2; i >= 1; --i) ring.emplace_back(-o[static_cast<std::size_t>(i)].X, o[static_cast<std::size_t>(i)].Y, z);
            return ring;
        }

        /// Front/rear face setback (metres towards the car centre) as a function of height.
        float FrontSetback(const Shape& sh, const float y)
        {
            const float b = sh.ybot;
            return 0.07f * SmoothStep(b + 0.26f, b + 0.03f, y) + 0.05f * SmoothStep(b + 0.36f, b + 0.56f, y);
        }

        float RearSetback(const Shape& sh, const float y)
        {
            const float b = sh.ybot;
            return 0.05f * SmoothStep(b + 0.24f, b + 0.05f, y) + 0.06f * SmoothStep(b + 0.40f, b + 0.56f, y);
        }

        /// Rounded-box sweep: shrinks the ring towards its core box with an elliptical profile.
        void SweepRing(std::vector<Vector3>& ring, const float shrink, const float rx, const float ryTop, const float ryBottom)
        {
            float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
            for (const auto& p : ring) {
                minX = std::min(minX, p.X); maxX = std::max(maxX, p.X);
                minY = std::min(minY, p.Y); maxY = std::max(maxY, p.Y);
            }
            const float cx0 = minX + std::min(rx, (maxX - minX) * 0.45f);
            const float cx1 = maxX - std::min(rx, (maxX - minX) * 0.45f);
            const float cy0 = minY + std::min(ryBottom, (maxY - minY) * 0.45f);
            const float cy1 = maxY - std::min(ryTop, (maxY - minY) * 0.45f);
            for (auto& p : ring) {
                const float cx = std::clamp(p.X, cx0, cx1);
                const float cy = std::clamp(p.Y, cy0, cy1);
                p.X = cx + (p.X - cx) * shrink;
                p.Y = cy + (p.Y - cy) * shrink;
            }
        }

        /// Nearest point of the nose skin to a wanted lateral offset and height, with its outward
        /// normal. Details on the front face (fog lamps) cannot be placed from the centre-line
        /// setback alone: the nose rings sweep inwards, so a point at the outer end of the bumper
        /// computed that way floats in front of the body.
        bool FrontFacePoint(const SkinGrid& skin, const float x, const float y, Vector3& pos, Vector3& normal)
        {
            float best = 1e9f;
            bool found = false;
            for (int iv = 0; iv <= 48; ++iv) {
                const float v = 0.16f * static_cast<float>(iv) / 48.0f;   // front sixth of the body
                for (int iu = 0; iu < 256; ++iu) {
                    const float u = static_cast<float>(iu) / 256.0f;
                    Vector3 p, n;
                    skin.Sample(u, v, p, n);
                    if (n.Z > -0.30f) continue;              // must face forward
                    if (x * p.X < 0.0f) continue;            // same side of the car
                    const float d = (p.X - x) * (p.X - x) + (p.Y - y) * (p.Y - y);
                    if (d < best) { best = d; pos = p; normal = n; found = true; }
                }
            }
            return found;
        }

        std::vector<float> Stations(const Shape& sh)
        {
            std::vector<float> zs;
            const float zF = sh.zF, zR = sh.zR;
            const float noseEnd = zF + sh.s.noseRounding + 0.12f;
            const float tailStart = zR - sh.s.tailRounding - 0.12f;
            float z = zF + 0.004f;
            while (z < zR - 0.004f) {
                zs.push_back(z);
                const float step = (z < noseEnd || z > tailStart) ? 0.018f : 0.04f;
                z += step;
            }
            zs.push_back(zR - 0.004f);
            // Snap stations onto the material boundaries so glass and pillar edges are straight.
            for (const float b : {sh.zCowl, sh.zDoorFront, sh.zB - 0.05f, sh.zB + 0.05f, sh.zDoorRear, sh.zSideGlassRear, sh.zRoofFront,
                                  sh.zRoofRear, sh.zRoofRear + 0.03f, sh.zRearWindowBottom, sh.zFrontBumper, sh.zRearBumper}) {
                if (b <= zF + 0.03f || b >= zR - 0.03f) continue;
                float* nearest = nullptr;
                float best = 1e9f;
                for (float& s : zs) {
                    const float d = std::fabs(s - b);
                    if (d < best) { best = d; nearest = &s; }
                }
                if (nearest && best < 0.022f) *nearest = b; else zs.push_back(b);
            }
            std::sort(zs.begin(), zs.end());
            zs.erase(std::unique(zs.begin(), zs.end(), [](float a, float b) { return std::fabs(a - b) < 0.004f; }), zs.end());
            return zs;
        }

        SkinGrid BuildSkin(const Shape& sh)
        {
            SkinGrid skin;
            skin.frontZ = sh.zF;
            skin.vScale = 1.0f / (sh.zR - sh.zF);
            skin.stations = Stations(sh);
            const float noseD = sh.s.noseRounding;
            const float tailD = sh.s.tailRounding;
            for (const float z : skin.stations) {
                std::vector<Vector3> ring = RingFromOutline(Outline(sh, z), z);
                const float dn = z - sh.zF;
                const float dt = sh.zR - z;
                if (dn < noseD) {
                    const float q = (noseD - dn) / noseD;
                    const float shrink = std::sqrt(std::max(0.0f, 1.0f - q * q));
                    SweepRing(ring, shrink, sh.s.noseRounding, 0.09f, 0.07f);
                }
                if (dt < tailD) {
                    const float q = (tailD - dt) / tailD;
                    const float shrink = std::sqrt(std::max(0.0f, 1.0f - q * q));
                    SweepRing(ring, shrink, sh.s.tailRounding, 0.10f, 0.07f);
                }
                // Face profile: pushes the bumper forward relative to the hood edge and the air dam.
                const float wn = 1.0f - SmoothStep(0.0f, noseD + 0.30f, dn);
                const float wt = 1.0f - SmoothStep(0.0f, tailD + 0.30f, dt);
                for (auto& p : ring) {
                    p.Z += FrontSetback(sh, p.Y) * wn - RearSetback(sh, p.Y) * wt;
                }
                skin.rings.push_back(std::move(ring));
            }
            // Fixed u per ring point from the arc length at the B-pillar station.
            std::size_t ref = 0;
            for (std::size_t i = 0; i < skin.stations.size(); ++i) {
                if (std::fabs(skin.stations[i] - sh.zB) < std::fabs(skin.stations[ref] - sh.zB)) ref = i;
            }
            const auto& rr = skin.rings[ref];
            std::vector<float> cum(rr.size() + 1, 0.0f);
            for (std::size_t i = 0; i < rr.size(); ++i) {
                cum[i + 1] = cum[i] + Vector3::Distance(rr[i], rr[(i + 1) % rr.size()]);
            }
            skin.u.resize(rr.size());
            for (std::size_t i = 0; i < rr.size(); ++i) skin.u[i] = cum[i] / cum.back();
            skin.ComputeNormals();
            return skin;
        }

        enum class Skin
        {
            Paint,
            Glass,
            Black,
            Gloss,
            Cut,        // removed from the skin (decal housing)
            GrilleCut,
            Underbody
        };

        struct DecalRegion
        {
            std::vector<Vector2> polygon;   // (u, v)
            CarMaterial lens = CarMaterial::LampHead;
            std::string name;
            float lift = 0.003f;
            bool grille = false;
        };

        /// Skin material of the quad at (station r, ring segment seg) with centroid c.
        Skin ClassifySkin(const Shape& sh, const int seg, const Vector3& c, const float archTop, const float rail, const float belt)
        {
            const bool arch = archTop > sh.ybot + 0.04f;
            const float z = c.Z, y = c.Y;
            const bool bumperZone = z < sh.zFrontBumper || z > sh.zRearBumper;
            const float blackBand = sh.ybot + 0.17f;
            const bool cladding = sh.s.blackCladding;
            if (seg <= 1) return Skin::Underbody;
            if (seg <= 3) return arch ? Skin::Black : (bumperZone && y < blackBand ? Skin::Black : (cladding ? Skin::Black : Skin::Paint));
            if (seg == 4) return arch ? Skin::Black : (cladding ? Skin::Black : Skin::Paint);
            if (seg <= 10) {
                if (bumperZone && y < blackBand) return Skin::Black;
                if (cladding && arch && y < archTop + 0.06f) return Skin::Black;
                return Skin::Paint;
            }
            const bool doorZone = z > sh.zDoorFront - 0.02f && z < sh.zSideGlassRear + 0.02f;
            if (seg == 11) return doorZone ? Skin::Gloss : Skin::Paint;   // window channel
            const bool tumble = seg <= 18;
            if (z < sh.zCowl) return Skin::Paint;                                            // hood and fenders
            if (z < sh.zRoofFront) {
                // Windshield zone: the A-pillar runs from the cowl corner to the roof front corner.
                const float t = (z - sh.zCowl) / std::max(0.05f, sh.zRoofFront - sh.zCowl);
                const float yPillar = Lerp(sh.Belt(sh.zCowl) + 0.03f, sh.Rail(sh.zRoofFront), t);
                if (tumble) {
                    if (std::fabs(y - yPillar) < 0.042f) return Skin::Paint;
                    if (y < yPillar) return z > sh.zDoorFront ? Skin::Glass : Skin::Paint;
                    return Skin::Glass;
                }
                if (seg == 19) return Skin::Paint;   // rail
                if (z < sh.zCowl + 0.03f) return Skin::Paint;
                return y > yPillar ? Skin::Glass : Skin::Paint;
            }
            if (z < sh.zSideGlassRear) {
                if (tumble) {
                    if (std::fabs(z - sh.zB) < 0.055f) return Skin::Gloss;
                    if (y > rail - 0.03f) return Skin::Paint;
                    return Skin::Glass;
                }
                return Skin::Paint;   // roof
            }
            // Tail zone.
            if (tumble) return Skin::Paint;   // C-pillar / quarter panel
            if (seg >= 20 && z > sh.zRoofRear + 0.03f && z < sh.zRearWindowBottom - 0.005f) {
                if (sh.s.body == CarStyle::Body::Sedan && seg == 20) return Skin::Paint;
                return Skin::Glass;
            }
            (void)belt;
            return Skin::Paint;
        }

        void CopyQuad(MeshData& dst, const SkinGrid& skin, const int r, const int seg, const float offset, const bool flip)
        {
            const int n = Ring::kPoints;
            const int s1 = (seg + 1) % n;
            const auto& a0 = skin.rings[static_cast<std::size_t>(r)];
            const auto& a1 = skin.rings[static_cast<std::size_t>(r + 1)];
            const auto& n0 = skin.normals[static_cast<std::size_t>(r)];
            const auto& n1 = skin.normals[static_cast<std::size_t>(r + 1)];
            const float v0 = skin.V(skin.stations[static_cast<std::size_t>(r)]);
            const float v1 = skin.V(skin.stations[static_cast<std::size_t>(r + 1)]);
            const float uA = skin.u[static_cast<std::size_t>(seg)];
            const float uB = s1 == 0 ? 1.0f : skin.u[static_cast<std::size_t>(s1)];
            const float sign = flip ? -1.0f : 1.0f;
            const auto add = [&](const Vector3& p, const Vector3& nn, float uu, float vv) {
                return dst.AddVertex(p + nn * offset, nn * sign, Vector2(uu, vv), kWhite);
            };
            const std::uint32_t i00 = add(a0[static_cast<std::size_t>(seg)], n0[static_cast<std::size_t>(seg)], uA, v0);
            const std::uint32_t i01 = add(a0[static_cast<std::size_t>(s1)], n0[static_cast<std::size_t>(s1)], uB, v0);
            const std::uint32_t i11 = add(a1[static_cast<std::size_t>(s1)], n1[static_cast<std::size_t>(s1)], uB, v1);
            const std::uint32_t i10 = add(a1[static_cast<std::size_t>(seg)], n1[static_cast<std::size_t>(seg)], uA, v1);
            if (flip) dst.AddQuad(i00, i10, i11, i01); else dst.AddQuad(i00, i01, i11, i10);
        }

        /// Builds a decal mesh on the body surface inside a (u, v) polygon.
        void BuildDecal(MeshData& mesh, const SkinGrid& skin, const std::vector<Vector2>& polygon, const float lift, const float uMetres,
                        const float vMetres)
        {
            if (polygon.size() < 3) return;
            // Boundary subdivided to ~2 cm.
            std::vector<Vector2> boundary;
            for (std::size_t i = 0; i < polygon.size(); ++i) {
                const Vector2& a = polygon[i];
                const Vector2& b = polygon[(i + 1) % polygon.size()];
                const float len = std::hypot((b.X - a.X) * uMetres, (b.Y - a.Y) * vMetres);
                const int n = std::max(1, static_cast<int>(len / 0.02f));
                for (int k = 0; k < n; ++k) {
                    const float t = static_cast<float>(k) / static_cast<float>(n);
                    boundary.push_back(Vector2::Lerp(a, b, t));
                }
            }
            Vector2 centre(0.0f, 0.0f);
            for (const auto& p : boundary) centre = centre + p;
            centre = centre * (1.0f / static_cast<float>(boundary.size()));
            const float scales[] = {1.0f, 0.72f, 0.45f, 0.2f};
            MeshData local;
            std::vector<std::vector<std::uint32_t>> loops;
            for (const float s : scales) {
                std::vector<std::uint32_t> loop;
                for (const auto& p : boundary) {
                    const Vector2 q = centre + (p - centre) * s;
                    Vector3 pos, nrm;
                    skin.Sample(q.X, q.Y, pos, nrm);
                    loop.push_back(local.AddVertex(pos + nrm * lift, nrm, Vector2((q.X - centre.X) * uMetres * 2.0f + 0.5f, (q.Y - centre.Y) * vMetres * 2.0f + 0.5f), kWhite));
                }
                loops.push_back(std::move(loop));
            }
            Vector3 cpos, cnrm;
            skin.Sample(centre.X, centre.Y, cpos, cnrm);
            const std::uint32_t c = local.AddVertex(cpos + cnrm * lift, cnrm, Vector2(0.5f, 0.5f), kWhite);
            const std::size_t nb = boundary.size();
            // Orientation: emitted normal must follow the sampled normal.
            const auto emitted = [&](std::uint32_t a, std::uint32_t b, std::uint32_t cc) {
                const Vector3& pa = local.vertices[a].position;
                return Vector3::Cross(local.vertices[b].position - pa, local.vertices[cc].position - pa);
            };
            const bool flip = Vector3::Dot(emitted(loops[0][0], loops[0][1 % nb], loops[1][0]), local.vertices[loops[0][0]].normal) < 0.0f;
            for (std::size_t l = 0; l + 1 < loops.size(); ++l) {
                for (std::size_t i = 0; i < nb; ++i) {
                    const std::size_t k = (i + 1) % nb;
                    if (flip) local.AddQuad(loops[l][i], loops[l + 1][i], loops[l + 1][k], loops[l][k]);
                    else local.AddQuad(loops[l][i], loops[l][k], loops[l + 1][k], loops[l + 1][i]);
                }
            }
            const auto& inner = loops.back();
            for (std::size_t i = 0; i < nb; ++i) {
                const std::size_t k = (i + 1) % nb;
                if (flip) local.AddTriangle(c, inner[k], inner[i]); else local.AddTriangle(c, inner[i], inner[k]);
            }
            mesh.Append(local, Matrix::getIdentityProperty());
        }

        /// Face grid (front or rear) inside the tip ring, zipped to the ring. Materials by cell.
        struct FaceCell
        {
            float x0, x1, y0, y1;
            Skin skin;
            float recess;
        };

        void BuildFace(CarModel& model, const Shape& sh, const SkinGrid& skin, const bool front, std::map<CarMaterial, CarPart*>& parts,
                       const std::vector<float>& xsIn, const std::vector<float>& ysIn,
                       const std::function<std::pair<CarMaterial, float>(float, float)>& classify)
        {
            const std::size_t rIdx = front ? 0 : skin.rings.size() - 1;
            const auto& ring = skin.rings[rIdx];
            const auto& nrms = skin.normals[rIdx];
            float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
            for (const auto& p : ring) {
                minX = std::min(minX, p.X); maxX = std::max(maxX, p.X);
                minY = std::min(minY, p.Y); maxY = std::max(maxY, p.Y);
            }
            const float inset = 0.012f;
            std::vector<float> xs = xsIn, ys = ysIn;
            xs.push_back(minX + inset); xs.push_back(maxX - inset);
            ys.push_back(minY + inset); ys.push_back(maxY - inset);
            const auto tidy = [](std::vector<float>& v, float lo, float hi) {
                v.erase(std::remove_if(v.begin(), v.end(), [&](float x) { return x < lo || x > hi; }), v.end());
                std::sort(v.begin(), v.end());
                v.erase(std::unique(v.begin(), v.end(), [](float a, float b) { return std::fabs(a - b) < 0.004f; }), v.end());
            };
            tidy(xs, minX + inset - 1e-4f, maxX - inset + 1e-4f);
            tidy(ys, minY + inset - 1e-4f, maxY - inset + 1e-4f);
            // Fill gaps wider than 8 cm.
            const auto fill = [](std::vector<float>& v) {
                std::vector<float> out;
                for (std::size_t i = 0; i + 1 < v.size(); ++i) {
                    out.push_back(v[i]);
                    const int n = static_cast<int>((v[i + 1] - v[i]) / 0.08f);
                    for (int k = 1; k <= n; ++k) out.push_back(v[i] + (v[i + 1] - v[i]) * static_cast<float>(k) / static_cast<float>(n + 1));
                }
                out.push_back(v.back());
                v.swap(out);
            };
            fill(xs);
            fill(ys);
            const float zTip = front ? sh.zF : sh.zR;
            const float dir = front ? -1.0f : 1.0f;   // outward face normal direction (z)
            const auto faceZ = [&](float x, float y) {
                const float setback = front ? FrontSetback(sh, y) : RearSetback(sh, y);
                const float cx = 0.5f * (minX + maxX);
                const float hw = std::max(0.1f, 0.5f * (maxX - minX));
                const float t = (x - cx) / hw;
                return zTip - dir * setback - dir * 0.02f * t * t;
            };
            // Per material: one mesh with the grid cells of that material (with their recess).
            std::map<CarMaterial, MeshData> cells;
            std::map<CarMaterial, std::vector<std::pair<std::pair<int, int>, float>>> dummy;
            (void)dummy;
            const Vector3 n(0.0f, 0.0f, dir);
            for (std::size_t j = 0; j + 1 < ys.size(); ++j) {
                for (std::size_t i = 0; i + 1 < xs.size(); ++i) {
                    const float x0 = xs[i], x1 = xs[i + 1], y0 = ys[j], y1 = ys[j + 1];
                    const auto [material, recess] = classify(0.5f * (x0 + x1), 0.5f * (y0 + y1));
                    MeshData& m = cells[material];
                    const float rz = -dir * recess;
                    const Vector3 a(x0, y0, faceZ(x0, y0) + rz), b(x1, y0, faceZ(x1, y0) + rz), c(x1, y1, faceZ(x1, y1) + rz), d(x0, y1, faceZ(x0, y1) + rz);
                    const Vector2 uvA(x0 * 2.0f, y0 * 2.0f), uvB(x1 * 2.0f, y0 * 2.0f), uvC(x1 * 2.0f, y1 * 2.0f), uvD(x0 * 2.0f, y1 * 2.0f);
                    if (front) {
                        // Seen from -z (front), +x is on the viewer's left: a(x0,y0) -> d(x0,y1) -> c -> b is counter-clockwise.
                        m.AddQuad(a, d, c, b, n, uvA, uvD, uvC, uvB);
                    } else {
                        m.AddQuad(a, b, c, d, n, uvA, uvB, uvC, uvD);
                    }
                    if (recess > 0.001f) {
                        // Recess walls so the cut does not show the inside.
                        const Vector3 lift(0.0f, 0.0f, -rz);
                        const auto wall = [&](const Vector3& p, const Vector3& q, const Vector3& wn) {
                            if (front) m.AddQuad(p, q, q + lift, p + lift, wn, Vector2(0, 0), Vector2(1, 0), Vector2(1, 1), Vector2(0, 1));
                            else m.AddQuad(q, p, p + lift, q + lift, wn, Vector2(0, 0), Vector2(1, 0), Vector2(1, 1), Vector2(0, 1));
                        };
                        // Only add walls at cell edges facing a different material; approximated by all edges (cheap).
                        wall(a, b, Vector3(0, 1, 0));
                        wall(c, d, Vector3(0, -1, 0));
                        wall(b, c, Vector3(-1, 0, 0));
                        wall(d, a, Vector3(1, 0, 0));
                    }
                }
            }
            // Zipper between the tip ring and the grid boundary (both start at the bottom centre, right side first).
            std::vector<Vector3> boundary;
            const float xc = 0.5f * (xs.front() + xs.back());
            // Right side going up: bottom edge from centre to +x, then up the +x edge, across the top to -x, down and back to centre.
            for (std::size_t i = 0; i < xs.size(); ++i) if (xs[i] >= xc - 1e-4f) boundary.emplace_back(xs[i], ys.front(), 0.0f);
            for (std::size_t j = 1; j < ys.size(); ++j) boundary.emplace_back(xs.back(), ys[j], 0.0f);
            for (std::size_t i = xs.size() - 1; i-- > 0;) boundary.emplace_back(xs[i], ys.back(), 0.0f);
            for (std::size_t j = ys.size() - 1; j-- > 0;) boundary.emplace_back(xs.front(), ys[j], 0.0f);
            for (std::size_t i = 1; i < xs.size(); ++i) if (xs[i] < xc - 1e-4f) boundary.emplace_back(xs[i], ys.front(), 0.0f);
            MeshData band;
            std::vector<std::uint32_t> loopRing, loopGrid;
            for (std::size_t i = 0; i < ring.size(); ++i) {
                loopRing.push_back(band.AddVertex(ring[i], nrms[i], Vector2(skin.u[i], front ? 0.0f : 1.0f), kWhite));
            }
            for (auto& p : boundary) {
                p.Z = faceZ(p.X, p.Y);
                loopGrid.push_back(band.AddVertex(p, n, Vector2(p.X * 2.0f, p.Y * 2.0f), kWhite));
            }
            // Orientation: the ring runs up the right side first; seen from the front (-z) that is clockwise on screen.
            AddZipper(band, loopRing, loopGrid, front);
            // The zipper band is painted or black by height (bumper band), split per triangle.
            for (std::size_t t = 0; t < band.TriangleCount(); ++t) {
                float cy = 0.0f;
                std::uint32_t ids[3];
                for (int k = 0; k < 3; ++k) {
                    const MeshVertex& v = band.vertices[band.indices[t * 3 + static_cast<std::size_t>(k)]];
                    cy += v.position.Y / 3.0f;
                }
                const auto [material, recess] = classify(0.0f, cy);
                MeshData& m = cells[material == CarMaterial::BlackTrim ? CarMaterial::BlackTrim : CarMaterial::Paint];
                for (int k = 0; k < 3; ++k) ids[k] = m.AddVertex(band.vertices[band.indices[t * 3 + static_cast<std::size_t>(k)]]);
                m.indices.push_back(ids[0]);
                m.indices.push_back(ids[1]);
                m.indices.push_back(ids[2]);
                (void)recess;
            }
            for (auto& [material, m] : cells) {
                CarPart* part = parts[material];
                if (part) part->mesh.Append(m, Matrix::getIdentityProperty());
            }
            (void)model;
        }
    }

    // ================================================================== generator

    CarModel GenerateCar(const Sim::VehicleDefinition& definition)
    {
        return GenerateCar(CarStyle::FromDefinition(definition), &definition, true);
    }

    CarModel GenerateCar(const CarStyle& style, const Sim::VehicleDefinition* definition, const bool interior)
    {
        if (style.Heavy()) {
            return GenerateHeavyVehicle(style);
        }
        CarModel model;
        model.style = style;
        model.wheelRadius = style.wheelRadius;
        const Shape sh = MakeShape(style);
        const SkinGrid skin = BuildSkin(sh);
        const float uMetres = 2.0f * (style.width + style.height);   // approximate ring perimeter
        const float vMetres = style.length;

        // ---- Parts -----------------------------------------------------------------------
        CarPart paint = MakePart("body_paint", CarMaterial::Paint);
        CarPart glass = MakePart("body_glass", CarMaterial::Glass);
        CarPart trim = MakePart("body_trim", CarMaterial::BlackTrim);
        CarPart gloss = MakePart("body_gloss", CarMaterial::GlossBlack);
        CarPart housings = MakePart("lamp_housings", CarMaterial::GlossBlack);
        CarPart grille = MakePart("grille", CarMaterial::Grille);
        CarPart chrome = MakePart("chrome", CarMaterial::Chrome);
        // One part per wing mirror, so each can show its own mirror image from the cockpit.
        CarPart mirrorGlassL = MakePart("mirror_glass_left", CarMaterial::MirrorGlass);
        CarPart mirrorGlassR = MakePart("mirror_glass_right", CarMaterial::MirrorGlass);
        CarPart plates = MakePart("plates", CarMaterial::Plate);
        CarPart lampHead = MakePart("lamp_head", CarMaterial::LampHead);
        CarPart lampTail = MakePart("lamp_tail", CarMaterial::LampTail);
        CarPart lampReverse = MakePart("lamp_reverse", CarMaterial::LampReverse);
        CarPart indLF = MakePart("indicator_left_front", CarMaterial::LampIndicator);
        CarPart indRF = MakePart("indicator_right_front", CarMaterial::LampIndicator);
        CarPart indLR = MakePart("indicator_left_rear", CarMaterial::LampIndicator);
        CarPart indRR = MakePart("indicator_right_rear", CarMaterial::LampIndicator);
        CarPart repeaterL = MakePart("repeater_left", CarMaterial::LampIndicator);
        CarPart repeaterR = MakePart("repeater_right", CarMaterial::LampIndicator);
        repeaterL.detail = repeaterR.detail = true;

        // ---- Decal regions (u, v) ---------------------------------------------------------
        const float uBelt = skin.u[Ring::kBelt];
        const float uGlassBase = skin.u[Ring::kGlassBase];
        const float uDoor9 = skin.u[Ring::kDoorFirst + 3];
        const float uDoor10 = skin.u[Ring::kDoorFirst + 4];
        const float uRail = skin.u[Ring::kRail];
        const float uCrown1 = skin.u[Ring::kCrownFirst];
        const auto mirrorU = [](std::vector<Vector2> poly) {
            for (auto& p : poly) p.X = 1.0f - p.X;
            std::reverse(poly.begin(), poly.end());
            return poly;
        };
        std::vector<DecalRegion> decals;
        {
            // Headlamp: wraps from the fender shoulder over the nose corner, longer along the hood edge.
            const float v0 = skin.V(sh.zF + 0.015f);
            std::vector<Vector2> head = {{uBelt - 0.012f, v0}, {uBelt - 0.012f, skin.V(sh.zF + 0.22f)}, {uGlassBase + 0.012f, skin.V(sh.zF + 0.40f)},
                                         {uRail + 0.004f, skin.V(sh.zF + 0.44f)}, {uCrown1 + 0.006f, skin.V(sh.zF + 0.30f)}, {uCrown1 + 0.006f, v0}};
            decals.push_back({head, CarMaterial::LampHead, "head_right", 0.006f, false});
            decals.push_back({mirrorU(head), CarMaterial::LampHead, "head_left", 0.006f, false});
            // Tail lamp cluster: tall unit on the tail corner. Split into red / amber / white bands by u.
            const float v1 = skin.V(sh.zR - 0.012f);
            const float vT = skin.V(sh.zR - 0.30f);
            const float vT2 = skin.V(sh.zR - 0.34f);
            std::vector<Vector2> red = {{uGlassBase - 0.004f, v1}, {uGlassBase - 0.004f, vT2}, {uCrown1 + 0.006f, vT}, {uCrown1 + 0.006f, v1}};
            std::vector<Vector2> amber = {{uDoor10 - 0.002f, v1}, {uDoor10 - 0.002f, vT2}, {uGlassBase - 0.004f, vT2}, {uGlassBase - 0.004f, v1}};
            const float uWhite0 = 0.5f * (uDoor9 - 0.004f + uDoor10 - 0.002f);   // reversing lamp: a narrow inner segment
            std::vector<Vector2> white = {{uWhite0, v1}, {uWhite0, vT2}, {uDoor10 - 0.002f, vT2}, {uDoor10 - 0.002f, v1}};
            decals.push_back({red, CarMaterial::LampTail, "tail_right", 0.006f, false});
            decals.push_back({mirrorU(red), CarMaterial::LampTail, "tail_left", 0.006f, false});
            decals.push_back({amber, CarMaterial::LampIndicator, "indicator_right_rear", 0.006f, false});
            decals.push_back({mirrorU(amber), CarMaterial::LampIndicator, "indicator_left_rear", 0.006f, false});
            decals.push_back({white, CarMaterial::LampReverse, "reverse_right", 0.006f, false});
            decals.push_back({mirrorU(white), CarMaterial::LampReverse, "reverse_left", 0.006f, false});
        }

        // ---- Skin classification ---------------------------------------------------------
        const int nPts = Ring::kPoints;
        std::vector<std::vector<CarMaterial>> skinMaterials(skin.rings.size() - 1, std::vector<CarMaterial>(static_cast<std::size_t>(nPts), CarMaterial::Paint));
        for (std::size_t r = 0; r + 1 < skin.rings.size(); ++r) {
            const float z0 = skin.stations[r], z1 = skin.stations[r + 1];
            const float zc = 0.5f * (z0 + z1);
            const float archTop = sh.ArchTop(zc);
            const float rail = sh.Rail(zc);
            const float belt = sh.Belt(zc);
            const float vc = 0.5f * (skin.V(z0) + skin.V(z1));
            for (int seg = 0; seg < nPts; ++seg) {
                const int s1 = (seg + 1) % nPts;
                const Vector3 c = (skin.rings[r][static_cast<std::size_t>(seg)] + skin.rings[r][static_cast<std::size_t>(s1)] +
                                   skin.rings[r + 1][static_cast<std::size_t>(seg)] + skin.rings[r + 1][static_cast<std::size_t>(s1)]) * 0.25f;
                const float uA = skin.u[static_cast<std::size_t>(seg)];
                const float uB = s1 == 0 ? 1.0f : skin.u[static_cast<std::size_t>(s1)];
                const Vector2 uv(0.5f * (uA + uB), vc);
                Skin sk = ClassifySkin(sh, Ring::RightSegment(seg), c, archTop, rail, belt);
                // A quad becomes a recessed lamp housing only when all four of its corners are
                // inside the lens polygon. Testing the centre instead let housing quads stick out
                // past the lens, which showed as black notches around the headlamps, and let paint
                // quads intrude under the lens edge as red slivers.
                const float v0 = skin.V(z0);
                const float v1 = skin.V(z1);
                bool cut = false;
                for (const auto& d : decals) {
                    if (InsidePolygon(Vector2(uA, v0), d.polygon) && InsidePolygon(Vector2(uB, v0), d.polygon) &&
                        InsidePolygon(Vector2(uA, v1), d.polygon) && InsidePolygon(Vector2(uB, v1), d.polygon) &&
                        InsidePolygon(uv, d.polygon)) { cut = true; break; }
                }
                CarMaterial material = CarMaterial::Paint;
                switch (sk) {
                    case Skin::Paint: material = CarMaterial::Paint; break;
                    case Skin::Glass: material = CarMaterial::Glass; break;
                    case Skin::Black: material = CarMaterial::BlackTrim; break;
                    case Skin::Gloss: material = CarMaterial::GlossBlack; break;
                    case Skin::Underbody: material = CarMaterial::BlackTrim; break;
                    default: break;
                }
                if (cut) {
                    CopyQuad(housings.mesh, skin, static_cast<int>(r), seg, -0.022f, false);
                    skinMaterials[r][static_cast<std::size_t>(seg)] = CarMaterial::GlossBlack;
                    continue;
                }
                skinMaterials[r][static_cast<std::size_t>(seg)] = material;
                switch (material) {
                    case CarMaterial::Glass: CopyQuad(glass.mesh, skin, static_cast<int>(r), seg, -0.0015f, false); break;
                    case CarMaterial::BlackTrim: CopyQuad(trim.mesh, skin, static_cast<int>(r), seg, 0.0f, false); break;
                    case CarMaterial::GlossBlack: CopyQuad(gloss.mesh, skin, static_cast<int>(r), seg, -0.004f, false); break;
                    default: CopyQuad(paint.mesh, skin, static_cast<int>(r), seg, 0.0f, false); break;
                }
            }
        }
        // Lens decals and their glow anchors.
        for (const auto& d : decals) {
            CarPart* target = nullptr;
            if (d.lens == CarMaterial::LampHead) target = &lampHead;
            else if (d.lens == CarMaterial::LampTail) target = &lampTail;
            else if (d.lens == CarMaterial::LampReverse) target = &lampReverse;
            else if (d.name.find("left") != std::string::npos) target = &indLR;
            else target = &indRR;
            BuildDecal(target->mesh, skin, d.polygon, d.lift, uMetres, vMetres);
            Vector2 centre(0.0f, 0.0f);
            for (const auto& q : d.polygon) centre = centre + q;
            centre = centre * (1.0f / static_cast<float>(d.polygon.size()));
            LampGlow glow;
            skin.Sample(centre.X, centre.Y, glow.position, glow.normal);
            glow.kind = d.lens;
            glow.left = d.name.find("left") != std::string::npos;
            model.lamps.push_back(glow);
        }

        // ---- Front and rear faces --------------------------------------------------------
        const float bumperTop = sh.ybot + 0.17f;
        {
            std::map<CarMaterial, CarPart*> parts = {{CarMaterial::Paint, &paint}, {CarMaterial::BlackTrim, &trim}, {CarMaterial::Grille, &grille},
                                                     {CarMaterial::GlossBlack, &gloss}, {CarMaterial::Glass, &glass}};
            const float W = style.width * 0.5f;
            const float gy0 = sh.ybot + 0.39f, gy1 = sh.ybot + 0.50f;        // upper grille band
            const float iy0 = sh.ybot + 0.08f, iy1 = sh.ybot + 0.22f;        // lower intake
            const float py0 = sh.ybot + 0.245f, py1 = sh.ybot + 0.365f;      // plate recess
            const float gx = W * 0.42f, ix = W * 0.56f, px = 0.29f;
            const std::vector<float> xs = {-ix, -gx, -px, 0.0f, px, gx, ix};
            const std::vector<float> ys = {iy0, iy1, py0, py1, gy0, gy1, bumperTop};
            BuildFace(model, sh, skin, true, parts, xs, ys, [&](float x, float y) -> std::pair<CarMaterial, float> {
                if (std::fabs(x) < gx && y > gy0 && y < gy1) return {CarMaterial::Grille, 0.03f};
                if (std::fabs(x) < ix && y > iy0 && y < iy1) return {CarMaterial::Grille, 0.025f};
                if (std::fabs(x) < px && y > py0 && y < py1) return {CarMaterial::BlackTrim, 0.014f};
                if (y < bumperTop) return {CarMaterial::BlackTrim, 0.0f};
                return {CarMaterial::Paint, 0.0f};
            });
            model.frontPlateCenter = Vector3(0.0f, 0.5f * (py0 + py1), sh.zF - FrontSetback(sh, 0.5f * (py0 + py1)) + 0.014f - 0.004f);
            // Rear face: plate recess on the tailgate, lower black bumper band, optional window (estate / van).
            const float ry0 = sh.ybot + 0.30f, ry1 = sh.ybot + 0.42f;
            const std::vector<float> rxs = {-px, 0.0f, px, -(W - 0.14f), W - 0.14f};
            std::vector<float> rys = {ry0, ry1, bumperTop};
            if (sh.rearFaceGlassBottom > 0.0f) rys.push_back(sh.rearFaceGlassBottom);
            BuildFace(model, sh, skin, false, parts, rxs, rys, [&](float x, float y) -> std::pair<CarMaterial, float> {
                if (sh.rearFaceGlassBottom > 0.0f && y > sh.rearFaceGlassBottom && std::fabs(x) < W - 0.14f) return {CarMaterial::Glass, 0.002f};
                if (std::fabs(x) < px && y > ry0 && y < ry1) return {CarMaterial::BlackTrim, 0.014f};
                if (y < bumperTop) return {CarMaterial::BlackTrim, 0.0f};
                return {CarMaterial::Paint, 0.0f};
            });
            model.rearPlateCenter = Vector3(0.0f, 0.5f * (ry0 + ry1), sh.zR + RearSetback(sh, 0.5f * (ry0 + ry1)) - 0.014f + 0.004f);
        }

        // ---- Plates ----------------------------------------------------------------------
        {
            const float plateW = 0.52f, plateH = 0.11f;
            const Vector3 f = model.frontPlateCenter;
            plates.mesh.AddQuad(f + Vector3(-plateW * 0.5f, -plateH * 0.5f, 0), f + Vector3(-plateW * 0.5f, plateH * 0.5f, 0),
                                f + Vector3(plateW * 0.5f, plateH * 0.5f, 0), f + Vector3(plateW * 0.5f, -plateH * 0.5f, 0),
                                Vector3(0, 0, -1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0), Vector2(0, 1));
            const Vector3 b = model.rearPlateCenter;
            plates.mesh.AddQuad(b + Vector3(plateW * 0.5f, -plateH * 0.5f, 0), b + Vector3(plateW * 0.5f, plateH * 0.5f, 0),
                                b + Vector3(-plateW * 0.5f, plateH * 0.5f, 0), b + Vector3(-plateW * 0.5f, -plateH * 0.5f, 0),
                                Vector3(0, 0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0), Vector2(0, 1));
        }

        // ---- Exterior details ------------------------------------------------------------
        {
            // Mirrors: flattened ellipsoid housing on a short stalk at the front door's leading edge.
            const float mz = sh.zDoorFront + 0.12f;
            const float my = sh.Belt(mz) + 0.07f;
            for (const float side : {-1.0f, 1.0f}) {
                const float xBody = sh.SideX(mz, sh.Belt(mz));
                const Vector3 centre(side * (xBody + 0.115f), my, mz);
                AddEllipsoid(trim.mesh, centre, Vector3(0.10f, 0.062f, 0.048f), 8, 16);
                MeshData stalk;
                stalk.AddBox(Vector3(-0.045f, -0.012f, -0.03f), Vector3(0.045f, 0.012f, 0.03f), 1.0f);
                stalk.Transform(Matrix::CreateTranslation(side * (xBody + 0.035f), my - 0.02f, mz));
                trim.mesh.Append(stalk, Matrix::getIdentityProperty());
                // Mirror glass on the rear face of the housing. A wing mirror is convex, and it
                // has to be modelled that way: a flat quad reflects one direction of the sky cube
                // over its whole face and reads as a blank grey card. The bulge fans the normals
                // so the face carries sky at the top and ground at the bottom, and it is aimed a
                // little outboard, where a driver would set it.
                const Vector3 g = centre + Vector3(0.0f, 0.0f, 0.049f);
                CarPart& mirrorGlass = side < 0.0f ? mirrorGlassL : mirrorGlassR;
                model.wingMirrors[side < 0.0f ? 0 : 1] = WingMirror{g, side * 0.20f};
                const float halfW = 0.085f;
                const float halfH = 0.05f;
                const float bulge = 0.014f;                  // sagitta across the face
                const float aim = side * 0.20f;              // radians, outboard
                const int cols = 5;
                const int rows = 4;
                std::vector<std::vector<std::uint32_t>> grid(static_cast<std::size_t>(rows) + 1);
                for (int iy = 0; iy <= rows; ++iy) {
                    grid[static_cast<std::size_t>(iy)].resize(static_cast<std::size_t>(cols) + 1);
                    const float fy = static_cast<float>(iy) / static_cast<float>(rows) * 2.0f - 1.0f;
                    for (int ix = 0; ix <= cols; ++ix) {
                        const float fx = static_cast<float>(ix) / static_cast<float>(cols) * 2.0f - 1.0f;
                        const float out = bulge * (1.0f - fx * fx) * (1.0f - fy * fy);
                        Vector3 local(fx * halfW, fy * halfH, out);
                        Vector3 normal(-fx * bulge * 2.0f / halfW, -fy * bulge * 2.0f / halfH, 1.0f);
                        normal.Normalize();
                        const Matrix yaw = Matrix::CreateRotationY(aim);
                        grid[static_cast<std::size_t>(iy)][static_cast<std::size_t>(ix)] = mirrorGlass.mesh.AddVertex(
                            g + Vector3::TransformNormal(local, yaw), Vector3::TransformNormal(normal, yaw),
                            Vector2(0.5f + fx * 0.5f, 0.5f - fy * 0.5f), Color(255, 255, 255, 255));
                    }
                }
                for (int iy = 0; iy < rows; ++iy) {
                    for (int ix = 0; ix < cols; ++ix) {
                        const auto a = grid[static_cast<std::size_t>(iy)][static_cast<std::size_t>(ix)];
                        const auto b = grid[static_cast<std::size_t>(iy)][static_cast<std::size_t>(ix + 1)];
                        const auto c = grid[static_cast<std::size_t>(iy + 1)][static_cast<std::size_t>(ix + 1)];
                        const auto d = grid[static_cast<std::size_t>(iy + 1)][static_cast<std::size_t>(ix)];
                        // Front faces towards the driver behind the mirror; the opposite order
                        // was culled from every viewpoint that could see the glass.
                        mirrorGlass.mesh.AddQuad(c, d, a, b);
                    }
                }
            }
            // Door handles: flush pull handles with a dark finger recess.
            for (const float side : {-1.0f, 1.0f}) {
                for (const float zH : {sh.zB - 0.16f, sh.zDoorRear - 0.16f}) {
                    if (zH > sh.zSideGlassRear) continue;
                    const float yH = sh.Belt(zH) - 0.16f;
                    const float xH = sh.SideX(zH, yH);
                    MeshData handle;
                    AddRoundedBox(handle, Vector3(0.022f, 0.028f, 0.16f), 0.01f, Matrix::CreateTranslation(side * (xH + 0.004f), yH, zH));
                    paint.mesh.Append(handle, Matrix::getIdentityProperty());
                    MeshData recess;
                    recess.AddBox(Vector3(-0.004f, -0.03f, -0.10f), Vector3(0.004f, 0.03f, 0.10f), 1.0f);
                    recess.Transform(Matrix::CreateTranslation(side * (xH - 0.003f), yH - 0.004f, zH));
                    gloss.mesh.Append(recess, Matrix::getIdentityProperty());
                }
            }
            // Wipers on the windshield base. The player's car (built with its interior) gets
            // animated wipers from WindscreenRainRenderer instead.
            if (!interior) {
                const float zw = sh.zCowl + 0.09f;
                const float slope = std::atan2(sh.Top(zw + 0.2f) - sh.Top(zw), 0.2f);
                const float yw = sh.Top(zw) + 0.014f;
                for (const float side : {-0.42f, 0.12f}) {
                    const float yaw = side < 0.0f ? -0.35f : -0.35f;
                    AddOrientedBox(trim, Vector3(side, yw, zw), Vector3(0.62f, 0.012f, 0.03f), yaw, -slope);
                    AddOrientedBox(trim, Vector3(side - 0.15f, yw + 0.012f, zw + 0.02f), Vector3(0.10f, 0.012f, 0.014f), yaw, -slope);
                }
            }
            // Rear wiper on hatchbacks and estates.
            if (style.body == CarStyle::Body::Hatchback || style.body == CarStyle::Body::Estate || style.body == CarStyle::Body::Suv) {
                const float zr = std::min(sh.zRearWindowBottom - 0.06f, sh.zR - 0.1f);
                const float slope = std::atan2(sh.Top(zr) - sh.Top(zr - 0.15f), 0.15f);
                AddOrientedBox(trim, Vector3(0.10f, sh.Top(zr) + 0.012f, zr), Vector3(0.36f, 0.012f, 0.025f), -0.4f, slope);
            }
            // Exhaust, antenna, badges, fog lamps, side repeaters.
            chrome.mesh.AddCylinder(Vector3(0.36f, sh.ybot + 0.045f, sh.zR - 0.22f), Vector3(0, 0, 1), 0.026f, 0.26f, 12, true);
            trim.mesh.AddCylinder(Vector3(0.36f, sh.ybot + 0.045f, sh.zR - 0.5f), Vector3(0, 0, 1), 0.024f, 0.3f, 8, false);
            {
                const float za = sh.zRoofRear - 0.25f;
                MeshData antenna;
                antenna.AddCylinder(Vector3(0, 0, 0), Vector3(0, 0.82f, 0.57f), 0.006f, 0.30f, 6, true);
                antenna.AddCylinder(Vector3(0, -0.005f, 0), Vector3(0, 1, 0), 0.02f, 0.02f, 8, true);
                antenna.Transform(Matrix::CreateTranslation(0.0f, sh.Top(za) - 0.004f, za));
                trim.mesh.Append(antenna, Matrix::getIdentityProperty());
            }
            {
                const float by = sh.ybot + 0.545f;
                const float bz = sh.zF - FrontSetback(sh, by) - 0.006f;
                chrome.mesh.AddCylinder(Vector3(0.0f, by, bz + 0.02f), Vector3(0, 0, -1), 0.05f, 0.022f, 20, true);
                gloss.mesh.AddCylinder(Vector3(0.0f, by, bz + 0.015f), Vector3(0, 0, -1), 0.036f, 0.02f, 20, true);
                const float ry = sh.ybot + 0.56f;
                const float rz = sh.zR + RearSetback(sh, ry) + 0.004f;
                chrome.mesh.AddCylinder(Vector3(0.0f, ry, rz - 0.02f), Vector3(0, 0, 1), 0.04f, 0.022f, 20, true);
                gloss.mesh.AddCylinder(Vector3(0.0f, ry, rz - 0.015f), Vector3(0, 0, 1), 0.028f, 0.02f, 20, true);
            }
            for (const float side : {-1.0f, 1.0f}) {
                const float fy = sh.ybot + 0.15f;
                const float fx = side * (style.width * 0.5f - 0.33f);
                Vector3 fp(fx, fy, sh.zF - FrontSetback(sh, fy) + 0.005f);
                Vector3 fn(0.0f, 0.0f, -1.0f);
                FrontFacePoint(skin, fx, fy, fp, fn);
                chrome.mesh.AddTorus(fp + fn * 0.006f, fn, 0.052f, 0.008f, 20, 6);
                gloss.mesh.AddCylinder(fp - fn * 0.004f, fn, 0.05f, 0.022f, 20, true);
                // Side repeater on the front fender.
                const float sz = sh.zCowl - 0.42f;
                const float sy = sh.Belt(sz) - 0.10f;
                CarPart& rep = side < 0.0f ? repeaterL : repeaterR;
                AddBoxTo(rep, Vector3(side * (sh.SideX(sz, sy) + 0.004f), sy, sz), Vector3(0.012f, 0.026f, 0.07f));
            }
            // Front indicators: amber strip under the headlamp on the front corner.
            for (const float side : {-1.0f, 1.0f}) {
                CarPart& ind = side < 0.0f ? indLF : indRF;
                const float iy = sh.ybot + 0.50f;
                std::vector<Vector2> poly;
                const float uA = side > 0.0f ? uBelt - 0.05f : 1.0f - (uBelt - 0.05f);
                const float uB = side > 0.0f ? uBelt - 0.016f : 1.0f - (uBelt - 0.016f);
                poly = {{uA, skin.V(sh.zF + 0.03f)}, {uA, skin.V(sh.zF + 0.20f)}, {uB, skin.V(sh.zF + 0.20f)}, {uB, skin.V(sh.zF + 0.03f)}};
                if (side < 0.0f) std::reverse(poly.begin(), poly.end());
                BuildDecal(ind.mesh, skin, poly, 0.006f, uMetres, vMetres);
                LampGlow glow;
                skin.Sample(0.5f * (uA + uB), skin.V(sh.zF + 0.11f), glow.position, glow.normal);
                glow.kind = CarMaterial::LampIndicator;
                glow.left = side < 0.0f;
                model.lamps.push_back(glow);
                (void)iy;
            }
        }

        // ---- Wheels ----------------------------------------------------------------------
        const CarPart::Role wheelRoles[4] = {CarPart::Role::WheelFL, CarPart::Role::WheelFR, CarPart::Role::WheelRL, CarPart::Role::WheelRR};
        const char* wheelNames[4] = {"FL", "FR", "RL", "RR"};
        for (int i = 0; i < 4; ++i) {
            const float side = (i % 2 == 0) ? -1.0f : 1.0f;
            const float zw = i < 2 ? sh.zFA : sh.zRA;
            Vector3 centre(side * style.track * 0.5f, style.wheelRadius, zw);
            if (definition && definition->wheels.size() == 4) {
                centre = definition->wheels[static_cast<std::size_t>(i)].position;
            }
            model.wheelCenters[static_cast<std::size_t>(i)] = centre;
            MeshData tyreMesh, rimMesh, discMesh;
            BuildWheel(style, style.tyreWidth * 0.5f, tyreMesh, rimMesh, discMesh);
            if (side < 0.0f) {
                for (MeshData* m : {&tyreMesh, &rimMesh, &discMesh}) {
                    m->Transform(Matrix::CreateScale(-1.0f, 1.0f, 1.0f));
                    m->FlipWinding();
                    for (auto& v : m->vertices) v.normal = -v.normal;   // FlipWinding negated; the scale mirrored: restore outward
                }
            }
            CarPart tyre = MakePart(std::string("tyre_") + wheelNames[i], CarMaterial::Tyre, wheelRoles[i]);
            CarPart rim = MakePart(std::string("rim_") + wheelNames[i], CarMaterial::Rim, wheelRoles[i]);
            CarPart disc = MakePart(std::string("disc_") + wheelNames[i], CarMaterial::BrakeDisc, wheelRoles[i]);
            tyre.pivot = rim.pivot = disc.pivot = centre;
            tyre.axis = rim.axis = disc.axis = Vector3(1, 0, 0);
            tyre.mesh = std::move(tyreMesh);
            rim.mesh = std::move(rimMesh);
            disc.mesh = std::move(discMesh);
            disc.detail = true;
            model.parts.push_back(std::move(tyre));
            model.parts.push_back(std::move(rim));
            model.parts.push_back(std::move(disc));
            // Arch liner: dark half-shell above the wheel, closing the wheel well.
            CarPart liner = MakePart(std::string("arch_liner_") + wheelNames[i], CarMaterial::BlackTrim);
            const float archR = sh.archR - 0.01f;
            std::vector<std::vector<Vector3>> linerRings;
            for (int k = 0; k <= 14; ++k) {
                const float a = static_cast<float>(k) / 14.0f * kPi;
                const float y = centre.Y + std::sin(a) * archR;
                const float z = centre.Z + std::cos(a) * archR;
                const float xIn = side * (sh.xInner - 0.02f);
                const float xOut = side * (sh.SideX(z, std::min(y, sh.Belt(z))) + 0.01f);
                linerRings.push_back({Vector3(xIn, y, z), Vector3(xOut, y, z)});
            }
            liner.mesh.AddLoft(linerRings, false);
            liner.mesh.ComputeSmoothNormals();
            if (!liner.mesh.vertices.empty() && liner.mesh.vertices[1].normal.Y > 0.0f) {
                liner.mesh.FlipWinding();
            }
            model.parts.push_back(std::move(liner));
        }

        // ---- Underbody plate (closes the floor between the sills) ---------------------------
        {
            const float y = sh.ybot - 0.002f;
            const float x = sh.xInner - 0.05f;
            trim.mesh.AddQuad(Vector3(-x, y, sh.zR - 0.2f), Vector3(x, y, sh.zR - 0.2f), Vector3(x, y, sh.zF + 0.2f), Vector3(-x, y, sh.zF + 0.2f),
                              Vector3(0, -1, 0), Vector2(0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0));
        }

        // ---- UV layout for the paint detail texture ----------------------------------------
        {
            BodyUvLayout& uv = model.uv;
            uv.uUnderbody = skin.u[Ring::kUnderbodyEdge];
            uv.uRockerBottom = skin.u[Ring::kRockerBottom];
            uv.uRockerTop = skin.u[Ring::kRockerTop];
            uv.uDoorMid = skin.u[Ring::kDoorFirst + 2];
            uv.uBelt = uBelt;
            uv.uRoofRail = uRail;
            uv.uTop = skin.u[Ring::kTop];
            uv.vNose = 0.0f;
            uv.vHoodStart = skin.V(sh.zF + 0.16f);
            uv.vFrontBumper = skin.V(sh.zFrontBumper);
            uv.vCowl = skin.V(sh.zCowl);
            {
                const float zb = sh.zCowl + 0.07f;
                const float zt = sh.zRoofFront - 0.05f;
                const float hwb = std::max(0.2f, sh.HwRoof(zb) - 0.10f);
                const float hwt = std::max(0.2f, sh.HwRoof(zt) - 0.08f);
                model.windscreen = {Vector3(-hwb, sh.Top(zb), zb), Vector3(hwb, sh.Top(zb), zb), Vector3(hwt, sh.Top(zt), zt),
                                    Vector3(-hwt, sh.Top(zt), zt)};
            }
            uv.vDoorFront = skin.V(sh.zDoorFront);
            uv.vBPillar = skin.V(sh.zB);
            uv.vDoorRear = skin.V(sh.zDoorRear);
            uv.vTailgate = skin.V(sh.zRoofRear + 0.03f);
            uv.vRearBumper = skin.V(sh.zRearBumper);
            uv.vFrontArch = skin.V(sh.zFA);
            uv.vRearArch = skin.V(sh.zRA);
            uv.archHalfV = sh.archR * skin.vScale;
            uv.fuelFlapU = skin.u[Ring::kDoorFirst + 3];
            uv.fuelFlapV = skin.V(sh.zDoorRear + 0.22f);
        }

        // ---- Interior --------------------------------------------------------------------
        if (interior) {
            BuildCockpit(model, style, definition, skin, skinMaterials, sh.zCowl, sh.zRoofFront, sh.zSideGlassRear);
        } else {
            BuildCabinBlock(model, style, skin, skinMaterials, sh.zCowl, sh.zSideGlassRear);
        }

        model.bodyTriangles = static_cast<int>(paint.mesh.TriangleCount() + glass.mesh.TriangleCount() + trim.mesh.TriangleCount() + gloss.mesh.TriangleCount());
        for (CarPart* p : {&paint, &glass, &trim, &gloss, &housings, &grille, &chrome, &mirrorGlassL, &mirrorGlassR, &plates, &lampHead, &lampTail, &lampReverse,
                           &indLF, &indRF, &indLR, &indRR, &repeaterL, &repeaterR}) {
            if (p->mesh.TriangleCount() > 0) model.parts.push_back(std::move(*p));
        }
        for (auto& part : model.parts) {
            // Parts with authored per-vertex normals keep them; skin copies carry the grid normals.
            (void)part;
        }
        return model;
    }
}
