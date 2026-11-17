











#define LIBREGPU_LIB_DECLARATIONS_ONLY       
#include "lib.c"

#ifndef LIBREGPU_UI_DECLARED
#define LIBREGPU_UI_DECLARED

#define LIBREGPU_UI_PATH_MAX 512

typedef struct {
    char        game_path[LIBREGPU_UI_PATH_MAX];
    bool        game_selected;
    LibreGPUConfig config;
    bool        demo;            
    bool        selftest;
    bool        help;
    bool        quiet;           
    bool        compat;          
    bool        doctor;          
    size_t      demo_frames;
} LibreGPULauncherSettings;



LibreGPUStatus ui_parse_args(LibreGPULauncherSettings *s, int argc, char **argv, char *err, size_t err_len);

void ui_print_banner(FILE *out);
void ui_print_usage(FILE *out, const char *prog);
void ui_show_status(FILE *out, const LibreGPURuntimeStatus *st);
void ui_show_cycle(FILE *out, uint64_t cycle, const LibreGPUCycleInfo *info);
void ui_show_error(FILE *out, const char *where, LibreGPUStatus st);









typedef struct {
    int            width, height;
    unsigned char *rgba;          
} LibreGPUImage;


#define LIBREGPU_IMAGE_SKIP_BLACK_PLATE 1u

LibreGPUStatus libregpu_image_load_svg(const char *path, int width, int height,
                                 unsigned flags, LibreGPUImage *out);
LibreGPUStatus libregpu_image_parse_svg(const char *text, size_t length,
                                  int width, int height, unsigned flags,
                                  LibreGPUImage *out);
void        libregpu_image_free(LibreGPUImage *image);
int         libregpu_image_trim(LibreGPUImage *image);       
void        libregpu_image_gamma(LibreGPUImage *image, double gamma);



int         libregpu_game_icon_find(const char *game_path, char *out, size_t capacity);





int         libregpu_asset_find(const char *name, char *out, size_t capacity);



void        libregpu_asset_set_base(const char *directory);

















typedef struct {
    void *userdata;

    

    int  (*is_runnable)(void *userdata, const char *path);

    void (*minimize)(void *userdata);   
    void (*restore)(void *userdata);    

    

    int  (*launch)(void *userdata, const char *path,
                   char *error_text, size_t error_capacity);
} LibreGPULaunchHost;





int         ui_launch_game(const LibreGPULaunchHost *host, const char *game_path,
                           char *message, size_t message_capacity);

#endif 

#ifndef LIBREGPU_UI_DECLARATIONS_ONLY
#ifndef LIBREGPU_UI_IMPLEMENTED
#define LIBREGPU_UI_IMPLEMENTED

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>






static void set_err(char *err, size_t n, const char *fmt, const char *arg)
{
    if (err && n) snprintf(err, n, fmt, arg ? arg : "");
}

static bool parse_double(const char *s, double *out)
{
    char *end = NULL;
    errno = 0;
    const double v = strtod(s, &end);
    if (errno || end == s || *end != '\0' || !isfinite(v)) return false;   
    *out = v;
    return true;
}

static bool parse_u64(const char *s, unsigned long long *out)
{
    char *end = NULL;
    errno = 0;
    if (*s == '-' || *s == '+' || *s == '\0') return false;
    const unsigned long long v = strtoull(s, &end, 10);
    if (errno || end == s || *end != '\0') return false;
    *out = v;
    return true;
}


static bool looks_like_option(const char *v) { return v[0] == '-' && v[1] == '-'; }

LibreGPUStatus ui_parse_args(LibreGPULauncherSettings *s, int argc, char **argv, char *err, size_t err_len)
{
    if (!s) return LIBREGPU_ERR_INVALID_ARG;
    memset(s, 0, sizeof *s);
    libregpu_config_defaults(&s->config);
    s->demo_frames = 120;
    if (argc < 0 || (argc > 0 && !argv)) {
        set_err(err, err_len, "invalid argument vector%s", "");
        return LIBREGPU_ERR_INVALID_ARG;
    }

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!a) { set_err(err, err_len, "null argument%s", ""); return LIBREGPU_ERR_INVALID_ARG; }
        const char *val = (i + 1 < argc) ? argv[i + 1] : NULL;
        double dv;
        unsigned long long uv;

        if (!strcmp(a, "--help") || !strcmp(a, "-h"))      { s->help = true; continue; }
        if (!strcmp(a, "--selftest"))                      { s->selftest = true; continue; }
        if (!strcmp(a, "--compat"))                        { s->compat = true; continue; }
        if (!strcmp(a, "--doctor"))                        { s->doctor = true; continue; }
        if (!strcmp(a, "--demo"))                          { s->demo = true; continue; }
        if (!strcmp(a, "--quiet"))                         { s->quiet = true; continue; }
        if (!strcmp(a, "--no-micro"))                      { s->config.enable_micro_pixel = false; continue; }
        if (!strcmp(a, "--no-camera"))                     { s->config.enable_camera = false; continue; }

        
        const bool takes_value = !strcmp(a, "--game") || !strcmp(a, "--fps") || !strcmp(a, "--frames") ||
                                 !strcmp(a, "--baseline-sample");
        if (!takes_value) { set_err(err, err_len, "unknown option '%s'", a); return LIBREGPU_ERR_INVALID_ARG; }
        if (!val || looks_like_option(val)) {
            set_err(err, err_len, "%s needs a value", a);
            return LIBREGPU_ERR_INVALID_ARG;
        }

        if (!strcmp(a, "--game")) {
            const size_t len = strlen(val);
            if (len == 0 || len >= sizeof s->game_path) {          
                set_err(err, err_len, "game path is empty or longer than %s characters (limit 511)", "511");
                return LIBREGPU_ERR_INVALID_ARG;
            }
            memcpy(s->game_path, val, len + 1);
            s->game_selected = true;
        } else if (!strcmp(a, "--fps")) {
            if (!parse_double(val, &dv) || dv < 0.0 || dv > LIBREGPU_GOV_MAX_HZ) {
                set_err(err, err_len, "%s needs a finite number from 0 to 1000000", a); return LIBREGPU_ERR_INVALID_ARG;
            }
            s->config.target_loops_per_second = dv;
        } else if (!strcmp(a, "--frames")) {
            if (!parse_u64(val, &uv) || uv == 0 || uv > 200) {
                set_err(err, err_len, "%s needs an integer from 1 to 200 (demo queue limit)", a); return LIBREGPU_ERR_INVALID_ARG;
            }
            s->demo_frames = (size_t)uv;
        } else if (!strcmp(a, "--baseline-sample")) {
            if (!parse_u64(val, &uv) || uv > 1000000) {
                set_err(err, err_len, "%s needs a non-negative integer", a); return LIBREGPU_ERR_INVALID_ARG;
            }
            s->config.baseline_sample_interval = (uint32_t)uv;
        }
        i++;                                                        
    }
    return LIBREGPU_OK;
}

