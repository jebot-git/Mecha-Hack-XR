#pragma once
#include <filesystem>
#include <nlohmann/json_fwd.hpp>
struct SDL_Window;
namespace iron { class Renderer; }
// Native in-process simulation capture. No pilot persistence or transport.
void previewOpeners(iron::Renderer &, SDL_Window *,
                    const std::filesystem::path &, nlohmann::json &frames);
