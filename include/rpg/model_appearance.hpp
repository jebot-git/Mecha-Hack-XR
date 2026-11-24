#pragma once
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace iron::rpg {
inline nlohmann::json loadModelAppearance(const std::filesystem::path &modelRoot) {
    auto result=nlohmann::json::object();
    for(const auto *name:{"chassis-voidmaw-appearance.json","cover-unit-appearance.json"}){
        std::ifstream file(modelRoot/name);
        if(!file)continue;
        nlohmann::json layer;file>>layer;
        if(layer.at("schema")!=1||!layer.at("models").is_object())
            throw std::runtime_error("Invalid model appearance library: "+std::string(name));
        result.update(layer.at("models"));
    }
    return result;
}

// The simulation's mesh is measured in metres and the player renderer's
// source model is measured in centimetres. Both use the same authored edits.
inline void applyModelAppearance(nlohmann::json &model,const nlohmann::json &library,
                                 const std::string &name,float unitsPerMetre) {
    const auto appearance=library.find(name);
    if(appearance==library.end())return;
    auto &vertices=model.at("vertices");
    auto &faces=model.at("faces");
    if(vertices.size()!=appearance->at("base_vertices").get<size_t>()||
       faces.size()!=appearance->at("base_faces").get<size_t>())
        throw std::runtime_error("Appearance does not match model topology: "+name);
    const auto &positions=appearance->at("positions");
    const auto &colors=appearance->at("colors");
    if(!positions.is_array()||!colors.is_array()||positions.size()>vertices.size()||colors.size()>faces.size())
        throw std::runtime_error("Invalid model appearance budget: "+name);
    for(const auto &value:positions) {
        if(!value.is_array()||value.size()!=4)throw std::runtime_error("Invalid appearance position: "+name);
        vertices.at(value[0].get<size_t>())={value[1].get<float>()*unitsPerMetre,
                                             value[2].get<float>()*unitsPerMetre,
                                             value[3].get<float>()*unitsPerMetre};
    }
    if(appearance->contains("scale")){
        const auto scale=appearance->at("scale").get<std::array<float,3>>();
        for(float factor:scale)if(!std::isfinite(factor)||factor<=0.f)
            throw std::runtime_error("Invalid model appearance scale: "+name);
        for(auto &vertex:vertices)
            for(size_t axis=0;axis<scale.size();++axis)
                vertex.at(axis)=vertex.at(axis).get<float>()*scale[axis];
    }
    for(const auto &value:colors) {
        if(!value.is_array()||value.size()!=4)throw std::runtime_error("Invalid appearance color: "+name);
        faces.at(value[0].get<size_t>()).at("color")={value[1],value[2],value[3]};
    }
    if(appearance->contains("trims")){
        const auto &trims=appearance->at("trims");
        if(!trims.is_array()||trims.size()>faces.size())throw std::runtime_error("Invalid appearance trims: "+name);
        for(const auto &value:trims){
            if(!value.is_array()||value.size()!=2)throw std::runtime_error("Invalid appearance UV: "+name);
            faces.at(value[0].get<size_t>()).at("uv")=value[1];
        }
    }
}
}
