// NAPI bridge: XComponent surfaceId -> OHNativeWindow -> dedicated render
// thread with a pluggable VK/GLES backend. VK preferred (user decision),
// GLES fallback. All present-path diagnostics land in hilog under "isaacoh".
#include <hilog/log.h>
#include "napi/native_api.h"
#include <native_window/external_window.h>
#include "backend.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <execinfo.h>
#include <mutex>
#include <pthread.h>
#include <signal.h>
#include <string>
#include <unistd.h>

extern "C" {
int isaac_guest_init(const char *exe_path);
int isaac_guest_run(void);
void isaac_guest_shutdown(void);
}

#define TAG "isaacoh"
#define LOGI(...) ((void)OH_LOG_Print(LOG_APP, LOG_INFO, 0, TAG, __VA_ARGS__))
#define LOGE(...) ((void)OH_LOG_Print(LOG_APP, LOG_ERROR, 0, TAG, __VA_ARGS__))
#define LOGF(...) ((void)OH_LOG_Print(LOG_APP, LOG_FATAL, 0, TAG, __VA_ARGS__))

// Native crashes on device are unreadable without this: dump backtrace to hilog.
static void OhosCrashHandler(int sig) {
    void* frames[64];
    int n = backtrace(frames, 64);
    LOGF("CRASH signal=%{public}d frames=%{public}d", sig, n);
    char** syms = backtrace_symbols(frames, n);
    if (syms != nullptr) {
        for (int i = 0; i < n; i++) {
            LOGF("  #%02d %{public}s", i, syms[i] != nullptr ? syms[i] : "?");
        }
        free(syms);
    }
    _exit(128 + sig);
}

__attribute__((constructor(101))) static void InstallEarlyCrashHandler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = OhosCrashHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
    sigaction(SIGILL, &sa, nullptr);
#ifdef SIGFPE
    sigaction(SIGFPE, &sa, nullptr);
#endif
}

namespace {

std::mutex g_mtx;
OHNativeWindow* g_win = nullptr;
int g_w = 0;
int g_h = 0;
std::string g_backend = "gles";
std::atomic<bool> g_run{false};
pthread_t g_thr = 0;
std::atomic<int> g_fps{0};
std::string g_dataPath;

/* Surface size, used by the touch-to-d-pad mapping in Touch(). */
int32_t g_surface_w = 0;
int32_t g_surface_h = 0;

/* Input state feed (implemented in isaac/host_ohos.c). */
extern "C" void isaac_input_key(uint32_t vk, int down);
extern "C" void isaac_input_pointer(int32_t x, int32_t y);

float NowSec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<float>(ts.tv_sec) + static_cast<float>(ts.tv_nsec) * 1e-9f;
}

void LogLines(const char* tag, const std::string& s) {
    size_t pos = 0;
    while (pos < s.size()) {
        size_t nl = s.find('\n', pos);
        if (nl == std::string::npos) {
            nl = s.size();
        }
        if (nl > pos) {
            LOGI("[%{public}s] %{public}s", tag, s.substr(pos, nl - pos).c_str());
        }
        pos = nl + 1;
    }
}

Backend* MakeBackend(const std::string& name) {
    return name == "vk" ? CreateBackendVK() : CreateBackendGLES();
}

const char* AltName(const std::string& name) {
    return name == "vk" ? "gles" : "vk";
}

