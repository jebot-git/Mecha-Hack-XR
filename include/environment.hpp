#pragma once
#include "vector.hpp"
#include <array>
#include <string_view>
#include <cstdint>
#include <vector>
#include <string>
#include <nlohmann/json_fwd.hpp>
namespace iron {
enum class Lighting { Authored, Day, Night };
enum class SkyKind { Legacy, Desert, Space, Underwater, Urban, Volcanic, Wilderness, Polar, Lunar, Interior };
enum class Weather { Clear, Fog, Rain, Storm, Sandstorm, IonStorm, CorruptionSporeBurst,
                     FireStorm, MeteorShower, MagneticBurst, AshCloud, RadioactiveWind, Snowstorm };
inline constexpr std::array<std::string_view,3> lightingNames{"authored","day","night"};
inline constexpr std::array weatherNames{
    std::string_view{"clear"},std::string_view{"fog"},std::string_view{"rain"},std::string_view{"storm"},
    std::string_view{"sandstorm"},std::string_view{"ion_storm"},std::string_view{"corruption_spore_burst"},
    std::string_view{"fire_storm"},std::string_view{"meteor_shower"},std::string_view{"magnetic_burst"},
    std::string_view{"ash_cloud"},std::string_view{"radioactive_wind"},std::string_view{"snowstorm"}};
struct WeatherHazard {
    std::string_view label;
    Vec3 haze,tint,particles;
    float density,cadence,warning,radius;
};
const WeatherHazard *weatherHazard(Weather);
struct EnvironmentSettings {Lighting lighting=Lighting::Authored;Weather weather=Weather::Clear;uint32_t seed=1;bool authoredSky=false;};
enum class Climate { Temperate, Desert, Coastal };
inline constexpr std::array<std::string_view,3> climateNames{"temperate","desert","coastal"};
Climate climateForBiome(std::string_view);
bool weatherAllowed(Climate,Weather);
EnvironmentSettings restrictEnvironment(EnvironmentSettings,Climate);
struct EnvironmentSample {EnvironmentSettings settings;float weatherSeconds=0;double hour=0;};
// One authoritative simulated world clock; regional weather uses the same front.
struct EnvironmentCycle {
    double seconds=0,daySeconds=1440,weatherSeconds=120,startHour=8;
    uint32_t seed=0x49535743u;
    void advance(double);
    EnvironmentSample sample(Climate)const;
};
EnvironmentCycle readEnvironmentCycle(const nlohmann::json&);
nlohmann::json environmentCycleWire(const EnvironmentCycle&);
std::string environmentDescription(const EnvironmentSample&,bool shared);
EnvironmentSettings readEnvironment(const nlohmann::json&);
nlohmann::json environmentWire(const EnvironmentSettings&);
struct EnvironmentVisuals {
    Vec3 sky{},ground{},fog{},tint{1,1,1};float density=0,solidGain=1,modelGain=1,precipitation=0;bool night=false;
};
EnvironmentVisuals environmentVisuals(const EnvironmentSettings&,bool authoredNight,Vec3 sky,Vec3 ground,float seconds);
enum class WeatherLayer { Precipitation, SandGrain, SandSheet, SandVeil, HazardStreak, HazardVeil };
struct WeatherSprite {
    Vec3 position{},colour{1,1,1},drift{};
    float width=1,height=1,opacity=1;unsigned frame=0;
    WeatherLayer layer=WeatherLayer::Precipitation;
};
inline constexpr const char* weatherSpriteAsset="smoke-atlas";
inline constexpr unsigned weatherSpriteFrames=10,weatherSpriteFps=10;
std::vector<WeatherSprite> weatherSprites(const EnvironmentSettings&,Vec3 anchor,float seconds);
float weatherStormPhase(const EnvironmentSettings&,float seconds);
bool weatherLightningActive(const EnvironmentSettings&,float seconds);
// Separate stateless visual stream: no gameplay RNG or replicated effects.
float weatherNoise(uint32_t seed,uint32_t index);
}
