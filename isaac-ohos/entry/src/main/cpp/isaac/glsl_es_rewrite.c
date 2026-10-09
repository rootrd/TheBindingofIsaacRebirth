#include "glsl_es_rewrite.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int glsl_es_is_es(const char *src) {
    if (src == 0) {
        return 0;
    }
    return strncmp(src, "#version 300 es", 15) == 0 ||
           strncmp(src, "#version 310 es", 15) == 0 ||
           strncmp(src, "#version 320 es", 15) == 0;
}

static int is_word(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

/* Whole-word replace; returns malloc'd string or 0. */
static char *replace_word(const char *text, const char *word, const char *repl) {
    size_t wlen = strlen(word);
    size_t rlen = strlen(repl);
    size_t tlen = strlen(text);
    /* worst case: every char a boundary hit */
    size_t cap = tlen * (rlen > wlen ? (rlen + wlen) : wlen) + 16;
    char *out = (char *)malloc(cap);
    if (out == 0) {
        return 0;
    }
    size_t o = 0;
    const char *p = text;
    while (*p != 0) {
        const char *hit = strstr(p, word);
        if (hit == 0) {
            size_t rest = strlen(p);
            memcpy(out + o, p, rest);
            o += rest;
            break;
        }
        int boundary = (hit == text || !is_word((unsigned char)hit[-1])) &&
                       !is_word((unsigned char)hit[wlen]);
        if (!boundary) {
            memcpy(out + o, p, (size_t)(hit - p) + wlen);
            o += (size_t)(hit - p) + wlen;
            p = hit + wlen;
            continue;
        }
        memcpy(out + o, p, (size_t)(hit - p));
        o += (size_t)(hit - p);
        memcpy(out + o, repl, rlen);
        o += rlen;
        p = hit + wlen;
    }
    out[o] = 0;
    return out;
}

char *glsl_es_rewrite(const char *src, size_t *len_out) {
    if (src == 0) {
        return 0;
    }
    if (glsl_es_is_es(src)) {
        char *copy = strdup(src);
        if (copy != 0 && len_out != 0) {
            *len_out = strlen(copy);
        }
        return copy;
    }

    int is_vertex = strstr(src, "gl_Position") != 0;

    char *work = strdup(src);
    if (work == 0) {
        return 0;
    }

    /* version line */
    if (strncmp(work, "#version", 8) == 0) {
        const char *nl = strchr(work, '\n');
        char *rest = nl != 0 ? strdup(nl) : strdup("\n");
        if (rest == 0) {
            free(work);
            return 0;
        }
        char *nw = (char *)malloc(strlen("#version 300 es\n") + strlen(rest) + 1);
        if (nw == 0) {
            free(work);
            free(rest);
            return 0;
        }
        strcpy(nw, "#version 300 es\n");
        strcat(nw, rest);
        free(rest);
        free(work);
        work = nw;
    } else {
        /* No #version line: still prepend one. The rewrites below emit
         * in/out/layout, which are only legal under ES 3.00 -- without the
         * header the driver compiles as ES 100 and rejects them with
         * "Storage qualifier not allowed in GLSL ES version 100". */
        char *nw = (char *)malloc(strlen("#version 300 es\n") + strlen(work) + 1);
        if (nw == 0) {
            goto fail;
        }
        strcpy(nw, "#version 300 es\n");
        strcat(nw, work);
        free(work);
        work = nw;
    }

    /* ES 3.00 fragment shaders must declare a float precision; the desktop
     * sources (and the hand-written ones) never do. */
    if (!is_vertex && strstr(work, "precision ") == 0) {
        const char *nl = strchr(work, '\n');
        if (nl != 0) {
            size_t head = (size_t)(nl - work) + 1U;
            const char *decl = "precision mediump float;\n";
            size_t dlen = strlen(decl);
            size_t total = strlen(work);
            char *nw = (char *)malloc(head + dlen + (total - head) + 1U);
            if (nw != 0) {
                memcpy(nw, work, head);
                memcpy(nw + head, decl, dlen);
                memcpy(nw + head + dlen, work + head, total - head + 1U);
                free(work);
                work = nw;
            }
        }
    }

    /* vertex stage: attribute -> in, varying -> out */
    if (is_vertex) {
        char *s = replace_word(work, "attribute", "in");
        if (s == 0) goto fail;
        free(work);
        work = s;
        s = replace_word(work, "varying", "out");
        if (s == 0) goto fail;
        free(work);
        work = s;
    } else {
        /* fragment stage: varying -> in */
        char *s = replace_word(work, "varying", "in");
        if (s == 0) goto fail;
        free(work);
        work = s;
    }

    /* texture functions (both stages) */
    {
        char *s = replace_word(work, "texture2DProj", "textureProj");
        if (s == 0) goto fail;
        free(work);
        work = s;
        s = replace_word(work, "texture2D", "texture");
        if (s == 0) goto fail;
        free(work);
        work = s;
    }

    /* fragment output: gl_FragColor -> declared ES3 out.
     * gl_FragData[n] intentionally left as-is: a compile failure naming it
     * beats a silently wrong render target. Multiple draw buffers need a
     * per-index out table once the corpus proves it uses them. */
    if (strstr(work, "gl_FragColor") != 0) {
        /* Carry the precision on the declaration itself: ES 3.00 has no
         * default float precision in the fragment stage, and the declaration
         * may end up ahead of the inserted precision statement. */
        const char *decl = "layout(location = 0) out mediump vec4 isaac_fragColor;\n";
        char *s = replace_word(work, "gl_FragColor", "isaac_fragColor");
        if (s == 0) goto fail;
        size_t dlen = strlen(decl);
        char *nw = (char *)malloc(strlen(s) + dlen + 1);
        if (nw == 0) {
            free(s);
            goto fail;
        }
        /* insert the declaration right after the version line */
        const char *nl = strchr(s, '\n');
        size_t head = nl != 0 ? (size_t)(nl - s) + 1 : 0;
        memcpy(nw, s, head);
        memcpy(nw + head, decl, dlen);
        strcpy(nw + head + dlen, s + head);
        free(s);
        free(work);
        work = nw;
    }

    if (len_out != 0) {
        *len_out = strlen(work);
    }
    return work;

fail:
    free(work);
    return 0;
}