void* RenderThreadMain(void*) {
    LOGI("render thread start");
    std::string want = "gles";
    std::string active;
    Backend* cur = nullptr;
    int64_t frames = 0;
    int64_t winMs = 0;
    int guest_started = 0;

    while (g_run.load()) {
        {
            std::lock_guard<std::mutex> lk(g_mtx);
            want = g_backend;
        }
        if (want != active) {
            if (cur != nullptr) {
                cur->shutdown();
                delete cur;
                cur = nullptr;
            }
            active.clear();
            Backend* b = MakeBackend(want);
            if (b != nullptr && b->init(g_win, g_w, g_h)) {
                LogLines(b->name(), b->probe());
                cur = b;
                active = want;
                LOGI("backend active: %{public}s", active.c_str());
            } else {
                LOGE("backend %{public}s init FAILED, trying %{public}s", want.c_str(), AltName(want));
                delete b;
                const char* alt = AltName(want);
                Backend* altB = MakeBackend(alt);
                if (altB != nullptr && altB->init(g_win, g_w, g_h)) {
                    LogLines(altB->name(), altB->probe());
                    cur = altB;
                    active = alt;
                    LOGI("backend fallback active: %{public}s", active.c_str());
                } else {
                    LOGE("backend %{public}s init FAILED too; no graphics this window", alt);
                    delete altB;
                    usleep(1000000);
                }
            }
        }

        if (cur != nullptr && !guest_started) {
            std::string exePath = g_dataPath + "/isaac-ng.exe";
            LOGI("guest init: exe=%{public}s data=%{public}s", exePath.c_str(), g_dataPath.c_str());
            int gi = isaac_guest_init(exePath.c_str());
            if (gi == 0) {
                LOGI("guest init OK, starting main loop");
                guest_started = 1;
                int gr = isaac_guest_run();
                LOGI("guest run returned: %{public}d", gr);
            } else {
                LOGE("guest init FAILED: %{public}d", gi);
            }
        }

        if (guest_started) {
            static int64_t last_hb = 0;
            struct timespec hbts;
            clock_gettime(CLOCK_MONOTONIC, &hbts);
            int64_t hbms = static_cast<int64_t>(hbts.tv_sec) * 1000 + hbts.tv_nsec / 1000000;
            if (last_hb == 0) last_hb = hbms;
            if (hbms - last_hb >= 2000) {
                LOGI("heartbeat: guest_started=%d backend=%s fps=%d",
                     guest_started, active.c_str(), g_fps.load());
                last_hb = hbms;
            }
        }

        if (cur != nullptr) {
            cur->frame(NowSec());
            frames++;
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            int64_t ms = static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
            if (winMs == 0) {
                winMs = ms;
            }
            int64_t el = ms - winMs;
            if (el >= 1000) {
                int fps = static_cast<int>(frames * 1000 / el);
                g_fps.store(fps);
                LOGI("fps=%{public}d frames=%{public}lld backend=%{public}s", fps,
                     static_cast<long long>(frames), active.c_str());
                winMs = ms;
                frames = 0;
            }
        } else {
            usleep(100000);
        }
    }
    if (guest_started) {
        isaac_guest_shutdown();
    }
    if (cur != nullptr) {
        cur->shutdown();
        delete cur;
        cur = nullptr;
    }
    LOGI("render thread exit");
    return nullptr;
}

uint64_t GetSurfaceId(napi_env env, napi_value arg) {
    uint64_t surfaceId = 0;
    napi_valuetype type = napi_undefined;
    napi_typeof(env, arg, &type);
    if (type == napi_string) {
        size_t len = 0;
        napi_get_value_string_utf8(env, arg, nullptr, 0, &len);
        std::string buf(len, '\0');
        if (len > 0) {
            napi_get_value_string_utf8(env, arg, &buf[0], len + 1, &len);
        }
        surfaceId = strtoull(buf.c_str(), nullptr, 10);
    } else if (type == napi_number) {
        double v = 0.0;
        napi_get_value_double(env, arg, &v);
        surfaceId = static_cast<uint64_t>(v);
    }
    return surfaceId;
}

std::string GetStringArg(napi_env env, napi_value arg) {
    size_t len = 0;
    napi_get_value_string_utf8(env, arg, nullptr, 0, &len);
    std::string s(len, '\0');
    if (len > 0) {
        napi_get_value_string_utf8(env, arg, &s[0], len + 1, &len);
    }
    return s;
}

napi_value Init(napi_env env, napi_callback_info info) {
    size_t argc = 4;
    napi_value args[4] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_run.load()) {
        LOGI("Init: already running");
        napi_value ret = nullptr;
        napi_create_int32(env, 0, &ret);
        return ret;
    }

    uint64_t surfaceId = (argc >= 1) ? GetSurfaceId(env, args[0]) : 0;
    int32_t screenW = 0;
    int32_t screenH = 0;
    std::string backend = (argc >= 4) ? GetStringArg(env, args[3]) : std::string("vk");
    if (argc >= 2) {
        napi_get_value_int32(env, args[1], &screenW);
    }
    if (argc >= 3) {
        napi_get_value_int32(env, args[2], &screenH);
    }
    g_surface_w = screenW;
    g_surface_h = screenH;
    if (backend != "gles") {
        backend = "vk";
    }

    if (surfaceId == 0) {
        LOGE("Init: invalid surfaceId");
        napi_value ret = nullptr;
        napi_create_int32(env, -1, &ret);
        return ret;
    }

    int32_t ret32 = OH_NativeWindow_CreateNativeWindowFromSurfaceId(surfaceId, &g_win);
    if (ret32 != 0 || g_win == nullptr) {
        LOGE("CreateNativeWindowFromSurfaceId FAILED ret=%{public}d", ret32);
        napi_value ret = nullptr;
        napi_create_int32(env, -2, &ret);
        return ret;
    }
    g_w = screenW;
    g_h = screenH;
    g_backend = backend;

    g_run.store(true);
    if (pthread_create(&g_thr, nullptr, RenderThreadMain, nullptr) != 0) {
        LOGE("pthread_create FAILED");
        g_run.store(false);
        OH_NativeWindow_DestroyNativeWindow(g_win);
        g_win = nullptr;
        napi_value ret = nullptr;
        napi_create_int32(env, -3, &ret);
        return ret;
    }

    LOGI("Init OK surfaceId=%{public}llu size=%{public}dx%{public}d backend=%{public}s",
         static_cast<unsigned long long>(surfaceId), g_w, g_h, g_backend.c_str());
    napi_value ret = nullptr;
    napi_create_int32(env, 0, &ret);
    return ret;
}

