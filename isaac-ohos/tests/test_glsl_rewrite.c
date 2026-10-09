/* Offline test for glsl_es_rewrite (pure C, host-compilable). */
#include "../entry/src/main/cpp/isaac/glsl_es_rewrite.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0;

static void expect(const char *name, const char *got, const char *must_contain) {
    if (got != 0 && strstr(got, must_contain) != 0) {
        printf("%s: PASS\n", name);
    } else {
        fails++;
        printf("%s: FAIL\n  expected to contain: %s\n  got: %s\n", name, must_contain, got ? got : "(null)");
    }
}

int main(void) {
    const char *vs = "#version 120\n"
                     "attribute vec2 aPos;\n"
                     "varying vec2 vUV;\n"
                     "void main(){ vUV = aPos; gl_Position = vec4(aPos, 0.0, 1.0); }\n";
    char *r = glsl_es_rewrite(vs, 0);
    expect("vertex.version", r, "#version 300 es");
    expect("vertex.attribute_in", r, "in vec2 aPos");
    expect("vertex.varying_out", r, "out vec2 vUV");
    free(r);

    const char *fs = "#version 110\n"
                     "varying vec2 vUV;\n"
                     "uniform sampler2D tex;\n"
                     "void main(){ gl_FragColor = texture2D(tex, vUV); }\n";
    r = glsl_es_rewrite(fs, 0);
    expect("frag.version", r, "#version 300 es");
    expect("frag.out_decl", r, "layout(location = 0) out vec4 isaac_fragColor;");
    expect("frag.fragcolor", r, "isaac_fragColor = texture(tex, vUV)");
    expect("frag.varying_in", r, "in vec2 vUV");
    free(r);

    const char *es = "#version 300 es\n"
                     "in vec2 p;\n"
                     "out vec4 o;\n"
                     "void main(){ o = vec4(1.0); }\n";
    r = glsl_es_rewrite(es, 0);
    expect("es.passthrough", r, "#version 300 es");
    expect("es.untouched", r, "out vec4 o;");
    free(r);

    if (fails == 0) {
        printf("RESULT: ALL PASS\n");
        return 0;
    }
    printf("RESULT: FAIL (%d)\n", fails);
    return 1;
}
