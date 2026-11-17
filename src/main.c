#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L      
#endif














#define LIBREGPU_LIB_DECLARATIONS_ONLY
#define LIBREGPU_UI_DECLARATIONS_ONLY
#include "lib.c"
#include "ui.c"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <errno.h>
#include <spawn.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif



LibreGPUBackend *libregpu_sim_backend_create(uint32_t fence_latency);
LibreGPUStatus   libregpu_sim_demo_scenario(LibreGPUBackend *be, size_t frames);









#ifndef LIBREGPU_LAUNCH_PATH_MAX
#define LIBREGPU_LAUNCH_PATH_MAX 4096
#endif

#if !defined(_WIN32)
#define LIBREGPU_PRESENT_TEXT_MAX 8192

static bool copy_text(char *out, size_t cap, const char *text)
{
    if (!out || cap == 0 || !text) return false;
    const size_t n = strlen(text);
    if (n >= cap) return false;
    memcpy(out, text, n + 1);
    return true;
}


static bool resolve_executable(const char *in, char *out, size_t cap)
{
    if (!in || !in[0]) return false;
    if (strchr(in, '/') != NULL) return copy_text(out, cap, in);

    const char *path = getenv("PATH");
    if (!path) path = "/usr/local/bin:/usr/bin:/bin";
    const char *p = path;
    while (*p) {
        const char *end = strchr(p, ':');
        const size_t len = end ? (size_t)(end - p) : strlen(p);
        char candidate[LIBREGPU_LAUNCH_PATH_MAX];
        if (len == 0) {
            if ((size_t)snprintf(candidate, sizeof candidate, "%s", in) < sizeof candidate &&
                access(candidate, X_OK) == 0 && copy_text(out, cap, candidate)) return true;
        } else if (len + 1 + strlen(in) < sizeof candidate) {
            memcpy(candidate, p, len);
            candidate[len] = '/';
            memcpy(candidate + len + 1, in, strlen(in) + 1);
            if (access(candidate, X_OK) == 0 && copy_text(out, cap, candidate)) return true;
        }
        if (!end) break;
        p = end + 1;
    }
    return false;
}



static bool find_interposer(char *out, size_t cap)
{
    const char *env = getenv("LIBREGPU_INTERPOSER");
    if (env && env[0]) return copy_text(out, cap, env);

#if defined(__linux__)
    char exe[LIBREGPU_LAUNCH_PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) return false;
    exe[n] = '\0';
    char *slash = strrchr(exe, '/');
    if (!slash) return false;
    *slash = '\0';

    static const char *const suffixes[] = {
        "/liblibregpu_interpose.so",                    
        "/../lib/libregpu/liblibregpu_interpose.so",       
        "/../lib/liblibregpu_interpose.so"              
    };
    char candidate[LIBREGPU_LAUNCH_PATH_MAX];
    for (size_t i = 0; i < sizeof suffixes / sizeof suffixes[0]; ++i) {
        const int w = snprintf(candidate, sizeof candidate, "%s%s", exe, suffixes[i]);
        if (w <= 0 || (size_t)w >= sizeof candidate) continue;
        if (access(candidate, R_OK) == 0) return copy_text(out, cap, candidate);
    }
    return false;
#else
    (void)out;
    (void)cap;
    return false;
#endif
}

