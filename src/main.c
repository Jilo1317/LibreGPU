













#define LIBREGPU_LIB_DECLARATIONS_ONLY
#define LIBREGPU_UI_DECLARATIONS_ONLY
#include "lib.c"
#include "ui.c"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>



LibreGPUBackend *libregpu_sim_backend_create(uint32_t fence_latency);
LibreGPUStatus   libregpu_sim_demo_scenario(LibreGPUBackend *be, size_t frames);

static volatile sig_atomic_t g_stop = 0;

int libregpu_gui_main(int argc, char **argv);

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

    for (int i = 1; i < argc; i++) {
        if (argv[i] && !strcmp(argv[i], "--gui")) {
            for (int j = i; j < argc - 1; j++) argv[j] = argv[j + 1];
            return libregpu_gui_main(argc - 1, argv);
        }
        if (argv[i] && !strcmp(argv[i], "--version")) {
            printf("%s %s (%s)\n", LIBREGPU_CODE_NAME, LIBREGPU_VERSION, LIBREGPU_STEP_LABEL);
            return 0;
        }
    }

    if (ui_parse_args(&s, argc, argv, err, sizeof err) != LIBREGPU_OK) {
        fprintf(stderr, "error: %s\n", err);
        ui_print_usage(stderr, argc > 0 ? argv[0] : "libregpu");
        return 2;
    }
    if (s.help || (!s.demo && !s.selftest && !s.game_selected)) {
        ui_print_banner(stdout);
        ui_print_usage(s.help ? stdout : stderr, argc > 0 ? argv[0] : "libregpu");
        return s.help ? 0 : 2;
    }
    if (s.selftest) return libregpu_selftest(stdout) == 0 ? 0 : 1;

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
