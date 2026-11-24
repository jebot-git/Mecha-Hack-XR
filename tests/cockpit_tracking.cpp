#include "cockpit_tracking.hpp"
#include <cassert>
#include <iostream>
using namespace avp;
int main(){
    auto head=multiply(translation(.8f,1.9f,-.4f),rotation(.8f,.35f));
    auto relative=multiply(inverseRigid(iron::cockpitTrackingAnchor(head)),head);
    assert(std::hypot(relative[12],relative[13],relative[14])<1e-5);
    assert(std::abs(std::atan2(relative[8],relative[10]))<1e-5);
    assert(std::abs(std::asin(-relative[9])-.35f)<1e-5);
    auto moved=multiply(translation(.9f,2.f,-.2f),rotation(.8f,.35f));
    auto before=multiply(inverseRigid(iron::cockpitTrackingAnchor(head)),moved);
    assert(std::hypot(before[12],before[13],before[14])>.1f);
    auto after=multiply(inverseRigid(iron::cockpitTrackingAnchor(moved)),moved);
    assert(std::hypot(after[12],after[13],after[14])<1e-5);
    iron::RecenterPress press;
    assert(!press.update(true,true,true)); // startup-held control
    assert(!press.update(false,true,true));assert(press.update(true,true,true));
    assert(!press.update(true,true,true));assert(!press.update(true,false,true));
    assert(!press.update(true,true,true)); // held across focus recovery
    assert(!press.update(false,true,true));assert(press.update(true,true,true));
    assert(!press.update(false,true,false));assert(!press.update(true,true,true));
    std::cout<<"Cockpit XYZ/yaw reset preserves pitch; release and focus gating passed\n";
}
