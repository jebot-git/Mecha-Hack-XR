#include "rpg/session.hpp"
#include "rpg/enemy_effects.hpp"
namespace iron::rpg {
static Rarity lootGrade(Dice &roll,int difficulty){
    static constexpr int uncommon[]={24,48,58},rare[]={3,15,36};
    const int tier=std::clamp(difficulty,0,2);
    const int chance=int(roll.next()%100);
    return chance<rare[tier]?Rarity::Rare:chance<rare[tier]+uncommon[tier]?Rarity::Uncommon:Rarity::Common;
}
Gear safeBoxReward(const SupplyDrop &drop,uint32_t pilot){
    Dice roll{drop.seed^(pilot*0x9e3779b9u)};
    if(drop.kind==SafeBoxKind::FieldSupplies)
        return roll.next()%2?Gear::ExplosiveRounds:Gear::Repair;
    static const auto modules=[]{
        std::vector<Gear> result;
        for(int i=0;i<int(Module::Count);++i){
            const auto module=Module(i);
            const auto gear=moduleGear(module);
            if(gradedModule(gear))result.push_back(gear);
        }
        return result;
    }();
    const int difficulty=std::clamp(drop.difficulty,0,2);
    static constexpr unsigned moduleChance[]={48,40,32};
    if(drop.kind==SafeBoxKind::Garrison||drop.kind==SafeBoxKind::ModuleCache||roll.next()%100<moduleChance[difficulty])
        return modules[roll.next()%modules.size()];
    const auto rarity=lootGrade(roll,difficulty);
    const auto families=weaponFamilies();
    return weaponVariant(families[roll.next()%families.size()],rarity);
}
RecoveredItem safeBoxEquipment(const SupplyDrop &drop,uint32_t pilot){
    const auto gear=safeBoxReward(drop,pilot);
    Dice tuning{drop.seed^(pilot*0x9e3779b9u)^0x6172736eu};
    const auto spread=rollWeaponStats(tuning);
    return {gear,spread,gradedModule(gear)?lootGrade(tuning,drop.difficulty):Rarity::Common};
}
bool Session::placeSafeBox(Vec3 position,SafeBoxKind kind,unsigned source,int difficulty){
    if(supplies.size()>=128||world.pickups.size()>=256)return false;
    position.y=world.walkableHeight(position);
    if(!clearPosition(world,position,2)){
        position=missionNavigablePosition(world,position,3.5f);
        position.y=world.walkableHeight(position);
    }
    const auto pickup=world.pickups.size();
    world.pickups.push_back({position,rpgSafeBoxPickup});
    const unsigned tag=kind==SafeBoxKind::Depot?0x6465706fu:
        kind==SafeBoxKind::Dungeon?0x64756e67u:
        kind==SafeBoxKind::ModuleCache?0x6d6f6475u:
        kind==SafeBoxKind::FieldSupplies?0x6669656cu:0x67756172u;
    Dice roll{mission.recipe.seed^tag^source};
    supplies.push_back({pickup,kind,roll.next(),difficulty});
    return true;
}
void Session::collectSupplies(){
    for(const auto &drop:supplies){
        auto &crate=world.pickups[drop.pickup];
        for(auto &m:members){
            const auto bit=1u<<m.body.playerSlot;
            if((crate.collectedBy&bit)||m.ejected||m.sheet.hp<=0||
               length(m.body.position-crate.position)>7.f)continue;
            if(drop.kind==SafeBoxKind::Depot){
                ++m.pendingDepotSalvage;
                crate.collectedBy|=bit;
                if(world.sounds.size()<96)world.sounds.push_back({SoundEvent::PickupSound,0,crate.position});
                log(m.name+" RECOVERED DEPOT SALVAGE / BANKED ON VICTORY");
                continue;
            }
            if(drop.kind==SafeBoxKind::FieldSupplies){
                auto &charges=m.sheet.inventory[size_t(Gear::Repair)];
                const bool ammo=charges>=99||(m.explosive<=0&&safeBoxReward(drop,m.id)==Gear::ExplosiveRounds);
                if(ammo){
                    if(m.explosive>0)continue;
                    m.explosive=1;
                    log(m.name+" / HIGH-PRESSURE AMMO LOADED");
                }else{
                    m.sheet.repairKits=++charges;
                    log(m.name+" / NANITE CHARGE +1");
                }
                crate.collectedBy|=bit;
                if(world.sounds.size()<96)world.sounds.push_back({SoundEvent::PickupSound,0,crate.position});
                continue;
            }
            if(m.pendingRecovery.size()>=maxRecoveryItems)continue;
            const auto item=safeBoxEquipment(drop,m.id);
            const auto reward=item.gear;
            m.pendingRecovery.push_back(item);
            crate.collectedBy|=bit;
            if(world.sounds.size()<96)world.sounds.push_back({SoundEvent::PickupSound,0,crate.position});
            log(m.name+" RECOVERED "+rarityName(gradedModule(reward)?item.rarity:weaponRarity(reward))+" "+gearName(reward)+" / BANKED ON VICTORY");
            if(drop.kind==SafeBoxKind::Dungeon&&!m.fieldAmmo&&
               (drop.seed^m.id*0x9e3779b9u)%4u<=unsigned(drop.difficulty)){
                m.fieldAmmo=true;
                log(m.name+" / OVERPRESSURE AMMO ACTIVE FOR THIS MISSION");
            }
        }
    }
}
void Session::bankRecoveredGear(Member &m){
    m.recoveredGear.clear();
    auto &scrap=m.sheet.inventory[size_t(Gear::Scrap)];
    scrap=std::min(99,scrap+int(m.pendingRecovery.size()));
    for(const auto &reward:m.pendingRecovery){
        const auto gear=reward.gear;
        auto &stock=m.sheet.owned[size_t(gear)];
        if(stock<99){
            recoverWeapon(m.sheet,gear,reward.roll,reward.rarity);
            m.recoveredGear.push_back(gear);
        }
        else m.sheet.credits=std::min(1000000,m.sheet.credits+std::max(gearInfo(gear).module==Module::Count?0:modulePurchaseCost/2,gearInfo(gear).cost/2));
    }
    m.pendingRecovery.clear();
    m.fieldAmmo=false;
}
void Session::stepDrone(Member &m,const Input &input,float dt){
    if(m.droneControl&&input.focused){
        const float yaw=wrap(m.droneLaunchYaw+attackYaw(wrap(input.headYaw-m.droneHeadYaw)));
        const float pitch=std::clamp(input.headPitch-m.droneHeadPitch,-1.3f,1.3f);
        m.droneYaw=wrap(m.droneYaw+std::clamp(wrap(yaw-m.droneYaw),-1.8f*dt,1.8f*dt));
        m.dronePitch+=std::clamp(pitch-m.dronePitch,-1.8f*dt,1.8f*dt);
        const bool trigger=input.fire[RightHand];
        const bool detonate=trigger&&!m.droneDetonateHeld;
        m.droneDetonateHeld=trigger;
        if(detonate){detonateDrone(m);return;}
    }
    const auto heading=direction(m.droneYaw,m.dronePitch);
    const float distance=32.f*dt;
    const auto next=m.dronePosition+heading*distance;
    const auto hit=world.traceAim(m.dronePosition,heading,distance,false);
    const float ground=world.groundContact(m.dronePosition,next);
    const float traceDistance=length(hit.position-m.dronePosition);
    const float floor=world.walkableHeight(next);
    const float ceiling=world.interiorCeilingAt(m.dronePosition,.5f);
    const bool contact=hit.target>=0||traceDistance<distance-.0001f||ground<=1||
        next.y<=floor||next.y>=ceiling||std::abs(next.x)>missionRadius()*.98f||
        std::abs(next.z)>missionRadius()*.98f;
    m.dronePosition=m.dronePosition+heading*std::min(traceDistance,distance*std::min(1.f,ground));
    m.drone=std::max(0.f,m.drone-dt);
    if(contact||m.drone<=0)detonateDrone(m);
}
void Session::detonateDrone(Member &m){
    const auto p=m.dronePosition;
    const int damage=m.droneArc?dice.roll(4):dice.roll(6)+dice.roll(6);
    const auto effect=m.droneArc?AttackEffect::Stun:AttackEffect::Damage;
    blast(m,p,10.f,damage,m.droneArc?Quality::Arc:Quality::None,true,2,effect);
    addEffect({WeaponEffect::Blast,p,{},Weapon::Cruise,0,6.f,m.droneArc?rpgWeaponArc:rpgWeaponBlast});
    world.sounds.push_back({SoundEvent::Explosion,int(Weapon::Cruise),p});
    m.drone=0;m.droneControl=false;
}
}
