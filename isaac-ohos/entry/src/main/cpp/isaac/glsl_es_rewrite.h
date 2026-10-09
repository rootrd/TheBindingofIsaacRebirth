// GLSL desktop 1.x -> GLSL ES 3.00 source rewriter for the glShaderSource
// seam. The game's shader sources ship in desktop GLSL; GLES 3.2 refuses
// them. This is a pragmatic line-level rewriter covering the constructs the
// 73-command surface implies (attribute/varying, fixed gl_FragColor/
// gl_FragData, texture2D, ftransform-free vertex paths).
#ifndef ISAAC_GLSL_ES_REWRITE_H
#define ISAAC_GLSL_ES_REWRITE_H

#include <stddef.h>

/* Returns a malloc'd rewritten source (caller frees), or 0 on allocation
 * failure. `len_out` receives the rewritten length when non-zero. */
char *glsl_es_rewrite(const char *src, size_t *len_out);

/* 1 if the source already declares an ES profile version. */
int glsl_es_is_es(const char *src);

#endif /* ISAAC_GLSL_ES_REWRITE_H */
