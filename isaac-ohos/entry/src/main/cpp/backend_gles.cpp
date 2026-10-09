// GLES backend: EGL + GLES 3.x present path, rotating triangle on cycling
// clear. Also carries the G0 probe: glGetString via the linked libGLESv3 vs
// dlsym'd glGetString from libGLESv2 (the library that killed gtasa).
#include "backend.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <dlfcn.h>
#include <hilog/log.h>

#include <cmath>
#include <cstring>
#include <string>

#ifndef EGL_OPENGL_ES3_BIT
#define EGL_OPENGL_ES3_BIT 0x0040
#endif

#define TAG "isaac.gles"
#define LOGI(...) ((void)OH_LOG_Print(LOG_APP, LOG_INFO, 0, TAG, __VA_ARGS__))
#define LOGE(...) ((void)OH_LOG_Print(LOG_APP, LOG_ERROR, 0, TAG, __VA_ARGS__))

namespace {

const char* SafeStr(const GLubyte* s) {
    return s ? reinterpret_cast<const char*>(s) : "(NULL)";
}

GLuint CompileShader(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = {0};
        glGetShaderInfoLog(sh, sizeof(log) - 1, nullptr, log);
        LOGE("shader compile failed: %{public}s", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

struct BackendGLES : public Backend {
    EGLDisplay dpy = EGL_NO_DISPLAY;
    EGLSurface surf = EGL_NO_SURFACE;
    EGLContext ctx = EGL_NO_CONTEXT;
    int w = 0;
    int h = 0;
    GLuint prog = 0;
    GLint uT = -1;
    std::string info;

    const char* name() const override { return "gles"; }

    bool init(OHNativeWindow* win, int width, int height) override {
        w = width;
        h = height;
        dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (dpy == EGL_NO_DISPLAY) {
            LOGE("eglGetDisplay failed");
            return false;
        }
        EGLint maj = 0;
        EGLint min = 0;
        if (!eglInitialize(dpy, &maj, &min)) {
            LOGE("eglInitialize failed 0x%{public}x", eglGetError());
            return false;
        }
        info += "egl_client=" + std::to_string(maj) + "." + std::to_string(min) + "\n";

        const EGLint cfg3[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
            EGL_NONE};
        EGLConfig cfg = nullptr;
        EGLint ncfg = 0;
        if (!eglChooseConfig(dpy, cfg3, &cfg, 1, &ncfg) || ncfg == 0) {
            const EGLint cfg2[] = {
                EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                EGL_NONE};
            if (!eglChooseConfig(dpy, cfg2, &cfg, 1, &ncfg) || ncfg == 0) {
                LOGE("eglChooseConfig failed");
                return false;
            }
        }

        surf = eglCreateWindowSurface(dpy, cfg, reinterpret_cast<EGLNativeWindowType>(win), nullptr);
        if (surf == EGL_NO_SURFACE) {
            LOGE("eglCreateWindowSurface failed 0x%{public}x", eglGetError());
            return false;
        }

        const EGLint ctx3[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx3);
        if (ctx == EGL_NO_CONTEXT) {
            const EGLint ctx2[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
            ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx2);
        }
        if (ctx == EGL_NO_CONTEXT) {
            LOGE("eglCreateContext failed");
            return false;
        }
        if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
            LOGE("eglMakeCurrent failed 0x%{public}x", eglGetError());
            return false;
        }
        eglQuerySurface(dpy, surf, EGL_WIDTH, &w);
        eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);
        eglSwapInterval(dpy, 1);
        info += "surface=" + std::to_string(w) + "x" + std::to_string(h) + "\n";

        BuildPipeline();
        RunG0Probe();
        return true;
    }

    void BuildPipeline() {
        static const char* vs =
            "#version 300 es\n"
            "layout(location=0) in vec2 aPos;\n"
            "uniform float uT;\n"
            "out vec3 vColor;\n"
            "void main(){\n"
            "  float a = 6.2831853 * uT * 0.25;\n"
            "  mat2 rot = mat2(cos(a), -sin(a), sin(a), cos(a));\n"
            "  vec2 p = rot * (aPos * 0.45);\n"
            "  vColor = vec3(0.1 + 0.9 * (0.5 + 0.5 * sin(uT)), 0.85, 0.2 + 0.8 * (0.5 + 0.5 * cos(uT)));\n"
            "  gl_Position = vec4(p, 0.0, 1.0);\n"
            "}\n";
        static const char* fs =
            "#version 300 es\n"
            "precision mediump float;\n"
            "in vec3 vColor;\n"
            "out vec4 oColor;\n"
            "void main(){ oColor = vec4(vColor, 1.0); }\n";
        GLuint v = CompileShader(GL_VERTEX_SHADER, vs);
        GLuint f = CompileShader(GL_FRAGMENT_SHADER, fs);
        if (v == 0 || f == 0) {
            return;
        }
        prog = glCreateProgram();
        glAttachShader(prog, v);
        glAttachShader(prog, f);
        glLinkProgram(prog);
        GLint ok = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok) {
            LOGE("program link failed");
            glDeleteProgram(prog);
            prog = 0;
        }
        glDeleteShader(v);
        glDeleteShader(f);
        if (prog != 0) {
            uT = glGetUniformLocation(prog, "uT");
        }
        GLuint vbo = 0;
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        const GLfloat tri[] = {0.0f, 0.6f, -0.6f, -0.5f, 0.6f, -0.5f};
        glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    }

