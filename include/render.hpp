#pragma once
#include "graphics.hpp"
#include "rpg/chassis_visual.hpp"
#include "math.hpp"
#include "simulation.hpp"
#include "frontend.hpp"
#include "animation.hpp"
#include "cockpit_effects.hpp"
#include "cockpit_glass.hpp"
#include "cockpit_lighting.hpp"
#include "searchlight.hpp"
#include <optional>
#include <future>
#include <map>
#include <vector>
#include <string>
#include <filesystem>
namespace iron {
namespace rpg { struct EnemyRig; }
class Renderer {
    friend struct MaterialStudy; // Offline probe of the actual uploaded model VAOs and linked shader.
    GLuint program=0,vao=0,vbo=0;
    GLuint aaFbo=0,aaColor=0,aaDepth=0;int aaWidth=0,aaHeight=0;
    void drawScene(const World&,const avp::Matrix&,const avp::Matrix&,const avp::Matrix&,bool,const Frontend*);
    void drawWorldLayer(const World&,const avp::Matrix&,bool,const Frontend*);
    void drawCockpit(const World&,const avp::Matrix&,bool);
    bool worldPass=false,emissiveWorldMarkers=false;float worldNear=.75f,worldFar=3000;
    avp::Matrix worldView=avp::identity;
    std::array<Vec3,4> worldFrustum{};
    EnvironmentVisuals environmentView;
    EnvironmentSettings environmentProfile;
    Vec3 teamTint{1,1,1};
    struct EnvironmentUniforms {GLint fog=-1,tint=-1,gain=-1,pose=-1,lamp=-1,axis=-1;};
    Pilot visibilityObserver;
    Vec3 localLampOrigin{},localLampAxis{};float localLampRange=0,visibilityBase=250;bool visibilityPenalty=false,searchlightPass=false;
    float lightTraceTime=-1,lightTraceRange=-1;std::array<SearchlightBeam,8> lightBeams{};std::array<bool,8> lightStates{};
    const WorldMesh* lightMap=nullptr;
    GLuint lightProgram=0,lightVao=0,lightVbo=0;
    GLint lightTransform=-1,lightViewModel=-1,lightDepthRange=-1,lightColour=-1,lightOpacity=-1,lightKind=-1;
    void loadSearchlight();
    void prepareSearchlights(const World&);void drawSearchlights(const World&,const avp::Matrix&);
    std::map<GLuint,EnvironmentUniforms> environmentLocations;
    void applyEnvironment(GLuint,float gain=1,const avp::Matrix* pose=nullptr);
    void drawWeather(const World&,const avp::Matrix&);
    void drawInsertionWorld(const World&);
    void drawInsertionCockpit(const World&,const avp::Matrix&);
    void drawInsertionStatus(const World&,const avp::Matrix&);
    void drawInsertionWater(const World&);
    struct InsertionWaterResources {
        GLuint program=0,vao=0;
        ~InsertionWaterResources();
    } insertionWater;
    GLint transform=-1,tint=-1,primitiveOpacity=-1;
    struct EnemyEffectVertex { Vec3 position,color; float opacity; };
    std::vector<EnemyEffectVertex> enemyEffectVertices;
    GLuint enemyEffectVao=0,enemyEffectVbo=0;
    GLint vertexColors=-1;
    float shapeOpacity=1;
    avp::Matrix vp=avp::identity;
    GLuint cockpitProgram=0,cockpitVao=0,cockpitVbo=0,originalTexture=0;
    GLint cockpitTransform=-1,cockpitEye=-1,cockpitBaked=-1;
    std::vector<float> cockpitMaterials;
    struct CockpitTrim {
        std::vector<float> vertices;
        GLsizei hullVertices=0;
        GLsizei count() const {return GLsizei(vertices.size()/10);}
    };
    struct Surface {
        avp::Matrix pose=avp::identity;
        float width=0,height=0;
        bool hatch=false;
        enum class Style { Segments,Dial,Attitude,Servo } style=Style::Segments;
        avp::Matrix fit(float designWidth,float designHeight) const;
    };
    struct Interior {
        CockpitTrim mesh;
        Surface overview,radar,torso,heading,reactor,health;
        std::array<Surface,MountCount> ammo;
        std::optional<Surface> waveform,servo;
        struct Gauge {
            enum class Signal { Heat,Hull,Plating } signal;
            Surface surface;
        };
        std::vector<Gauge> gauges;
        std::vector<GlassVertex> glass;
    };
    std::array<Interior,rpg::chassisIds.size()> interiors;
    const Interior* activeInterior() const {
        return activeCockpit<interiors.size()&&!interiors[activeCockpit].mesh.vertices.empty()
            ?&interiors[activeCockpit]:nullptr;
    }
    static Interior loadInterior(const std::filesystem::path&);
    avp::Matrix instrumentPose(unsigned module) const;
    avp::Matrix hatchMotion=avp::identity;
    // The last slot is the common IS cockpit with no chassis hardware.
    std::array<CockpitTrim,rpg::chassisIds.size()+1> cockpitTrims;
    size_t activeCockpit=rpg::chassisIds.size();
    std::optional<CockpitLighting> cockpitBake;
    void prepareCockpitLighting(const World&);
    GLsizei cockpitVertices=0,cockpitHullVertices=0;
    GLint cockpitLamp=-1,cockpitLampBounds=-1,cockpitLampFill=-1;
    std::array<std::pair<GLint,GLsizei>,5> lampRanges{};
    std::array<std::array<float,4>,5> lampBounds{};
    std::pair<GLint,GLsizei> reactorLampRange{};
    GLint cockpitPower=-1,cockpitPowerRail=-1;
    CockpitEffects cockpitFx;
    float instrumentGain=1;
    struct PanelInstance {avp::Matrix transform; std::array<float,4> color,uv;};
    std::vector<PanelInstance> panelInstances;
    GLuint panelProgram=0,panelBoxes=0,panelGlyphs=0,panelBuffer=0;
    GLint panelTextured=-1;
    bool batchingPanel=false,panelIsText=false;
    void loadPanelBatch();
    void panelInstance(bool,const avp::Matrix&,Vec3,const std::array<float,4>&);
    void flushPanel();
    GLuint glassProgram=0,glassVao=0,glassVbo=0,detailProgram=0,detailVao=0,detailVbo=0;
    GLint glassTransform=-1,glassEye=-1,glassSky=-1,glassGround=-1,glassRain=-1,glassTime=-1,glassSlime=-1,glassBite=-1,detailTransform=-1;
    GLsizei glassVertices=0;
    std::vector<GlassVertex> classicGlass;
    Vec3 frontGlassPoint(float x,float y) const;
    struct DetailVertex {Vec3 position,color;};
    std::array<std::vector<DetailVertex>,4> fractures;
    std::vector<DetailVertex> projectedFractures;
    size_t fractureCockpit=rpg::chassisIds.size();
    int projectedStage=-1;
    GLsizei detailVertices=0,fractureVertices=0;
    void loadCockpitEffects();
    void prepareCockpitDetails();
    const rpg::EnemyRig *droolRig=nullptr;
    int droolSide=0,droolPart=-1; Vec3 droolTip{};
    void drawCanopyDrool(const World&,const rpg::EnemyRig&,const Target&,const WalkerPose&,const avp::Matrix&);
    void drawCockpitGlass(const World&,const avp::Matrix&,const avp::Matrix&);
    void drawCockpitDetails(const World&,const avp::Matrix&,const avp::Matrix&);
    void bootInstruments(const avp::Matrix&);
    avp::Matrix cockpitPose(const World&)const;
    struct Readout { Vec3 position; float size=.006f; };
    std::array<Readout,MountCount> ammoReadouts{};
    Vec3 overview{},radar{},torso{},heading{};
    struct Texture { GLuint id=0; int width=0,height=0;Vec3 skyTop{};bool hasSky=false; };
    std::map<std::string,Texture> textures;
    struct Mission { std::string title; std::vector<std::string> briefing; };
    std::array<std::vector<Mission>,2> campaigns;
    GLuint artProgram=0,artVao=0,artVbo=0;
    GLint artTransform=-1,artUV=-1,artColor=-1,artDepthRange=-1,artAlpha=-1,artTaper=-1,artSoftness=-1,artAngle=-1,artAspect=-1;
    GLuint skyProgram=0,skyVao=0;GLint skyCamera=-1,skyLens=-1,skyRim=-1,skyZenith=-1,skyCloud=-1,skyParams=-1,skyWeather=-1;
    float spriteAlpha=1,spriteTaper=0,spriteSoftness=0,spriteUVRotation=0;
    struct Model { GLuint vao=0,vbo=0; GLsizei count=0; Vec3 low{},high{}; };
    struct WeaponPart {
        GLint first=0;
        GLsizei count=0;
        std::vector<avp::Matrix> frames;
        std::vector<float> glow;
    };
    struct WeaponAssembly {
        Model bounds;
        float socket=0,reach=0;
        std::vector<WeaponPart> parts;
        Model fittedBounds(float stretch) const;
        float stretchFor(int handling) const;
    };
    std::map<std::string,WeaponAssembly> weapons;
    Model weaponBuffer;
    GLint weaponPass=-1,weaponPose=-1,weaponStretch=-1,weaponGlow=-1;
    static void uploadModelVertices(Model&,const std::vector<float>&);
    void loadWeapons(const std::filesystem::path&);
    void drawWeapon(const WeaponAssembly&,const avp::Matrix&,float phase,int handling);
    bool visibleWorldSphere(Vec3,float) const;
    bool visibleWorld(const Model&,const avp::Matrix&) const;
    struct WorldDrawCandidate { size_t index; avp::Matrix pose; Quaternion orientation; unsigned bands; };
    std::vector<WorldDrawCandidate> worldDrawCandidates;
    bool prepareWorldDrawCandidates=false;
    unsigned worldDrawBand=0;
    static constexpr std::array<std::pair<float,float>,3> worldDepthBands{{{512,3000},{64,512},{.75f,64}}};
    std::map<std::string,Model> models;
    struct BoosterSocket {Vec3 position,normal;};
    struct ModelRig {
        WalkerRig rig;
        std::vector<std::vector<std::pair<GLint,GLsizei>>> ranges;
        std::vector<std::pair<GLint,GLsizei>> drawRanges;
        GLsizei vertices=0;
        int backpack=-1;
        std::array<BoosterSocket,2> boosters{};
    };
    std::map<std::string,ModelRig> walkerRigs;
    std::string soldierModel(const Pilot&)const;
    void drawSoldier(const Pilot&,float tilt=0,const avp::Matrix* camera=nullptr,float time=-1);
    ArmRig decodedRig{};
    std::shared_ptr<const int> worldSignature;
    struct WorldModelSlot {
        Model bounds;
        std::shared_ptr<const Model> buffer;
        const Model& model()const{return buffer?*buffer:bounds;}
    };
    std::vector<WorldModelSlot> worldModels,worldWrecks;
    std::map<std::pair<const WorldMesh*,const void*>,std::weak_ptr<const Model>> sectorBuffers;
    std::vector<const rpg::EnemyRig*> worldEnemyRigs;
    std::map<std::pair<std::string,size_t>,const rpg::EnemyRig*> assetRigs;
    std::vector<std::pair<std::shared_ptr<WorldMesh>,std::shared_ptr<WorldMesh>>> worldMeshSources;
    std::array<unsigned,32> pickupTypes{};
    Model groundMesh,waterMesh,interiorDeckMesh;
    Model damageDecals;uint64_t damageDecalKey=0;
    struct CorpsePoseCache {float age=-1,started=-2,floor=0;WalkerPose pose;};
    std::vector<CorpsePoseCache> corpsePoses;
    struct LivingPoseCache {WalkerPose pose; uint64_t frame=0; float time=-1;};
    uint64_t poseFrame=1;
    std::vector<LivingPoseCache> livingPoses;
    std::array<Model,5> pickupModels;
    GLuint pickupTextures[2]{}, terrainTexture=0,shelterTexture=0;
    std::filesystem::path gameplayRoot;
    GLuint worldProgram=0,worldTextures[2]{};int worldTextureGame=-1;
    GLint worldTransform=-1,surfaceReceiver=-1,surfacePaint=-1,surfaceWetness=-1,shelterArea=-1;
    size_t stagingCursor=0;
    std::vector<size_t> stagingOrder;
    static Model worldBounds(const WorldMesh&);
    static Model uploadWorldMesh(const WorldMesh&);
    struct ShelterMip {int width,height;std::vector<float> pixels;};
    std::future<std::vector<ShelterMip>> shelterJob;
    void prepareShelter(const World&,bool required);
    void prepareWorldSlots(const World&);
    void uploadTargetMesh(const World&,size_t,bool wreck);
    void drawWorld(const World&);
    void drawEnemyEffect(const WeaponEffect&,const avp::Matrix&,const World&);
    void drawMechaWave(const Projectile&,const avp::Matrix&);
    void drawEnemyStreak(Vec3,Vec3,float,Vec3,const avp::Matrix&,float taper=0);
    void flushEnemyEffects();
    void clearWorld();
    void loadModels(const std::filesystem::path&);
    void model(const std::string&,const avp::Matrix&,float phase=0,int handling=0);
    void drawArms(const World&);
    void modelPreview(const std::string&,const avp::Matrix&,float size,float yaw,int handling=0);
    void loadArt(const std::filesystem::path&);
    void art(const std::string&,const avp::Matrix&,std::array<float,4> uv={0,0,1,1},Vec3 color={1,1,1});
    void frontend(const Frontend&,const avp::Matrix&);
    void horizon(const World&);
    void sky(const World&,const avp::Matrix&,const avp::Matrix&);
    void loadCockpit(const std::filesystem::path&);
    void instruments(const World&,const avp::Matrix&,bool paused);
    void line(const avp::Matrix&,Vec3,Vec3,float,Vec3);
    void box(const avp::Matrix&,Vec3 color);
    void text(const std::string&,const avp::Matrix&,float size,Vec3 color);
public:
    explicit Renderer(const std::filesystem::path& cockpit);
    ~Renderer();
    void prepareWorldMaterials(const World&);
    void stageWorld(const World&);
    void uploadBriefingHeader(int width,int height,const uint16_t *rgb565);
    const ArmRig& armRig() const { return decodedRig; }
    void resetCockpit(){cockpitFx.reset();detailVertices=0;}
    void updateCockpit(const World& world,float dt,bool active){++poseFrame;cockpitFx.update(world,dt,active);prepareCockpitDetails();}
    const CockpitEffects& cockpitEffects()const{return cockpitFx;}
    void draw(const World&,const avp::Matrix& projection,const avp::Matrix& camera,const avp::Matrix& head,bool paused,const Frontend* menu=nullptr);
};
}
