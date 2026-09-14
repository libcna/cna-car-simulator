// Instrument cluster rendered into a RenderTarget2D with SpriteBatch: speedometer, tachometer,
// fuel and coolant gauges, odometer/trip display, gear and mode, warning and indicator lamps.
// The vehicle renderer maps the target onto the cluster face inside the cockpit.
#pragma once

#include "CarSim/Render/BitmapFont.hpp"
#include "CarSim/Sim/Vehicle.hpp"
#include "CarSim/Sim/VehicleDefinition.hpp"

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

#include <memory>
#include <string>
#include <vector>

namespace CarSim::Render
{
    class InstrumentCluster
    {
    public:
        static constexpr int kWidth = 1024;
        static constexpr int kHeight = 448;

        InstrumentCluster(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const Sim::VehicleDefinition& definition,
                          const BitmapFont& gaugeFont, const BitmapFont& textFont, const BitmapFont& boldFont);

        /// Redraws the cluster for the current vehicle state. Leaves the back buffer bound afterwards.
        void Render(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, Microsoft::Xna::Framework::Graphics::SpriteBatch& batch,
                    const Sim::VehicleState& state, double timeSeconds);

        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* Texture() const;

        /// Lamp identifiers in drawing order (also used by tests of the lamp logic).
        enum class Lamp
        {
            IndicatorLeft,
            HighBeam,
            LowBeam,
            IndicatorRight,
            Handbrake,
            Coolant,
            Fuel,
            Battery,
            Engine,
            Count
        };

        /// Whether a lamp is lit for a given state (pure function of the vehicle state).
        [[nodiscard]] static bool LampLit(Lamp lamp, const Sim::VehicleState& state);

    private:
        struct Dial
        {
            Microsoft::Xna::Framework::Vector2 centre;
            float radius = 100.0f;
            float startDeg = -135.0f;   // needle angle at value 0 (0 = up, clockwise positive)
            float sweepDeg = 270.0f;
        };

        void BuildStatic(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, Microsoft::Xna::Framework::Graphics::SpriteBatch& batch);
        void DrawDialFace(Microsoft::Xna::Framework::Graphics::SpriteBatch& batch, const Dial& dial, float maxValue, float majorStep,
                          float minorStep, float labelScale, float labelDivisor, float redFrom) const;
        void DrawTick(Microsoft::Xna::Framework::Graphics::SpriteBatch& batch, const Dial& dial, float angleDeg, float innerRadius,
                      float length, float thickness, const Microsoft::Xna::Framework::Color& color) const;
        void DrawFace(Microsoft::Xna::Framework::Graphics::SpriteBatch& batch, const Dial& dial, float bezelWidth) const;
        void DrawNeedle(Microsoft::Xna::Framework::Graphics::SpriteBatch& batch, const Dial& dial, float fraction, float length,
                        const Microsoft::Xna::Framework::Color& color) const;
        void DrawLamp(Microsoft::Xna::Framework::Graphics::SpriteBatch& batch, Lamp lamp, Microsoft::Xna::Framework::Vector2 centre,
                      bool lit, float scale) const;

        const Sim::VehicleDefinition& definition_;
        const BitmapFont& gaugeFont_;
        const BitmapFont& textFont_;
        const BitmapFont& boldFont_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTarget2D> target_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTarget2D> background_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> white_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> disc_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> face_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> bezel_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> needle_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> icons_;
        Dial speedo_;
        Dial tacho_;
        Dial fuel_;
        Dial temp_;
        bool staticBuilt_ = false;
    };
}