    // G0 verdict: strings via the linked libGLESv3 with a live context, then
    // glGetString dlsym'd out of libGLESv2 (gtasa's fatal path) on the same
    // current context. If the second returns NULL while the first works, the
    // "libGLESv2 is a stub" theory is confirmed on this device.
    void RunG0Probe() {
        const GLubyte* vend = glGetString(GL_VENDOR);
        const GLubyte* rend = glGetString(GL_RENDERER);
        const GLubyte* ver = glGetString(GL_VERSION);
        LOGI("G0 linked-lib VENDOR  = %{public}s", SafeStr(vend));
        LOGI("G0 linked-lib RENDERER= %{public}s", SafeStr(rend));
        LOGI("G0 linked-lib VERSION = %{public}s", SafeStr(ver));
        GLint nExt = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &nExt);
        LOGI("G0 linked-lib EXTENSIONS count = %{public}d", nExt);
        info += std::string("GL_VENDOR=") + SafeStr(vend) + "\n";
        info += std::string("GL_RENDERER=") + SafeStr(rend) + "\n";
        info += std::string("GL_VERSION=") + SafeStr(ver) + "\n";

        void* h2 = dlopen("libGLESv2.so", RTLD_NOW | RTLD_LOCAL);
        if (h2 != nullptr) {
            using GetStringFn = const GLubyte* (*)(GLenum);
            GetStringFn gs = reinterpret_cast<GetStringFn>(dlsym(h2, "glGetString"));
            const GLubyte* v2 = gs != nullptr ? gs(GL_VENDOR) : nullptr;
            const GLubyte* r2 = gs != nullptr ? gs(GL_RENDERER) : nullptr;
            LOGI("G0 libGLESv2.so dlsym glGetString VENDOR=%{public}s RENDERER=%{public}s",
                 v2 != nullptr ? reinterpret_cast<const char*>(v2) : "(NULL)",
                 r2 != nullptr ? reinterpret_cast<const char*>(r2) : "(NULL)");
            info += std::string("libGLESv2_Vendor=") + (v2 != nullptr ? reinterpret_cast<const char*>(v2) : "(NULL)") + "\n";
            dlclose(h2);
        } else {
            LOGI("G0 libGLESv2.so dlopen FAILED");
            info += "libGLESv2_Vendor=dlopen-failed\n";
        }
    }

    std::string probe() override { return info; }

    void frame(float t) override {
        /* Guest-render takeover: keep the swap cadence at 60fps but draw a
         * plain dark frame instead of the G0 probe triangle. The moment the
         * translated guest reaches its own render loop it draws into this
         * same surface/context and takes over what is on screen. */
        (void)t;
        glViewport(0, 0, w, h);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        eglSwapBuffers(dpy, surf);
    }

    void shutdown() override {
        if (dpy != EGL_NO_DISPLAY) {
            eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (ctx != EGL_NO_CONTEXT) {
                eglDestroyContext(dpy, ctx);
            }
            if (surf != EGL_NO_SURFACE) {
                eglDestroySurface(dpy, surf);
            }
            eglTerminate(dpy);
        }
        dpy = EGL_NO_DISPLAY;
        surf = EGL_NO_SURFACE;
        ctx = EGL_NO_CONTEXT;
        prog = 0;
    }
};

} // namespace

Backend* CreateBackendGLES() {
    return new BackendGLES();
}
