// OHOS GLES 3.2 backend for the frozen guest-GL surface (73 commands).
//
// Contract: repentogxm gl_bridge.h / gl_surface_generated.h. Pointer-shaped
// parameters arrive as guest_gl_addr (uint32) and are translated here — at
// this edge only — via the flat guest arena (guest_memory.h).
//
// Surface notes that shape this implementation:
//  * No glBindBuffer in the surface: the guest renderer is client-array
//    based (as on Vita/vitaGL). Vertex attrib and index "pointers" are
//    guest addresses -> arena pointers. If Maleoon rejects client arrays,
//    the draw path is where a copy-fallback lands (TODO, measure first).
//  * glShaderSource rewrites desktop GLSL -> ES 3.00 per shader part.
//  * glAlphaFunc is fixed-function alpha test, absent in ES: state is kept
//    and must reach the fragment shaders via discard injection (TODO once
//    corpus proves the exact need; loud one-shot log until then).
//  * glGetString/glGetStringi return GUEST ADDRESSES of cached strings.
//  * UniformMatrix* ES accepts only transpose=FALSE: transpose host-side.
#include "gl_types.h"
#include "guest_memory.h"
#include "glsl_es_rewrite.h"

#include <GLES3/gl3.h>
#include <hilog/log.h>

#include <stdlib.h>
#include <string.h>

#define TAG "isaac.glbe"
#define LOGI(...) ((void)OH_LOG_Print(LOG_APP, LOG_INFO, 0, TAG, __VA_ARGS__))
#define LOGE(...) ((void)OH_LOG_Print(LOG_APP, LOG_ERROR, 0, TAG, __VA_ARGS__))

/* Desktop-GL legacy constant, absent from GLES headers. Only used to
 * recognize the guest enabling alpha test; ES has no such cap. */
#ifndef GL_ALPHA_TEST
#define GL_ALPHA_TEST 0x0BC0
#endif

/* ---------------- cached GL strings (guest addresses) ---------------- */

static uint32_t s_str_vendor, s_str_renderer, s_str_version, s_str_glsl;
static uint32_t s_str_extensions;
static uint32_t s_ext_addrs[128];
static int s_ext_count;
static int s_alphafunc_warned;

static void cache_strings(void) {
    if (s_str_vendor != 0) {
        return;
    }
    const GLubyte *v = glGetString(GL_VENDOR);
    const GLubyte *r = glGetString(GL_RENDERER);
    const GLubyte *ver = glGetString(GL_VERSION);
    const GLubyte *glsl = glGetString(GL_SHADING_LANGUAGE_VERSION);
    s_str_vendor = guest_mem_push_string((const char *)(v ? (const char *)v : ""), v ? strlen((const char *)v) : 0);
    s_str_renderer = guest_mem_push_string((const char *)(r ? (const char *)r : ""), r ? strlen((const char *)r) : 0);
    s_str_version = guest_mem_push_string((const char *)(ver ? (const char *)ver : ""), ver ? strlen((const char *)ver) : 0);
    s_str_glsl = guest_mem_push_string((const char *)(glsl ? (const char *)glsl : ""), glsl ? strlen((const char *)glsl) : 0);

    /* Extension list: keep the whole string and per-name addresses. */
    const GLubyte *e = glGetString(GL_EXTENSIONS);
    if (e != 0) {
        const char *es = (const char *)e;
        s_str_extensions = guest_mem_push_string(es, strlen(es));
        /* guest_mem arena grew downward; parse a copy in place afterwards */
        char *base = (char *)guest_mem_ptr(s_str_extensions);
        size_t blen = strlen(base);
        char *mut = (char *)malloc(blen + 1);
        if (mut != 0) {
            memcpy(mut, base, blen + 1);
            int n = 0;
            char *save = 0;
            for (char *tok = strtok_r(mut, " ", &save); tok != 0 && n < 128; tok = strtok_r(0, " ", &save)) {
                /* push each token copy */
                s_ext_addrs[n++] = guest_mem_push_string(tok, strlen(tok));
            }
            s_ext_count = n;
            free(mut);
        }
    }
    LOGI("strings cached: vendor=%{public}u renderer=%{public}u exts=%{public}d", s_str_vendor, s_str_renderer, s_ext_count);
}

/* ---------------- helpers ---------------- */

