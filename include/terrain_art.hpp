#pragma once

namespace iron::terrainArt {
// The first eight layers follow the RPG environment catalogue.
enum Layer { Desert, Space, Underwater, Urban, Volcanic, Wilderness, Polar, Lunar,
             ServiceRoad, Arterial, Silt, InteriorDeck, Count };
inline constexpr int side = 128;
inline constexpr float textureMode = 4;
// Road modes also identify the substrate visible beyond a diagonal carriageway.
constexpr float roadMode(int ground,int road) {
    return 5.f+float(ground*2+(road==Arterial));
}
constexpr int surfaceLayer(float mode,float parameter) {
    return mode==textureMode?int(parameter):((int(mode)-5)%2?Arterial:ServiceRoad);
}
}
