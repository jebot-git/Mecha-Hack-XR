#pragma once

// Frame the span built by the normal mission generator, including its terrain,
// map dressing and approach ramps. These two fixed seeds are also traversal fixtures.
template<class MapView>
void captureCrossings(MapView view, Json &frames) {
    struct Fixture { const char *name; unsigned seed; int location; };
    for(auto sample : {Fixture{"canal-bridge",0,0}, Fixture{"road-overpass",3,6}}) {
        World world;
        buildMissionEnvironment(world,sample.seed,3,1000,{},sample.location,0,0,5,-1,2);
        world.mission=0;
        const auto count=world.interiorDecks.size();
        if(count<3) throw std::runtime_error("Missing crossing in gallery fixture");
        const auto &approach=world.interiorDecks[count-3];
        const auto &span=world.interiorDecks[count-2];
        const auto middle=(span.start+span.end)*.5f;
        const auto along=unit(span.end-span.start);
        const auto side=cross(along,Vec3{0,1,0});
        auto capture=[&](const char *suffix,Vec3 eye,Vec3 look) {
            view(world,std::string(sample.name)+suffix,eye,look-eye,false);
            frames.back()["seed"]=sample.seed;
            frames.back()["span_m"]=length(span.end-span.start);
            frames.back()["clearance_m"]=world.interiorCeilingAt({middle.x,world.surfaceHeight(middle.x,middle.z),middle.z})
                                         -world.surfaceHeight(middle.x,middle.z);
        };
        const float spanLength=length(span.end-span.start);
        const auto outside=middle+side*(spanLength*.58f+58.f)-along*(spanLength*.33f)+Vec3{0,42,0};
        capture("-overview",outside,middle-Vec3{0,7,0});
        auto low=middle+side*39.f-along*9.f;
        low.y=world.surfaceHeight(low.x,low.z)+13.f;
        capture("-underpass",low,middle-along*4.f+side*2.f-Vec3{0,8,0});
        capture("-approach",approach.start-along*6.f+Vec3{0,13,0},span.end+Vec3{0,4,0});
    }
}