static int32_t read_i32(uint32_t addr) {
    if (addr == 0) {
        return 0;
    }
    int32_t v;
    memcpy(&v, guest_mem_ptr(addr), sizeof(v));
    return v;
}

static uint32_t read_u32(uint32_t addr) {
    if (addr == 0) {
        return 0;
    }
    uint32_t v;
    memcpy(&v, guest_mem_ptr(addr), sizeof(v));
    return v;
}

static float read_f32(uint32_t addr) {
    if (addr == 0) {
        return 0.0f;
    }
    float v;
    memcpy(&v, guest_mem_ptr(addr), sizeof(v));
    return v;
}

static void write_i32(uint32_t addr, int32_t v) {
    if (addr != 0) {
        memcpy(guest_mem_ptr(addr), &v, sizeof(v));
    }
}

/* Column-major transpose: src has C columns of R floats; dst has R columns
 * of C floats. dst[c*Cn + r] = src[r*Cn + c]. */
static void transpose_f(const float *src, float *dst, int cn, int rn) {
    for (int c = 0; c < cn; c++) {
        for (int r = 0; r < rn; r++) {
            dst[r * cn + c] = src[c * rn + r];
        }
    }
}

/* ---------------- the backend ---------------- */

static void be_glActiveTexture(guest_gl_enum texture) { glActiveTexture(texture); }

static void be_glAlphaFunc(guest_gl_enum func, guest_gl_float ref) {
    if (!s_alphafunc_warned) {
        s_alphafunc_warned = 1;
        LOGE("glAlphaFunc(func=%{public}u ref=%{public}f) kept as state; discard injection into fragment shaders is TODO", func, ref);
    }
}

static void be_glAttachShader(guest_gl_uint program, guest_gl_uint shader) { glAttachShader(program, shader); }
static void be_glBindFramebuffer(guest_gl_enum target, guest_gl_uint framebuffer) { glBindFramebuffer(target, framebuffer); }
static void be_glBindRenderbuffer(guest_gl_enum target, guest_gl_uint renderbuffer) { glBindRenderbuffer(target, renderbuffer); }
static void be_glBindTexture(guest_gl_enum target, guest_gl_uint texture) { glBindTexture(target, texture); }
static void be_glBlendFuncSeparate(guest_gl_enum a, guest_gl_enum b, guest_gl_enum c, guest_gl_enum d) { glBlendFuncSeparate(a, b, c, d); }

static guest_gl_enum be_glCheckFramebufferStatus(guest_gl_enum target) { return glCheckFramebufferStatus(target); }
static void be_glClampColorARB(guest_gl_enum target, guest_gl_enum clamp) { (void)target; (void)clamp; }
static void be_glClear(guest_gl_bitfield mask) { glClear(mask); }
static void be_glClearColor(guest_gl_float r, guest_gl_float g, guest_gl_float b2, guest_gl_float a) { glClearColor(r, g, b2, a); }
static void be_glClearDepth(guest_gl_double d) { glClearDepthf((guest_gl_float)d); }
static void be_glCompileShader(guest_gl_uint shader) { glCompileShader(shader); }
static guest_gl_uint be_glCreateProgram(void) { return glCreateProgram(); }
static guest_gl_uint be_glCreateShader(guest_gl_enum type) { return glCreateShader(type); }
static void be_glCullFace(guest_gl_enum mode) { glCullFace(mode); }

static void be_glDeleteFramebuffers(guest_gl_sizei n, guest_gl_addr a) {
    if (a == 0 || n <= 0) return;
    GLuint *t = (GLuint *)guest_mem_ptr(a);
    glDeleteFramebuffers((GLsizei)n, t);
}
static void be_glDeleteProgram(guest_gl_uint p) { glDeleteProgram(p); }
static void be_glDeleteRenderbuffers(guest_gl_sizei n, guest_gl_addr a) {
    if (a == 0 || n <= 0) return;
    GLuint *t = (GLuint *)guest_mem_ptr(a);
    glDeleteRenderbuffers((GLsizei)n, t);
}
static void be_glDeleteShader(guest_gl_uint s) { glDeleteShader(s); }
static void be_glDeleteTextures(guest_gl_sizei n, guest_gl_addr a) {
    if (a == 0 || n <= 0) return;
    GLuint *t = (GLuint *)guest_mem_ptr(a);
    glDeleteTextures((GLsizei)n, t);
}
static void be_glDepthFunc(guest_gl_enum f) { glDepthFunc(f); }
static void be_glDisableVertexAttribArray(guest_gl_uint i) { glDisableVertexAttribArray(i); }

