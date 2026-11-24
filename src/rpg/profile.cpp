#include "config.hpp"
#include "rpg/rules.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif
#include <fstream>
#include <stdexcept>
namespace iron::rpg {
std::filesystem::path defaultProfilePath() {
#ifdef _WIN32
  if (auto root = _wgetenv(L"LOCALAPPDATA"); root && *root)
    return std::filesystem::path(root) / "mecha-hack-xr/profiles/default.json";
#endif
  if (const char *root = std::getenv("XDG_DATA_HOME"))
    return std::filesystem::path(root) / "mecha-hack-xr/profiles/default.json";
  if (const char *home = std::getenv("HOME"))
    return std::filesystem::path(home) /
           ".local/share/mecha-hack-xr/profiles/default.json";
  throw std::runtime_error("No profile storage directory");
}
void saveProfile(const std::filesystem::path &path, const Sheet &sheet,
                 const std::string &callsign) {
  validate(sheet);
  Sheet prepared = sheet;
  prepare(prepared);
  auto parent = path.parent_path();
  if (!parent.empty())
    std::filesystem::create_directories(parent);
  static std::atomic<unsigned> serial{0};
  auto temporary = path;
  temporary +=
      ".tmp." +
      std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()) +
      "." + std::to_string(serial++);
  {
    std::ofstream out(temporary);
    if (!out)
      throw std::runtime_error("Cannot write pilot profile");
    out << Json{{"schema", 9}, {"callsign", callsign}, {"sheet", prepared}}
               .dump(2)
        << '\n';
    out.flush();
    if (!out)
      throw std::runtime_error("Cannot flush pilot profile");
  }
  try {
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      throw std::runtime_error("Cannot replace pilot profile");
#else
    std::filesystem::rename(temporary, path);
#endif
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    throw;
  }
}
Sheet loadProfile(const std::filesystem::path &path, std::string *callsign) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in || in.tellg() > 1048576)
    throw std::runtime_error("Missing or oversized pilot profile");
  in.seekg(0);
  Json j;
  in >> j;
  if(!j.at("schema").is_number_integer()||j.at("schema")<2||j.at("schema")>9)
    throw std::runtime_error("Unsupported pilot profile");
  if (callsign)
    *callsign = validatePlayerName(j.at("callsign").get<std::string>());
  auto data=j.at("sheet");
  // Append-only catalogs preserve old enum IDs and stocks across the expansion.
  for(auto [key,count]:std::array<std::pair<const char*,size_t>,3>{{{"inventory",size_t(Gear::Count)},{"owned",size_t(Gear::Count)},{"oneShotStock",size_t(Module::Count)}}})
    if(data.contains(key)&&data[key].is_array()&&data[key].size()<count)
      while(data[key].size()<count)data[key].push_back(0);
  auto s = data.get<Sheet>();
  if(!data.contains("career"))importLegacyCareer(s);
  if(j.at("schema")==2)migrateProfileSheet(s);
  if(j.at("schema").get<int>()<4)refitMounts(s);
  if(j.at("schema").get<int>()<6&&!s.campaign.active&&s.level>1){
    s.campaign.active=true;s.campaign.seed=s.creationSeed?s.creationSeed:0x4d454348u;s.campaign.randomState=s.campaign.seed;
    int completed=std::min(9,s.level-1+int(s.pendingLevels>0));
    s.campaign.completed=(1u<<completed)-1;s.campaign.revision=unsigned(completed);s.campaign.awaitingAdvance=s.pendingLevels>0;
  }
  retireObsoleteEquipment(s);
  normalizeCombatProfile(s);
  validate(s);
  prepare(s);
  return s;
}
} // namespace iron::rpg
