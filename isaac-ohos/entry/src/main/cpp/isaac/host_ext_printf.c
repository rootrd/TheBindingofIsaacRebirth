/* host_ext_printf.c — UCRT printf/scanf family imports for the recompiled guest.
 *
 * The guest was compiled with the Universal CRT, whose narrow stdio entry
 * points are the seven-dword "common" forms:
 *
 *   int  __cdecl __stdio_common_vsprintf   (uint64 options, char *buffer,
 *                                           size_t count, const char *format,
 *                                           _locale_t locale, va_list ap);
 *   int  __cdecl __stdio_common_vsprintf_s (same);
 *   int  __cdecl __stdio_common_vfprintf   (uint64 options, FILE *stream,
 *                                           const char *format,
 *                                           _locale_t locale, va_list ap);
 *   int  __cdecl __stdio_common_vsscanf    (uint64 options, const char *buffer,
 *                                           size_t count, const char *format,
 *                                           _locale_t locale, va_list ap);
 *   FILE *__cdecl __acrt_iob_func          (unsigned index);
 *
 * The whole point of this file is that `va_list` cannot cross the host
 * boundary: on x86-32 it is a bare pointer that walks the *guest* stack, so it
 * is meaningless to native vsnprintf/vsscanf.  Everything below therefore
 * re-implements the conversions while reading arguments as guest dwords.
 *
 * Argument layout on the guest stack (esp+4 is the first argument):
 *   index 0/1 : options (low/high dword)
 *   index 2   : buffer / stream
 *   index 3   : count / format
 *   index 4   : format
 *   index 5   : locale
 *   index 6   : va_list (guest address)      [absent for vfprintf]
 *
 * Options bits (ucrt corecrt.h):
 *   0x01 _CRT_INTERNAL_PRINTF_LEGACY_VSPRINTF_NULL_TERMINATION
 *   0x02 _CRT_INTERNAL_PRINTF_STANDARD_SNPRINTF_PROCESSING
 *   0x04 _CRT_INTERNAL_PRINTF_LEGACY_WIDE_SPECIFIERS
 *   0x08 _CRT_INTERNAL_PRINTF_LEGACY_MSVCRT_COMPATIBILITY
 *   0x10 _CRT_INTERNAL_PRINTF_LEGACY_THREE_DIGIT_EXPONENTS
 * Only 0x02 changes the observable contract here (return would-be count rather
 * than the legacy "-1 on truncation"), so the rest is accepted and ignored.
 *
 * The guest is single threaded across this layer (native audio/input threads
 * never enter guest code), so the two scratch buffers are static rather than
 * on the stack; format_guest() is not re-entrant and does not need to be.
 *
 * Unsupported conversions and unknown FILE tokens are a loud frontier: they
 * log, stop formatting and return, never crash.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "guest.h"
#include "host_ext.h"

#include <hilog/log.h>
#define PRINTF_ERR(fmt, ...) \
    OH_LOG_Print(LOG_APP, LOG_ERROR, 0, "isaacoh", fmt, ##__VA_ARGS__)
#define PRINTF_INFO(fmt, ...) \
    OH_LOG_Print(LOG_APP, LOG_INFO, 0, "isaacoh", fmt, ##__VA_ARGS__)

#define PRINTF_TAG "host_ext_printf"

/* ------------------------------------------------------------ guest va_list */

/* A guest va_list is a 4-byte pointer into the guest stack.  Walking it is a
 * pure address increment: MSVC's x86 _crt_va_arg is `(*(t *)((ap += sizeof t)
 * - sizeof t))`, i.e. it never pads, so `double`/`long long` are two
 * consecutive dwords at whatever 4-byte-aligned spot the caller chose. */
typedef struct {
    uint32_t cursor;   /* guest address of the next argument word */
} guest_va;

static uint32_t guest_va_word(guest_va *va)
{
    uint32_t value = ld32(va->cursor);
    va->cursor += 4U;
    return value;
}

static int32_t guest_va_i32(guest_va *va)
{
    return (int32_t)guest_va_word(va);
}

static uint64_t guest_va_dword64(guest_va *va)
{
    uint64_t value = ld64(va->cursor);
    va->cursor += 8U;
    return value;
}

static double guest_va_double(guest_va *va)
{
    uint64_t raw = guest_va_dword64(va);
    double value;
    memcpy(&value, &raw, sizeof value);
    return value;
}

/* --------------------------------------------------------------- output sink */

/* Destination is either a guest buffer (vsprintf) or a host buffer (vfprintf),
 * or neither, in which case the sink only counts.  `limit` is the number of
 * bytes that may be stored; `written` always counts what a full conversion
 * would have produced. */
typedef struct {
    uint8_t *host;     /* host destination, or NULL */
    uint32_t guest;    /* guest destination address, or 0 */
    uint32_t limit;    /* bytes that may be stored in the destination */
    uint32_t written;  /* characters produced so far */
} fmt_out;

static void out_byte(fmt_out *out, int ch)
{
    if (out->written < out->limit) {
        if (out->host != NULL) {
            out->host[out->written] = (uint8_t)ch;
        } else if (out->guest != 0U) {
            st8(out->guest + out->written, (uint8_t)ch);
        }
    }
    out->written++;
}

static void out_fill(fmt_out *out, int ch, uint32_t count)
{
    while (count--)
        out_byte(out, ch);
}

static void out_bytes(fmt_out *out, const char *text, uint32_t count)
{
    uint32_t i;
    for (i = 0; i < count; i++)
        out_byte(out, (uint8_t)text[i]);
}