static void be_glDrawElements(guest_gl_enum mode, guest_gl_sizei count, guest_gl_enum type, guest_gl_addr indices) {
    /* Client-array renderer: indices is a guest address. */
    glDrawElements(mode, count, type, guest_mem_ptr(indices));
}

static void be_glEnable(guest_gl_enum cap) {
    if (cap == GL_ALPHA_TEST) {
        be_glAlphaFunc(0, 0);
        return;
    }
    glEnable(cap);
}
static void be_glEnableVertexAttribArray(guest_gl_uint i) { glEnableVertexAttribArray(i); }

static void be_glFramebufferRenderbuffer(guest_gl_enum t, guest_gl_enum at, guest_gl_enum rt, guest_gl_uint rb) {
    glFramebufferRenderbuffer(t, at, rt, rb);
}
static void be_glFramebufferTexture2D(guest_gl_enum t, guest_gl_enum at, guest_gl_enum tt, guest_gl_uint tex, guest_gl_int lv) {
    glFramebufferTexture2D(t, at, tt, tex, lv);
}
static void be_glGenFramebuffers(guest_gl_sizei n, guest_gl_addr a) {
    if (a == 0 || n <= 0) return;
    GLuint *t = (GLuint *)guest_mem_ptr(a);
    glGenFramebuffers((GLsizei)n, t);
}
static void be_glGenRenderbuffers(guest_gl_sizei n, guest_gl_addr a) {
    /* The guest's renderbuffer allocator stores the result at manager+0xcc;
     * if that stays zero the fault is here or in the adapter that feeds this,
     * so log what actually arrives and what GL writes back. */
    LOGI("glGenRenderbuffers: n=%{public}d addr=0x%{public}08x", (int)n, a);
    if (a == 0 || n <= 0) return;
    GLuint *t = (GLuint *)guest_mem_ptr(a);
    glGenRenderbuffers((GLsizei)n, t);
    LOGI("glGenRenderbuffers: wrote name %{public}u at 0x%{public}08x (glGetError=0x%{public}x)",
         (unsigned)t[0], a, (unsigned)glGetError());
}
static void be_glGenTextures(guest_gl_sizei n, guest_gl_addr a) {
    if (a == 0 || n <= 0) return;
    GLuint *t = (GLuint *)guest_mem_ptr(a);
    glGenTextures((GLsizei)n, t);
}

static guest_gl_int be_glGetAttribLocation(guest_gl_uint program, guest_gl_addr name) {
    const char *n = name != 0 ? (const char *)guest_mem_ptr(name) : "";
    return glGetAttribLocation(program, n);
}

/* How many GLint a glGetIntegerv pname actually returns. The guest sizes its
 * buffer to the query, so copying a fixed 16-element scratch always overruns
 * it: the KAGE allocator's glGetIntegerv(GL_FRAMEBUFFER_BINDING, &local) had
 * 64 bytes written over its own stack frame, wiping the out-pointer slot it
 * reads back afterwards (the reason manager+0xcc stayed 0 on device). Anything
 * not listed is a single value; the vector queries GL defines are enumerated. */
static unsigned be_get_pname_count(guest_gl_enum pname) {
    switch (pname) {
        case 0x0ba2: /* GL_VIEWPORT */
        case 0x0c10: /* GL_SCISSOR_BOX */
        case 0x0b12: /* GL_MAX_VIEWPORT_DIMS */
        case 0x0b11: /* GL_VIEWPORT_DIMS */
        case 0x0d3a: /* GL_MAX_VIEWPORT_DIMS (ES) */
        case 0x0b10: /* GL_LINE_WIDTH_RANGE */
        case 0x0b22: /* GL_DEPTH_RANGE */
        case 0x0ba6: /* GL_COLOR_CLEAR_VALUE */
        case 0x0c22: /* GL_COLOR_WRITEMASK */
        case 0x8dfb: /* GL_MAX_COMPUTE_WORK_GROUP_SIZE */
        case 0x8dfc: /* GL_MAX_COMPUTE_WORK_GROUP_COUNT */
            return 4U;
        case 0x846d: /* GL_ALIASED_POINT_SIZE_RANGE */
        case 0x846e: /* GL_ALIASED_LINE_WIDTH_RANGE */
            return 2U;
        default:
            return 1U;
    }
}