void ui_print_banner(FILE *out)
{
    fprintf(out, "LIBREGPU %s (%s, %s) - game graphics workload optimizer (launcher)\n",
            LIBREGPU_VERSION, LIBREGPU_STEP_LABEL, LIBREGPU_RELEASE);
}

void ui_print_usage(FILE *out, const char *prog)
{
    fprintf(out,
        "usage: %s [options]\n"
        "  --game <path>          launch a game under the presentation interposer\n"
        "                         (proves interception + unchanged forwarding; no draw-call capture)\n"
        "  --demo                 run the scripted game on the simulated backend\n"
        "  --frames <n>           demo length, 1..200 (default 120)\n"
        "  --fps <F>              target loop rate F (T = 1/F); 0 = unlimited (default)\n"
        "  --no-micro             disable the Micro-Pixel Update Unit\n"
        "  --no-camera            disable the Camera Update Unit\n"
        "  --baseline-sample <n>  force the ordinary path every n cycles to measure GPU time\n"
        "  --quiet                print only errors and the final report\n"
        "  --compat               print the compatibility matrix and exit\n"
        "  --doctor               check this installation and the host and exit\n"
        "  --selftest             run the validation suite and exit\n"
        "  --help                 this text\n",
        prog ? prog : "libregpu-core");
}

void ui_show_status(FILE *out, const LibreGPURuntimeStatus *st)
{
    fprintf(out, "status: runtime=%s path=%s session=%s baseline=%s z=%s mode=%s gpu_in_flight=%s\n",
            libregpu_runtime_state_str(st->state),
            st->graphics_path_active ? "active" : "inactive",
            st->session_valid ? "valid" : "INVALID",
            st->baseline_exists ? "yes" : "no",
            st->z_constructed ? "built" : "-",
            st->optimizing ? "optimizing" : "ordinary/fallback",
            st->gpu_in_flight ? "yes" : "no");
}

void ui_show_cycle(FILE *out, uint64_t cycle, const LibreGPUCycleInfo *info)
{
    fprintf(out, "cycle %4llu  %-20s %-11s ops %3zu/%-3zu  %s%s\n",
            (unsigned long long)cycle,
            info->was_baseline ? "baseline" : libregpu_stream_action_str(info->decision),
            info->submitted_mode == LIBREGPU_Z_INCREMENTAL ? "incremental" :
            info->submitted_mode == LIBREGPU_Z_NO_IMPACT   ? "no-impact"   : "ordinary",
            info->ops_submitted, info->ops_total, info->reason ? info->reason : "",
            info->baseline_lost ? "  [baseline lost]" : "");
}

void ui_show_error(FILE *out, const char *where, LibreGPUStatus st)
{
    fprintf(out, "error: %s: %s\n", where, libregpu_status_str(st));
}


















#define LIBREGPU_SVG_MAX_BYTES    (8u * 1024u * 1024u)
#define LIBREGPU_IMAGE_MAX_DIM    2048
#define LIBREGPU_IMAGE_MAX_PIXELS 4194304
#define LIBREGPU_SVG_SUBSCAN      4

typedef struct { float x0, y0, x1, y1; } SvgEdge;
typedef struct { SvgEdge *edges; size_t count, capacity; } SvgEdgeList;
typedef struct { float x; int dir; } SvgCross;

typedef struct {
    double scale, off_x, off_y, vb_x, vb_y;
} SvgTransform;

static bool svg_edges_push(SvgEdgeList *list, float x0, float y0,
                           float x1, float y1)
{
    if (x0 == x1 && y0 == y1) return true;
    if (list->count == list->capacity) {
        size_t capacity = list->capacity ? list->capacity * 2u : 256u;
        SvgEdge *edges;
        if (capacity < list->capacity || capacity > SIZE_MAX / sizeof *edges)
            return false;
        edges = (SvgEdge *)realloc(list->edges, capacity * sizeof *edges);
        if (!edges) return false;
        list->edges = edges;
        list->capacity = capacity;
    }
    list->edges[list->count].x0 = x0;
    list->edges[list->count].y0 = y0;
    list->edges[list->count].x1 = x1;
    list->edges[list->count].y1 = y1;
    list->count++;
    return true;
}

static bool svg_number(const char **cursor, const char *end, double *out)
{
    const char *p = *cursor;
    const char *start;
    int sign = 1;
    bool digits = false;
    double value = 0.0;

    while (p < end && (isspace((unsigned char)*p) || *p == ',')) p++;
    start = p;
    if (p < end && (*p == '+' || *p == '-')) {
        if (*p == '-') sign = -1;
        p++;
    }
    while (p < end && isdigit((unsigned char)*p)) {
        value = value * 10.0 + (double)(*p - '0');
        p++;
        digits = true;
    }
    if (p < end && *p == '.') {
        double fraction = 0.1;
        p++;
        while (p < end && isdigit((unsigned char)*p)) {
            value += (double)(*p - '0') * fraction;
            fraction *= 0.1;
            p++;
            digits = true;
        }
    }
    if (!digits) {
        *cursor = start;
        return false;
    }
    if (p < end && (*p == 'e' || *p == 'E')) {
        const char *exponent_start = p;
        int exponent_sign = 1;
        int exponent = 0;
        bool exponent_digits = false;
        p++;
        if (p < end && (*p == '+' || *p == '-')) {
            if (*p == '-') exponent_sign = -1;
            p++;
        }
        while (p < end && isdigit((unsigned char)*p)) {
            if (exponent < 10000) exponent = exponent * 10 + (*p - '0');
            p++;
            exponent_digits = true;
        }
        if (exponent_digits) value *= pow(10.0, (double)(exponent_sign * exponent));
        else p = exponent_start;
    }
    *out = (double)sign * value;
    *cursor = p;
    return true;
}


