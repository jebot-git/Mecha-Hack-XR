#include "xr.hpp"
#include "hud_pose.hpp"
#include "graphics.hpp"
#include <SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
#ifdef TEMPEST_OPENXR
#include "openxr_gl_platform.hpp"

namespace avp {
static void check(XrResult r, const char *name) {
    if (XR_FAILED(r))
        throw std::runtime_error(std::string(name) + " (OpenXR " + std::to_string(r) + ")");
}
#define XR_CHECK(call) check(call, #call)
struct XR::Impl {
    bool rpgControls=false;
    XrInstance instance = XR_NULL_HANDLE;
    XrSession session = XR_NULL_HANDLE;
    XrSpace space = XR_NULL_HANDLE;
    XrSystemId system = XR_NULL_SYSTEM_ID;
#ifdef __ANDROID__
    PFN_xrRequestDisplayRefreshRateFB requestRefresh=nullptr;
    PFN_xrPerfSettingsSetPerformanceLevelEXT setPerformance=nullptr;
    void applyQuestPreset() const {
        if(requestRefresh){
            const auto result=requestRefresh(session,90.f);
            std::cerr<<"Quest display request 90 Hz: "<<result<<'\n';
        }
        if(setPerformance){
            // Horizon chooses the actual clock levels within the requested envelope.
            for(auto domain:{XR_PERF_SETTINGS_DOMAIN_CPU_EXT,XR_PERF_SETTINGS_DOMAIN_GPU_EXT}){
                const auto result=setPerformance(session,domain,XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT);
                std::cerr<<"Quest sustained-high "<<(domain==XR_PERF_SETTINGS_DOMAIN_CPU_EXT?"CPU":"GPU")
                         <<" request: "<<result<<'\n';
            }
        }
    }
#endif
    bool running = false, hasAnchor = false, quit = false, isFocused = false, visible = true;
    XrEnvironmentBlendMode blend = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    struct Eye {
        XrSwapchain chain = XR_NULL_HANDLE;
        int width = 0, height = 0;
        std::vector<xr_platform::SwapchainImage> images;
        GLuint depth = 0;
    };
    std::vector<Eye> eyes;
    GLuint fbo = 0;
    XrVector3f anchor{};
    bool heightCalibrated = false;
    float standingY = 0;
    float yaw = 0;
    std::string runtime;
    uint64_t frames = 0, stereoFrames = 0;
    Tracking tracking;
    SDL_Window *window = nullptr;
    bool frameOpen = false, renderFrame = false;
    bool measureTiming = false;
    XRFrameTiming timing;
    XrTime displayTime = 0;
    std::array<XrView, 2> views{{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}}};
    bool touchPro = false;
    XrActionSet actions = XR_NULL_HANDLE;
    XrAction pose = XR_NULL_HANDLE, trigger = XR_NULL_HANDLE, stick = XR_NULL_HANDLE,
             operate = XR_NULL_HANDLE, menu = XR_NULL_HANDLE, previous = XR_NULL_HANDLE,
             next = XR_NULL_HANDLE, select = XR_NULL_HANDLE, haptic = XR_NULL_HANDLE;
    XrAction facePrimary = XR_NULL_HANDLE, faceSecondary = XR_NULL_HANDLE;
    XrAction recenter = XR_NULL_HANDLE, inventory = XR_NULL_HANDLE;
    XrAction gripPose = XR_NULL_HANDLE, supportHold = XR_NULL_HANDLE, supportClick = XR_NULL_HANDLE;
    XrPath handPaths[2]{};
    XrSpace handSpaces[2]{};
    XrSpace gripSpaces[2]{};
    ~Impl() {
        if (instance)
            std::cerr << "OpenXR frames=" << frames << " stereo_frames=" << stereoFrames << '\n';
        if (fbo)
            glDeleteFramebuffers(1, &fbo);
        for (auto &e : eyes) {
            if (e.depth)
                glDeleteRenderbuffers(1, &e.depth);
            if (e.chain)
                xrDestroySwapchain(e.chain);
        }
        for (auto hand : handSpaces)
            if (hand)
                xrDestroySpace(hand);
        for (auto hand : gripSpaces)
            if (hand)
                xrDestroySpace(hand);
        if (space)
            xrDestroySpace(space);

        if (session)
            xrDestroySession(session);
        if (actions)
            xrDestroyActionSet(actions);
        if (instance)
            xrDestroyInstance(instance);
    }
};
XR::XR(bool rpgControls) : p(std::make_unique<Impl>()) {p->rpgControls=rpgControls;}
XR::~XR() = default;
void XR::initialize(SDL_Window *window) {
    auto &x = *p;
    x.window = window;
    xr_platform::Binding graphics(window);
    bool useEgl = graphics.egl;
    uint32_t extensionCount = 0;
    XR_CHECK(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr));
    std::vector<XrExtensionProperties> available(extensionCount, {XR_TYPE_EXTENSION_PROPERTIES});
    XR_CHECK(xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount,
                                                    available.data()));
    std::vector<const char *> extensions{xr_platform::graphicsExtension};
