#pragma once
#include "matrix.hpp"
#include <cmath>
#include <utility>
namespace iron {
// Capture translation and yaw only: recentering preserves head pitch/roll and
// changes the tracking reference, never the pilot's position in the map.
inline avp::Matrix cockpitTrackingAnchor(const avp::Matrix& head){
    return avp::multiply(avp::translation(head[12],head[13],head[14]),
                         avp::rotation(std::atan2(head[8],head[10])));
}
class RecenterPress {
    bool armed=false;
public:
    bool update(bool pressed,bool focused,bool cockpit){
        if(!focused||!cockpit){armed=false;return false;}
        if(!pressed){armed=true;return false;}
        return std::exchange(armed,false);
    }
};
}