static bool svg_attr(const char **cursor, const char *end, char *name,
                     size_t name_cap, const char **value, size_t *value_len)
{
    const char *p = *cursor;

    for (;;) {
        const char *name_start;
        size_t name_len;
        char quote;
        const char *value_start;
        const char *value_end;

        while (p < end && isspace((unsigned char)*p)) p++;
        if (p >= end || *p == '>' || *p == '/') { *cursor = p; return false; }

        name_start = p;
        while (p < end && !isspace((unsigned char)*p) && *p != '=' &&
               *p != '>' && *p != '/') p++;
        name_len = (size_t)(p - name_start);
        while (p < end && isspace((unsigned char)*p)) p++;
        if (p >= end || *p != '=') { *cursor = p; continue; }
        p++;
        while (p < end && isspace((unsigned char)*p)) p++;
        if (p >= end || (*p != '"' && *p != '\'')) { *cursor = p; continue; }
        quote = *p++;
        value_start = p;
        while (p < end && *p != quote) p++;
        value_end = p;
        if (p < end) p++;
        if (name_len == 0 || name_len >= name_cap) { *cursor = p; continue; }
        memcpy(name, name_start, name_len);
        name[name_len] = '\0';
        *value = value_start;
        *value_len = (size_t)(value_end - value_start);
        *cursor = p;
        return true;
    }
}

static const char *svg_find(const char *p, const char *end, const char *needle)
{
    const size_t length = strlen(needle);
    if (length == 0) return p;
    while ((size_t)(end - p) >= length) {
        if (!memcmp(p, needle, length)) return p;
        p++;
    }
    return NULL;
}


static const char *svg_tag_end(const char *p, const char *end)
{
    char quote = '\0';
    while (p < end) {
        if (quote) {
            if (*p == quote) quote = '\0';
        } else if (*p == '"' || *p == '\'') {
            quote = *p;
        } else if (*p == '>') {
            return p;
        }
        p++;
    }
    return end;
}


static const char *svg_next_tag(const char *p, const char *end,
                                const char **name, size_t *name_len)
{
    while (p < end) {
        const char *lt = (const char *)memchr(p, '<', (size_t)(end - p));
        const char *name_start;
        const char *name_end;
        size_t rest;

        if (!lt) return NULL;
        rest = (size_t)(end - lt);
        if (rest >= 4 && !memcmp(lt, "<!--", 4)) {
            const char *e = svg_find(lt + 4, end, "-->");
            p = e ? e + 3 : end;
            continue;
        }
        if (rest >= 9 && !memcmp(lt, "<![CDATA[", 9)) {
            const char *e = svg_find(lt + 9, end, "]]>");
            p = e ? e + 3 : end;
            continue;
        }
        if (rest >= 2 && lt[1] == '?') {
            const char *e = svg_find(lt + 2, end, "?>");
            p = e ? e + 2 : end;
            continue;
        }
        if (rest >= 2 && (lt[1] == '!' || lt[1] == '/')) {
            const char *e = svg_find(lt + 2, end, ">");
            p = e ? e + 1 : end;
            continue;
        }
        name_start = lt + 1;
        name_end = name_start;
        while (name_end < end && !isspace((unsigned char)*name_end) &&
               *name_end != '>' && *name_end != '/') name_end++;
        if (name_end == name_start) { p = lt + 1; continue; }
        *name = name_start;
        *name_len = (size_t)(name_end - name_start);
        return lt;
    }
    return NULL;
}

static int svg_hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool svg_color(const char *v, size_t n, unsigned char *r,
                      unsigned char *g, unsigned char *b, bool *none)
{
    *none = false;
    *r = 0;
    *g = 0;
    *b = 0;
    if (n == 4 && !memcmp(v, "none", 4)) { *none = true; return true; }
    if (n == 5 && !memcmp(v, "black", 5)) return true;
    if (n == 5 && !memcmp(v, "white", 5)) { *r = *g = *b = 255; return true; }
    if (n == 3 && !memcmp(v, "red", 3)) { *r = 255; return true; }
    if (n == 5 && !memcmp(v, "green", 5)) { *g = 128; return true; }
    if (n == 4 && !memcmp(v, "blue", 4)) { *b = 255; return true; }
    if (n == 0 || v[0] != '#') return false;
    if (n == 4) {
        const int rh = svg_hex(v[1]), gh = svg_hex(v[2]), bh = svg_hex(v[3]);
        if (rh < 0 || gh < 0 || bh < 0) return false;
        *r = (unsigned char)(rh * 17);
        *g = (unsigned char)(gh * 17);
        *b = (unsigned char)(bh * 17);
        return true;
    }
    if (n == 7) {
        const int r0 = svg_hex(v[1]), r1 = svg_hex(v[2]);
        const int g0 = svg_hex(v[3]), g1 = svg_hex(v[4]);
        const int b0 = svg_hex(v[5]), b1 = svg_hex(v[6]);
        if (r0 < 0 || r1 < 0 || g0 < 0 || g1 < 0 || b0 < 0 || b1 < 0)
            return false;
        *r = (unsigned char)(r0 * 16 + r1);
        *g = (unsigned char)(g0 * 16 + g1);
        *b = (unsigned char)(b0 * 16 + b1);
        return true;
    }
    return false;
}

static void svg_map(const SvgTransform *t, double x, double y,
                    float *out_x, float *out_y)
{
    *out_x = (float)((x - t->vb_x) * t->scale + t->off_x);
    *out_y = (float)((y - t->vb_y) * t->scale + t->off_y);
}

static bool svg_cubic(SvgEdgeList *list, float x0, float y0, float x1, float y1,
                      float x2, float y2, float x3, float y3)
{
    const double length =
        hypot((double)x1 - (double)x0, (double)y1 - (double)y0) +
        hypot((double)x2 - (double)x1, (double)y2 - (double)y1) +
        hypot((double)x3 - (double)x2, (double)y3 - (double)y2);
    int steps = (int)(length / 1.5);
    float px = x0, py = y0;

    if (steps < 2) steps = 2;
    if (steps > 64) steps = 64;
    for (int i = 1; i <= steps; i++) {
        const double t = (double)i / (double)steps;
        const double mt = 1.0 - t;
        const double bx = mt * mt * mt * (double)x0 +
                          3.0 * mt * mt * t * (double)x1 +
                          3.0 * mt * t * t * (double)x2 +
                          t * t * t * (double)x3;
        const double by = mt * mt * mt * (double)y0 +
                          3.0 * mt * mt * t * (double)y1 +
                          3.0 * mt * t * t * (double)y2 +
                          t * t * t * (double)y3;
        const float nx = (float)bx, ny = (float)by;
        if (!svg_edges_push(list, px, py, nx, ny)) return false;
        px = nx;
        py = ny;
    }
    return true;
}



