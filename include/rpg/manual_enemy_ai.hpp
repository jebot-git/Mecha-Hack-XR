#pragma once
#include "rpg/session.hpp"
namespace iron::rpg {
bool npcAllyInRange(const Session &,size_t index,unsigned requiredTraits,float range);
float manualEnemySpeed(const EnemySpec &,int biome);
float manualEnemySpread(const EnemySpec &,float range);
int manualEnemyDamageBonus(const Session &,size_t index);
int manualEnemyExtraDice(const Session &,size_t index);
int manualEnemyArmorDamage(const EnemySpec &,int amount);
// Insert missing spawned actors before canonical scenery; refresh changed or
// transformed models without regenerating the original scene's dressing.
void syncManualEnemyModels(World &,std::span<const Enemy>,size_t previousCount,const MissionRecipe *story=nullptr);
}
