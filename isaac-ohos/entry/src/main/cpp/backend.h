// Pluggable render backend interface for isaac-ohos skeleton.
// VK preferred (user decision D8/D2), GLES as fallback + G0 probe carrier.
#pragma once

#include <string>

// Must match external_window.h / vulkan_ohos.h: the real tag is NativeWindow.
typedef struct NativeWindow OHNativeWindow;

struct Backend {
    virtual ~Backend() {}
    virtual const char* name() const = 0;
    // init(): bind to native window and prepare present path. No GL/VK state
    // may coexist on the same window with another backend (Maleoon quirk,
    // knowledge base doc 13: VK+EGL on one window -> flicker).
    virtual bool init(OHNativeWindow* win, int w, int h) = 0;
    // One-shot diagnostics, multi-line, one fact per line. Logged by the
    // render thread right after init (this is where the G0 verdict lands).
    virtual std::string probe() = 0;
    virtual void frame(float tSec) = 0;
    virtual void shutdown() = 0;
};

Backend* CreateBackendVK();
Backend* CreateBackendGLES();
