#pragma once
#include <array>
#include <functional>
#include <memory>
#include <openxr/openxr.h>
#include <string>
struct SDL_Window;
namespace avp {
struct Tracking {
    XrPosef hands[2]{}, grips[2]{};
    bool valid[2]{}, gripValid[2]{}, headValid = false;
    bool squeezeHeld[2]{};
    bool activate = false, vision = false, heal = false;
    bool triggerHeld[2]{};
    bool facePrimary[2]{}, faceSecondary[2]{};
    bool supportHeld = false, supportAvailable = false, fire = false, use = false, menu = false,
         previousWeapon = false, nextWeapon = false, jump = false, recenter = false, inventory = false;
    XrVector2f move{}, turn{};
    XrVector3f anchor{}, headPosition{};
    float anchorYaw = 0, headYawRadians = 0;
};
struct XRFrameTiming {
    XrTime predictedDisplayTime = 0;
    XrDuration predictedDisplayPeriod = 0;
    // CPU wall time around the individual runtime calls, measured only for reports.
    XrDuration waitFrame = 0, waitSwapchain = 0, endFrame = 0;
};
class XR {
    struct Impl;
    std::unique_ptr<Impl> p;
    void initializeActions();
    void sampleActions(XrTime time, bool sampleHiddenInput);

  public:
    explicit XR(bool rpgControls=false);
    ~XR();
    XR(const XR &) = delete;
    XR &operator=(const XR &) = delete;
    void initialize(SDL_Window *);
    // Hidden input is opt-in for an explicit frontend resume gesture. It
    // samples focused tracking/actions without submitting hidden XR layers.
    bool beginFrame(bool measureTiming = false, bool sampleHiddenInput = false);
    void endFrame(const std::function<void(const XrView &, int, int, int)> &draw,
                  bool mirror = true);
    const XRFrameTiming &frameTiming() const;
    const Tracking &tracking() const;
    XrPosef headPose() const;
    XrFovf hudFov() const;
    bool focused() const;
    void setVisible(bool);
    void recenter();
    bool resetHeight();
    std::string description() const;
    void haptic(float amplitude = .15f, int milliseconds = 20);
};
} // namespace avp