/* Guest NUL-terminated string; `limit` is UINT32_MAX for "no precision". */
static void out_guest_string(fmt_out *out, uint32_t address, uint32_t limit)
{
    static const char null_string[] = "(null)";
    if (address == 0U) {
        uint32_t i;
        for (i = 0; null_string[i] != '\0' && i < limit; i++)
            out_byte(out, (uint8_t)null_string[i]);
        return;
    }
    while (limit != 0U) {
        uint8_t value = ld8(address++);
        if (value == 0U)
            return;
        out_byte(out, value);
        limit--;
    }
}

/* Guest UTF-16 string, narrowed byte by byte (non-Latin-1 becomes '?'). */
static void out_guest_wide_string(fmt_out *out, uint32_t address, uint32_t limit)
{
    static const char null_string[] = "(null)";
    if (address == 0U) {
        uint32_t i;
        for (i = 0; null_string[i] != '\0' && i < limit; i++)
            out_byte(out, (uint8_t)null_string[i]);
        return;
    }
    while (limit != 0U) {
        uint16_t unit = ld16(address);
        address += 2U;
        if (unit == 0U)
            return;
        out_byte(out, unit < 0x100U ? (int)unit : (int)'?');
        limit--;
    }
}

/* ------------------------------------------------------------- format spec */

#define FMT_LEFT  0x01U
#define FMT_PLUS  0x02U
#define FMT_SPACE 0x04U
#define FMT_HASH  0x08U
#define FMT_ZERO  0x10U

enum fmt_length {
    LEN_NONE = 0, LEN_HH, LEN_H, LEN_L, LEN_LL, LEN_Z, LEN_J, LEN_T
};

typedef struct {
    unsigned flags;
    int      width;       /* -1 when absent */
    int      precision;   /* -1 when absent */
    enum fmt_length length;
    char     conv;
} fmt_spec;

/* Width/precision as large as 0x7fffffff are legal in the source language but
 * not worth an unbounded buffer; clamp and say so once per occurrence. */
#define FMT_MAX_FIELD 1024

static int fmt_clamp_field(int value, const char *what, const char *conv)
{
    if (value > FMT_MAX_FIELD) {
        PRINTF_ERR("%{public}s: %{public}s %{public}d clamped to %{public}d "
                   "(conv %{public}c)", PRINTF_TAG, what, value,
                   FMT_MAX_FIELD, conv);
        return FMT_MAX_FIELD;
    }
    return value;
}

/* Native renderer for every conversion whose argument is already a machine
 * value: rebuild the specification with an explicit 64-bit length and let the
 * host libc do the padding/precision work, then copy the bytes through the
 * sink so truncation accounting stays uniform. */
static char s_render[FMT_MAX_FIELD + 128];

static char *fmt_emit_prefix(char *spec, const fmt_spec *sp, int honour_sign)
{
    *spec++ = '%';
    if (sp->flags & FMT_LEFT)  *spec++ = '-';
    if (honour_sign && (sp->flags & FMT_PLUS))  *spec++ = '+';
    if (honour_sign && (sp->flags & FMT_SPACE)) *spec++ = ' ';
    if (sp->flags & FMT_HASH)  *spec++ = '#';
    if (sp->flags & FMT_ZERO)  *spec++ = '0';
    if (sp->width > 0)
        spec += sprintf(spec, "%d", fmt_clamp_field(sp->width, "width", "-"));
    *spec = '\0';   /* no flags/width leaves spec unterminated, and the
                     * caller's strlen(p) would then walk fresh stack bytes */
    return spec;
}

static void fmt_emit_precision(char *spec, int precision, int max_precision)
{
    if (precision < 0)
        return;
    if (precision > max_precision)
        precision = max_precision;
    sprintf(spec, ".%d", precision);
}

static void render_integer(fmt_out *out, const fmt_spec *sp,
                           uint64_t uvalue, int64_t svalue, int is_signed)
{
    char spec[64];
    char *p = spec;
    int length;

    p = fmt_emit_prefix(p, sp, is_signed);
    fmt_emit_precision(p, sp->precision, 512);
    p += strlen(p);
    *p++ = 'l';
    *p++ = 'l';
    *p++ = sp->conv;
    *p = '\0';

    if (is_signed)
        length = snprintf(s_render, sizeof s_render, spec, (long long)svalue);
    else
        length = snprintf(s_render, sizeof s_render, spec,
                          (unsigned long long)uvalue);
    if (length < 0) {
        PRINTF_ERR("%{public}s: native render failed for %{public}s",
                   PRINTF_TAG, spec);
        return;
    }
    if ((size_t)length >= sizeof s_render)
        length = (int)(sizeof s_render - 1U);
    out_bytes(out, s_render, (uint32_t)length);
}

static void render_double(fmt_out *out, const fmt_spec *sp, double value)
{
    char spec[64];
    char *p = spec;
    int length;

    p = fmt_emit_prefix(p, sp, 1);
    fmt_emit_precision(p, sp->precision, 512);
    p += strlen(p);
    *p++ = sp->conv;
    *p = '\0';

    length = snprintf(s_render, sizeof s_render, spec, value);
    if (length < 0) {
        PRINTF_ERR("%{public}s: native render failed for %{public}s",
                   PRINTF_TAG, spec);
        return;
    }
    if ((size_t)length >= sizeof s_render)
        length = (int)(sizeof s_render - 1U);
    out_bytes(out, s_render, (uint32_t)length);
}

/* %p: MSVC prints the address as uppercase hex with no 0x prefix and no
 * pointer-size padding beyond the natural 8 digits of x86-32. */