#ifdef __ANDROID__
    extensions.push_back(XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME);
    auto questExtension=[&](const char *name){
        if(!x.rpgControls)return false;
        for(const auto &property:available)if(std::strcmp(property.extensionName,name)==0){
            extensions.push_back(name);
            return true;
        }
        std::cerr<<"Quest preset extension unavailable: "<<name<<'\n';
        return false;
    };
    const bool refresh=questExtension(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
    const bool performance=questExtension(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME);
#elif !defined(_WIN32)
    if (useEgl)
        extensions.push_back(XR_MNDX_EGL_ENABLE_EXTENSION_NAME);
#endif
    x.touchPro = std::any_of(available.begin(), available.end(), [](const auto &item) {
        return std::strcmp(item.extensionName, "XR_FB_touch_controller_pro") == 0;
    });
    if (x.touchPro)
        extensions.push_back("XR_FB_touch_controller_pro");
    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
#ifdef __ANDROID__
    ci.next = &graphics.instance;
#endif
    std::strcpy(ci.applicationInfo.applicationName, x.rpgControls ? "Mecha Hack XR" : "Iron Soldier XR Foundation");
    ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    ci.enabledExtensionCount = extensions.size();
    ci.enabledExtensionNames = extensions.data();
    XR_CHECK(xrCreateInstance(&ci, &x.instance));
#ifdef __ANDROID__
    if(refresh)XR_CHECK(xrGetInstanceProcAddr(x.instance,"xrRequestDisplayRefreshRateFB",
        reinterpret_cast<PFN_xrVoidFunction*>(&x.requestRefresh)));
    if(performance)XR_CHECK(xrGetInstanceProcAddr(x.instance,"xrPerfSettingsSetPerformanceLevelEXT",
        reinterpret_cast<PFN_xrVoidFunction*>(&x.setPerformance)));
#endif
    XrInstanceProperties properties{XR_TYPE_INSTANCE_PROPERTIES};
    XR_CHECK(xrGetInstanceProperties(x.instance, &properties));
    x.runtime = properties.runtimeName;
    XrSystemGetInfo si{XR_TYPE_SYSTEM_GET_INFO};
    si.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XR_CHECK(xrGetSystem(x.instance, &si, &x.system));
    xr_platform::RequirementsFn requirementsFn = nullptr;
    XR_CHECK(xrGetInstanceProcAddr(x.instance, xr_platform::requirementsName,
                                   reinterpret_cast<PFN_xrVoidFunction *>(&requirementsFn)));
    xr_platform::Requirements requirements{xr_platform::requirementsType};
    XR_CHECK(requirementsFn(x.instance, x.system, &requirements));
    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    auto version = XR_MAKE_VERSION(major, minor, 0);
    if (version < requirements.minApiVersionSupported ||
        version > requirements.maxApiVersionSupported)
        throw std::runtime_error("OpenGL version is outside the OpenXR runtime's supported range");
    XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
    sessionInfo.next = graphics.get();
    sessionInfo.systemId = x.system;
    XR_CHECK(xrCreateSession(x.instance, &sessionInfo, &x.session));
    XrReferenceSpaceCreateInfo ri{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    ri.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    ri.poseInReferenceSpace.orientation.w = 1;
    XR_CHECK(xrCreateReferenceSpace(x.session, &ri, &x.space));
    uint32_t n = 0;
    XR_CHECK(xrEnumerateEnvironmentBlendModes(
        x.instance, x.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &n, nullptr));
    std::vector<XrEnvironmentBlendMode> blends(n);
    XR_CHECK(xrEnumerateEnvironmentBlendModes(
        x.instance, x.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, n, &n, blends.data()));
    if (blends.empty())
        throw std::runtime_error("No OpenXR blend modes");
    x.blend = blends.front();
    XR_CHECK(xrEnumerateSwapchainFormats(x.session, 0, &n, nullptr));
    std::vector<int64_t> formats(n);
    XR_CHECK(xrEnumerateSwapchainFormats(x.session, n, &n, formats.data()));
    int64_t format = 0;
    for (auto wanted : {GL_RGBA8, GL_SRGB8_ALPHA8})
        if (std::find(formats.begin(), formats.end(), wanted) != formats.end()) {
            format = wanted;
            break;
        }
    if (!format)
        throw std::runtime_error("No supported OpenGL RGBA swapchain format");
    XR_CHECK(xrEnumerateViewConfigurationViews(
        x.instance, x.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &n, nullptr));
    std::vector<XrViewConfigurationView> views(n, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    XR_CHECK(xrEnumerateViewConfigurationViews(
        x.instance, x.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, n, &n, views.data()));
    if (n != 2)
        throw std::runtime_error("Prototype requires stereo OpenXR views");
    x.eyes.resize(n);
    for (size_t i = 0; i < x.eyes.size(); i++) {
        auto &e = x.eyes[i];
        e.width = int(views[i].recommendedImageRectWidth);
        e.height = int(views[i].recommendedImageRectHeight);
#ifdef __ANDROID__
        if(x.rpgControls){
            const auto scale=std::min({2048.0/e.width,double(views[i].maxImageRectWidth)/e.width,
                                       double(views[i].maxImageRectHeight)/e.height});
            e.width=int(std::lround(e.width*scale));
            e.height=int(std::lround(e.height*scale));
        }
#endif
        XrSwapchainCreateInfo sc{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        sc.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        sc.format = format;
        sc.sampleCount = 1;
        sc.width = e.width;
        sc.height = e.height;
        sc.faceCount = 1;
        sc.arraySize = 1;
        sc.mipCount = 1;
        XR_CHECK(xrCreateSwapchain(x.session, &sc, &e.chain));
        XR_CHECK(xrEnumerateSwapchainImages(e.chain, 0, &n, nullptr));
        e.images.resize(n, {xr_platform::imageType});
        XR_CHECK(xrEnumerateSwapchainImages(
            e.chain, n, &n, reinterpret_cast<XrSwapchainImageBaseHeader *>(e.images.data())));
        glGenRenderbuffers(1, &e.depth);
        glBindRenderbuffer(GL_RENDERBUFFER, e.depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, e.width, e.height);
    }
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    initializeActions();
    glGenFramebuffers(1, &x.fbo);
    std::cerr << "OpenXR initialized: " << x.runtime << " / " << x.eyes[0].width << "x"
              << x.eyes[0].height << " per eye\n";
}
void XR::initializeActions() {
    auto &x = *p;
    auto path = [&](const char *name) {
        XrPath value;
        XR_CHECK(xrStringToPath(x.instance, name, &value));
        return value;
    };
    x.handPaths[0] = path("/user/hand/left");
    x.handPaths[1] = path("/user/hand/right");
    XrActionSetCreateInfo set{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::strcpy(set.actionSetName, "gameplay");
    std::strcpy(set.localizedActionSetName, "Gameplay");
    XR_CHECK(xrCreateActionSet(x.instance, &set, &x.actions));
    auto action = [&](XrAction &out, const char *name, XrActionType type) {
        XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
        info.actionType = type;
        std::strcpy(info.actionName, name);
        std::strcpy(info.localizedActionName,
                    std::strcmp(name, "use") == 0 ? "Use / Operate" : std::strcmp(name, "cockpit_recenter") == 0 ? "Reset cockpit position and height" : name);
        info.countSubactionPaths = 2;
        info.subactionPaths = x.handPaths;
        XR_CHECK(xrCreateAction(x.actions, &info, &out));
    };
    action(x.facePrimary, "primary_mount", XR_ACTION_TYPE_BOOLEAN_INPUT);
    action(x.faceSecondary, "secondary_mount", XR_ACTION_TYPE_BOOLEAN_INPUT);
    action(x.pose, "aim_pose", XR_ACTION_TYPE_POSE_INPUT);
    action(x.gripPose, "grip_pose", XR_ACTION_TYPE_POSE_INPUT);
    action(x.supportHold, "support_hold", XR_ACTION_TYPE_FLOAT_INPUT);
    action(x.supportClick, "support_click", XR_ACTION_TYPE_BOOLEAN_INPUT);
    action(x.trigger, "trigger", XR_ACTION_TYPE_FLOAT_INPUT);
    action(x.stick, "move_turn", XR_ACTION_TYPE_VECTOR2F_INPUT);
    action(x.operate, "use", XR_ACTION_TYPE_BOOLEAN_INPUT);
    action(x.menu, "pause", XR_ACTION_TYPE_BOOLEAN_INPUT);
    action(x.recenter, x.rpgControls?"horizontal_thruster":"cockpit_recenter", XR_ACTION_TYPE_BOOLEAN_INPUT);
    action(x.inventory, x.rpgControls?"vertical_thruster":"searchlight", XR_ACTION_TYPE_BOOLEAN_INPUT);
    action(x.previous, "previous_weapon", XR_ACTION_TYPE_BOOLEAN_INPUT);
    action(x.next, "next_weapon", XR_ACTION_TYPE_BOOLEAN_INPUT);
    action(x.select, "select", XR_ACTION_TYPE_BOOLEAN_INPUT);
    action(x.haptic, "feedback", XR_ACTION_TYPE_VIBRATION_OUTPUT);
    std::vector<const char *> profiles{"/interaction_profiles/oculus/touch_controller",
                                       "/interaction_profiles/valve/index_controller",
                                       "/interaction_profiles/htc/vive_controller",
                                       "/interaction_profiles/microsoft/motion_controller",
                                       "/interaction_profiles/khr/simple_controller"};
    if (x.touchPro)
        profiles.push_back("/interaction_profiles/facebook/touch_controller_pro");
    for (auto profile : profiles) {
        bool simple = std::strstr(profile, "simple"), vive = std::strstr(profile, "vive"),
             motion = std::strstr(profile, "microsoft/motion_controller");
        std::vector<XrActionSuggestedBinding> bindings;
        auto bind = [&](XrAction a, const std::string &b) {
            bindings.push_back({a, path(b.c_str())});
        };
        for (int h = 0; h < 2; ++h) {
            std::string hand = h ? "/user/hand/right" : "/user/hand/left";
            bind(x.pose, hand + "/input/aim/pose");
            bind(x.gripPose, hand + "/input/grip/pose");
            bind(x.haptic, hand + "/output/haptic");
            if (simple)
                bind(x.select, hand + "/input/select/click");
            else {
                bind(x.trigger, hand + "/input/trigger/value");
                bind(x.stick, hand + (vive ? "/input/trackpad" : "/input/thumbstick"));
                bind((vive || motion) ? x.supportClick : x.supportHold,
                     hand + "/input/squeeze/" + ((vive || motion) ? "click" : "value"));
            }
            if (!simple && ((!vive && !motion) || h))
                bind(x.operate, hand + ((vive || motion) ? "/input/squeeze/click"
                                        : h              ? "/input/a/click"
                                                         : "/input/x/click"));
        }
        bind(x.menu, "/user/hand/left/input/menu/click");
        if (!simple && !vive && !motion) {
            bind(x.previous, "/user/hand/left/input/y/click");
            bind(x.next, "/user/hand/right/input/b/click");
        }
        if (!simple) {
            bind(x.recenter, std::string("/user/hand/left/input/") +
                            ((vive || (motion&&!x.rpgControls)) ? "trackpad/click" : "thumbstick/click"));
            bind(x.inventory, std::string("/user/hand/right/input/") +
                                  (vive ? "trackpad/click" : "thumbstick/click"));
        }
        if (motion) {
            bind(x.previous, "/user/hand/left/input/thumbstick/click");
            bind(x.next, "/user/hand/right/input/thumbstick/click");
        }
        if (vive)
            bind(x.next, "/user/hand/right/input/menu/click");
        // Index has no menu path or A/X naming distinction; use its A/B buttons.
        if (std::strstr(profile, "index")) {
            bindings.clear();
            for (int h = 0; h < 2; ++h) {
                std::string hand = h ? "/user/hand/right" : "/user/hand/left";
                bind(x.pose, hand + "/input/aim/pose");
                bind(x.gripPose, hand + "/input/grip/pose");
                bind(x.haptic, hand + "/output/haptic");
                bind(x.trigger, hand + "/input/trigger/value");
                bind(x.stick, hand + "/input/thumbstick");
                bind(x.operate, hand + "/input/a/click");
                bind(x.supportHold, hand + "/input/squeeze/value");
            }
            // Index has a force-sensitive trackpad, without a trackpad/click
            // component. OpenXR applies scalar-to-boolean threshold/hysteresis.
            bind(x.recenter, x.rpgControls?"/user/hand/left/input/thumbstick/click":"/user/hand/right/input/trackpad/force");
            bind(x.inventory, "/user/hand/right/input/thumbstick/click");
            if(!x.rpgControls)bind(x.menu, "/user/hand/left/input/thumbstick/click");
            bind(x.previous, "/user/hand/left/input/b/click");
            bind(x.next, "/user/hand/right/input/b/click");
        }
        if (!simple && !vive && !motion) {
            bool index = std::strstr(profile, "index");
            bind(x.facePrimary, "/user/hand/right/input/a/click");
            bind(x.faceSecondary, "/user/hand/right/input/b/click");
            bind(x.facePrimary, index ? "/user/hand/left/input/a/click" : "/user/hand/left/input/x/click");
            bind(x.faceSecondary, index ? "/user/hand/left/input/b/click" : "/user/hand/left/input/y/click");
        }
        XrInteractionProfileSuggestedBinding info{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        info.interactionProfile = path(profile);
        info.countSuggestedBindings = bindings.size();
        info.suggestedBindings = bindings.data();
        auto result = xrSuggestInteractionProfileBindings(x.instance, &info);
        if (XR_FAILED(result))
            std::cerr << "Controller profile unavailable: " << profile << " (" << result << ")\n";
    }
    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = 1;
    attach.actionSets = &x.actions;
    XR_CHECK(xrAttachSessionActionSets(x.session, &attach));
    for (int h = 0; h < 2; ++h) {
        XrActionSpaceCreateInfo info{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        info.action = x.pose;
        info.subactionPath = x.handPaths[h];
        info.poseInActionSpace.orientation.w = 1;
        XR_CHECK(xrCreateActionSpace(x.session, &info, &x.handSpaces[h]));
        info.action = x.gripPose;
        XR_CHECK(xrCreateActionSpace(x.session, &info, &x.gripSpaces[h]));
    }
}
void XR::sampleActions(XrTime time, bool sampleHiddenInput) {
    auto &x = *p;
    auto &t = x.tracking;

    std::array<bool, 2> wasSqueeze{t.squeezeHeld[0], t.squeezeHeld[1]};
    t.squeezeHeld[0] = t.squeezeHeld[1] = false;
    t.fire = t.use = t.menu = t.previousWeapon = t.nextWeapon = t.jump = false;
    t.recenter = t.inventory = false;
    t.activate = t.vision = t.heal = false;
    t.triggerHeld[0] = t.triggerHeld[1] = false;
    t.facePrimary[0] = t.facePrimary[1] = t.faceSecondary[0] = t.faceSecondary[1] = false;
    t.supportHeld = false;
    t.supportAvailable = false;
    t.move = {};
    t.turn = {};
    t.valid[0] = t.valid[1] = false;
    t.gripValid[0] = t.gripValid[1] = false;
    XrActiveActionSet active{x.actions, XR_NULL_PATH};
    XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    XR_CHECK(xrSyncActions(x.session, &sync));
    if (!x.isFocused || (!x.visible && !sampleHiddenInput) || !t.headValid)
        return;
    for (int h = 0; h < 2; ++h) {
        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        get.subactionPath = x.handPaths[h];
        get.action = x.pose;
        XrActionStatePose pose{XR_TYPE_ACTION_STATE_POSE};
        XR_CHECK(xrGetActionStatePose(x.session, &get, &pose));
        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
        XR_CHECK(xrLocateSpace(x.handSpaces[h], x.space, time, &location));
        auto required =
            XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        t.valid[h] = pose.isActive && (location.locationFlags & required) == required;
        t.hands[h] = location.pose;
        get.action = x.gripPose;
        XrActionStatePose grip{XR_TYPE_ACTION_STATE_POSE};
        XR_CHECK(xrGetActionStatePose(x.session, &get, &grip));
        XrSpaceLocation gripLocation{XR_TYPE_SPACE_LOCATION};
        XR_CHECK(xrLocateSpace(x.gripSpaces[h], x.space, time, &gripLocation));
        t.gripValid[h] = grip.isActive && (gripLocation.locationFlags & required) == required &&
                         (gripLocation.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT);
        t.grips[h] = gripLocation.pose;
        get.action = x.stick;
        XrActionStateVector2f stick{XR_TYPE_ACTION_STATE_VECTOR2F};
        XR_CHECK(xrGetActionStateVector2f(x.session, &get, &stick));
        if (stick.isActive) {
            if (h)
                t.turn = stick.currentState;
            else
                t.move = stick.currentState;
        }
        auto boolean = [&](XrAction action) {
            get.action = action;
            XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
            XR_CHECK(xrGetActionStateBoolean(x.session, &get, &state));
            return state.isActive && state.currentState;
        };
        t.facePrimary[h] = boolean(x.facePrimary);
        t.faceSecondary[h] = boolean(x.faceSecondary);
        bool clicked = boolean(x.select);
        if (h)
            t.fire = clicked;
        else
            t.use = clicked;
        bool operate = boolean(x.operate);
        t.use = t.use || operate;
        if (h) t.activate = operate || clicked;
        else t.vision = operate;
        t.menu = t.menu || boolean(x.menu);
        t.recenter = t.recenter || boolean(x.recenter);
        t.inventory = t.inventory || boolean(x.inventory);
        t.previousWeapon = t.previousWeapon || boolean(x.previous);
        t.nextWeapon = t.nextWeapon || boolean(x.next);
        {
            get.action = x.supportClick;
            XrActionStateBoolean click{XR_TYPE_ACTION_STATE_BOOLEAN};
            XR_CHECK(xrGetActionStateBoolean(x.session, &get, &click));
            get.action = x.supportHold;
            XrActionStateFloat squeeze{XR_TYPE_ACTION_STATE_FLOAT};
            XR_CHECK(xrGetActionStateFloat(x.session, &get, &squeeze));
            t.squeezeHeld[h] =
                (click.isActive && click.currentState) ||
                (squeeze.isActive && squeeze.currentState > (wasSqueeze[h] ? .35f : .65f));
            if (!h) {
                t.supportAvailable = click.isActive || squeeze.isActive;
                t.supportHeld = t.squeezeHeld[h];
            }
        }
        get.action = x.trigger;
        XrActionStateFloat trigger{XR_TYPE_ACTION_STATE_FLOAT};
        XR_CHECK(xrGetActionStateFloat(x.session, &get, &trigger));
        t.triggerHeld[h] = trigger.isActive && trigger.currentState > .55f;
        if (!h && trigger.isActive && trigger.currentState > .55f &&
            (t.valid[h] || t.gripValid[h]))
            t.heal = true;
        if (trigger.isActive && trigger.currentState > .55f && t.valid[h]) {
            if (h)
                t.fire = true;
            else
                t.use = true;
        }
    }
    if (!t.valid[1])
        t.fire = false;
}
bool XR::beginFrame(bool measureTiming, bool sampleHiddenInput) {
    auto &x = *p;
    x.measureTiming = measureTiming;
    x.timing = {};
    x.frameOpen = false;
    x.tracking.headValid = false;
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    XrResult result;
    while ((result = xrPollEvent(x.instance, &event)) == XR_SUCCESS) {
        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            auto state = reinterpret_cast<XrEventDataSessionStateChanged &>(event).state;
            x.isFocused = state == XR_SESSION_STATE_FOCUSED;
            if (state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo info{XR_TYPE_SESSION_BEGIN_INFO};
                info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                XR_CHECK(xrBeginSession(x.session, &info));
                x.running = true;
#ifdef __ANDROID__
                x.applyQuestPreset();
#endif
            }
            if (state == XR_SESSION_STATE_STOPPING) {
                x.running = false;
                XR_CHECK(xrEndSession(x.session));
            }
            if (state == XR_SESSION_STATE_EXITING || state == XR_SESSION_STATE_LOSS_PENDING)
                x.quit = true;
        } else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
            x.quit = true;
        event = {XR_TYPE_EVENT_DATA_BUFFER};
    }
    if (result != XR_EVENT_UNAVAILABLE)
        check(result, "xrPollEvent");
    if (x.quit)
        return false;
    if (!x.running)
        return true;
    XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState state{XR_TYPE_FRAME_STATE};
    auto waitStart =
        measureTiming ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    XR_CHECK(xrWaitFrame(x.session, &wait, &state));
    if (measureTiming)
        x.timing.waitFrame = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::steady_clock::now() - waitStart)
                                 .count();
    x.timing.predictedDisplayTime = state.predictedDisplayTime;
    x.timing.predictedDisplayPeriod = state.predictedDisplayPeriod;
    XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
    XR_CHECK(xrBeginFrame(x.session, &begin));
    x.frameOpen = true;
    x.displayTime = state.predictedDisplayTime;
    XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
    locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate.displayTime = x.displayTime;
    locate.space = x.space;
    XrViewState valid{XR_TYPE_VIEW_STATE};
    uint32_t count = 0;
    XR_CHECK(xrLocateViews(x.session, &locate, &valid, 2, &count, x.views.data()));
    bool tracked = count == 2 &&
                   (valid.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) &&
                   (valid.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT);
    x.renderFrame = x.visible && state.shouldRender && tracked;
    x.tracking.headValid = x.renderFrame || (sampleHiddenInput && tracked);
    if (x.tracking.headValid)
        x.tracking.headPosition = {(x.views[0].pose.position.x + x.views[1].pose.position.x) * .5f,
                                   (x.views[0].pose.position.y + x.views[1].pose.position.y) * .5f,
                                   (x.views[0].pose.position.z + x.views[1].pose.position.z) * .5f};
    sampleActions(x.displayTime, sampleHiddenInput);
    return true;
}
void XR::endFrame(const std::function<void(const XrView &, int, int, int)> &draw, bool mirror) {
    auto &x = *p;
    if (!x.frameOpen)
        return;
    std::array<XrCompositionLayerProjectionView, 2> views{
        {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}}};
    if (x.renderFrame)
        for (int eye = 0; eye < 2; eye++) {
            auto &e = x.eyes[eye];
            uint32_t image = 0;
            XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            XR_CHECK(xrAcquireSwapchainImage(e.chain, &acquire, &image));
            XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            wait.timeout = XR_INFINITE_DURATION;
            auto waitStart = x.measureTiming ? std::chrono::steady_clock::now()
                                             : std::chrono::steady_clock::time_point{};
            XR_CHECK(xrWaitSwapchainImage(e.chain, &wait));
            if (x.measureTiming)
                x.timing.waitSwapchain += std::chrono::duration_cast<std::chrono::nanoseconds>(
                                              std::chrono::steady_clock::now() - waitStart)
                                              .count();
            glBindFramebuffer(GL_FRAMEBUFFER, x.fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                   e.images.at(image).image, 0);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                                      e.depth);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                throw std::runtime_error("Incomplete native XR framebuffer");
            glViewport(0, 0, e.width, e.height);
#ifndef __ANDROID__
            glDisable(GL_FRAMEBUFFER_SRGB);
#endif
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            draw(x.views[eye], eye, e.width, e.height);
            if (x.window && mirror) {
                int width, height;
                SDL_GL_GetDrawableSize(x.window, &width, &height);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, x.fbo);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                glBlitFramebuffer(0, 0, e.width, e.height, eye * width / 2, 0,
                                  (eye + 1) * width / 2, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            }
            glFlush();
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            XR_CHECK(xrReleaseSwapchainImage(e.chain, &release));
            views[eye].pose = x.views[eye].pose;
            views[eye].fov = x.views[eye].fov;
            views[eye].subImage.swapchain = e.chain;
            views[eye].subImage.imageRect.extent = {int32_t(e.width), int32_t(e.height)};
        }
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    layer.space = x.space;
    layer.viewCount = 2;
    layer.views = views.data();
    const XrCompositionLayerBaseHeader *layers[] = {
        reinterpret_cast<const XrCompositionLayerBaseHeader *>(&layer)};
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
    end.displayTime = x.displayTime;
    end.environmentBlendMode = x.blend;
    end.layerCount = x.renderFrame ? 1 : 0;
    end.layers = x.renderFrame ? layers : nullptr;
    auto endStart = x.measureTiming ? std::chrono::steady_clock::now()
                                    : std::chrono::steady_clock::time_point{};
    XR_CHECK(xrEndFrame(x.session, &end));
    if (x.measureTiming)
        x.timing.endFrame = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now() - endStart)
                                .count();
    x.frames++;
    if (x.renderFrame)
        x.stereoFrames++;
    x.frameOpen = false;
}
const XRFrameTiming &XR::frameTiming() const { return p->timing; }
const Tracking &XR::tracking() const { return p->tracking; }
XrPosef XR::headPose() const {
    XrPosef pose = p->views[0].pose;
    pose.position = p->tracking.headPosition;
    return pose;
}
XrFovf XR::hudFov() const { return commonHudFov(p->views[0].fov, p->views[1].fov); }
bool XR::focused() const { return p->isFocused; }
void XR::setVisible(bool value) { p->visible = value; }
void XR::recenter() { p->tracking.anchor = p->tracking.headPosition; }
bool XR::resetHeight() {
    recenter();
    return p->tracking.headValid;
}
std::string XR::description() const { return p->runtime; }
void XR::haptic(float amplitude, int milliseconds) {
    if (!p->isFocused)
        return;
    XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
    info.action = p->haptic;
    info.subactionPath = p->handPaths[1];
    XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
    vibration.amplitude = std::clamp(amplitude, 0.f, 1.f);
    vibration.duration = milliseconds * 1000000LL;
    vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
    xrApplyHapticFeedback(p->session, &info, reinterpret_cast<XrHapticBaseHeader *>(&vibration));
}
} // namespace avp
#endif