napi_value Destroy(napi_env env, napi_callback_info info) {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_run.load()) {
        g_run.store(false);
        if (g_thr != 0) {
            pthread_join(g_thr, nullptr);
            g_thr = 0;
        }
    }
    if (g_win != nullptr) {
        OH_NativeWindow_DestroyNativeWindow(g_win);
        g_win = nullptr;
    }
    g_fps.store(0);
    LOGI("Destroy OK");
    return nullptr;
}

napi_value Touch(napi_env env, napi_callback_info info) {
    size_t argc = 4;
    napi_value args[4] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    double x = 0;
    double y = 0;
    int32_t action = 0;
    if (argc >= 1) {
        napi_get_value_double(env, args[0], &x);
    }
    if (argc >= 2) {
        napi_get_value_double(env, args[1], &y);
    }
    if (argc >= 3) {
        napi_get_value_int32(env, args[2], &action);
    }
    if (action != 1) { // skip Move spam
        LOGI("touch action=%{public}d x=%.0f y=%.0f", action, x, y);
    }
    /* Pointer only: the ArkTS overlay owns the stick/button mapping and drives
     * keys through native.key(). */
    isaac_input_pointer(static_cast<int32_t>(x), static_cast<int32_t>(y));
    return nullptr;
}

/* native.key(vk, down): the touch overlay's sticks and buttons drive the same
 * key table the engine polls through GetAsyncKeyState. */
napi_value Key(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    int32_t vk = 0;
    int32_t down = 0;
    if (argc >= 1) {
        napi_get_value_int32(env, args[0], &vk);
    }
    if (argc >= 2) {
        napi_get_value_int32(env, args[1], &down);
    }
    isaac_input_key(static_cast<uint32_t>(vk), down);
    return nullptr;
}

napi_value GetFps(napi_env env, napi_callback_info info) {
    napi_value ret = nullptr;
    napi_create_int32(env, g_fps.load(), &ret);
    return ret;
}

napi_value SetBackend(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    std::string b = (argc >= 1) ? GetStringArg(env, args[0]) : std::string("vk");
    if (b != "gles") {
        b = "vk";
    }
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_backend = b;
    }
    LOGI("SetBackend -> %{public}s", b.c_str());
    napi_value ret = nullptr;
    napi_create_int32(env, 0, &ret);
    return ret;
}

napi_value SetDataPath(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    std::string p = (argc >= 1) ? GetStringArg(env, args[0]) : std::string("");
    g_dataPath = p;
    /* Wire the data root into the C shim: host_data_root() reads this env
     * var. Without it the device guest falls back to the literal "isaac-data"
     * relative path, which lands outside the writable sandbox (EACCES) and
     * every resource lookup fails. */
    if (!p.empty()) {
        setenv("ISAAC_DATA_ROOT", p.c_str(), 1);
        LOGI("ISAAC_DATA_ROOT set -> %{public}s", p.c_str());
    }
    LOGI("SetDataPath -> %{public}s", p.c_str());
    napi_value ret = nullptr;
    napi_create_int32(env, 0, &ret);
    return ret;
}

napi_value NativeInit(napi_env env, napi_value exports) {
    napi_property_descriptor desc[] = {
        {"init", nullptr, Init, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"destroy", nullptr, Destroy, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"touch", nullptr, Touch, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"key", nullptr, Key, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"getFps", nullptr, GetFps, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"setBackend", nullptr, SetBackend, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"setDataPath", nullptr, SetDataPath, nullptr, nullptr, nullptr, napi_default, nullptr},
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    return exports;
}

napi_module isaacModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = NativeInit,
    .nm_modname = "entry",
    .nm_priv = ((void*)0),
    .reserved = {0},
};

} // namespace

extern "C" __attribute__((constructor)) void RegisterEntryModule(void) {
    napi_module_register(&isaacModule);
}