static void be_glGetIntegerv(guest_gl_enum pname, guest_gl_addr data) {
    if (data == 0) return;
    GLint scratch[16] = {0};
    unsigned count = be_get_pname_count(pname);
    if (count > 16U) count = 16U;
    if (pname == GL_NUM_EXTENSIONS) {
        GLint v = s_ext_count;
        memcpy(guest_mem_ptr(data), &v, sizeof(v));
        return;
    }
    glGetIntegerv(pname, scratch);
    memcpy(guest_mem_ptr(data), scratch, (size_t)count * sizeof(GLint));
}

static void be_glGetProgramInfoLog(guest_gl_uint p, guest_gl_sizei bufSize, guest_gl_addr length, guest_gl_addr infoLog) {
    if (bufSize <= 0) return;
    GLsizei len = 0;
    char *buf = (char *)malloc((size_t)bufSize);
    if (buf == 0) return;
    glGetProgramInfoLog(p, bufSize, &len, buf);
    if (infoLog != 0 && len > 0) {
        memcpy(guest_mem_ptr(infoLog), buf, (size_t)len);
    }
    if (length != 0) {
        int32_t il = len;
        memcpy(guest_mem_ptr(length), &il, sizeof(il));
    }
    free(buf);
}
static void be_glGetProgramiv(guest_gl_uint p, guest_gl_enum pname, guest_gl_addr params) {
    GLint v = 0;
    glGetProgramiv(p, pname, &v);
    write_i32(params, v);
}
static void be_glGetRenderbufferParameteriv(guest_gl_enum t, guest_gl_enum pname, guest_gl_addr params) {
    GLint v = 0;
    glGetRenderbufferParameteriv(t, pname, &v);
    write_i32(params, v);
}
static void be_glGetShaderInfoLog(guest_gl_uint s, guest_gl_sizei bufSize, guest_gl_addr length, guest_gl_addr infoLog) {
    if (bufSize <= 0) return;
    GLsizei len = 0;
    char *buf = (char *)malloc((size_t)bufSize);
    if (buf == 0) return;
    glGetShaderInfoLog(s, bufSize, &len, buf);
    /* Echo compile failures straight to hilog: the guest's own log goes
     * through a truncating pipe and the exact driver message is the only way
     * to see what GLSL ES 3.00 still rejects. */
    if (len > 0) {
        char line[256];
        GLsizei n = len < (GLsizei)(sizeof line - 1U) ? len : (GLsizei)(sizeof line - 1U);
        memcpy(line, buf, (size_t)n);
        line[n] = '\0';
        LOGI("shader info log: %{public}s", line);
    }
    if (infoLog != 0 && len > 0) {
        memcpy(guest_mem_ptr(infoLog), buf, (size_t)len);
    }
    if (length != 0) {
        int32_t il = len;
        memcpy(guest_mem_ptr(length), &il, sizeof(il));
    }
    free(buf);
}
static void be_glGetShaderiv(guest_gl_uint s, guest_gl_enum pname, guest_gl_addr params) {
    GLint v = 0;
    glGetShaderiv(s, pname, &v);
    write_i32(params, v);
    /* The moment a compile is reported as failed, pull the driver's message
     * out and log it here: the guest forwards it only through a lossy pipe. */
    if (pname == 0x8B81 /* GL_COMPILE_STATUS */ && v == 0) {
        char buf[512];
        GLsizei len = 0;
        buf[0] = '\0';
        glGetShaderInfoLog(s, (GLsizei)sizeof buf - 1, &len, buf);
        buf[sizeof buf - 1] = '\0';
        LOGE("shader COMPILE FAILED: %{public}s", buf);
    }
}

static guest_gl_addr be_glGetString(guest_gl_enum name) {
    cache_strings();
    switch (name) {
        case GL_VENDOR: return s_str_vendor;
        case GL_RENDERER: return s_str_renderer;
        case GL_VERSION: return s_str_version;
        case GL_SHADING_LANGUAGE_VERSION: return s_str_glsl;
        case GL_EXTENSIONS: return s_str_extensions;
        default: return 0;
    }
}

static guest_gl_addr be_glGetStringi(guest_gl_enum name, guest_gl_uint index) {
    (void)name;
    cache_strings();
    if ((int)index < s_ext_count) {
        return s_ext_addrs[index];
    }
    return 0;
}