static bool svg_path(const char *text, const char *end, SvgEdgeList *list,
                     const SvgTransform *transform)
{
    const char *p = text;
    double cx = 0.0, cy = 0.0, sx = 0.0, sy = 0.0;
    char command = 0;
    bool started = false;

    while (p < end) {
        double v[6];
        float ax, ay, bx, by;

        while (p < end && (isspace((unsigned char)*p) || *p == ',')) p++;
        if (p >= end) break;
        if (isalpha((unsigned char)*p)) {
            command = *p++;
            if (command == 'z' || command == 'Z') {
                if (started) {
                    svg_map(transform, cx, cy, &ax, &ay);
                    svg_map(transform, sx, sy, &bx, &by);
                    if (!svg_edges_push(list, ax, ay, bx, by)) return false;
                    cx = sx;
                    cy = sy;
                }
                command = 0;
                continue;
            }
        } else if (command == 0) {
            return false;
        }

        switch (command) {
        case 'M': case 'm':
            if (!svg_number(&p, end, &v[0]) || !svg_number(&p, end, &v[1]))
                return false;
            if (command == 'm') { v[0] += cx; v[1] += cy; }
            if (started) {                         
                svg_map(transform, cx, cy, &ax, &ay);
                svg_map(transform, sx, sy, &bx, &by);
                if (!svg_edges_push(list, ax, ay, bx, by)) return false;
            }
            cx = v[0];
            cy = v[1];
            sx = cx;
            sy = cy;
            started = true;
            command = (command == 'm') ? 'l' : 'L';
            break;
        case 'L': case 'l':
            if (!svg_number(&p, end, &v[0]) || !svg_number(&p, end, &v[1]))
                return false;
            if (command == 'l') { v[0] += cx; v[1] += cy; }
            svg_map(transform, cx, cy, &ax, &ay);
            svg_map(transform, v[0], v[1], &bx, &by);
            if (!svg_edges_push(list, ax, ay, bx, by)) return false;
            cx = v[0];
            cy = v[1];
            break;
        case 'H': case 'h':
            if (!svg_number(&p, end, &v[0])) return false;
            if (command == 'h') v[0] += cx;
            svg_map(transform, cx, cy, &ax, &ay);
            svg_map(transform, v[0], cy, &bx, &by);
            if (!svg_edges_push(list, ax, ay, bx, by)) return false;
            cx = v[0];
            break;
        case 'V': case 'v':
            if (!svg_number(&p, end, &v[0])) return false;
            if (command == 'v') v[0] += cy;
            svg_map(transform, cx, cy, &ax, &ay);
            svg_map(transform, cx, v[0], &bx, &by);
            if (!svg_edges_push(list, ax, ay, bx, by)) return false;
            cy = v[0];
            break;
        case 'C': case 'c':
            for (int i = 0; i < 6; i++)
                if (!svg_number(&p, end, &v[i])) return false;
            if (command == 'c') {
                v[0] += cx; v[1] += cy;
                v[2] += cx; v[3] += cy;
                v[4] += cx; v[5] += cy;
            }
            svg_map(transform, cx, cy, &ax, &ay);
            svg_map(transform, v[0], v[1], &bx, &by);
            {
                float qx, qy, rx, ry;
                svg_map(transform, v[2], v[3], &qx, &qy);
                svg_map(transform, v[4], v[5], &rx, &ry);
                if (!svg_cubic(list, ax, ay, bx, by, qx, qy, rx, ry))
                    return false;
            }
            cx = v[4];
            cy = v[5];
            break;
        default:
            return false;
        }
    }
    if (started && (cx != sx || cy != sy)) {
        float ax, ay, bx, by;
        svg_map(transform, cx, cy, &ax, &ay);
        svg_map(transform, sx, sy, &bx, &by);
        if (!svg_edges_push(list, ax, ay, bx, by)) return false;
    }
    return true;
}

static int svg_cross_cmp(const void *a, const void *b)
{
    const SvgCross *ca = (const SvgCross *)a;
    const SvgCross *cb = (const SvgCross *)b;
    return (ca->x > cb->x) - (ca->x < cb->x);
}

static void svg_add_span(float *mask, int width, int y, float xa, float xb,
                         float weight)
{
    int first, last;

    if (xb <= xa) return;
    first = (int)floorf(xa);
    last = (int)ceilf(xb);
    if (first < 0) first = 0;
    if (last > width) last = width;
    for (int x = first; x < last; x++) {
        const float left = xa > (float)x ? xa : (float)x;
        const float right = xb < (float)(x + 1) ? xb : (float)(x + 1);
        if (right > left)
            mask[(size_t)y * (size_t)width + (size_t)x] += weight * (right - left);
    }
}

static unsigned char svg_byte(double value)
{
    if (value <= 0.0) return 0;
    if (value >= 255.0) return 255;
    return (unsigned char)(value + 0.5);
}