static void render_pointer(fmt_out *out, const fmt_spec *sp, uint32_t value)
{
    fmt_spec pointer = *sp;
    pointer.conv = 'X';
    pointer.flags = (sp->flags & (FMT_LEFT | FMT_ZERO));
    if (pointer.precision < 0)
        pointer.precision = 8;
    render_integer(out, &pointer, (uint64_t)value, (int64_t)(int32_t)value, 0);
}

static void render_char(fmt_out *out, const fmt_spec *sp, int ch)
{
    int width = fmt_clamp_field(sp->width > 0 ? sp->width : 0, "width", "c");
    if (!(sp->flags & FMT_LEFT) && width > 1)
        out_fill(out, ' ', (uint32_t)(width - 1));
    out_byte(out, ch);
    if ((sp->flags & FMT_LEFT) && width > 1)
        out_fill(out, ' ', (uint32_t)(width - 1));
}

/* A string conversion needs its width applied around the payload, and the
 * payload length is only known once the guest memory has been walked; the
 * count-only pass therefore measures it first (limit 0 stores nothing). */
static void render_guest_string(fmt_out *out, const fmt_spec *sp,
                                uint32_t address, int wide)
{
    uint32_t limit = sp->precision >= 0 ? (uint32_t)sp->precision : UINT32_MAX;
    uint32_t width = sp->width > 0 ? (uint32_t)fmt_clamp_field(sp->width, "width", "s") : 0U;
    uint32_t measured;
    uint32_t pad;
    fmt_out probe;

    probe.host = NULL;
    probe.guest = 0U;
    probe.limit = 0U;
    probe.written = 0U;
    if (wide)
        out_guest_wide_string(&probe, address, limit);
    else
        out_guest_string(&probe, address, limit);
    measured = probe.written;

    pad = width > measured ? width - measured : 0U;
    if (!(sp->flags & FMT_LEFT))
        out_fill(out, ' ', pad);
    if (wide)
        out_guest_wide_string(out, address, limit);
    else
        out_guest_string(out, address, limit);
    if ((sp->flags & FMT_LEFT))
        out_fill(out, ' ', pad);
}

/* ------------------------------------------------------------ varargs store */

/* A scanf conversion destination is a guest pointer; the width of the store is
 * decided by the length modifier (x86-32: hh=1, h=2, default/l/z/t=4, ll=8). */
static uint32_t scan_store_width(enum fmt_length length)
{
    switch (length) {
    case LEN_HH: return 1U;
    case LEN_H:  return 2U;
    case LEN_LL: return 8U;
    default:     return 4U;
    }
}

static void store_integer(uint32_t destination, enum fmt_length length,
                          uint64_t value)
{
    switch (scan_store_width(length)) {
    case 1U: st8(destination, (uint8_t)value); break;
    case 2U: st16(destination, (uint16_t)value); break;
    case 8U: st64(destination, value); break;
    default: st32(destination, (uint32_t)value); break;
    }
}

/* ---------------------------------------------------------- the formatter */

typedef struct {
    unsigned flags;
    int      width;
    int      precision;
    enum fmt_length length;
    char     conv;
    const char *start;    /* points at the '%' in the guest format */
} fmt_parse;

/* Parse "%..." at `text` (which points just past the '%').  Returns a pointer
 * past the conversion character. */
static const char *parse_spec(const char *text, fmt_parse *parse,
                              guest_va *va)
{
    const char *p = text;

    parse->flags = 0U;
    for (;;) {
        if (*p == '-')      parse->flags |= FMT_LEFT;
        else if (*p == '+') parse->flags |= FMT_PLUS;
        else if (*p == ' ') parse->flags |= FMT_SPACE;
        else if (*p == '#') parse->flags |= FMT_HASH;
        else if (*p == '0') parse->flags |= FMT_ZERO;
        else break;
        p++;
    }

    parse->width = -1;
    if (*p == '*') {
        parse->width = guest_va_i32(va);
        p++;
        if (parse->width < 0) {
            parse->flags |= FMT_LEFT;
            if (parse->width == INT32_MIN)
                parse->width = FMT_MAX_FIELD;
            else
                parse->width = -parse->width;
        }
    } else {
        while (*p >= '0' && *p <= '9') {
            if (parse->width < 0)
                parse->width = 0;
            if (parse->width <= 1000000)
                parse->width = parse->width * 10 + (*p - '0');
            p++;
        }
    }

    parse->precision = -1;
    if (*p == '.') {
        p++;
        if (*p == '*') {
            parse->precision = guest_va_i32(va);
            p++;
            if (parse->precision < 0)
                parse->precision = -1;   /* negative means "omitted" */
        } else {
            parse->precision = 0;
            while (*p >= '0' && *p <= '9') {
                if (parse->precision <= 1000000)
                    parse->precision = parse->precision * 10 + (*p - '0');
                p++;
            }
        }
    }

    parse->length = LEN_NONE;
    if (*p == 'h') {
        p++;
        if (*p == 'h') { p++; parse->length = LEN_HH; }
        else parse->length = LEN_H;
    } else if (*p == 'l') {
        p++;
        if (*p == 'l') { p++; parse->length = LEN_LL; }
        else parse->length = LEN_L;
    } else if (*p == 'j') { p++; parse->length = LEN_J; }
    else if (*p == 'z')   { p++; parse->length = LEN_Z; }
    else if (*p == 't')   { p++; parse->length = LEN_T; }
    else if (*p == 'L')   { p++; parse->length = LEN_LL; parse->conv = 'L'; }
    else if (*p == 'I') {
        /* MSVC's I64/I32 size prefixes; a bare "I" means ptrdiff_t (32-bit). */
        p++;
        if (p[0] == '6' && p[1] == '4') { p += 2; parse->length = LEN_LL; }
        else if (p[0] == '3' && p[1] == '2') { p += 2; parse->length = LEN_NONE; }
    }

    parse->conv = *p;
    if (*p != '\0')
        p++;
    return p;
}