static guest_gl_int be_glGetUniformLocation(guest_gl_uint program, guest_gl_addr name) {
    const char *n = name != 0 ? (const char *)guest_mem_ptr(name) : "";
    return glGetUniformLocation(program, n);
}
static void be_glLinkProgram(guest_gl_uint p) { glLinkProgram(p); }

static void be_glReadPixels(guest_gl_int x, guest_gl_int y, guest_gl_sizei w, guest_gl_sizei h,
                            guest_gl_enum format, guest_gl_enum type, guest_gl_addr pixels) {
    glReadPixels(x, y, w, h, format, type, pixels != 0 ? guest_mem_ptr(pixels) : 0);
}
static void be_glRenderbufferStorage(guest_gl_enum t, guest_gl_enum ifmt, guest_gl_sizei w, guest_gl_sizei h) {
    glRenderbufferStorage(t, ifmt, w, h);
}

static void be_glShaderSource(guest_gl_uint shader, guest_gl_sizei count, guest_gl_addr string, guest_gl_addr length) {
    if (count <= 0 || string == 0) return;
    char **parts = (char **)calloc((size_t)count, sizeof(char *));
    const GLchar **lines = (const GLchar **)calloc((size_t)count, sizeof(GLchar *));
    GLint *lens = (GLint *)calloc((size_t)count, sizeof(GLint));
    if (parts == 0 || lines == 0 || lens == 0) {
        free(parts); free(lines); free(lens);
        return;
    }
    uint32_t *addrs = (uint32_t *)guest_mem_ptr(string);
    const int32_t *lens_guest = length != 0 ? (const int32_t *)guest_mem_ptr(length) : 0;
    for (GLsizei i = 0; i < count; i++) {
        const char *s = (const char *)guest_mem_ptr(addrs[i]);
        size_t sl = lens_guest != 0 && lens_guest[i] > 0 ? (size_t)lens_guest[i] : strlen(s);
        char *part = (char *)malloc(sl + 1);
        if (part == 0) goto done;
        memcpy(part, s, sl);
        part[sl] = 0;
        size_t rl = 0;
        char *rewritten = glsl_es_rewrite(part, &rl);
        if (rewritten != 0) {
            parts[i] = rewritten;
            lens[i] = (GLint)rl;
            free(part);
            /* Show the first lines of what actually reaches the driver: the
             * only way to see why GLSL ES 3.00 rejects a rewritten shader. */
            {
                char head[192];
                size_t n = rl < sizeof head - 1U ? rl : sizeof head - 1U;
                memcpy(head, rewritten, n);
                head[n] = '\0';
                for (size_t k = 0; k < n; k++) {
                    if (head[k] == '\n') {
                        head[k] = '|';
                    }
                }
                LOGI("shader src[%{public}d]: %{public}s", (int)i, head);
            }
        } else {
            LOGI("shader src[%{public}d]: rewrite returned NULL, sending raw", (int)i);
            parts[i] = part;
            lens[i] = (GLint)sl;
        }
        lines[i] = parts[i];
    }
    glShaderSource(shader, count, lines, lens);
done:
    for (GLsizei i = 0; i < count; i++) free(parts[i]);
    free(parts);
    free(lines);
    free(lens);
}

static void be_glTexImage2D(guest_gl_enum target, guest_gl_int level, guest_gl_int internalformat,
                            guest_gl_sizei width, guest_gl_sizei height, guest_gl_int border,
                            guest_gl_enum format, guest_gl_enum type, guest_gl_addr pixels) {
    glTexImage2D(target, level, internalformat, width, height, border, format, type,
                 pixels != 0 ? guest_mem_ptr(pixels) : 0);
}
static void be_glTexParameteri(guest_gl_enum target, guest_gl_enum pname, guest_gl_int param) { glTexParameteri(target, pname, param); }
static void be_glTexSubImage2D(guest_gl_enum target, guest_gl_int level, guest_gl_int xo, guest_gl_int yo,
                               guest_gl_sizei w, guest_gl_sizei h, guest_gl_enum format, guest_gl_enum type,
                               guest_gl_addr pixels) {
    glTexSubImage2D(target, level, xo, yo, w, h, format, type, pixels != 0 ? guest_mem_ptr(pixels) : 0);
}