static void svg_raster(unsigned char *canvas, int width, int height,
                       float *mask, const SvgEdgeList *list,
                       unsigned char r, unsigned char g, unsigned char b,
                       double alpha, int even_odd)
{
    float minx, maxx, miny, maxy;
    int y_first, y_last, x_first, x_last;
    SvgCross *crossings;
    const float sub_weight = 1.0f / (float)LIBREGPU_SVG_SUBSCAN;

    if (!list->count || !(alpha > 0.0)) return;
    minx = maxx = list->edges[0].x0;
    miny = maxy = list->edges[0].y0;
    for (size_t i = 0; i < list->count; i++) {
        const SvgEdge *e = &list->edges[i];
        if (e->x0 < minx) minx = e->x0;
        if (e->x0 > maxx) maxx = e->x0;
        if (e->x1 < minx) minx = e->x1;
        if (e->x1 > maxx) maxx = e->x1;
        if (e->y0 < miny) miny = e->y0;
        if (e->y0 > maxy) maxy = e->y0;
        if (e->y1 < miny) miny = e->y1;
        if (e->y1 > maxy) maxy = e->y1;
    }
    y_first = (int)floorf(miny);
    y_last = (int)ceilf(maxy);
    x_first = (int)floorf(minx);
    x_last = (int)ceilf(maxx);
    if (y_first < 0) y_first = 0;
    if (y_last > height) y_last = height;
    if (x_first < 0) x_first = 0;
    if (x_last > width) x_last = width;
    if (y_first >= y_last || x_first >= x_last) return;

    memset(mask + (size_t)y_first * (size_t)width, 0,
           (size_t)(y_last - y_first) * (size_t)width * sizeof *mask);

    crossings = (SvgCross *)malloc(list->count * sizeof *crossings);
    if (!crossings) return;

    for (int y = y_first; y < y_last; y++) {
        for (int sub = 0; sub < LIBREGPU_SVG_SUBSCAN; sub++) {
            const float sy = (float)y + ((float)sub + 0.5f) * sub_weight;
            size_t count = 0;
            int winding = 0;
            float span = 0.0f;

            for (size_t i = 0; i < list->count; i++) {
                const SvgEdge *e = &list->edges[i];
                if ((e->y0 <= sy && sy < e->y1) || (e->y1 <= sy && sy < e->y0)) {
                    const float t = (sy - e->y0) / (e->y1 - e->y0);
                    crossings[count].x = e->x0 + t * (e->x1 - e->x0);
                    crossings[count].dir = e->y1 > e->y0 ? 1 : -1;
                    count++;
                }
            }
            if (count < 2) continue;
            qsort(crossings, count, sizeof *crossings, svg_cross_cmp);
            for (size_t i = 0; i < count; i++) {
                if (even_odd) {
                    if (winding == 0) span = crossings[i].x;
                    winding ^= 1;
                    if (winding == 0)
                        svg_add_span(mask, width, y, span, crossings[i].x,
                                     sub_weight);
                } else {
                    const int previous = winding;
                    winding += crossings[i].dir;
                    if (previous == 0 && winding != 0) {
                        span = crossings[i].x;
                    } else if (previous != 0 && winding == 0) {
                        svg_add_span(mask, width, y, span, crossings[i].x,
                                     sub_weight);
                    }
                }
            }
        }
    }
    free(crossings);

    for (int y = y_first; y < y_last; y++) {
        for (int x = x_first; x < x_last; x++) {
            double coverage = (double)mask[(size_t)y * (size_t)width + (size_t)x];
            unsigned char *pixel;
            double dst_a, src_a, out_a;
            if (coverage <= 0.0) continue;
            if (coverage > 1.0) coverage = 1.0;
            pixel = canvas + ((size_t)y * (size_t)width + (size_t)x) * 4u;
            src_a = alpha * coverage;
            dst_a = (double)pixel[3] / 255.0;
            out_a = src_a + dst_a * (1.0 - src_a);
            if (out_a <= 0.0) continue;
            pixel[0] = svg_byte(((double)r * src_a +
                                 (double)pixel[0] * dst_a * (1.0 - src_a)) / out_a);
            pixel[1] = svg_byte(((double)g * src_a +
                                 (double)pixel[1] * dst_a * (1.0 - src_a)) / out_a);
            pixel[2] = svg_byte(((double)b * src_a +
                                 (double)pixel[2] * dst_a * (1.0 - src_a)) / out_a);
            pixel[3] = svg_byte(out_a * 255.0);
        }
    }
}

static bool svg_is_plate(float minx, float miny, float maxx, float maxy,
                         const SvgTransform *t, double vb_w, double vb_h)
{
    const double left = t->off_x;
    const double top = t->off_y;
    const double right = t->off_x + vb_w * t->scale;
    const double bottom = t->off_y + vb_h * t->scale;
    const double margin_x = (right - left) * 0.02;
    const double margin_y = (bottom - top) * 0.02;

    return (double)minx <= left + margin_x && (double)miny <= top + margin_y &&
           (double)maxx >= right - margin_x && (double)maxy >= bottom - margin_y;
}

static void svg_edge_bounds(const SvgEdgeList *list, float *minx, float *miny,
                            float *maxx, float *maxy)
{
    *minx = *maxx = list->edges[0].x0;
    *miny = *maxy = list->edges[0].y0;
    for (size_t i = 0; i < list->count; i++) {
        const SvgEdge *e = &list->edges[i];
        if (e->x0 < *minx) *minx = e->x0;
        if (e->x0 > *maxx) *maxx = e->x0;
        if (e->x1 < *minx) *minx = e->x1;
        if (e->x1 > *maxx) *maxx = e->x1;
        if (e->y0 < *miny) *miny = e->y0;
        if (e->y0 > *maxy) *maxy = e->y0;
        if (e->y1 < *miny) *miny = e->y1;
        if (e->y1 > *maxy) *maxy = e->y1;
    }
}

