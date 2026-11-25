#include "rpg/rules.hpp"
#include <stdexcept>
#include <numeric>
namespace iron::rpg {
int equipmentOwner(const Sheet &s,int slot){
    if(slot<0||slot>=MountCount)throw std::runtime_error("Invalid equipment mount");
    if(!pairedWeapon(s.chassis,s.equipment[slot]))return slot;
    int owner=s.heavyOwners[size_t(slot/2)];
    return owner==slot||owner==(slot^1)?owner:slot;
}
bool gradedModule(Gear gear){
    if(gear<Gear::Empty||gear>=Gear::Count)return false;
    const auto module=gearInfo(gear).module;
    return module!=Module::Count&&!retiredModule(module)&&!frameUpgrade(module);
}
int traderPrice(Gear gear){return gradedModule(gear)?modulePurchaseCost:gearInfo(gear).cost;}
int equipmentCopyCount(const Sheet &s,Gear gear){
    if(gradedModule(gear))return s.owned[size_t(gear)]+int(s.has(gearInfo(gear).module));
    return weaponTuning(gear)?s.owned[size_t(gear)]:0;
}
Rarity equipmentRarity(const Sheet &s,Gear gear,int copy){
    if(gradedModule(gear)){
        const auto *item=weaponCopy(s,gear,copy);
        return item?item->rarity:Rarity::Common;
    }
    return weaponRarity(gear);
}
int equippedWeaponCopy(const Sheet &s,int slot){
    if(slot>=LeftShoulder&&slot<MountCount&&gradedModule(s.equipment[slot])){
        const auto gear=s.equipment[slot];
        const int fitted=s.moduleCopies[slot-LeftShoulder];
        if(fitted>=0)return fitted;
        if(s.has(gearInfo(gear).module))return s.owned[size_t(gear)];
        return s.owned[size_t(gear)]?0:-1;
    }
    if(slot<LeftHand||slot>RightHand||equipmentOwner(s,slot)!=slot)return -1;
    const auto gear=s.equipment[slot];
    if(!weaponTuning(gear)||s.owned[size_t(gear)]==0)return -1;
    if(s.fittedCopies[slot]>=0)return s.fittedCopies[slot];
    const int other=slot^1;
    if(s.equipment[other]!=gear||equipmentOwner(s,other)!=other)return 0;
    // Old profiles used fungible stock, so assign their two arms deterministically.
    if(s.fittedCopies[other]>=0)return s.fittedCopies[other]==0?1:0;
    return slot;
}
bool weaponCopyFitted(const Sheet &s,Gear gear,int copy,int except){
    if(copy<0)return false;
    for(int hand=0;hand<MountCount;++hand)
        if(hand!=except&&s.equipment[hand]==gear&&equippedWeaponCopy(s,hand)==copy)return true;
    return false;
}
const WeaponCopy *weaponCopy(const Sheet &s,Gear gear,int copy){
    if(copy<0)return nullptr;
    const auto found=std::find_if(s.weaponCopies.begin(),s.weaponCopies.end(),[&](const auto &item){
        return item.gear==gear&&item.copy==copy;
    });
    return found==s.weaponCopies.end()?nullptr:&*found;
}
WeaponRoll rollWeaponStats(Dice &dice){
    WeaponRoll roll{};
    for(auto &axis:roll)axis=dice.roll(3)-2;
    return roll;
}
void recoverWeapon(Sheet &s,Gear gear,const WeaponRoll &roll,Rarity grade){
    if((!gradedModule(gear)&&(!weaponTuning(gear)||grade!=Rarity::Common))||grade<Rarity::Common||grade>Rarity::Rare||std::any_of(roll.begin(),roll.end(),[](int value){return value< -1||value>1;}))
        throw std::runtime_error("Invalid weapon recovery");
    auto &count=s.owned[size_t(gear)];
    if(count>=99)throw std::runtime_error("Weapon stock is full");
    if(roll!=WeaponRoll{}||grade!=Rarity::Common)s.weaponCopies.push_back({gear,count,roll,{},grade});
    ++count;
}
WeaponStats weaponStats(const Sheet &s,Gear gear,int copy){
    const auto &info=gearInfo(gear);
    WeaponStats stats{info.cooldown,info.stress,1.f,info.range};
    if(weaponTuning(gear)){
        // Faster weapons heat up more slowly per shot. Heavy ordnance still
        // consumes a larger share of the reactor budget per firing cycle.
        stats.heat=std::min(info.stress*.55f,info.cooldown*(heavy(gear)?.27f:.19f));
    }
    if(const auto *item=weaponCopy(s,gear,copy)){
        auto modifier=[&](WeaponStat stat){
            const auto i=size_t(stat);
            return .03f*float(item->roll[i]+item->tuning[i])+.09f*int(item->rarity);
        };
        stats.cooldown*=1.f-modifier(WeaponStat::Cycle);
        stats.heat*=1.f-modifier(WeaponStat::Heat);
        if(gradedModule(gear)){
            stats.output=1.f+modifier(WeaponStat::Handling);
            stats.range*=stats.output;
        }else if(melee(gear))stats.range*=1.f+modifier(WeaponStat::Handling);
        else stats.spreadFactor=1.f-modifier(WeaponStat::Handling);
    }
    return stats;
}
float mountedModuleOutput(const Sheet &s,Gear gear){
    for(int slot=LeftShoulder;slot<MountCount;++slot)
        if(s.equipment[slot]==gear)return weaponStats(s,gear,equippedWeaponCopy(s,slot)).output;
    return 1.f;
}
int weaponTuneCount(const WeaponCopy *item){
    return item?std::accumulate(item->tuning.begin(),item->tuning.end(),0):0;
}
int weaponTuneCost(Gear gear,int steps){
    return 2+int(weaponRarity(gear))+2*steps;
}
int availableCampaignAwards(const Sheet &s){return s.campaignAwards-s.campaignAwardsSpent;}
static void checkRefinementPayment(const Sheet &s,int cost,bool useAward){
    if(useAward){
        if(availableCampaignAwards(s)<1)throw std::runtime_error("No campaign awards available");
    }else if(s.inventory[size_t(Gear::Scrap)]<cost)throw std::runtime_error("Insufficient scrap for refinement");
}
static void payForRefinement(Sheet &s,int cost,bool useAward){
    if(useAward)++s.campaignAwardsSpent;
    else s.inventory[size_t(Gear::Scrap)]-=cost;
}
void tuneWeapon(Sheet &s,Gear gear,int copy,WeaponStat stat,bool useAward){
    if(!weaponTuning(gear)||copy<0||copy>=s.owned[size_t(gear)]||stat<WeaponStat::Cycle||stat>=WeaponStat::Count)
        throw std::runtime_error("Select an owned weapon and a stat");
    const auto *current=weaponCopy(s,gear,copy);
    const int steps=weaponTuneCount(current);
    if(steps>=weaponTuneLimit)throw std::runtime_error("Weapon is fully tuned");
    const int cost=weaponTuneCost(gear,steps);
    checkRefinementPayment(s,cost,useAward);
    WeaponCopy next=current?*current:WeaponCopy{gear,copy};
    ++next.tuning[size_t(stat)];
    if(current)s.weaponCopies[size_t(current-s.weaponCopies.data())]=next;
    else s.weaponCopies.push_back(next);
    payForRefinement(s,cost,useAward);
}
int pilotRefinementCount(const Sheet &s){
    return std::accumulate(s.refinements.begin(),s.refinements.end(),0);
}
int pilotRefinementCost(const Sheet &s){return pilotRefinementCount(s)==0?6:10;}
bool canRefinePilot(const Sheet &s,PilotRefinement stat){
    return s.created&&stat>=PilotRefinement::Hull&&stat<PilotRefinement::Count&&
        pilotRefinementCount(s)<pilotRefinementLimit&&
        s.refinements[size_t(stat)]<(stat==PilotRefinement::Hull?pilotRefinementLimit:1);
}
void refinePilot(Sheet &s,PilotRefinement stat,bool useAward){
    if(!canRefinePilot(s,stat))throw std::runtime_error("Pilot refinement limit reached or invalid stat");
    const int cost=pilotRefinementCost(s);
    checkRefinementPayment(s,cost,useAward);
    ++s.refinements[size_t(stat)];
    normalizeCombatProfile(s);
    payForRefinement(s,cost,useAward);
}
void normalizeHeavyEquipment(Sheet &s){
    for(int pair=0;pair<3;++pair){
        int left=pair*2,right=left+1;
        if(!pairedWeapon(s.chassis,s.equipment[left])&&!pairedWeapon(s.chassis,s.equipment[right])){s.heavyOwners[pair]=-1;continue;}
        int owner=s.heavyOwners[pair];
        if((owner!=left&&owner!=right)||!heavy(s.equipment[owner]))owner=heavy(s.equipment[left])?left:right;
        if(pair==0)s.fittedCopies[owner^1]=-1;
        s.equipment[owner^1]=s.equipment[owner];s.qualities[owner^1]=s.qualities[owner];s.heavyOwners[pair]=owner;
    }
}
int availableEquipment(const Sheet &s,Gear gear){
    if(gear==Gear::Comlink||gear==Gear::Scrap)return 0;
    if(gear==Gear::Empty||consumable(gear))return 99;
    if(gear==Gear::Scan)return 1;
    const auto &info=gearInfo(gear);
    if(info.module!=Module::Count)return s.owned[size_t(gear)]>0||s.has(info.module);
    return s.owned[size_t(gear)];
}
void equipGear(Sheet &s,int slot,Gear gear,int copy){
    if(slot<0||slot>=MountCount)throw std::runtime_error("Invalid mount");
    gearInfo(gear);if(!mountAllowed(gear,slot))throw std::runtime_error("Weapons require arms; modules and supplies require shoulders or hips");
    Sheet next=s;
    for(int hand=0;hand<2;++hand)next.fittedCopies[hand]=equippedWeaponCopy(s,hand);
    if(copy< -1||(copy>=0&&!weaponTuning(gear)&&!gradedModule(gear)))throw std::runtime_error("Invalid weapon copy");
    if(slot<LeftShoulder&&(next.qualities[LeftHand]==Quality::Dual||next.qualities[RightHand]==Quality::Dual))next.qualities[LeftHand]=next.qualities[RightHand]=Quality::None;
    auto clear=[&](int mount){
        if(pairedWeapon(s.chassis,next.equipment[mount])){
            if(mount<2)next.fittedCopies[mount^1]=-1;
            next.equipment[mount^1]=Gear::Empty;next.qualities[mount^1]=Quality::None;
        }
        if(mount<2)next.fittedCopies[mount]=-1;
        else next.moduleCopies[mount-LeftShoulder]=-1;
        next.equipment[mount]=Gear::Empty;next.qualities[mount]=Quality::None;next.heavyOwners[mount/2]=-1;
    };
    clear(slot);
    if(pairedWeapon(s.chassis,gear)){clear(slot^1);next.equipment[slot^1]=gear;next.heavyOwners[slot/2]=slot;}
    next.equipment[slot]=gear;next.qualities[slot]=Quality::None;
    if(gear!=Gear::Empty&&!consumable(gear)){
        int count=0;for(int i=0;i<MountCount;++i)if(next.equipment[i]==gear&&equipmentOwner(next,i)==i)++count;
        if(count>availableEquipment(next,gear))throw std::runtime_error("Equipment is not in this pilot's inventory");
    }
    if(gradedModule(gear)){
        const int count=equipmentCopyCount(next,gear);
        if(copy<0)copy=s.equipment[slot]==gear?equippedWeaponCopy(s,slot):
            (s.has(gearInfo(gear).module)?s.owned[size_t(gear)]:0);
        if(copy<0||copy>=count)throw std::runtime_error("Module copy unavailable");
        next.moduleCopies[slot-LeftShoulder]=copy<s.owned[size_t(gear)]?copy:-1;
    }
    if(weaponTuning(gear)){
        const int count=availableEquipment(next,gear);
        if(copy<0){
            const int previous=s.equipment[slot]==gear?equippedWeaponCopy(s,slot):-1;
            if(previous>=0&&!weaponCopyFitted(next,gear,previous,slot))copy=previous;
            else for(copy=0;copy<count&&weaponCopyFitted(next,gear,copy,slot);++copy){}
        }
        if(copy>=count||weaponCopyFitted(next,gear,copy,slot))
            throw std::runtime_error("Weapon copy is unavailable or already mounted");
        next.fittedCopies[slot]=copy;
    }
    validate(next);s=std::move(next);
}
void equipQuality(Sheet &s,int slot,Quality quality){
    if(slot<0||slot>=MountCount||!qualityAllowed(s.equipment[slot],quality))throw std::runtime_error("Incompatible weapon quality");
    Sheet next=s;
    if(slot<LeftShoulder&&(next.qualities[LeftHand]==Quality::Dual||next.qualities[RightHand]==Quality::Dual))next.qualities[LeftHand]=next.qualities[RightHand]=Quality::None;
    if(quality==Quality::Dual){
        if(slot>=LeftShoulder||next.equipment[slot^1]!=next.equipment[slot]||heavy(next.equipment[slot]))throw std::runtime_error("Dual needs matching light weapons in both hands");
        next.qualities[slot^1]=quality;
    }
    next.qualities[slot]=quality;
    if(pairedWeapon(s.chassis,next.equipment[slot]))next.qualities[slot^1]=quality;
    validate(next);s=std::move(next);
}
void craftFromScrap(Sheet &s,Gear gear,int count){
    const auto &info=gearInfo(gear);
    if(retiredGear(gear)||weaponRarity(gear)!=Rarity::Common||count<1||count>99||info.cost<=0||gear==Gear::Scrap||(!consumable(gear)&&!melee(gear)&&!ranged(gear)))throw std::runtime_error("Scrap crafts weapons and consumables only");
    int required=((info.cost+24)/25)*count;
    auto &scrap=s.inventory[size_t(Gear::Scrap)];auto &stock=consumable(gear)?s.inventory[size_t(gear)]:s.owned[size_t(gear)];
    if(scrap<required||stock+count>99||(gear==Gear::OneShotModule&&s.oneShotStock[size_t(s.oneShotModule)]+count>99))throw std::runtime_error("Insufficient scrap or inventory full");
    scrap-=required;stock+=count;
    if(gear==Gear::OneShotModule)s.oneShotStock[size_t(s.oneShotModule)]+=count;
    s.repairKits=s.inventory[size_t(Gear::Repair)];
}
void purchase(Sheet &s,Gear gear){
    const int cost=traderPrice(gear);
    if(retiredGear(gear)||weaponRarity(gear)!=Rarity::Common||gear==Gear::Comlink||cost<=0||(gearInfo(gear).module!=Module::Count&&!gradedModule(gear)))throw std::runtime_error("Item is not for sale");
    auto &count=consumable(gear)?s.inventory[size_t(gear)]:s.owned[size_t(gear)];
    if(s.credits<cost||count>=99)throw std::runtime_error("Insufficient credits or inventory full");
    s.credits-=cost;++count;
    if(gear==Gear::OneShotModule)++s.oneShotStock[size_t(s.oneShotModule)];
    s.repairKits=s.inventory[size_t(Gear::Repair)];
}
void purchaseArmor(Sheet &s,Armor armor){
    if(armor==Armor::None){s.armor=armor;return;}
    if(armor!=Armor::Light&&armor!=Armor::Heavy)throw std::runtime_error("Invalid armor");
    if(!chassisArmorAllowed(s.chassis,armor))throw std::runtime_error("Chassis requires light armor");
    int index=int(armor),cost=armor==Armor::Light?40:70;
    if(!s.ownedArmor[index]){if(s.credits<cost)throw std::runtime_error("Insufficient credits");s.credits-=cost;s.ownedArmor[index]=true;}
    s.armor=armor;
}
void installModule(Sheet &s,Module module){
    moduleId(module);
    if(retiredModule(module)||frameUpgrade(module)||fittedModuleCount(s)>=moduleLimit||s.has(module))throw std::runtime_error("No module slot available");
    if(s.credits<modulePurchaseCost)throw std::runtime_error("Insufficient credits");
    s.modules.push_back(module);
    s.credits-=modulePurchaseCost;
    auto caps=s;prepare(caps);s.maxAp=caps.maxAp;s.maxReactor=caps.maxReactor;
    validate(s);
}
void upgradeFrame(Sheet &s,Module module){
    if(!s.created||!frameUpgrade(module)||s.has(module))throw std::runtime_error("Frame upgrade unavailable");
    if(s.credits<frameUpgradeCost)throw std::runtime_error("Insufficient credits");
    s.modules.push_back(module);
    s.credits-=frameUpgradeCost;
}
void startingEquipment(Sheet &s,bool credits){
    if(s.created)throw std::runtime_error("Starting package is already finalized");
    s.fittedCopies.fill(-1);s.moduleCopies.fill(-1);s.weaponCopies.clear();
    s.startingCredits=credits;s.owned.fill(0);s.inventory.fill(0);s.oneShotStock.fill(0);s.ownedArmor={true,false,false};
    s.equipment.fill(Gear::Empty);s.qualities.fill(Quality::None);s.heavyOwners={-1,-1,-1};
    s.credits=credits?120:0;s.armor=Armor::None;
    if(!credits){
        const bool heavyHull=chassisInfo(s.chassis).armor==5;
        s.armor=heavyHull?Armor::Heavy:Armor::Light;s.ownedArmor[int(s.armor)]=true;
        if(heavyHull){
            s.owned[size_t(Gear::Cannon)]=1;s.equipment[LeftHand]=Gear::Cannon;
            if(s.chassis!=Chassis::Titan){
                s.owned[size_t(Gear::HeavyBlade)]=1;
                s.qualities[LeftHand]=Quality::Beam;
            }
        }else{
            const auto gun=s.chassis==Chassis::Auxiliary?Gear::Scattergun:Gear::Rifle;
            s.owned[size_t(Gear::Blade)]=s.owned[size_t(gun)]=1;
            s.equipment[LeftHand]=Gear::Blade;s.equipment[RightHand]=gun;
            if(s.chassis==Chassis::Auxiliary)s.qualities[LeftHand]=Quality::Arc;
            if(s.chassis==Chassis::Vanguard)s.qualities[RightHand]=Quality::Beam;
            if(s.chassis!=Chassis::Scout)s.owned[size_t(Gear::Shield)]=1;
        }
    }
    s.equipment[LeftHip]=Gear::Repair;
    s.equipment[RightShoulder]=Gear::Scan;
    for(auto module:s.modules)if(moduleGear(module)!=Gear::Empty){s.equipment[RightHip]=moduleGear(module);break;}
    normalizeHeavyEquipment(s);
    s.repairKits=s.inventory[size_t(Gear::Repair)]=1;
    prepare(s);
}
void refitMounts(Sheet &s){
    normalizeHeavyEquipment(s);
    struct Item{Gear gear;Quality quality;int slot,copy;};std::vector<Item> items;
    for(int slot=0;slot<MountCount;++slot)if(s.equipment[slot]!=Gear::Empty&&equipmentOwner(s,slot)==slot)items.push_back({s.equipment[slot],s.qualities[slot],slot,equippedWeaponCopy(s,slot)});
    s.fittedCopies.fill(-1);s.moduleCopies.fill(-1);
    s.equipment.fill(Gear::Empty);s.qualities.fill(Quality::None);s.heavyOwners={-1,-1,-1};
    auto priority=[](Gear gear){return heavy(gear)?0:(ranged(gear)||melee(gear))?1:gear==Gear::Shield?2:3;};
    std::stable_sort(items.begin(),items.end(),[&](const Item &a,const Item &b){return priority(a.gear)<priority(b.gear);});
    for(const auto &item:items){
        if(item.gear==Gear::Comlink)continue;
        auto fit=[&](int slot){if(!mountAllowed(item.gear,slot)||s.equipment[slot]!=Gear::Empty||(pairedWeapon(s.chassis,item.gear)&&s.equipment[slot^1]!=Gear::Empty))return false;
            if(slot<2&&weaponTuning(item.gear))s.fittedCopies[slot]=item.copy;
            if(slot>=LeftShoulder&&gradedModule(item.gear)&&item.copy<s.owned[size_t(item.gear)])s.moduleCopies[slot-LeftShoulder]=item.copy;
            s.equipment[slot]=item.gear;s.qualities[slot]=item.quality;if(pairedWeapon(s.chassis,item.gear)){s.equipment[slot^1]=item.gear;s.qualities[slot^1]=item.quality;s.heavyOwners[slot/2]=slot;}return true;};
        if(fit(item.slot))continue;
        for(int slot=0;slot<MountCount;++slot)if(fit(slot))break;
    }
}
void sellEquipment(Sheet &s,Gear gear,int copy){
    const int price=traderPrice(gear);if(price<=0||gear==Gear::Comlink||(gearInfo(gear).module!=Module::Count&&!gradedModule(gear)))throw std::runtime_error("Item cannot be sold");
    auto &stock=consumable(gear)?s.inventory[size_t(gear)]:s.owned[size_t(gear)];int equipped=0;
    if(!consumable(gear))for(int slot=0;slot<MountCount;++slot)if(s.equipment[slot]==gear&&equipmentOwner(s,slot)==slot)++equipped;
    if((!gradedModule(gear)&&stock<=equipped)||stock<=0||(gear==Gear::OneShotModule&&s.oneShotStock[size_t(s.oneShotModule)]<=0))throw std::runtime_error("Unequip item first or select stocked subtype");
    if(weaponTuning(gear)||gradedModule(gear)){
        if(copy==-1)for(copy=stock-1;copy>=0&&weaponCopyFitted(s,gear,copy);--copy){}
        if(copy<0||copy>=stock||weaponCopyFitted(s,gear,copy))
            throw std::runtime_error("Only an owned, unmounted copy can be sold");
        const std::array fittedBefore={equippedWeaponCopy(s,0),equippedWeaponCopy(s,1)};
        for(int hand=0;hand<2;++hand)if(s.equipment[hand]==gear){
            const int fitted=fittedBefore[hand];
            s.fittedCopies[hand]=fitted>copy?fitted-1:fitted;
        }
        for(int slot=LeftShoulder;slot<MountCount;++slot)
            if(s.equipment[slot]==gear&&s.moduleCopies[slot-LeftShoulder]>copy)--s.moduleCopies[slot-LeftShoulder];
        std::erase_if(s.weaponCopies,[&](const auto &item){return item.gear==gear&&item.copy==copy;});
        for(auto &item:s.weaponCopies)if(item.gear==gear&&item.copy>copy)--item.copy;
    }
    --stock;if(gear==Gear::OneShotModule)--s.oneShotStock[size_t(s.oneShotModule)];s.credits=std::min(1000000,s.credits+price/2);s.repairKits=s.inventory[size_t(Gear::Repair)];
}
void sellArmor(Sheet &s,Armor armor){
    if(armor!=Armor::Light&&armor!=Armor::Heavy)throw std::runtime_error("Select owned armor");
    if(s.armor==armor||!s.ownedArmor[int(armor)])throw std::runtime_error("Remove armor before selling it");
    s.ownedArmor[int(armor)]=false;s.credits=std::min(1000000,s.credits+(armor==Armor::Light?20:35));
}
void finishCreation(Sheet &s){
    if(s.created)return;
    s.created=true;
    try{validate(s);}catch(...){s.created=false;throw;}
}
void retireObsoleteEquipment(Sheet &s){
    if(retiredModule(s.starterModule))
        s.starterModule=Module::Count;
    int refunds=0;
    for(int i=0;i<int(Gear::Count);++i){
        const auto gear=Gear(i);
        if(!retiredGear(gear))continue;
        if(s.owned[i]<0||s.owned[i]>99||s.inventory[i]<0||s.inventory[i]>99)
            throw std::runtime_error("Invalid legacy equipment stock");
        refunds+=(s.owned[i]+s.inventory[i])*gearInfo(gear).cost;
        s.owned[i]=s.inventory[i]=0;
    }
    for(int i=0;i<int(Module::Count);++i){
        const auto module=Module(i);
        if(s.oneShotStock[i]<0||s.oneShotStock[i]>99)throw std::runtime_error("Invalid legacy module stock");
        s.oneShotStock[i]=0; // The pooled inventory was refunded above.
        if(!retiredModule(module))continue;
        const bool removed=std::erase(s.modules,module)>0;
        constexpr int legacyModulePrice=120;
        refunds+=int(removed)*legacyModulePrice;
    }
    s.oneShotModule=Module::ChestBeam;
    for(int slot=0;slot<MountCount;++slot)if(retiredGear(s.equipment[slot])||s.equipment[slot]==Gear::Scrap){
        s.equipment[slot]=Gear::Empty;s.qualities[slot]=Quality::None;
    }

    if(s.credits<0||s.credits>1000000)throw std::runtime_error("Invalid legacy credits");
    s.credits=std::min(1000000,s.credits+refunds);
}
void migrateProfileSheet(Sheet &s){
    if(int(s.armor)<0||int(s.armor)>2||moduleGear(s.oneShotModule)==Gear::Empty)throw std::runtime_error("Invalid legacy armor or one-shot module");
    auto existing=s.owned;
    s.created=true;s.owned.fill(0);s.ownedArmor[int(s.armor)]=true;
    for(int slot=0;slot<MountCount;++slot){auto gear=s.equipment[slot];
        if(gear==Gear::Comlink){s.equipment[slot]=Gear::Empty;s.qualities[slot]=Quality::None;continue;}
        if(gear!=Gear::Empty&&!consumable(gear)&&gearInfo(gear).module==Module::Count)++s.owned[size_t(gear)];
    }
    for(size_t i=0;i<s.owned.size();++i)s.owned[i]=std::max(s.owned[i],existing[i]);
    if(s.inventory[size_t(Gear::OneShotModule)]>0&&std::all_of(s.oneShotStock.begin(),s.oneShotStock.end(),[](int n){return n==0;}))s.oneShotStock[size_t(s.oneShotModule)]=s.inventory[size_t(Gear::OneShotModule)];
    normalizeHeavyEquipment(s);
}
}