static void be_glUniform1fv(guest_gl_int l, guest_gl_sizei c, guest_gl_addr v) { glUniform1fv(l, c, guest_mem_ptr(v)); }
static void be_glUniform1i(guest_gl_int l, guest_gl_int v0) { glUniform1i(l, v0); }
static void be_glUniform1iv(guest_gl_int l, guest_gl_sizei c, guest_gl_addr v) { glUniform1iv(l, c, guest_mem_ptr(v)); }
static void be_glUniform1uiv(guest_gl_int l, guest_gl_sizei c, guest_gl_addr v) { glUniform1uiv(l, c, (const GLuint *)guest_mem_ptr(v)); }
static void be_glUniform2fv(guest_gl_int l, guest_gl_sizei c, guest_gl_addr v) { glUniform2fv(l, c, guest_mem_ptr(v)); }
static void be_glUniform2iv(guest_gl_int l, guest_gl_sizei c, guest_gl_addr v) { glUniform2iv(l, c, guest_mem_ptr(v)); }
static void be_glUniform2uiv(guest_gl_int l, guest_gl_sizei c, guest_gl_addr v) { glUniform2uiv(l, c, (const GLuint *)guest_mem_ptr(v)); }
static void be_glUniform3fv(guest_gl_int l, guest_gl_sizei c, guest_gl_addr v) { glUniform3fv(l, c, guest_mem_ptr(v)); }
static void be_glUniform3iv(guest_gl_int l, guest_gl_sizei c, guest_gl_addr v) { glUniform3iv(l, c, guest_mem_ptr(v)); }
static void be_glUniform3uiv(guest_gl_int l, guest_gl_sizei c, guest_gl_addr v) { glUniform3uiv(l, c, (const GLuint *)guest_mem_ptr(v)); }
static void be_glUniform4fv(guest_gl_int l, guest_gl_sizei c, guest_gl_addr v) { glUniform4fv(l, c, guest_mem_ptr(v)); }
static void be_glUniform4iv(guest_gl_int l, guest_gl_sizei c, guest_gl_addr v) { glUniform4iv(l, c, guest_mem_ptr(v)); }
static void be_glUniform4uiv(guest_gl_int l, guest_gl_sizei c, guest_gl_addr v) { glUniform4uiv(l, c, (const GLuint *)guest_mem_ptr(v)); }