LibreGPUStatus libregpu_image_parse_svg(const char *text, size_t length, int width,
                                  int height, unsigned flags, LibreGPUImage *out)
{
    const char *end;
    const char *name = NULL;
    size_t name_len = 0;
    const char *tag;
    const char *tag_end;
    const char *cursor;
    double vb_x = 0.0, vb_y = 0.0, vb_w = 0.0, vb_h = 0.0;
    double svg_w = 0.0, svg_h = 0.0;
    unsigned char *canvas;
    float *mask;
    SvgTransform transform;

    if (out) memset(out, 0, sizeof *out);
    if (!text || !out || width <= 0 || height <= 0 || length == 0)
        return LIBREGPU_ERR_INVALID_ARG;
    if (width > LIBREGPU_IMAGE_MAX_DIM || height > LIBREGPU_IMAGE_MAX_DIM)
        return LIBREGPU_ERR_UNSUPPORTED;
    if ((int64_t)width * (int64_t)height > (int64_t)LIBREGPU_IMAGE_MAX_PIXELS)
        return LIBREGPU_ERR_UNSUPPORTED;
    if (length > (size_t)LIBREGPU_SVG_MAX_BYTES) return LIBREGPU_ERR_UNSUPPORTED;

    end = text + length;
    tag = svg_next_tag(text, end, &name, &name_len);
    while (tag && !(name_len == 3 && !memcmp(name, "svg", 3))) {
        const char *e = svg_tag_end(name + name_len, end);
        if (e >= end) { tag = NULL; break; }
        tag = svg_next_tag(e + 1, end, &name, &name_len);
    }
    if (!tag) return LIBREGPU_ERR_VALIDATION;

    tag_end = svg_tag_end(name + name_len, end);
    {
        const char *attr_cursor = name + name_len;
        char attr[32];
        const char *value = NULL;
        size_t value_len = 0;
        while (svg_attr(&attr_cursor, tag_end, attr, sizeof attr, &value,
                        &value_len)) {
            if (!strcmp(attr, "viewBox")) {
                const char *vp = value;
                if (!svg_number(&vp, value + value_len, &vb_x) ||
                    !svg_number(&vp, value + value_len, &vb_y) ||
                    !svg_number(&vp, value + value_len, &vb_w) ||
                    !svg_number(&vp, value + value_len, &vb_h))
                    return LIBREGPU_ERR_VALIDATION;
            } else if (!strcmp(attr, "width")) {
                const char *vp = value;
                (void)svg_number(&vp, value + value_len, &svg_w);
            } else if (!strcmp(attr, "height")) {
                const char *vp = value;
                (void)svg_number(&vp, value + value_len, &svg_h);
            }
        }
    }
    if (!(vb_w > 0.0) || !(vb_h > 0.0)) {
        if (svg_w > 0.0 && svg_h > 0.0) {
            vb_x = 0.0;
            vb_y = 0.0;
            vb_w = svg_w;
            vb_h = svg_h;
        } else {
            return LIBREGPU_ERR_VALIDATION;
        }
    }

    canvas = (unsigned char *)calloc((size_t)width * (size_t)height, 4u);
    mask = (float *)malloc((size_t)width * (size_t)height * sizeof *mask);
    if (!canvas || !mask) {
        free(canvas);
        free(mask);
        return LIBREGPU_ERR_NO_MEMORY;
    }

    transform.scale = fmin((double)width / vb_w, (double)height / vb_h);
    transform.off_x = ((double)width - vb_w * transform.scale) * 0.5;
    transform.off_y = ((double)height - vb_h * transform.scale) * 0.5;
    transform.vb_x = vb_x;
    transform.vb_y = vb_y;

    cursor = tag_end < end ? tag_end + 1 : end;
    while ((tag = svg_next_tag(cursor, end, &name, &name_len)) != NULL) {
        const char *path_end = svg_tag_end(name + name_len, end);
        if (path_end >= end) break;

        if (name_len == 4 && !memcmp(name, "path", 4)) {
            const char *attr_cursor = name + name_len;
            char attr[32];
            const char *value = NULL;
            size_t value_len = 0;
            const char *d = NULL;             size_t d_len = 0;
            const char *fill = NULL;          size_t fill_len = 0;
            const char *opacity = NULL;       size_t opacity_len = 0;
            const char *fill_opacity = NULL;  size_t fill_opacity_len = 0;
            const char *fill_rule = NULL;     size_t fill_rule_len = 0;
            const char *transform_attr = NULL;

            while (svg_attr(&attr_cursor, path_end, attr, sizeof attr, &value,
                            &value_len)) {
                if (!strcmp(attr, "d")) { d = value; d_len = value_len; }
                else if (!strcmp(attr, "fill")) { fill = value; fill_len = value_len; }
                else if (!strcmp(attr, "opacity")) { opacity = value; opacity_len = value_len; }
                else if (!strcmp(attr, "fill-opacity")) { fill_opacity = value; fill_opacity_len = value_len; }
                else if (!strcmp(attr, "fill-rule")) { fill_rule = value; fill_rule_len = value_len; }
                else if (!strcmp(attr, "transform")) { transform_attr = value; }
            }

            

            if (d && d_len && !transform_attr) {
                unsigned char r = 0, g = 0, b = 0;
                bool none = false;
                bool color_ok = true;

                if (fill) color_ok = svg_color(fill, fill_len, &r, &g, &b, &none);
                if (color_ok && !none) {
                    double alpha = 1.0;
                    double number = 0.0;
                    const int even_odd =
                        fill_rule && fill_rule_len == 7 &&
                        !memcmp(fill_rule, "evenodd", 7);
                    SvgEdgeList list;

                    if (opacity) {
                        const char *vp = opacity;
                        if (svg_number(&vp, opacity + opacity_len, &number))
                            alpha *= number;
                    }
                    if (fill_opacity) {
                        const char *vp = fill_opacity;
                        if (svg_number(&vp, fill_opacity + fill_opacity_len,
                                       &number))
                            alpha *= number;
                    }
                    if (alpha < 0.0) alpha = 0.0;
                    if (alpha > 1.0) alpha = 1.0;

                    memset(&list, 0, sizeof list);
                    if (!svg_path(d, d + d_len, &list, &transform)) {
                        free(list.edges);          
                    } else {
                        if (list.count) {
                            float minx, miny, maxx, maxy;
                            svg_edge_bounds(&list, &minx, &miny, &maxx, &maxy);
                            if (!((flags & LIBREGPU_IMAGE_SKIP_BLACK_PLATE) &&
                                  r == 0 && g == 0 && b == 0 && alpha >= 0.999 &&
                                  svg_is_plate(minx, miny, maxx, maxy, &transform,
                                               vb_w, vb_h)))
                                svg_raster(canvas, width, height, mask, &list,
                                           r, g, b, alpha, even_odd);
                        }
                        free(list.edges);
                    }
                }
            }
        }
        cursor = path_end + 1;
    }

    free(mask);
    out->width = width;
    out->height = height;
    out->rgba = canvas;
    return LIBREGPU_OK;
}

LibreGPUStatus libregpu_image_load_svg(const char *path, int width, int height,
                                 unsigned flags, LibreGPUImage *out)
{
    FILE *file;
    long size;
    char *text;
    size_t got;
    LibreGPUStatus status;

    if (out) memset(out, 0, sizeof *out);
    if (!path || !out) return LIBREGPU_ERR_INVALID_ARG;
    file = fopen(path, "rb");
    if (!file) return LIBREGPU_ERR_INVALID_ARG;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return LIBREGPU_ERR_INVALID_ARG; }
    size = ftell(file);
    if (size <= 0 || (unsigned long)size > (unsigned long)LIBREGPU_SVG_MAX_BYTES) {
        fclose(file);
        return LIBREGPU_ERR_UNSUPPORTED;
    }
    if (fseek(file, 0, SEEK_SET) != 0) { fclose(file); return LIBREGPU_ERR_INVALID_ARG; }
    text = (char *)malloc((size_t)size);
    if (!text) { fclose(file); return LIBREGPU_ERR_NO_MEMORY; }
    got = fread(text, 1, (size_t)size, file);
    fclose(file);
    if (got != (size_t)size) { free(text); return LIBREGPU_ERR_INVALID_ARG; }
    status = libregpu_image_parse_svg(text, (size_t)size, width, height, flags, out);
    free(text);
    return status;
}

