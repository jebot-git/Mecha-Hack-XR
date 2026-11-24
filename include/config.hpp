#pragma once
#include <filesystem>
#include <string>
namespace iron {
inline constexpr const char* defaultLobbyServer="87.106.27.237:29597";
std::string validatePlayerName(std::string);
struct ClientConfig {
    std::string server=defaultLobbyServer,playerName="Pilot";
    float soundVolume=1,musicVolume=1;
    static std::filesystem::path defaultPath();
    void load(const std::filesystem::path&,bool required=false);
    void environment();
    void saveAudio(const std::filesystem::path&) const;
};
}