/* ES3 rejects transpose=TRUE: transpose host-side into stack storage. */
static void be_glUniformMatrix2fv(guest_gl_int l, guest_gl_sizei c, guest_gl_boolean t, guest_gl_addr v) {
    if (t) { float tmp[8]; const float *s = (const float *)guest_mem_ptr(v);
        for (guest_gl_sizei i = 0; i < c; i++) transpose_f(s + i * 4, tmp + i * 4, 2, 2);
        glUniformMatrix2fv(l, c, GL_FALSE, tmp); }
    else glUniformMatrix2fv(l, c, GL_FALSE, (const float *)guest_mem_ptr(v));
}
static void be_glUniformMatrix2x3fv(guest_gl_int l, guest_gl_sizei c, guest_gl_boolean t, guest_gl_addr v) {
    if (t) { float tmp[64]; const float *s = (const float *)guest_mem_ptr(v);
        for (guest_gl_sizei i = 0; i < c; i++) transpose_f(s + i * 6, tmp + i * 6, 2, 3);
        glUniformMatrix2x3fv(l, c, GL_FALSE, tmp); }
    else glUniformMatrix2x3fv(l, c, GL_FALSE, (const float *)guest_mem_ptr(v));
}
static void be_glUniformMatrix2x4fv(guest_gl_int l, guest_gl_sizei c, guest_gl_boolean t, guest_gl_addr v) {
    if (t) { float tmp[128]; const float *s = (const float *)guest_mem_ptr(v);
        for (guest_gl_sizei i = 0; i < c; i++) transpose_f(s + i * 8, tmp + i * 8, 2, 4);
        glUniformMatrix2x4fv(l, c, GL_FALSE, tmp); }
    else glUniformMatrix2x4fv(l, c, GL_FALSE, (const float *)guest_mem_ptr(v));
}
static void be_glUniformMatrix3fv(guest_gl_int l, guest_gl_sizei c, guest_gl_boolean t, guest_gl_addr v) {
    if (t) { float tmp[32]; const float *s = (const float *)guest_mem_ptr(v);
        for (guest_gl_sizei i = 0; i < c; i++) transpose_f(s + i * 9, tmp + i * 9, 3, 3);
        glUniformMatrix3fv(l, c, GL_FALSE, tmp); }
    else glUniformMatrix3fv(l, c, GL_FALSE, (const float *)guest_mem_ptr(v));
}
static void be_glUniformMatrix3x2fv(guest_gl_int l, guest_gl_sizei c, guest_gl_boolean t, guest_gl_addr v) {
    if (t) { float tmp[64]; const float *s = (const float *)guest_mem_ptr(v);
        for (guest_gl_sizei i = 0; i < c; i++) transpose_f(s + i * 6, tmp + i * 6, 3, 2);
        glUniformMatrix3x2fv(l, c, GL_FALSE, tmp); }
    else glUniformMatrix3x2fv(l, c, GL_FALSE, (const float *)guest_mem_ptr(v));
}
static void be_glUniformMatrix3x4fv(guest_gl_int l, guest_gl_sizei c, guest_gl_boolean t, guest_gl_addr v) {
    if (t) { float tmp[128]; const float *s = (const float *)guest_mem_ptr(v);
        for (guest_gl_sizei i = 0; i < c; i++) transpose_f(s + i * 12, tmp + i * 12, 3, 4);
        glUniformMatrix3x4fv(l, c, GL_FALSE, tmp); }
    else glUniformMatrix3x4fv(l, c, GL_FALSE, (const float *)guest_mem_ptr(v));
}
static void be_glUniformMatrix4fv(guest_gl_int l, guest_gl_sizei c, guest_gl_boolean t, guest_gl_addr v) {
    if (t) { float tmp[64]; const float *s = (const float *)guest_mem_ptr(v);
        for (guest_gl_sizei i = 0; i < c; i++) transpose_f(s + i * 16, tmp + i * 16, 4, 4);
        glUniformMatrix4fv(l, c, GL_FALSE, tmp); }
    else glUniformMatrix4fv(l, c, GL_FALSE, (const float *)guest_mem_ptr(v));
}
static void be_glUniformMatrix4x2fv(guest_gl_int l, guest_gl_sizei c, guest_gl_boolean t, guest_gl_addr v) {
    if (t) { float tmp[64]; const float *s = (const float *)guest_mem_ptr(v);
        for (guest_gl_sizei i = 0; i < c; i++) transpose_f(s + i * 8, tmp + i * 8, 4, 2);
        glUniformMatrix4x2fv(l, c, GL_FALSE, tmp); }
    else glUniformMatrix4x2fv(l, c, GL_FALSE, (const float *)guest_mem_ptr(v));
}
static void be_glUniformMatrix4x3fv(guest_gl_int l, guest_gl_sizei c, guest_gl_boolean t, guest_gl_addr v) {
    if (t) { float tmp[96]; const float *s = (const float *)guest_mem_ptr(v);
        for (guest_gl_sizei i = 0; i < c; i++) transpose_f(s + i * 12, tmp + i * 12, 4, 3);
        glUniformMatrix4x3fv(l, c, GL_FALSE, tmp); }
    else glUniformMatrix4x3fv(l, c, GL_FALSE, (const float *)guest_mem_ptr(v));
}

static void be_glUseProgram(guest_gl_uint program) { glUseProgram(program); }
static void be_glVertexAttribPointer(guest_gl_uint index, guest_gl_int size, guest_gl_enum type,
                                     guest_gl_boolean normalized, guest_gl_sizei stride, guest_gl_addr pointer) {
    /* Client-array renderer: pointer is a guest address (arena). */
    glVertexAttribPointer(index, size, type, normalized, stride, guest_mem_ptr(pointer));
}
static void be_glViewport(guest_gl_int x, guest_gl_int y, guest_gl_sizei w, guest_gl_sizei h) { glViewport(x, y, w, h); }

/* Fill the frozen struct. Any member left NULL faults loudly at dispatch
 * with its GL name, so partial bring-up stays debuggable. */