/* Integer argument, truncated to the declared length. */
static uint64_t fetch_integer(guest_va *va, enum fmt_length length, int is_signed)
{
    uint64_t raw;

    switch (length) {
    case LEN_HH: raw = (uint64_t)guest_va_word(va); break;
    case LEN_H:  raw = (uint64_t)guest_va_word(va); break;
    case LEN_LL:
    case LEN_J:  raw = guest_va_dword64(va); break;
    default:     raw = (uint64_t)guest_va_word(va); break;
    }

    switch (length) {
    case LEN_HH:
        return is_signed ? (uint64_t)(int64_t)(int8_t)raw : (uint64_t)(uint8_t)raw;
    case LEN_H:
        return is_signed ? (uint64_t)(int64_t)(int16_t)raw : (uint64_t)(uint16_t)raw;
    case LEN_LL:
    case LEN_J:
        return raw;
    default:
        return is_signed ? (uint64_t)(int64_t)(int32_t)raw : (uint64_t)(uint32_t)raw;
    }
}

/* Format one guest format string.  Returns 0 when the format had to be
 * abandoned (unsupported conversion), 1 when it completed. */
static int format_guest(fmt_out *out, uint32_t format_address, guest_va *va)
{
    const char *text = (const char *)(isaac_arena_base + format_address);

    if (format_address == 0U) {
        PRINTF_ERR("%{public}s: NULL format pointer", PRINTF_TAG);
        return 0;
    }

    while (*text != '\0') {
        fmt_parse parse;
        fmt_spec spec;

        if (*text != '%') {
            out_byte(out, (uint8_t)*text++);
            continue;
        }

        parse.start = text;
        text = parse_spec(text + 1, &parse, va);

        spec.flags = parse.flags;
        spec.width = parse.width;
        spec.precision = parse.precision;
        spec.length = parse.length;
        spec.conv = parse.conv;

        switch (parse.conv) {
        case '%':
            out_byte(out, '%');
            break;

        case 'd':
        case 'i': {
            uint64_t raw = fetch_integer(va, parse.length, 1);
            render_integer(out, &spec, raw, (int64_t)raw, 1);
            break;
        }
        case 'u':
        case 'o':
        case 'x':
        case 'X': {
            uint64_t raw = fetch_integer(va, parse.length, 0);
            render_integer(out, &spec, raw, (int64_t)raw, 0);
            break;
        }

        case 'f': case 'F':
        case 'e': case 'E':
        case 'g': case 'G':
        case 'a': case 'A':
            render_double(out, &spec, guest_va_double(va));
            break;

        case 'c': {
            int32_t raw = guest_va_i32(va);
            if (parse.length == LEN_L) {
                /* %lc: a UTF-16 unit narrowed the same way as %S. */
                uint16_t unit = (uint16_t)raw;
                render_char(out, &spec, unit < 0x100U ? (int)unit : (int)'?');
            } else {
                render_char(out, &spec, (int)(raw & 0xFF));
            }
            break;
        }
        case 'C': {
            uint16_t unit = (uint16_t)guest_va_i32(va);
            render_char(out, &spec, unit < 0x100U ? (int)unit : (int)'?');
            break;
        }

        case 's':
            render_guest_string(out, &spec, guest_va_word(va),
                                parse.length == LEN_L);
            break;
        case 'S':
            render_guest_string(out, &spec, guest_va_word(va), 1);
            break;

        case 'p':
            render_pointer(out, &spec, guest_va_word(va));
            break;

        case 'n': {
            uint32_t destination = guest_va_word(va);
            if (destination != 0U)
                store_integer(destination, parse.length, out->written);
            break;
        }

        case '\0':
            PRINTF_ERR("%{public}s: truncated conversion at \"%{public}s\"",
                       PRINTF_TAG, parse.start);
            return 0;

        default:
            PRINTF_ERR("%{public}s: UNSUPPORTED conversion \"%%%{public}c\" in "
                       "\"%{public}s\" (stopping at offset %{public}u)",
                       PRINTF_TAG, parse.conv, parse.start, out->written);
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------- vsprintf handlers */

/* `secure` selects the _s contract: a real destination is required and a
 * truncating conversion clears it instead of exposing a partial string. */
static void sprintf_common(CPU *c, int secure)
{
    uint32_t options = ld32(c->esp + 4U);
    uint32_t buffer = ld32(c->esp + 12U);
    uint32_t count = ld32(c->esp + 16U);
    uint32_t format = ld32(c->esp + 20U);
    uint32_t arguments = ld32(c->esp + 28U);
    int standard = (options & 0x02U) != 0U;
    fmt_out out;
    guest_va va;

    if (format == 0U || (secure && (buffer == 0U || count == 0U))) {
        if (secure && buffer != 0U && count != 0U)
            st8(buffer, 0U);
        c->eax = UINT32_MAX;
        c->esp += 4U;
        return;
    }

    out.host = NULL;
    out.guest = buffer;   /* 0 means "measure only" */
    out.limit = count > 0U ? count - 1U : 0U;
    out.written = 0U;
    va.cursor = arguments;

    format_guest(&out, format, &va);

    if (secure) {
        if (out.written >= count) {
            if (buffer != 0U && count != 0U)
                st8(buffer, 0U);
            c->eax = UINT32_MAX;
        } else {
            if (buffer != 0U)
                st8(buffer + out.written, 0U);
            c->eax = out.written;
        }
    } else {
        if (buffer != 0U && count != 0U) {
            uint32_t terminator = out.written < out.limit ? out.written
                                                          : out.limit;
            st8(buffer + terminator, 0U);
        }
        c->eax = standard ? out.written
                          : (out.written <= out.limit ? out.written
                                                      : UINT32_MAX);
    }
    c->esp += 4U;
}

static void h_stdio_common_vsprintf(CPU *c)
{
    sprintf_common(c, 0);
}

static void h_stdio_common_vsprintf_s(CPU *c)
{
    sprintf_common(c, 1);
}

/* ------------------------------------------------------- vfprintf handler */

#define IOB_BASE 0x00010000U

static void h_stdio_common_vfprintf(CPU *c)
{
    uint32_t token = ld32(c->esp + 12U);
    uint32_t format = ld32(c->esp + 16U);
    uint32_t arguments = ld32(c->esp + 24U);
    static char text[4096];
    fmt_out probe;
    fmt_out out;
    guest_va va;
    uint32_t length;
    uint32_t stored;
    unsigned index;
    /* A token outside the __acrt_iob_func range may still name a live fopen()
     * stream from host_ext_file.c. Writing through it (instead of dropping the
     * text) is what lets guest files like savedatapath.txt receive their
     * content, and it keeps the stream position consistent for the guest's
     * later ftell/fseek/fread. */
    FILE *stream = NULL;

    (void)ld32(c->esp + 4U);   /* options low */
    (void)ld32(c->esp + 8U);   /* options high */
    (void)ld32(c->esp + 20U);  /* locale */

    if (format == 0U) {
        PRINTF_ERR("%{public}s: vfprintf with NULL format", PRINTF_TAG);
        c->eax = UINT32_MAX;
        c->esp += 4U;
        return;
    }

    if (token >= IOB_BASE && token <= IOB_BASE + 2U) {
        index = (unsigned)(token - IOB_BASE);
    } else {
        stream = (FILE *)host_ext_file_stream(token);
        if (stream == NULL) {
            PRINTF_ERR("%{public}s: vfprintf with unknown FILE token "
                       "0x%{public}08x (not stdout/stderr)", PRINTF_TAG, token);
            c->eax = UINT32_MAX;
            c->esp += 4U;
            return;
        }
        index = 3U;   /* neither stdout nor stderr */
    }

    /* Pass 1 measures, pass 2 renders into the log buffer.  The guest va_list
     * is a plain cursor, so replaying the format from the same starting point
     * is exact. */
    probe.host = NULL;
    probe.guest = 0U;
    probe.limit = 0U;
    probe.written = 0U;
    va.cursor = arguments;
    format_guest(&probe, format, &va);
    length = probe.written;

    stored = length < sizeof text - 1U ? length : (uint32_t)(sizeof text - 1U);
    out.host = (uint8_t *)text;
    out.guest = 0U;
    out.limit = stored;
    out.written = 0U;
    va.cursor = arguments;
    format_guest(&out, format, &va);
    text[stored] = '\0';

    if (stream != NULL)
        fwrite(text, 1U, stored, stream);
    else if (index == 2U)
        PRINTF_ERR("%{public}s", text);
    else
        PRINTF_INFO("%{public}s", text);

    c->eax = length;
    c->esp += 4U;
}

/* --------------------------------------------------- __acrt_iob_func token */

/* Only the three standard streams exist on this runtime; the tokens are opaque
 * and are only ever handed back to vfprintf above. */
static void h_acrt_iob_func(CPU *c)
{
    uint32_t index = ld32(c->esp + 4U);

    if (index > 2U) {
        PRINTF_ERR("%{public}s: __acrt_iob_func(%{public}u) outside 0..2 -> 0",
                   PRINTF_TAG, index);
        c->eax = 0U;
        c->esp += 4U;
        return;
    }
    c->eax = IOB_BASE + index;
    c->esp += 4U;
}

/* ---------------------------------------------------------------- scanner */

typedef struct {
    uint32_t cursor;
    uint32_t end;      /* exclusive input bound, 0 = NUL terminated */
} scan_in;

static int scan_peek(scan_in *in)
{
    if (in->end != 0U && in->cursor >= in->end)
        return -1;
    return (int)ld8(in->cursor);
}

static int scan_take(scan_in *in)
{
    int ch = scan_peek(in);
    if (ch <= 0)
        return -1;
    in->cursor++;
    return ch;
}

static int scan_space(int ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' ||
           ch == '\v' || ch == '\f';
}

static void scan_skip_space(scan_in *in)
{
    while (scan_peek(in) > 0 && scan_space(scan_peek(in)))
        in->cursor++;
}

static int digit_value(int ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

/* Collect the longest run of characters the accepting class allows, then hand
 * it to the host strtoull/strtod and give back what it did not consume. */
static uint32_t scan_collect(scan_in *in, char *buffer, size_t capacity,
                             unsigned max_chars, int base)
{
    uint32_t used = 0U;
    int sign_allowed = 1;

    while (used + 1U < capacity && (max_chars == 0U || used < max_chars)) {
        int ch = scan_peek(in);
        int accept = 0;
        if (ch < 0)
            break;
        if ((ch == '+' || ch == '-') && sign_allowed && used == 0U)
            accept = 1;
        else if (digit_value(ch) >= 0)
            accept = (base == 16) || (base == 10 && digit_value(ch) < 10) ||
                     (base == 8 && digit_value(ch) < 8) || base == 0;
        else if ((ch == 'x' || ch == 'X') && base == 16 && used == 1U &&
                 buffer[0] == '0')
            accept = 1;
        else if ((ch == 'x' || ch == 'X') && base == 0 && used == 1U &&
                 buffer[0] == '0')
            accept = 0;   /* base 0 lets strtoull see the prefix itself */
        if (!accept)
            break;
        buffer[used++] = (char)ch;
        in->cursor++;
        sign_allowed = 0;
    }
    buffer[used] = '\0';
    return used;
}

/* Same idea for floating point: gather everything strtod could want. */
static uint32_t scan_collect_float(scan_in *in, char *buffer, size_t capacity,
                                   unsigned max_chars)
{
    static const char allowed[] = "0123456789+-.eEpPaAbBcCdDfFnNiI";
    uint32_t used = 0U;

    while (used + 1U < capacity && (max_chars == 0U || used < max_chars)) {
        int ch = scan_peek(in);
        if (ch <= 0 || strchr(allowed, ch) == NULL)
            break;
        buffer[used++] = (char)ch;
        in->cursor++;
    }
    buffer[used] = '\0';
    return used;
}

static void scan_push_back(scan_in *in, uint32_t collected, uint32_t consumed)
{
    if (collected > consumed)
        in->cursor -= collected - consumed;
}

static int scan_integer(scan_in *in, int base, unsigned max_chars,
                        int signed_result, uint64_t *value)
{
    char buffer[80];
    char *end = NULL;
    uint32_t collected;
    uint32_t consumed;

    scan_skip_space(in);
    collected = scan_collect(in, buffer, sizeof buffer, max_chars, base);
    if (collected == 0U)
        return 0;
    if (signed_result)
        *value = (uint64_t)strtoll(buffer, &end, base);
    else
        *value = (uint64_t)strtoull(buffer, &end, base);
    consumed = end != NULL ? (uint32_t)(end - buffer) : 0U;
    if (consumed == 0U) {
        scan_push_back(in, collected, 0U);
        return 0;
    }
    scan_push_back(in, collected, consumed);
    return 1;
}

static int scan_float_value(scan_in *in, unsigned max_chars, double *value)
{
    char buffer[80];
    char *end = NULL;
    uint32_t collected;
    uint32_t consumed;

    scan_skip_space(in);
    collected = scan_collect_float(in, buffer, sizeof buffer, max_chars);
    if (collected == 0U)
        return 0;
    *value = strtod(buffer, &end);
    consumed = end != NULL ? (uint32_t)(end - buffer) : 0U;
    if (consumed == 0U) {
        scan_push_back(in, collected, 0U);
        return 0;
    }
    scan_push_back(in, collected, consumed);
    return 1;
}

/* %[set]: the payload is a run of input characters, NUL terminated, with no
 * whitespace skipping. */
static int scan_set(scan_in *in, const char **format, unsigned max_chars,
                    uint32_t destination)
{
    unsigned char members[256];
    const char *p = *format;
    int negate = 0;
    uint32_t written = 0U;
    int ch;
    unsigned i;

    memset(members, 0, sizeof members);
    if (*p == '^') {
        negate = 1;
        p++;
    }
    if (*p == ']') {
        members[(unsigned char)']'] = 1U;
        p++;
    }
    while (*p != '\0' && *p != ']') {
        unsigned char first = (unsigned char)*p++;
        if (*p == '-' && p[1] != '\0' && p[1] != ']') {
            unsigned char last = (unsigned char)p[1];
            unsigned lower = first <= last ? first : last;
            unsigned upper = first <= last ? last : first;
            for (i = lower; i <= upper; i++)
                members[i] = 1U;
            p += 2;
        } else {
            members[first] = 1U;
        }
    }
    if (*p == ']')
        p++;
    *format = p;

    scan_skip_space(in);
    for (;;) {
        if (max_chars != 0U && written >= max_chars)
            break;
        ch = scan_peek(in);
        if (ch <= 0)
            break;
        if ((members[(unsigned char)ch] != 0U) == (negate != 0))
            break;
        in->cursor++;
        if (destination != 0U)
            st8(destination + written, (uint8_t)ch);
        written++;
    }
    if (written == 0U)
        return 0;
    if (destination != 0U)
        st8(destination + written, 0U);
    return 1;
}

static void scan_store_destination(uint32_t destination, uint64_t value,
                                   enum fmt_length length, int is_float,
                                   double fvalue)
{
    if (destination == 0U)
        return;
    if (is_float) {
        if (length == LEN_L)
            stf(destination, (float)fvalue);
        else
            std_(destination, fvalue);
        return;
    }
    store_integer(destination, length, value);
}

/* Shared engine for __stdio_common_vsscanf and the plain sscanf wrapper.
 * Returns the assignment count, or UINT32_MAX when input failure happened
 * before the first conversion (which is what the CRT reports as EOF). */
static uint32_t scan_guest(uint32_t buffer, uint32_t count, uint32_t format,
                           uint32_t arguments)
{
    scan_in in;
    guest_va va;
    const char *text;
    uint32_t assignments = 0U;
    uint32_t input_start;
    int saw_input = 0;

    if (arguments == 0U || format == 0U || buffer == 0U) {
        PRINTF_ERR("%{public}s: scanf with NULL buffer, format or args",
                   PRINTF_TAG);
        return UINT32_MAX;
    }

    in.cursor = buffer;
    in.end = count == UINT32_MAX ? 0U : buffer + count;
    input_start = in.cursor;
    va.cursor = arguments;
    text = (const char *)(isaac_arena_base + format);

    while (*text != '\0') {
        fmt_parse parse;
        unsigned max_chars;
        uint32_t destination;
        uint64_t integer = 0U;
        double floating = 0.0;

        if (scan_space((unsigned char)*text)) {
            while (scan_space((unsigned char)*text))
                text++;
            scan_skip_space(&in);
            continue;
        }
        if (*text != '%') {
            int ch = scan_take(&in);
            if (ch >= 0) {
                saw_input = 1;
                if (ch != (unsigned char)*text) {
                    text++;
                    break;   /* matching failure */
                }
            }
            text++;
            continue;
        }

        parse.start = text;
        text = parse_spec(text + 1, &parse, &va);
        max_chars = parse.width > 0 ? (unsigned)parse.width : 0U;
        if (max_chars > 4096U)
            max_chars = 4096U;

        switch (parse.conv) {
        case '%': {
            int ch = scan_take(&in);
            if (ch < 0)
                goto done;
            saw_input = 1;
            if (ch != '%')
                goto finish;
            break;
        }
        case 'n':
            destination = guest_va_word(&va);
            if (destination != 0U)
                store_integer(destination, parse.length,
                              in.cursor - input_start);
            break;

        case 'd':
        case 'i':
        case 'u':
        case 'o':
        case 'x':
        case 'X':
        case 'p': {
            int base = parse.conv == 'o' ? 8
                     : (parse.conv == 'x' || parse.conv == 'X' ||
                        parse.conv == 'p') ? 16
                     : (parse.conv == 'i') ? 0 : 10;
            int is_signed = parse.conv == 'd' || parse.conv == 'i';
            destination = guest_va_word(&va);
            if (scan_integer(&in, base, max_chars, is_signed, &integer)) {
                saw_input = 1;
                scan_store_destination(destination, integer, parse.length,
                                       0, 0.0);
                assignments++;
            } else if (!saw_input) {
                goto done;
            } else {
                goto finish;
            }
            break;
        }

        case 'f': case 'F':
        case 'e': case 'E':
        case 'g': case 'G':
        case 'a': case 'A': {
            destination = guest_va_word(&va);
            if (scan_float_value(&in, max_chars, &floating)) {
                saw_input = 1;
                scan_store_destination(destination, 0U, parse.length, 1,
                                       floating);
                assignments++;
            } else if (!saw_input) {
                goto done;
            } else {
                goto finish;
            }
            break;
        }

        case 's': {
            uint32_t written = 0U;
            destination = guest_va_word(&va);
            scan_skip_space(&in);
            for (;;) {
                int ch = scan_peek(&in);
                if (ch <= 0 || scan_space(ch))
                    break;
                if (max_chars != 0U && written >= max_chars)
                    break;
                in.cursor++;
                if (destination != 0U)
                    st8(destination + written, (uint8_t)ch);
                written++;
            }
            if (written == 0U) {
                if (!saw_input)
                    goto done;
                goto finish;
            }
            saw_input = 1;
            if (destination != 0U)
                st8(destination + written, 0U);
            assignments++;
            break;
        }

        case 'c': {
            uint32_t wanted = max_chars != 0U ? max_chars : 1U;
            uint32_t written = 0U;
            destination = guest_va_word(&va);
            while (written < wanted) {
                int ch = scan_take(&in);
                if (ch < 0)
                    break;
                if (destination != 0U)
                    st8(destination + written, (uint8_t)ch);
                written++;
            }
            if (written < wanted) {
                if (!saw_input)
                    goto done;
                goto finish;
            }
            saw_input = 1;
            assignments++;
            break;
        }

        case '[': {
            destination = guest_va_word(&va);
            if (scan_set(&in, &text, max_chars, destination)) {
                saw_input = 1;
                assignments++;
            } else if (!saw_input) {
                goto done;
            } else {
                goto finish;
            }
            break;
        }

        default:
            PRINTF_ERR("%{public}s: UNSUPPORTED scan conversion "
                       "\"%%%{public}c\" in \"%{public}s\"",
                       PRINTF_TAG, parse.conv, parse.start);
            goto finish;
        }
    }

done:
finish:
    return (assignments == 0U && !saw_input) ? UINT32_MAX : assignments;
}

static void h_stdio_common_vsscanf(CPU *c)
{
    uint32_t options = ld32(c->esp + 4U);
    uint32_t buffer = ld32(c->esp + 12U);
    uint32_t count = ld32(c->esp + 16U);
    uint32_t format = ld32(c->esp + 20U);
    uint32_t arguments = ld32(c->esp + 28U);

    (void)ld32(c->esp + 8U);   /* options high */
    (void)ld32(c->esp + 24U);  /* locale */
    /* 0x04 was the first accepted flag. 0x02 is the UCRT secure-family bit
     * (scanf_s); for numeric formats the va_list layout is identical to plain
     * sscanf (buffer sizes only trail %s/%c/[ arguments), and the guest uses
     * this call to parse version strings -- e.g. glGetString's "OpenGL ES
     * %d.%d". Rejecting it made the caller treat its init as failed and call
     * exit(), which skipped the file-system bring-up and left all 22 archives
     * unmountable. */
    if ((options & ~0x06U) != 0U) {
        PRINTF_ERR("%{public}s: vsscanf unsupported option bits "
                   "0x%{public}08x", PRINTF_TAG, options);
        c->eax = UINT32_MAX;
        c->esp += 4U;
        return;
    }
    c->eax = scan_guest(buffer, count, format, arguments);
    c->esp += 4U;
}

/* ---------------------------------------------------- plain CRT wrappers */

/* These are not statically imported by the frozen image, but a
 * GetProcAddress("sprintf") style lookup is cheap to serve correctly: the
 * arguments are still guest dwords, only the entry layout differs. */
static void h_plain_sprintf(CPU *c)
{
    uint32_t buffer = ld32(c->esp + 4U);
    uint32_t format = ld32(c->esp + 8U);
    guest_va va;
    fmt_out out;

    va.cursor = c->esp + 12U;
    out.host = NULL;
    out.guest = buffer;
    out.limit = 0x7FFFFFFFU;
    out.written = 0U;
    format_guest(&out, format, &va);
    /* sprintf owns no size, so the terminator goes wherever the format
     * ended; the sink already stored every byte. */
    if (buffer != 0U)
        st8(buffer + out.written, 0U);
    c->eax = out.written;
    c->esp += 4U;
}

static void h_plain_vsprintf(CPU *c)
{
    uint32_t buffer = ld32(c->esp + 4U);
    uint32_t format = ld32(c->esp + 8U);
    uint32_t arguments = ld32(c->esp + 12U);
    guest_va va;
    fmt_out out;

    va.cursor = arguments;
    out.host = NULL;
    out.guest = buffer;
    out.limit = 0x7FFFFFFFU;
    out.written = 0U;
    format_guest(&out, format, &va);
    if (buffer != 0U)
        st8(buffer + out.written, 0U);
    c->eax = out.written;
    c->esp += 4U;
}

static void h_plain_snprintf(CPU *c)
{
    uint32_t buffer = ld32(c->esp + 4U);
    uint32_t count = ld32(c->esp + 8U);
    uint32_t format = ld32(c->esp + 12U);
    guest_va va;
    fmt_out out;

    va.cursor = c->esp + 16U;
    out.host = NULL;
    out.guest = buffer;
    out.limit = count > 0U ? count - 1U : 0U;
    out.written = 0U;
    format_guest(&out, format, &va);
    if (buffer != 0U && count != 0U)
        st8(buffer + (out.written < out.limit ? out.written : out.limit), 0U);
    c->eax = out.written;
    c->esp += 4U;
}

static void h_plain_printf(CPU *c)
{
    uint32_t format = ld32(c->esp + 4U);
    static char text[4096];
    guest_va va;
    fmt_out probe;
    fmt_out out;
    uint32_t length;
    uint32_t stored;

    probe.host = NULL;
    probe.guest = 0U;
    probe.limit = 0U;
    probe.written = 0U;
    va.cursor = c->esp + 8U;
    format_guest(&probe, format, &va);
    length = probe.written;

    stored = length < sizeof text - 1U ? length : (uint32_t)(sizeof text - 1U);
    out.host = (uint8_t *)text;
    out.guest = 0U;
    out.limit = stored;
    out.written = 0U;
    va.cursor = c->esp + 8U;
    format_guest(&out, format, &va);
    text[stored] = '\0';
    PRINTF_INFO("%{public}s", text);

    c->eax = length;
    c->esp += 4U;
}

/* sscanf(source, format, ...) — same engine, three stack arguments. */
static void h_plain_sscanf(CPU *c)
{
    uint32_t buffer = ld32(c->esp + 4U);
    uint32_t format = ld32(c->esp + 8U);

    c->eax = scan_guest(buffer, UINT32_MAX, format, c->esp + 12U);
    c->esp += 4U;
}

/* ------------------------------------------------------------ descriptor */

static const host_ext_entry s_entries[] = {
    { "api-ms-win-crt-stdio-l1-1-0.dll!__stdio_common_vsprintf",
      h_stdio_common_vsprintf },
    { "api-ms-win-crt-stdio-l1-1-0.dll!__stdio_common_vsprintf_s",
      h_stdio_common_vsprintf_s },
    { "api-ms-win-crt-stdio-l1-1-0.dll!__stdio_common_vfprintf",
      h_stdio_common_vfprintf },
    { "api-ms-win-crt-stdio-l1-1-0.dll!__stdio_common_vsscanf",
      h_stdio_common_vsscanf },
    { "api-ms-win-crt-stdio-l1-1-0.dll!__acrt_iob_func", h_acrt_iob_func },
};

/* Names a GetProcAddress-style lookup may ask for.  The frozen image imports
 * the common forms above; these are not in its IAT, but serving them costs
 * nothing and keeps a dynamically resolved CRT call from failing.  Their
 * argument layouts differ (a plain sprintf has no count/locale/option words),
 * hence the separate wrappers. */
static const host_ext_entry s_dynamic[] = {
    { "sprintf", h_plain_sprintf },
    { "vsprintf", h_plain_vsprintf },
    { "snprintf", h_plain_snprintf },
    { "printf", h_plain_printf },
    { "__stdio_common_vsprintf", h_stdio_common_vsprintf },
    { "__stdio_common_vsprintf_s", h_stdio_common_vsprintf_s },
    { "__stdio_common_vfprintf", h_stdio_common_vfprintf },
    { "__stdio_common_vsscanf", h_stdio_common_vsscanf },
    { "__acrt_iob_func", h_acrt_iob_func },
    { "sscanf", h_plain_sscanf },
};

static const host_ext_module s_module = {
    s_entries, (unsigned)(sizeof s_entries / sizeof s_entries[0]),
    s_dynamic, (unsigned)(sizeof s_dynamic / sizeof s_dynamic[0]),
};

const host_ext_module *host_ext_printf(void)
{
    return &s_module;
}
