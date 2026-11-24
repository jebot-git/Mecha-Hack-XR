#pragma once
// Standard OpenXR OpenGL bindings. No compositor or streaming-client API.
#include <SDL.h>
#include <SDL_syswm.h>
#include <SDL_system.h>
#include <stdexcept>
#include <cstring>
#include <vector>
#if defined(__ANDROID__)
#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <jni.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#elif defined(_WIN32)
#define XR_USE_PLATFORM_WIN32
#include <unknwn.h>
#undef near
#undef far
#else
#define XR_USE_PLATFORM_XLIB
#define XR_USE_PLATFORM_EGL
#include <EGL/egl.h>
#include <GL/glx.h>
#endif
#ifndef __ANDROID__
#define XR_USE_GRAPHICS_API_OPENGL
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace xr_platform {
#ifdef __ANDROID__
using SwapchainImage = XrSwapchainImageOpenGLESKHR;
inline constexpr auto imageType = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
using Requirements = XrGraphicsRequirementsOpenGLESKHR;
using RequirementsFn = PFN_xrGetOpenGLESGraphicsRequirementsKHR;
inline constexpr auto requirementsType = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR;
inline constexpr const char *requirementsName = "xrGetOpenGLESGraphicsRequirementsKHR";
inline constexpr const char *graphicsExtension = XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME;
#else
using SwapchainImage = XrSwapchainImageOpenGLKHR;
inline constexpr auto imageType = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
using Requirements = XrGraphicsRequirementsOpenGLKHR;
using RequirementsFn = PFN_xrGetOpenGLGraphicsRequirementsKHR;
inline constexpr auto requirementsType = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR;
inline constexpr const char *requirementsName = "xrGetOpenGLGraphicsRequirementsKHR";
inline constexpr const char *graphicsExtension = XR_KHR_OPENGL_ENABLE_EXTENSION_NAME;
#endif
struct Binding {
    bool egl = false;
#if defined(__ANDROID__)
    XrGraphicsBindingOpenGLESAndroidKHR android{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    XrInstanceCreateInfoAndroidKHR instance{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    JNIEnv *env = nullptr;
    jobject activity = nullptr;
#elif defined(_WIN32)
    XrGraphicsBindingOpenGLWin32KHR win{XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};
#else
    XrGraphicsBindingOpenGLXlibKHR glx{XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR};
    XrGraphicsBindingEGLMNDX nativeEgl{XR_TYPE_GRAPHICS_BINDING_EGL_MNDX};
#endif
    explicit Binding(SDL_Window *window) {
        SDL_SysWMinfo wm{};
        SDL_VERSION(&wm.version);
#if defined(__ANDROID__)
        (void)window;
        env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
        activity = static_cast<jobject>(SDL_AndroidGetActivity());
        JavaVM *vm = nullptr;
        if (!env || !activity || env->GetJavaVM(&vm) != JNI_OK)
            throw std::runtime_error("Cannot obtain Android activity for OpenXR");
        PFN_xrInitializeLoaderKHR initialize = nullptr;
        if (XR_FAILED(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
                                            reinterpret_cast<PFN_xrVoidFunction *>(&initialize))) ||
            !initialize)
            throw std::runtime_error("OpenXR Android loader initialization unavailable");
        XrLoaderInitInfoAndroidKHR info{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
        info.applicationVM = vm;
        info.applicationContext = activity;
        if (XR_FAILED(initialize(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR *>(&info))))
            throw std::runtime_error("Cannot initialize OpenXR Android loader");
        instance.applicationVM = vm;
        instance.applicationActivity = activity;
        android.display = eglGetCurrentDisplay();
        android.context = eglGetCurrentContext();
        EGLint id = 0, found = 0;
        EGLint attributes[] = {EGL_CONFIG_ID, 0, EGL_NONE};
        if (!eglQueryContext(android.display, android.context, EGL_CONFIG_ID, &id))
            throw std::runtime_error("Cannot query Android EGL context");
        attributes[1] = id;
        if (!eglChooseConfig(android.display, attributes, &android.config, 1, &found) || !found)
            throw std::runtime_error("Cannot identify Android EGL configuration");
#elif defined(_WIN32)
        if (!SDL_GetWindowWMInfo(window, &wm) || wm.subsystem != SDL_SYSWM_WINDOWS)
            throw std::runtime_error("OpenXR requires an SDL Win32/WGL window");
        win.hDC = wglGetCurrentDC();
        win.hGLRC = wglGetCurrentContext();
        if (!win.hDC || !win.hGLRC)
            throw std::runtime_error("No current WGL context");
#else
        egl = eglGetCurrentContext() != EGL_NO_CONTEXT;
        if (egl) {
            nativeEgl.display = eglGetCurrentDisplay();
            nativeEgl.context = eglGetCurrentContext();
            nativeEgl.getProcAddress =
                reinterpret_cast<decltype(nativeEgl.getProcAddress)>(eglGetProcAddress);
            EGLint id = 0, found = 0;
            eglQueryContext(nativeEgl.display, nativeEgl.context, EGL_CONFIG_ID, &id);
            EGLint attributes[] = {EGL_CONFIG_ID, id, EGL_NONE};
            if (!eglChooseConfig(nativeEgl.display, attributes, &nativeEgl.config, 1, &found) ||
                !found)
                throw std::runtime_error("Could not identify SDL's EGL configuration");
        } else {
            if (!SDL_GetWindowWMInfo(window, &wm) || wm.subsystem != SDL_SYSWM_X11)
                throw std::runtime_error("OpenXR requires native EGL or X11/GLX");
            auto display = wm.info.x11.display;
            auto context = glXGetCurrentContext();
            int id = 0, count = 0;
            glXQueryContext(display, context, GLX_FBCONFIG_ID, &id);
            // SDL_Renderer may use a legacy GLX context whose FBConfig ID is -1.
            // Match its actual window visual instead of rejecting a valid context.
            XWindowAttributes attributes{};
            XGetWindowAttributes(display, wm.info.x11.window, &attributes);
            auto windowVisual = XVisualIDFromVisual(attributes.visual);
            auto configs = glXGetFBConfigs(display, DefaultScreen(display), &count);
            for (int i = 0; i < count; ++i) {
                int candidate = 0;
                glXGetFBConfigAttrib(display, configs[i], GLX_FBCONFIG_ID, &candidate);
                int visualId = 0;
                glXGetFBConfigAttrib(display, configs[i], GLX_VISUAL_ID, &visualId);
                if (candidate == id || (id <= 0 && VisualID(visualId) == windowVisual)) {
                    glx.glxFBConfig = configs[i];
                    if (auto visual = glXGetVisualFromFBConfig(display, configs[i])) {
                        glx.visualid = visual->visualid;
                        XFree(visual);
                    }
                    break;
                }
            }
            XFree(configs);
            if (!glx.glxFBConfig || !context)
                throw std::runtime_error("Could not identify SDL's GLX context");
            glx.xDisplay = display;
            glx.glxDrawable = glXGetCurrentDrawable();
            glx.glxContext = context;
        }
#endif
    }
    ~Binding() {
#ifdef __ANDROID__
        if (activity && env)
            env->DeleteLocalRef(activity);
#endif
    }
    const void *get() const {
#if defined(__ANDROID__)
        return &android;
#elif defined(_WIN32)
        return &win;
#else
        return egl ? static_cast<const void *>(&nativeEgl) : static_cast<const void *>(&glx);
#endif
    }
};
} // namespace xr_platform