void libregpu_image_free(LibreGPUImage *image)
{
    if (!image) return;
    free(image->rgba);
    memset(image, 0, sizeof *image);
}

int libregpu_image_trim(LibreGPUImage *image)
{
    int minx, miny, maxx = -1, maxy = -1;
    int new_width, new_height;
    unsigned char *rgba;
    size_t row_bytes;

    if (!image || !image->rgba || image->width <= 0 || image->height <= 0)
        return 0;
    minx = image->width;
    miny = image->height;
    for (int y = 0; y < image->height; y++) {
        for (int x = 0; x < image->width; x++) {
            if (image->rgba[((size_t)y * (size_t)image->width + (size_t)x) * 4u + 3u]) {
                if (x < minx) minx = x;
                if (x > maxx) maxx = x;
                if (y < miny) miny = y;
                if (y > maxy) maxy = y;
            }
        }
    }
    if (maxx < minx) return 0;
    if (minx == 0 && miny == 0 &&
        maxx == image->width - 1 && maxy == image->height - 1)
        return 0;

    new_width = maxx - minx + 1;
    new_height = maxy - miny + 1;
    row_bytes = (size_t)new_width * 4u;
    rgba = (unsigned char *)malloc(row_bytes * (size_t)new_height);
    if (!rgba) return 0;
    for (int y = 0; y < new_height; y++)
        memcpy(rgba + (size_t)y * row_bytes,
               image->rgba + (((size_t)(miny + y) * (size_t)image->width +
                               (size_t)minx) * 4u),
               row_bytes);
    free(image->rgba);
    image->rgba = rgba;
    image->width = new_width;
    image->height = new_height;
    return 1;
}

void libregpu_image_gamma(LibreGPUImage *image, double gamma)
{
    unsigned char lut[256];
    size_t pixels;

    if (!image || !image->rgba || !(gamma > 0.0)) return;
    for (int i = 0; i < 256; i++)
        lut[i] = svg_byte(255.0 * pow((double)i / 255.0, gamma));
    pixels = (size_t)image->width * (size_t)image->height;
    for (size_t i = 0; i < pixels; i++) {
        image->rgba[i * 4u + 0u] = lut[image->rgba[i * 4u + 0u]];
        image->rgba[i * 4u + 1u] = lut[image->rgba[i * 4u + 1u]];
        image->rgba[i * 4u + 2u] = lut[image->rgba[i * 4u + 2u]];
    }
}




static bool svg_file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    int probe;

    if (!file) return false;
    probe = fgetc(file);                    
    if (probe == EOF && ferror(file)) {
        fclose(file);
        return false;
    }
    fclose(file);
    return true;
}

static int svg_join_path(char *out, size_t capacity, const char *directory,
                         size_t directory_len, const char *name)
{
    int written;
    if (directory_len == 0) {
        written = snprintf(out, capacity, "%s", name);
    } else {
        const char last = directory[directory_len - 1];
        const char *separator = (last == '/' || last == '\\') ? "" : "/";
        written = snprintf(out, capacity, "%.*s%s%s",
                           (int)directory_len, directory, separator, name);
    }
    return written > 0 && (size_t)written < capacity;
}

static void svg_split_game_path(const char *path, const char **directory,
                                size_t *directory_len, const char **stem,
                                size_t *stem_len)
{
    const char *slash = NULL;
    const char *base;
    const char *dot;

    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\') slash = p;
    *directory = path;
    *directory_len = slash ? (size_t)(slash - path) : 0;
    if (slash && *directory_len == 0) *directory_len = 1;
    base = slash ? slash + 1 : path;
    dot = strrchr(base, '.');
    *stem = base;
    *stem_len = (dot && dot != base) ? (size_t)(dot - base) : strlen(base);
}

static int svg_icon_from_desktop(const char *desktop, const char *directory,
                                 size_t directory_len, char *out,
                                 size_t capacity)
{
    FILE *file = fopen(desktop, "rb");
    char line[1024];
    char icon[256];

    if (!file) return 0;
    icon[0] = '\0';
    while (fgets(line, sizeof line, file)) {
        if (!strncmp(line, "Icon=", 5)) {
            size_t length = strlen(line + 5);
            while (length &&
                   (line[5 + length - 1] == '\n' || line[5 + length - 1] == '\r' ||
                    line[5 + length - 1] == ' ' || line[5 + length - 1] == '\t'))
                length--;
            if (length >= sizeof icon) length = sizeof icon - 1;
            memcpy(icon, line + 5, length);
            icon[length] = '\0';
        }
    }
    fclose(file);
    if (!icon[0]) return 0;

    if (icon[0] == '/') {
        const size_t length = strlen(icon);
        if (length >= 4 && !strcmp(icon + length - 4, ".svg") &&
            svg_file_exists(icon)) {
            const int written = snprintf(out, capacity, "%s", icon);
            return written > 0 && (size_t)written < capacity;
        }
        return 0;
    }

    {
        char name[300];
        int written = snprintf(name, sizeof name, "%s.svg", icon);
        if (written <= 0 || (size_t)written >= sizeof name) return 0;
        if (svg_join_path(out, capacity, directory, directory_len, name) &&
            svg_file_exists(out))
            return 1;
    }
    {
        const char *home = getenv("HOME");
        const char *roots[] = { "/usr/local/share", "/usr/share" };
        int written;
        if (home && *home) {
            written = snprintf(out, capacity,
                               "%s/.local/share/icons/hicolor/scalable/apps/%s.svg",
                               home, icon);
            if (written > 0 && (size_t)written < capacity && svg_file_exists(out))
                return 1;
        }
        for (size_t i = 0; i < sizeof roots / sizeof roots[0]; i++) {
            written = snprintf(out, capacity,
                               "%s/icons/hicolor/scalable/apps/%s.svg",
                               roots[i], icon);
            if (written > 0 && (size_t)written < capacity && svg_file_exists(out))
                return 1;
        }
    }
    return 0;
}

static int svg_try_desktop(const char *desktop, const char *directory,
                           size_t directory_len, char *out, size_t capacity)
{
    if (!svg_file_exists(desktop)) return 0;
    return svg_icon_from_desktop(desktop, directory, directory_len, out, capacity);
}