static bool stat_field(const char *text, const char *key, char *out, size_t cap)
{
    const char *p = strstr(text, key);
    if (!p) return false;
    p += strlen(key);
    const char *end = strchr(p, '\n');
    if (!end) end = p + strlen(p);
    const size_t n = (size_t)(end - p);
    if (n == 0 || n >= cap) return false;
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

static void print_presentation_report(const char *interposer, const char *stats_path)
{
    printf("\nPresentation interception (M3)\n");
    printf("  interposer            : %s\n", interposer);

    FILE *f = fopen(stats_path, "rb");
    if (!f) {
        printf("  frames forwarded      : no statistics (the game may not have used OpenGL)\n");
        return;
    }
    char text[LIBREGPU_PRESENT_TEXT_MAX];
    const size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = '\0';

    char frames[32] = "0", glx[32] = "0", egl[32] = "0", sdl[32] = "0";
    char fail[32] = "0", renderer[256] = "", version[128] = "";
    (void)stat_field(text, "frames=", frames, sizeof frames);
    (void)stat_field(text, "glx_swaps=", glx, sizeof glx);
    (void)stat_field(text, "egl_swaps=", egl, sizeof egl);
    (void)stat_field(text, "sdl_swaps=", sdl, sizeof sdl);
    (void)stat_field(text, "forward_failures=", fail, sizeof fail);
    (void)stat_field(text, "renderer=", renderer, sizeof renderer);
    (void)stat_field(text, "version=", version, sizeof version);

    printf("  frames forwarded      : %s (glX %s, EGL %s, SDL %s)\n", frames, glx, egl, sdl);
    printf("  forward failures      : %s\n", fail);
    printf("  renderer              : %s\n", renderer[0] ? renderer : "unknown");
    printf("  driver version        : %s\n", version[0] ? version : "unknown");
    printf("  draw-call capture     : not performed (presentation boundary only)\n");
    printf("  forward policy        : unchanged; no reordering and no skipped frames\n");
}


static int run_interposed(const LibreGPULauncherSettings *s, FILE *err)
{
    char interposer[LIBREGPU_LAUNCH_PATH_MAX];
    char executable[LIBREGPU_LAUNCH_PATH_MAX];
    if (!find_interposer(interposer, sizeof interposer)) {
        fprintf(err,
                "error: cannot connect '%s': no native graphics backend is compiled into this build,\n"
                "       and liblibregpu_interpose.so was not found next to the launcher.\n"
                "       Build it with `make interpose`, or set LIBREGPU_INTERPOSER to its path.\n",
                s->game_path);
        return 3;
    }
    if (!resolve_executable(s->game_path, executable, sizeof executable)) {
        fprintf(err, "error: cannot find executable '%s'\n", s->game_path);
        return 3;
    }

    char stats_path[] = "/tmp/libregpu-present-XXXXXX";
    const int fd = mkstemp(stats_path);
    if (fd < 0) {
        fprintf(err, "error: cannot create a statistics file: %s\n", strerror(errno));
        return 3;
    }
    (void)close(fd);

    char old_preload[LIBREGPU_LAUNCH_PATH_MAX * 2];
    char old_stats[LIBREGPU_LAUNCH_PATH_MAX];
    const char *pre = getenv("LD_PRELOAD");
    const char *st  = getenv("LIBREGPU_INTERPOSE_STATS");
    const bool have_preload = pre && pre[0] && copy_text(old_preload, sizeof old_preload, pre);
    const bool have_stats   = st  && st[0]  && copy_text(old_stats,   sizeof old_stats,   st);

    char new_preload[LIBREGPU_LAUNCH_PATH_MAX * 2];
    const int w = have_preload
        ? snprintf(new_preload, sizeof new_preload, "%s:%s", interposer, old_preload)
        : snprintf(new_preload, sizeof new_preload, "%s", interposer);
    if (w <= 0 || (size_t)w >= sizeof new_preload) {
        (void)unlink(stats_path);
        fprintf(err, "error: the interposer path is too long to preload\n");
        return 3;
    }

    int rc = 3;
    if (setenv("LD_PRELOAD", new_preload, 1) != 0 ||
        setenv("LIBREGPU_INTERPOSE_STATS", stats_path, 1) != 0) {
        fprintf(err, "error: cannot prepare the game environment: %s\n", strerror(errno));
        goto restore;
    }

    {
        char *argv[2];
        argv[0] = executable;
        argv[1] = NULL;
        pid_t pid = 0;
        const int spawned = posix_spawn(&pid, executable, NULL, NULL, argv, environ);
        if (spawned != 0) {
            fprintf(err, "error: could not start the game: %s\n", strerror(spawned));
            goto restore;
        }

        int status = 0;
        while (waitpid(pid, &status, 0) < 0) {
            if (errno != EINTR) {
                fprintf(err, "error: lost the game process: %s\n", strerror(errno));
                goto restore;
            }
        }
        if (WIFEXITED(status))      rc = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) rc = 128 + WTERMSIG(status);
        else                         rc = 0;
    }

restore:
    if (have_preload) (void)setenv("LD_PRELOAD", old_preload, 1);
    else              (void)unsetenv("LD_PRELOAD");
    if (have_stats)   (void)setenv("LIBREGPU_INTERPOSE_STATS", old_stats, 1);
    else              (void)unsetenv("LIBREGPU_INTERPOSE_STATS");

    if (rc != 3 || access(stats_path, R_OK) == 0)
        print_presentation_report(interposer, stats_path);
    (void)unlink(stats_path);
    return rc;
}
#endif 





