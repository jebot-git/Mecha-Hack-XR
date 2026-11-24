#include "cockpit_glass.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
using namespace iron;
static float dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
int main(){auto mesh=cockpitGlazing();assert(mesh.panes.size()==5&&mesh.vertices.size()>18&&mesh.vertices.size()<200);
 for(const auto& v:mesh.vertices){assert(std::isfinite(length(v.position))&&std::abs(length(v.normal)-1)<1e-5);for(float u:v.uv)assert(u>=-.0001&&u<=1.0001);}
 auto contains=[&](unsigned pane,Vec3 point){const auto& p=mesh.panes[pane];return std::any_of(mesh.vertices.begin()+p.first,mesh.vertices.begin()+p.first+p.count,[&](const auto& v){return length(v.position-point)<1e-5;});};
 // Shared authored rail junctions: panes meet at the same 3D coordinates.
 for(float side:{-1.f,1.f}){unsigned id=side<0?1:2;assert(contains(0,{side*1.05f,.67f,-1.4f})&&contains(id,{side*1.05f,.67f,-1.4f})&&contains(3,{side*1.05f,.67f,-1.4f}));assert(contains(id,{side*.9f,.94f,-.55f})&&contains(3,{side*.9f,.94f,-.55f})&&contains(4,{side*.9f,.94f,-.55f}));assert(contains(id,{side*.88f,.88f,.65f})&&contains(4,{side*.88f,.88f,.65f}));}
 for(float x:{-1.f,-.42f,0.f,.47f,1.f})for(float y:{-.3f,.1f,.36f,.55f,.84f}){auto p=cockpitFrontGlass(x,y);bool onSurface=false;const auto& front=mesh.panes[0];
  for(unsigned i=front.first;i<front.first+front.count;i+=3){auto a=mesh.vertices[i].position,n=mesh.vertices[i].normal;float distance=std::abs(dot(p-a,n));onSurface|=distance<=.0016f;}assert(onSurface);}
 // Damage rays/forks may extend outside the original pane: every returned
 // point must still lie within one front triangle, with the small inset only.
 for(int ix=-18;ix<=18;++ix)for(int iy=-10;iy<=12;++iy){auto p=cockpitFrontGlass(ix*.1f,iy*.1f);bool inside=false;auto front=mesh.panes[0];
  for(unsigned i=front.first;i<front.first+front.count;i+=3){auto a=mesh.vertices[i].position,b=mesh.vertices[i+1].position,c=mesh.vertices[i+2].position;
   float d=(b.y-c.y)*(a.x-c.x)+(c.x-b.x)*(a.y-c.y);if(std::abs(d)<1e-8f)continue;
   float u=((b.y-c.y)*(p.x-c.x)+(c.x-b.x)*(p.y-c.y))/d,v=((c.y-a.y)*(p.x-c.x)+(a.x-c.x)*(p.y-c.y))/d,z=a.z*u+b.z*v+c.z*(1-u-v);
   inside|=u>=-.0002f&&v>=-.0002f&&1-u-v>=-.0002f&&std::abs(p.z-z-.0015f)<1e-5f;
  }assert(inside);
 }
 std::cout<<"Five canopy openings share rail junctions; finite metric UVs and damage projected inside glazing passed\n";
}