int libregpu_game_icon_find(const char *game_path, char *out, size_t capacity)
{
    static const char *const suffixes[] = {
        ".svg", "-icon.svg", "_icon.svg", "-logo.svg", ".logo.svg",
        "-cover.svg", ".cover.svg", "-thumbnail.svg"
    };
    static const char *const generic[] = {
        "icon.svg", "logo.svg", "cover.svg", "cover-art.svg",
        "thumbnail.svg", "game.svg"
    };
    const char *directory;
    const char *stem;
    size_t directory_len;
    size_t stem_len;
    char name[LIBREGPU_UI_PATH_MAX];
    char desktop[LIBREGPU_UI_PATH_MAX];
    int written;

    if (!game_path || !out || capacity == 0) return 0;
    out[0] = '\0';
    svg_split_game_path(game_path, &directory, &directory_len, &stem, &stem_len);

    for (size_t i = 0; i < sizeof suffixes / sizeof suffixes[0]; i++) {
        written = snprintf(name, sizeof name, "%.*s%s",
                           (int)stem_len, stem, suffixes[i]);
        if (written <= 0 || (size_t)written >= sizeof name) continue;
        if (svg_join_path(out, capacity, directory, directory_len, name) &&
            svg_file_exists(out))
            return 1;
    }
    for (size_t i = 0; i < sizeof generic / sizeof generic[0]; i++) {
        if (svg_join_path(out, capacity, directory, directory_len, generic[i]) &&
            svg_file_exists(out))
            return 1;
    }

    written = snprintf(name, sizeof name, "%.*s.desktop", (int)stem_len, stem);
    if (written <= 0 || (size_t)written >= sizeof name) return 0;
    if (svg_join_path(desktop, sizeof desktop, directory, directory_len, name) &&
        svg_try_desktop(desktop, directory, directory_len, out, capacity))
        return 1;

    {
        const char *home = getenv("HOME");
        const char *xdg = getenv("XDG_DATA_DIRS");
        if (home && *home) {
            written = snprintf(desktop, sizeof desktop,
                               "%s/.local/share/applications/%s", home, name);
            if (written > 0 && (size_t)written < sizeof desktop &&
                svg_try_desktop(desktop, directory, directory_len, out, capacity))
                return 1;
        }
        written = snprintf(desktop, sizeof desktop,
                           "/usr/local/share/applications/%s", name);
        if (written > 0 && (size_t)written < sizeof desktop &&
            svg_try_desktop(desktop, directory, directory_len, out, capacity))
            return 1;
        written = snprintf(desktop, sizeof desktop,
                           "/usr/share/applications/%s", name);
        if (written > 0 && (size_t)written < sizeof desktop &&
            svg_try_desktop(desktop, directory, directory_len, out, capacity))
            return 1;
        if (xdg && *xdg) {
            const char *start = xdg;
            while (*start) {
                const char *separator = strchr(start, ':');
                const size_t length = separator ? (size_t)(separator - start)
                                                : strlen(start);
                if (length > 0 && length < 400) {
                    written = snprintf(desktop, sizeof desktop,
                                       "%.*s/applications/%s",
                                       (int)length, start, name);
                    if (written > 0 && (size_t)written < sizeof desktop &&
                        svg_try_desktop(desktop, directory, directory_len,
                                        out, capacity))
                        return 1;
                }
                if (!separator) break;
                start = separator + 1;
            }
        }
    }
    return 0;
}




#ifndef LIBREGPU_ASSET_DIR
#define LIBREGPU_ASSET_DIR "/usr/local/share/libregpu/assets"
#endif

static int svg_try_join(char *out, size_t capacity, const char *directory,
                        const char *name)
{
    if (!directory || !*directory) return 0;
    return svg_join_path(out, capacity, directory, strlen(directory), name) &&
           svg_file_exists(out);
}

static char svg_asset_base[LIBREGPU_UI_PATH_MAX];

void libregpu_asset_set_base(const char *directory)
{
    if (!directory) {
        svg_asset_base[0] = '\0';
        return;
    }
    (void)snprintf(svg_asset_base, sizeof svg_asset_base, "%s", directory);
}

int libregpu_asset_find(const char *name, char *out, size_t capacity)
{
    static const char *const installed[] = {
        LIBREGPU_ASSET_DIR, "/usr/local/share/libregpu/assets",
        "/usr/share/libregpu/assets"
    };
    const char *env;

    if (!name || !*name || !out || capacity == 0) return 0;
    out[0] = '\0';

    env = getenv("LIBREGPU_ASSETS");
    if (svg_try_join(out, capacity, env, name)) return 1;

    
    if (svg_asset_base[0]) {
        if (svg_try_join(out, capacity, svg_asset_base, name)) return 1;
        {
            char directory[LIBREGPU_UI_PATH_MAX];
            const int written = snprintf(directory, sizeof directory,
                                         "%s%sassets", svg_asset_base,
                                         (svg_asset_base[strlen(svg_asset_base) - 1] == '/')
                                             ? "" : "/");
            if (written > 0 && (size_t)written < sizeof directory &&
                svg_try_join(out, capacity, directory, name))
                return 1;
        }
    }

    
    if (svg_try_join(out, capacity, "assets", name)) return 1;
    if (svg_try_join(out, capacity, ".", name)) return 1;

    for (size_t i = 0; i < sizeof installed / sizeof installed[0]; i++)
        if (svg_try_join(out, capacity, installed[i], name)) return 1;
    return 0;
}




int ui_launch_game(const LibreGPULaunchHost *host, const char *game_path,
                   char *message, size_t message_capacity)
{
    char error[192];

    if (message && message_capacity) message[0] = '\0';

    if (!game_path || game_path[0] == '\0') {
        set_err(message, message_capacity, "%s",
                "No executable path was selected");
        return 0;
    }
    if (!host || !host->launch) {
        set_err(message, message_capacity, "%s",
                "The launcher has no launch handler");
        return 0;
    }

    

    if (host->is_runnable && !host->is_runnable(host->userdata, game_path)) {
        set_err(message, message_capacity, "%s",
                "File not found or not executable");
        return 0;
    }

    if (host->minimize) host->minimize(host->userdata);

    error[0] = '\0';
    if (!host->launch(host->userdata, game_path, error, sizeof error)) {
        if (host->restore) host->restore(host->userdata);
        set_err(message, message_capacity, "%s",
                error[0] ? error : "Could not start the game");
        return 0;
    }

    


    return 1;
}

#endif 
#endif 