static void print_compat(FILE *out)
{
    fprintf(out,
        "LIBREGPU %s (%s, %s) compatibility\n"
        "\n"
        "One core, one graphics instruction record, one continuous X/Y/Z pipeline.\n"
        "There are no quality modes and no API preference; every backend fills the\n"
        "same interfaces and keeps the portable fallback.\n"
        "\n"
        "Graphics paths\n"
        "  Simulated backend    always available; scripted demo and self-test only.\n"
        "  OpenGL 2.1           optional real-GPU backend (M3); FBO + scissor replay.\n"
        "                       Requires libEGL, libGL and SDL2 at build time.\n"
        "  Vulkan               no hardware backend is shipped. The reference Intel\n"
        "                       Gen5 machine has no hardware Vulkan, so software\n"
        "                       Vulkan is not a valid performance path.\n"
        "\n"
        "Real-game interposition (M3, POSIX only)\n"
        "  Hooks the presentation boundary (glX, EGL and SDL swap) through\n"
        "  LD_PRELOAD and forwards every swap unchanged. It does not capture draw\n"
        "  calls, reorder frames or skip frames, and it makes no optimization claim\n"
        "  for the game's own workload.\n"
        "\n"
        "Units and measurement\n"
        "  Micro-Pixel Update   on by default; byte-exact scissor-limited replay.\n"
        "  Camera Update        off by default; exact whole-pixel 2D pan only.\n"
        "  Passive FPS governor off by default (target 0 = unlimited).\n"
        "  GPU timing           GL_ARB_sync fence completion; GL timer queries are\n"
        "                       rejected when the driver returns a sentinel.\n"
        "  Simulator numbers    are never reported as real GPU performance.\n"
        "\n"
        "Reference machine, known good\n"
        "  prboom-plus + Freedoom (-vidmode gl)\n"
        "  DDNet (gfx_backend OpenGL)\n"
        "  Controlled 960x540 scene: make gpu-check\n"
        "\n"
        "Not supported\n"
        "  Windows presentation interposition in this build (POSIX-only).\n"
        "  Arbitrary engine draw-call optimization; launchers do not rewrite engines.\n"
        "  Intel Gen5 hardware Vulkan (the hardware does not provide it).\n",
        LIBREGPU_VERSION, LIBREGPU_STEP_LABEL, LIBREGPU_RELEASE);
}

static void print_doctor(FILE *out)
{
    char interposer[LIBREGPU_LAUNCH_PATH_MAX];
    const bool have_interposer = find_interposer(interposer, sizeof interposer);
    const char *display = getenv("WAYLAND_DISPLAY");
    const char *xdg = getenv("XDG_DATA_HOME");
    char profile_path[LIBREGPU_LAUNCH_PATH_MAX];

    if (!display || !display[0]) display = getenv("DISPLAY");
    if (xdg && xdg[0]) {
        (void)snprintf(profile_path, sizeof profile_path,
                       "%s/LibreGPU/LibreGPU/profiles.txt", xdg);
    } else {
        const char *home = getenv("HOME");
        (void)snprintf(profile_path, sizeof profile_path,
                       "%s/.local/share/LibreGPU/LibreGPU/profiles.txt",
                       (home && home[0]) ? home : ".");
    }

    fprintf(out, "LIBREGPU %s (%s, %s) installation check\n\n",
            LIBREGPU_VERSION, LIBREGPU_STEP_LABEL, LIBREGPU_RELEASE);

    fprintf(out, "Build\n");
    fprintf(out, "  core backend         simulated (always available)\n");
    fprintf(out, "  real GPU backend     OpenGL 2.1 optional target (libregpu-gpu)\n");
    fprintf(out, "  graphics interposer  %s\n",
            have_interposer ? interposer : "not found (optional)");
#if defined(__linux__)
    {
        struct utsname u;
        if (uname(&u) == 0)
            fprintf(out, "  host                 %s %s (%s)\n",
                    u.sysname, u.release, u.machine);
    }
#endif
#if !defined(_WIN32)
    {
        const long cpus = sysconf(_SC_NPROCESSORS_ONLN);
        const long pages = sysconf(_SC_PHYS_PAGES);
        const long page_size = sysconf(_SC_PAGESIZE);
        if (cpus > 0) fprintf(out, "  processors           %ld\n", cpus);
        if (pages > 0 && page_size > 0)
            fprintf(out, "  memory               %ld MiB\n",
                    (pages / 1024) * (page_size / 1024));
    }
#endif
    fprintf(out, "\nRuntime\n");
    fprintf(out, "  display              %s\n",
            (display && display[0]) ? display : "not set (libregpu needs one)");
#if !defined(_WIN32)
    if (access(profile_path, R_OK) == 0)
        fprintf(out, "  profiles             %s\n", profile_path);
    else
        fprintf(out, "  profiles             %s (not created yet)\n", profile_path);
    if (access("/dev/dri", R_OK) == 0)
        fprintf(out, "  DRM devices          /dev/dri is readable\n");
    else
        fprintf(out, "  DRM devices          /dev/dri is not readable (software rendering)\n");
#else
    fprintf(out, "  profiles             %s\n", profile_path);
#endif

    fprintf(out, "\nResult\n");
    if (!have_interposer) {
        fprintf(out,
            "  core: OK. Presentation interposition is not installed; build it with\n"
            "  `make interpose`, install it with `make install`, or set LIBREGPU_INTERPOSER.\n");
    } else {
        fprintf(out, "  core: OK. Presentation interposer: OK.\n");
    }
    if (!display || !display[0]) {
        fprintf(out,
            "  The graphical launcher needs WAYLAND_DISPLAY or DISPLAY; the CLI does not.\n");
    }
}



