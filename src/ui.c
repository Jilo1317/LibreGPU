











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
    size_t      demo_frames;
} LibreGPULauncherSettings;



LibreGPUStatus ui_parse_args(LibreGPULauncherSettings *s, int argc, char **argv, char *err, size_t err_len);

void ui_print_banner(FILE *out);
void ui_print_usage(FILE *out, const char *prog);
void ui_show_status(FILE *out, const LibreGPURuntimeStatus *st);
void ui_show_cycle(FILE *out, uint64_t cycle, const LibreGPUCycleInfo *info);
void ui_show_error(FILE *out, const char *where, LibreGPUStatus st);

#endif 

#ifndef LIBREGPU_UI_DECLARATIONS_ONLY
#ifndef LIBREGPU_UI_IMPLEMENTED
#define LIBREGPU_UI_IMPLEMENTED

#include <errno.h>
#include <math.h>
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
        if (!strcmp(a, "--demo"))                          { s->demo = true; continue; }
        if (!strcmp(a, "--quiet"))                         { s->quiet = true; continue; }
        if (!strcmp(a, "--no-micro"))                      { s->config.enable_micro_pixel = false; continue; }
        if (!strcmp(a, "--no-camera"))                     { s->config.enable_camera = false; continue; }
        if (!strcmp(a, "--camera"))                        { s->config.enable_camera = true;  continue; }

        
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
            if (!parse_double(val, &dv) || dv < 0.0 ||
                (dv > 0.0 && dv < LIBREGPU_GOV_MIN_HZ) || dv > LIBREGPU_GOV_MAX_HZ) {
                set_err(err, err_len, "%s needs 0 (unlimited) or a finite rate from 1 to 1000000", a); return LIBREGPU_ERR_INVALID_ARG;
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
    fprintf(out, "%s %s (%s) - game graphics workload optimizer (launcher)\n",
            LIBREGPU_CODE_NAME, LIBREGPU_VERSION, LIBREGPU_STEP_LABEL);
}

void ui_print_usage(FILE *out, const char *prog)
{
    fprintf(out,
        "usage: %s [options]\n"
        "  --game <path>          connect a supported game (needs a native graphics backend)\n"
        "  --demo                 run the scripted game on the simulated backend\n"
        "  --frames <n>           demo length, 1..200 (default 120)\n"
        "  --fps <F>              target loop rate F (T = 1/F); 0 = unlimited, otherwise 1..1000000\n"
        "  --no-micro             disable the Micro-Pixel Update Unit\n"
        "  --no-camera            disable the Camera Update Unit\n"
        "  --camera               enable the Camera Update Unit (off by default)\n"
        "  --baseline-sample <n>  force the ordinary path every n cycles to measure GPU time\n"
        "  --quiet                print only errors and the final report\n"
        "  --selftest             run the validation suite and exit\n"
        "  --gui                  launch the graphical launcher\n"
        "  --version              print the version and exit\n"
        "  --help                 this text\n",
        prog ? prog : "libregpu");
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

#endif 
#endif 