const guest_gl_backend *gl_ohos_backend(void) {
    static guest_gl_backend be;
    static int init;
    if (init) {
        return &be;
    }
    memset(&be, 0, sizeof(be));
    be.glActiveTexture = be_glActiveTexture;
    be.glAlphaFunc = be_glAlphaFunc;
    be.glAttachShader = be_glAttachShader;
    be.glBindFramebuffer = be_glBindFramebuffer;
    be.glBindRenderbuffer = be_glBindRenderbuffer;
    be.glBindTexture = be_glBindTexture;
    be.glBlendFuncSeparate = be_glBlendFuncSeparate;
    be.glCheckFramebufferStatus = be_glCheckFramebufferStatus;
    be.glClampColorARB = be_glClampColorARB;
    be.glClear = be_glClear;
    be.glClearColor = be_glClearColor;
    be.glClearDepth = be_glClearDepth;
    be.glCompileShader = be_glCompileShader;
    be.glCreateProgram = be_glCreateProgram;
    be.glCreateShader = be_glCreateShader;
    be.glCullFace = be_glCullFace;
    be.glDeleteFramebuffers = be_glDeleteFramebuffers;
    be.glDeleteProgram = be_glDeleteProgram;
    be.glDeleteRenderbuffers = be_glDeleteRenderbuffers;
    be.glDeleteShader = be_glDeleteShader;
    be.glDeleteTextures = be_glDeleteTextures;
    be.glDepthFunc = be_glDepthFunc;
    be.glDisableVertexAttribArray = be_glDisableVertexAttribArray;
    be.glDrawElements = be_glDrawElements;
    be.glEnable = be_glEnable;
    be.glEnableVertexAttribArray = be_glEnableVertexAttribArray;
    be.glFramebufferRenderbuffer = be_glFramebufferRenderbuffer;
    be.glFramebufferTexture2D = be_glFramebufferTexture2D;
    be.glGenFramebuffers = be_glGenFramebuffers;
    be.glGenRenderbuffers = be_glGenRenderbuffers;
    be.glGenTextures = be_glGenTextures;
    be.glGetAttribLocation = be_glGetAttribLocation;
    be.glGetIntegerv = be_glGetIntegerv;
    be.glGetProgramInfoLog = be_glGetProgramInfoLog;
    be.glGetProgramiv = be_glGetProgramiv;
    be.glGetRenderbufferParameteriv = be_glGetRenderbufferParameteriv;
    be.glGetShaderInfoLog = be_glGetShaderInfoLog;
    be.glGetShaderiv = be_glGetShaderiv;
    be.glGetString = be_glGetString;
    be.glGetStringi = be_glGetStringi;
    be.glGetUniformLocation = be_glGetUniformLocation;
    be.glLinkProgram = be_glLinkProgram;
    be.glReadPixels = be_glReadPixels;
    be.glRenderbufferStorage = be_glRenderbufferStorage;
    be.glShaderSource = be_glShaderSource;
    be.glTexImage2D = be_glTexImage2D;
    be.glTexParameteri = be_glTexParameteri;
    be.glTexSubImage2D = be_glTexSubImage2D;
    be.glUniform1fv = be_glUniform1fv;
    be.glUniform1i = be_glUniform1i;
    be.glUniform1iv = be_glUniform1iv;
    be.glUniform1uiv = be_glUniform1uiv;
    be.glUniform2fv = be_glUniform2fv;
    be.glUniform2iv = be_glUniform2iv;
    be.glUniform2uiv = be_glUniform2uiv;
    be.glUniform3fv = be_glUniform3fv;
    be.glUniform3iv = be_glUniform3iv;
    be.glUniform3uiv = be_glUniform3uiv;
    be.glUniform4fv = be_glUniform4fv;
    be.glUniform4iv = be_glUniform4iv;
    be.glUniform4uiv = be_glUniform4uiv;
    be.glUniformMatrix2fv = be_glUniformMatrix2fv;
    be.glUniformMatrix2x3fv = be_glUniformMatrix2x3fv;
    be.glUniformMatrix2x4fv = be_glUniformMatrix2x4fv;
    be.glUniformMatrix3fv = be_glUniformMatrix3fv;
    be.glUniformMatrix3x2fv = be_glUniformMatrix3x2fv;
    be.glUniformMatrix3x4fv = be_glUniformMatrix3x4fv;
    be.glUniformMatrix4fv = be_glUniformMatrix4fv;
    be.glUniformMatrix4x2fv = be_glUniformMatrix4x2fv;
    be.glUniformMatrix4x3fv = be_glUniformMatrix4x3fv;
    be.glUseProgram = be_glUseProgram;
    be.glVertexAttribPointer = be_glVertexAttribPointer;
    be.glViewport = be_glViewport;
    init = 1;
    return &be;
}