static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

typedef struct { const LibreGPURuntime *rt; bool quiet; } CycleUi;

static void on_cycle(const LibreGPUCycleInfo *info, void *user)
{
    const CycleUi *ui = (const CycleUi *)user;
    if (!ui->quiet) ui_show_cycle(stdout, ui->rt->cycle, info);
}


static LibreGPUBackend *create_backend(const LibreGPULauncherSettings *s, FILE *err)
{
    if (s->demo) {
        LibreGPUBackend *be = libregpu_sim_backend_create(2);        
        if (!be) { ui_show_error(err, "simulated backend", LIBREGPU_ERR_NO_MEMORY); return NULL; }
        LibreGPUStatus st = libregpu_sim_demo_scenario(be, s->demo_frames);
        if (st != LIBREGPU_OK) {
            ui_show_error(err, "demo scenario", st);
            libregpu_backend_destroy(be);
            return NULL;
        }
        return be;
    }
    fprintf(err,
            "error: cannot connect '%s': no native graphics backend is compiled into this build.\n"
            "       Implement the LibreGPUBackend interface for the target API (see lib.h, section\n"
            "       'Graphics API Execution Bridge') and register it in create_backend(),\n"
            "       or use --demo to run the simulated backend.\n", s->game_path);
    return NULL;
}

int main(int argc, char **argv)
{
    LibreGPULauncherSettings s;
    char err[200] = "";

    if (ui_parse_args(&s, argc, argv, err, sizeof err) != LIBREGPU_OK) {
        fprintf(stderr, "error: %s\n", err);
        ui_print_usage(stderr, argc > 0 ? argv[0] : "libregpu-core");
        return 2;
    }
    if (s.help || (!s.demo && !s.selftest && !s.compat && !s.doctor &&
                   !s.game_selected)) {
        ui_print_banner(stdout);
        ui_print_usage(s.help ? stdout : stderr, argc > 0 ? argv[0] : "libregpu-core");
        return s.help ? 0 : 2;
    }
    if (s.compat) {
        print_compat(stdout);
        return 0;
    }
    if (s.doctor) {
        print_doctor(stdout);
        return 0;
    }
    if (s.selftest) return libregpu_selftest(stdout) == 0 ? 0 : 1;

    


    if (s.game_selected && !s.demo) {
        if (!s.quiet) ui_print_banner(stdout);
#if !defined(_WIN32)
        return run_interposed(&s, stderr);
#else
        fprintf(stderr,
                "error: the presentation interposer is POSIX-only in this build;\n"
                "       no engine-facing backend is compiled in.\n");
        return 3;
#endif
    }

    if (!s.quiet) ui_print_banner(stdout);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    LibreGPUBackend *backend = create_backend(&s, stderr);
    if (!backend) return 3;

    int rc = 0;
    LibreGPURuntime rt;
    LibreGPUStatus st = libregpu_runtime_init(&rt, &s.config, backend);
    if (st != LIBREGPU_OK) {
        ui_show_error(stderr, "runtime init", st);
        libregpu_backend_destroy(backend);
        return 4;
    }

    st = libregpu_runtime_start(&rt);
    if (st != LIBREGPU_OK) {
        ui_show_error(stderr, "runtime start", st);
        rc = 4;
    } else {
        LibreGPURuntimeStatus status;
        libregpu_runtime_status(&rt, &status);
        if (!s.quiet) ui_show_status(stdout, &status);

        CycleUi ui = { &rt, s.quiet };
        st = libregpu_runtime_run(&rt, &g_stop, s.demo , on_cycle, &ui);
        if (st != LIBREGPU_OK) { ui_show_error(stderr, "runtime", st); rc = 5; }

        libregpu_runtime_status(&rt, &status);
        if (!s.quiet) ui_show_status(stdout, &status);
    }

    if (!s.quiet) printf("\n");
    libregpu_runtime_stop(&rt);                       
    libregpu_metrics_report(&rt.metrics, &rt.gov, stdout);
    if (s.demo) printf("  (backend: simulated; its GPU time is a cost model, not hardware measurement)\n");

    libregpu_backend_destroy(backend);
    return rc;
}
