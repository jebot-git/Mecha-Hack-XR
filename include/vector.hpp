#pragma once
#include <cmath>
namespace iron {
constexpr float pi = 3.14159265358979323846f;
struct Vec3 {
    float x=0,y=0,z=0;
    Vec3 operator+(Vec3 b) const { return {x+b.x,y+b.y,z+b.z}; }
    Vec3 operator-(Vec3 b) const { return {x-b.x,y-b.y,z-b.z}; }
    Vec3 operator*(float k) const { return {x*k,y*k,z*k}; }
};
inline float length(Vec3 a) { return std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z); }
inline Vec3 direction(float yaw,float pitch) { return {-std::sin(yaw)*std::cos(pitch),std::sin(pitch),-std::cos(yaw)*std::cos(pitch)}; }
inline float wrap(float a) { return std::remainder(a,2*pi); }
}
