











#define LIBREGPU_LIB_DECLARATIONS_ONLY
#define LIBREGPU_UI_DECLARATIONS_ONLY
#include "lib.c"
#include "ui.c"           

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>





























void libregpu_config_defaults(LibreGPUConfig *c)
{
    memset(c, 0, sizeof *c);
    c->mode = LIBREGPU_MODE_OPTIMIZE;
    c->target_loops_per_second = 0.0;
    c->enable_micro_pixel = true;
    c->enable_camera = true;
    c->camera_max_reuse_angle = 0.35f;
    c->camera_position_epsilon = 1e-4f;
    c->camera_roll_epsilon = 1e-4f;
    c->camera_shift_tolerance_px = 1e-3f;
    c->exposed_margin_px = 2;
    c->micro_pixel_max_area_fraction = 0.5f;
    c->baseline_sample_interval = 0;
    c->full_refresh_interval = 600;
    c->vram_budget_bytes = 0;
}

LibreGPUStatus libregpu_config_validate(const LibreGPUConfig *c)
{
    if (!c) return LIBREGPU_ERR_INVALID_ARG;
    if ((unsigned)c->mode > (unsigned)LIBREGPU_MODE_OPTIMIZE) return LIBREGPU_ERR_INVALID_ARG;
    if (!isfinite(c->target_loops_per_second) || c->target_loops_per_second < 0.0 ||
        (c->target_loops_per_second > 0.0 && c->target_loops_per_second < LIBREGPU_GOV_MIN_HZ) ||
        c->target_loops_per_second > LIBREGPU_GOV_MAX_HZ) return LIBREGPU_ERR_INVALID_ARG;
    if (!isfinite(c->camera_max_reuse_angle) || c->camera_max_reuse_angle < 0.0f ||
        c->camera_max_reuse_angle > 1.5f) return LIBREGPU_ERR_INVALID_ARG;
    if (!isfinite(c->camera_position_epsilon) || c->camera_position_epsilon < 0.0f) return LIBREGPU_ERR_INVALID_ARG;
    if (!isfinite(c->camera_roll_epsilon) || c->camera_roll_epsilon < 0.0f) return LIBREGPU_ERR_INVALID_ARG;
    if (!isfinite(c->camera_shift_tolerance_px) || c->camera_shift_tolerance_px < 0.0f ||
        c->camera_shift_tolerance_px > 0.5f) return LIBREGPU_ERR_INVALID_ARG;
    if (c->exposed_margin_px < 0 || c->exposed_margin_px > 4096) return LIBREGPU_ERR_INVALID_ARG;
    if (!isfinite(c->micro_pixel_max_area_fraction) || c->micro_pixel_max_area_fraction < 0.0f ||
        c->micro_pixel_max_area_fraction > 1.0f) return LIBREGPU_ERR_INVALID_ARG;
    return LIBREGPU_OK;
}

LibreGPUStatus libregpu_backend_validate(const LibreGPUBackend *be)
{
    if (!be) return LIBREGPU_ERR_INVALID_ARG;
    if (!be->session_valid || !be->next_dataset || !be->submit ||
        !be->completed_fence || !be->wait_idle || !be->destroy_resource)
        return LIBREGPU_ERR_INVALID_ARG;                    
    if (be->caps.supports_gpu_timing && !be->gpu_time_ns) return LIBREGPU_ERR_INVALID_ARG;
    if (be->caps.supports_reproject && !be->create_scratch) return LIBREGPU_ERR_INVALID_ARG;
    return LIBREGPU_OK;
}

void libregpu_backend_destroy(LibreGPUBackend *be)
{
    if (be && be->destroy) be->destroy(be);
}

const char *libregpu_runtime_state_str(LibreGPURuntimeState s)
{
    switch (s) {
    case LIBREGPU_RT_IDLE:    return "idle";
    case LIBREGPU_RT_RUNNING: return "running";
    case LIBREGPU_RT_STOPPED: return "stopped";
    case LIBREGPU_RT_FAILED:  return "failed";
    }
    return "?";
}

LibreGPUStatus libregpu_runtime_init(LibreGPURuntime *rt, const LibreGPUConfig *cfg, LibreGPUBackend *backend)
{
    if (!rt) return LIBREGPU_ERR_INVALID_ARG;
    memset(rt, 0, sizeof *rt);
    LibreGPUStatus st = libregpu_config_validate(cfg);
    if (st != LIBREGPU_OK) return st;
    st = libregpu_backend_validate(backend);
    if (st != LIBREGPU_OK) return st;

    rt->config = *cfg;
    rt->backend = backend;
    libregpu_metrics_init(&rt->metrics);
    st = mem_init(&rt->mem, backend, cfg->vram_budget_bytes);
    if (st != LIBREGPU_OK) { memset(rt, 0, sizeof *rt); return st; }
    st = gov_init(&rt->gov, cfg->target_loops_per_second);
    if (st != LIBREGPU_OK) { mem_shutdown(&rt->mem); memset(rt, 0, sizeof *rt); return st; }
    return LIBREGPU_OK;
}

LibreGPUStatus libregpu_runtime_start(LibreGPURuntime *rt)
{
    if (!rt || rt->state != LIBREGPU_RT_IDLE || !rt->backend) return LIBREGPU_ERR_STATE;
    if (rt->backend->attach) {
        LibreGPUStatus st = rt->backend->attach(rt->backend->ctx);
        if (st != LIBREGPU_OK) { rt->state = LIBREGPU_RT_FAILED; return st; }   
    }
    rt->state = LIBREGPU_RT_RUNNING;
    gov_init(&rt->gov, rt->config.target_loops_per_second);
    return LIBREGPU_OK;
}

static void invalidate_baseline(LibreGPURuntime *rt)
{
    if (rt->have_baseline) rt->metrics.baseline_losses++;
    gir_release(&rt->gir_x);
    rt->have_baseline = false;
    mem_sync_owner_refs(&rt->mem, LIBREGPU_OWNER_X, NULL);       
    mem_sync_owner_refs(&rt->mem, LIBREGPU_OWNER_Y, NULL);
    mem_sync_owner_refs(&rt->mem, LIBREGPU_OWNER_Z, NULL);
}

void libregpu_runtime_stop(LibreGPURuntime *rt)
{
    if (!rt || !rt->backend || rt->state == LIBREGPU_RT_STOPPED) return;   
    const bool attached = (rt->state == LIBREGPU_RT_RUNNING);
    if (rt->backend->wait_idle) rt->backend->wait_idle(rt->backend->ctx);
    if (rt->backend->error_count) rt->metrics.lifetime_errors = rt->backend->error_count(rt->backend->ctx);
    invalidate_baseline(rt);
    mem_collect(&rt->mem);
    mem_shutdown(&rt->mem);                                   
    if (attached && rt->backend->detach) rt->backend->detach(rt->backend->ctx);
    rt->state = LIBREGPU_RT_STOPPED;
}

void libregpu_runtime_status(const LibreGPURuntime *rt, LibreGPURuntimeStatus *out)
{
    if (!out) return;
    memset(out, 0, sizeof *out);
    if (!rt || !rt->backend) return;
    const LibreGPUBackend *be = rt->backend;
    out->state = rt->state;
    out->graphics_path_active = rt->state == LIBREGPU_RT_RUNNING;
    out->session_valid = be->session_valid(be->ctx);
    out->baseline_exists = rt->have_baseline;
    out->y_in_progress = rt->processing_y;
    out->z_constructed = rt->last_cycle_z_built;
    out->optimizing = rt->last_cycle_optimized;
    out->gpu_in_flight = be->completed_fence(be->ctx) < rt->last_fence;
}




static bool track_dataset(LibreGPURuntime *rt, const LibreGPURawDataset *raw)
{
    if (mem_track_raw(&rt->mem, raw) == LIBREGPU_OK) return true;
    rt->metrics.bookkeeping_errors++;
    return false;
}



static void request_destroys(LibreGPURuntime *rt, const LibreGPURawDataset *raw)
{
    for (size_t i = 0; i < raw->op_count; i++) {
        const LibreGPURawOp *op = &raw->ops[i];
        if (op->kind != LIBREGPU_OP_RES_DESTROY) continue;
        for (uint8_t k = 0; k < op->write_count; k++)
            if (mem_request_release(&rt->mem, op->writes[k]) != LIBREGPU_OK)
                rt->metrics.bookkeeping_errors++;
    }
}



static bool promote(LibreGPURuntime *rt, GirDataset *y)
{
    if (mem_sync_owner_refs(&rt->mem, LIBREGPU_OWNER_X, y) != LIBREGPU_OK) {
        rt->metrics.bookkeeping_errors++;
        invalidate_baseline(rt);
        gir_release(y);
        return false;
    }
    gir_release(&rt->gir_x);
    rt->gir_x = *y;
    memset(y, 0, sizeof *y);
    rt->have_baseline = true;
    mem_sync_owner_refs(&rt->mem, LIBREGPU_OWNER_Y, NULL);
    mem_sync_owner_refs(&rt->mem, LIBREGPU_OWNER_Z, NULL);
    return true;
}

static void sample_vram(LibreGPURuntime *rt)
{
    LibreGPUMemStats ms;
    mem_get_stats(&rt->mem, &ms);
    rt->metrics.vram_sum += ms.bytes_tracked;
    rt->metrics.vram_samples++;
    if (ms.bytes_tracked_peak > rt->metrics.vram_peak) rt->metrics.vram_peak = ms.bytes_tracked_peak;
    if (ms.libregpu_bytes_peak > rt->metrics.libregpu_vram_peak) rt->metrics.libregpu_vram_peak = ms.libregpu_bytes_peak;
}

static void finish_cycle(LibreGPURuntime *rt, LibreGPUCycleInfo *info, uint64_t t0, uint64_t fence,
                         bool forced_baseline)
{
    const LibreGPUBackend *be = rt->backend;
    LibreGPUMetrics *m = &rt->metrics;
    info->cpu_ns = libregpu_now_ns() - t0;
    if (be->caps.supports_gpu_timing && be->gpu_time_ns) info->gpu_ns = be->gpu_time_ns(be->ctx, fence);

    m->cycles++;
    m->ops_total += info->ops_total;
    m->ops_submitted += info->ops_submitted;
    m->dirty_pixels_total = libregpu_sat_add_u64(m->dirty_pixels_total, info->dirty_pixels);
    m->cpu_ns_total += info->cpu_ns;
    if (info->cpu_ns > m->cpu_ns_max) m->cpu_ns_max = info->cpu_ns;
    if (info->gpu_ns) {
        if (info->submitted_mode == LIBREGPU_Z_INCREMENTAL || info->submitted_mode == LIBREGPU_Z_NO_IMPACT) {
            m->gpu_ns_incremental += info->gpu_ns; m->gpu_samples_incremental++;
        } else {
            m->gpu_ns_ordinary += info->gpu_ns; m->gpu_samples_ordinary++;
            if (forced_baseline) { m->gpu_ns_baseline += info->gpu_ns; m->gpu_samples_baseline++; }
        }
    }
    libregpu_metrics_note_cycle_time(m, libregpu_now_ns());
    if (be->presented_count) gov_note_presented(&rt->gov, be->presented_count(be->ctx));
    rt->last_fence = fence;
    mem_collect(&rt->mem);
    sample_vram(rt);
}




static LibreGPUStatus run_ordinary(LibreGPURuntime *rt, const LibreGPURawDataset *raw, LibreGPUCycleInfo *info,
                                uint64_t t0, const char *reason)
{
    LibreGPUZPlan plan;
    LibreGPURebuildResult rr;
    memset(&plan, 0, sizeof plan);                         
    if (rt->have_baseline) info->baseline_lost = true;
    invalidate_baseline(rt);
    track_dataset(rt, raw);                                
    info->reason = reason;
    info->decision = LIBREGPU_STREAM_FALLBACK_ORDINARY;

    LibreGPUStatus st = dataset_rebuilder_execute(&plan, raw, rt->backend, NULL, &rr);
    if (st != LIBREGPU_OK) return st;                         
    mem_note_raw_submission(&rt->mem, raw, rr.fence);      
    request_destroys(rt, raw);
    info->submitted_mode = LIBREGPU_Z_ORDINARY;
    info->ops_submitted = rr.ops_submitted;
    rt->metrics.fallback_cycles++;
    finish_cycle(rt, info, t0, rr.fence, false);
    return LIBREGPU_OK;
}


LibreGPUStatus libregpu_runtime_step(LibreGPURuntime *rt, LibreGPUCycleInfo *info)
{
    LibreGPUCycleInfo local;
    if (!info) info = &local;
    memset(info, 0, sizeof *info);
    if (!rt || rt->state != LIBREGPU_RT_RUNNING) return LIBREGPU_ERR_STATE;
    LibreGPUBackend *be = rt->backend;

    
    if (!be->session_valid(be->ctx)) {
        if (rt->have_baseline) info->baseline_lost = true;
        invalidate_baseline(rt);
        info->session_invalid = true;
        return LIBREGPU_ERR_SESSION_INVALID;
    }

    LibreGPURawDataset raw;
    libregpu_dataset_init(&raw);
    LibreGPUStatus st = be->next_dataset(be->ctx, &raw);
    if (st != LIBREGPU_OK) { libregpu_dataset_release(&raw); return st; }

    const uint64_t t0 = libregpu_now_ns();
    info->had_dataset = true;
    info->ops_total = raw.op_count;
    rt->cycle++;
    rt->processing_y = true;
    rt->last_cycle_z_built = false;
    rt->last_cycle_optimized = false;

    
    if (raw.corrupt) {
        rt->metrics.corrupt_datasets++;
        if (rt->have_baseline) info->baseline_lost = true;
        invalidate_baseline(rt);
        info->reason = "corrupt dataset: not optimised, not re-submitted";
        rt->processing_y = false;
        libregpu_dataset_release(&raw);
        return LIBREGPU_ERR_VALIDATION;
    }

    double dt = 0.0;                                       
    if (raw.timestamp_ns && rt->last_dataset_ts_ns && raw.timestamp_ns > rt->last_dataset_ts_ns)
        dt = (double)(raw.timestamp_ns - rt->last_dataset_ts_ns) * 1e-9;
    rt->last_dataset_ts_ns = raw.timestamp_ns;
    rt->last_dataset_ns = raw.timestamp_ns;                 

    GirDataset y;
    GirDelta delta;
    LibreGPUStreamlineResult sr;
    LibreGPURebuildResult rr;
    LibreGPUResourceId scratch = { 0, 0 };
    bool has_scratch = false, forced_baseline = false;
    const char *bail = NULL;
    LibreGPUStatus result = LIBREGPU_OK;
    memset(&y, 0, sizeof y);
    memset(&delta, 0, sizeof delta);
    memset(&sr, 0, sizeof sr);
    memset(&rr, 0, sizeof rr);

    
    const bool tracked = track_dataset(rt, &raw);
    st = gir_build(&y, &raw);
    if (st != LIBREGPU_OK) { bail = "representation failed"; goto ordinary; }
    if (!tracked)       { bail = "bookkeeping failed";    goto ordinary; }

    
    if (!rt->have_baseline) {
        LibreGPUZPlan plan;
        memset(&plan, 0, sizeof plan);
        st = dataset_rebuilder_execute(&plan, &raw, be, NULL, &rr);
        if (st != LIBREGPU_OK) { result = st; goto done; }
        mem_note_raw_submission(&rt->mem, &raw, rr.fence);
        request_destroys(rt, &raw);
        const bool kept = promote(rt, &y);
        info->was_baseline = true;
        info->baseline_lost = !kept;
        info->decision = LIBREGPU_STREAM_FALLBACK_ORDINARY;
        info->submitted_mode = LIBREGPU_Z_ORDINARY;
        info->ops_submitted = rr.ops_submitted;
        info->reason = kept ? "initial baseline" : "baseline not retained: bookkeeping failed";
        rt->metrics.baseline_cycles++;
        finish_cycle(rt, info, t0, rr.fence, false);
        goto done;
    }

    
    if (mem_sync_owner_refs(&rt->mem, LIBREGPU_OWNER_Y, &y) != LIBREGPU_OK) {
        rt->metrics.bookkeeping_errors++;
        bail = "bookkeeping failed";
        goto ordinary;
    }
    st = gir_compare(&rt->gir_x, &y, &delta);
    if (st == LIBREGPU_OK) {
        st = streamline_decide(&rt->config, &be->caps, &rt->gir_x, &y, &delta, dt, &sr);
        if (st != LIBREGPU_OK) { streamline_result_release(&sr); memset(&sr, 0, sizeof sr);
                              streamline_force_ordinary(&sr, "decision failed"); }
    } else {
        gir_delta_release(&delta);
        memset(&sr, 0, sizeof sr);
        streamline_force_ordinary(&sr, "comparison failed");
    }
    info->decision = sr.action;
    info->micro_reason = sr.micro_reason;
    info->camera_reason = sr.camera_reason;
    info->changes = delta.change_count;
    info->record_changes_per_second = sr.record_changes_per_second;
    info->changes_per_second = info->record_changes_per_second;   
    info->reason = sr.fallback_reason ? sr.fallback_reason
                                      : (sr.micro_pixel ? sr.micro_reason : sr.camera_reason);
    if (sr.plan.mode != LIBREGPU_Z_ORDINARY) rt->metrics.would_optimize_cycles++;

    
    {
        const LibreGPUConfig *cfg = &rt->config;
        const char *force = NULL;
        if (cfg->mode != LIBREGPU_MODE_OPTIMIZE)
            force = "decision-only mode";
        else if (cfg->baseline_sample_interval && rt->cycle % cfg->baseline_sample_interval == 0)
            { force = "baseline GPU sample"; forced_baseline = true; }
        else if (cfg->full_refresh_interval && rt->cycle % cfg->full_refresh_interval == 0)
            force = "periodic full refresh";
        if (force && sr.plan.mode != LIBREGPU_Z_ORDINARY) {
            streamline_force_ordinary(&sr, force);
            info->reason = force;
        } else {
            forced_baseline = false;
        }
    }
    if (mem_sync_owner_refs(&rt->mem, LIBREGPU_OWNER_Z, &y) != LIBREGPU_OK) {   
        rt->metrics.bookkeeping_errors++;
        bail = "bookkeeping failed";
        goto ordinary;
    }

    
    if (sr.plan.mode == LIBREGPU_Z_INCREMENTAL && sr.plan.reproject) {
        const uint64_t bytes = (uint64_t)sr.plan.reprojection.surface_w *
                               (uint64_t)sr.plan.reprojection.surface_h * 4u;
        const LibreGPUStatus sst = mem_acquire_scratch(&rt->mem, bytes, &scratch);
        if (sst == LIBREGPU_OK) {
            has_scratch = true;
        } else {
            

            const char *why = (sst == LIBREGPU_ERR_NO_MEMORY)
                                  ? "no VRAM for reprojection target"
                                  : "reprojection target unavailable";
            streamline_force_ordinary(&sr, why);
            info->reason = why;
        }
    }

    
    st = dataset_rebuilder_execute(&sr.plan, &raw, be, has_scratch ? &scratch : NULL, &rr);
    if (st != LIBREGPU_OK && sr.plan.mode != LIBREGPU_Z_ORDINARY) {
        rt->metrics.failed_rebuilds++;
        if (has_scratch) { mem_request_release(&rt->mem, scratch); has_scratch = false; }
        if (st == LIBREGPU_ERR_BACKEND) {                     
            info->baseline_lost = true;
            info->reason = "backend failed with unknown state";
            invalidate_baseline(rt);
            result = st;
            goto done;
        }
        info->fell_back_after_failure = true;              
        info->reason = "rebuild rejected; ordinary path used";
        streamline_force_ordinary(&sr, info->reason);
        st = dataset_rebuilder_execute(&sr.plan, &raw, be, NULL, &rr);
    }
    if (st != LIBREGPU_OK) {
        if (has_scratch) { mem_request_release(&rt->mem, scratch); has_scratch = false; }
        if (st == LIBREGPU_ERR_BACKEND) {                     
            info->baseline_lost = true;
            invalidate_baseline(rt);
        } else {                                           
            mem_sync_owner_refs(&rt->mem, LIBREGPU_OWNER_Y, NULL);
            mem_sync_owner_refs(&rt->mem, LIBREGPU_OWNER_Z, NULL);
        }
        result = st;
        goto done;
    }

    
    {
        const LibreGPUZMode mode = sr.plan.mode;
        info->submitted_mode = mode;
        info->ops_submitted = rr.ops_submitted;
        info->dirty_pixels = (mode == LIBREGPU_Z_INCREMENTAL) ? sr.dirty_pixels : 0;
        rt->last_cycle_optimized = (mode != LIBREGPU_Z_ORDINARY);
        rt->last_cycle_z_built = (mode != LIBREGPU_Z_ORDINARY);       
        switch (mode) {
        case LIBREGPU_Z_INCREMENTAL: rt->metrics.incremental_cycles++; break;
        case LIBREGPU_Z_NO_IMPACT:   rt->metrics.no_impact_cycles++;   break;
        case LIBREGPU_Z_ORDINARY:    rt->metrics.fallback_cycles++;    break;
        }
    }
    mem_note_raw_submission(&rt->mem, &raw, rr.fence);
    if (has_scratch) {
        mem_note_use(&rt->mem, scratch, rr.fence);
        mem_request_release(&rt->mem, scratch);            
        has_scratch = false;
    }
    request_destroys(rt, &raw);
    if (!promote(rt, &y)) info->baseline_lost = true;      
    finish_cycle(rt, info, t0, rr.fence, forced_baseline);
    goto done;

ordinary:
    if (has_scratch) { mem_request_release(&rt->mem, scratch); has_scratch = false; }
    result = run_ordinary(rt, &raw, info, t0, bail);

done:
    streamline_result_release(&sr);
    gir_delta_release(&delta);
    gir_release(&y);
    libregpu_dataset_release(&raw);
    rt->processing_y = false;
    return result;
}

LibreGPUStatus libregpu_runtime_run(LibreGPURuntime *rt, volatile sig_atomic_t *stop, bool stop_when_idle,
                              LibreGPUCycleCallback cb, void *user)
{
    if (!rt || rt->state != LIBREGPU_RT_RUNNING) return LIBREGPU_ERR_STATE;
    while (!stop || !*stop) {
        LibreGPUCycleInfo info;
        LibreGPUStatus st = libregpu_runtime_step(rt, &info);
        if (st == LIBREGPU_NO_DATA) {
            if (stop_when_idle) return LIBREGPU_OK;
            libregpu_sleep_ns(1000000ULL);                    
            continue;
        }
        if (st == LIBREGPU_ERR_SESSION_INVALID) { libregpu_sleep_ns(5000000ULL); continue; }
        if (st == LIBREGPU_ERR_VALIDATION) continue;          
        if (st != LIBREGPU_OK) return st;
        if (cb) cb(&info, user);
        gov_wait(&rt->gov);                                
    }
    return LIBREGPU_OK;
}

















static bool g_armed = false;
static bool g_paused = false;
static long g_fail_index = -1;
static long g_count = 0;

void libregpu_alloc_fault_arm(long fail_index)
{
    g_armed = true;
    g_paused = false;
    g_fail_index = fail_index;
    g_count = 0;
}

void libregpu_alloc_fault_pause(bool paused)
{
    g_paused = paused;
}

void libregpu_alloc_fault_disarm(void)
{
    g_armed = false;
    g_paused = false;
}

long libregpu_alloc_count(void)
{
    return g_count;
}


static bool should_fail(void)
{
    if (!g_armed || g_paused) return false;
    const long idx = g_count++;
    return g_fail_index >= 0 && idx == g_fail_index;
}

void *libregpu_malloc(size_t n)
{
    if (should_fail()) return NULL;
    return malloc(n);
}

void *libregpu_calloc(size_t count, size_t size)
{
    if (should_fail()) return NULL;
    return calloc(count, size);
}

void *libregpu_realloc(void *p, size_t n)
{
    if (should_fail()) return NULL;     
    return realloc(p, n);
}


































#define PIXELS (SIM_W * SIM_H)

typedef struct {
    uint32_t fb[PIXELS];
    uint32_t display[PIXELS];
    uint64_t shadow_value;
    uint64_t cost;
    uint64_t presented;
} SimGpu;

typedef struct { uint64_t key; bool alive; uint64_t last_use_fence; } SimRes;

typedef struct {
    LibreGPURawDataset *queue;
    size_t q_head, q_count, q_cap;
    SimGpu gpu;
    uint64_t submitted, completed;
    uint32_t latency;
    SimRes *res;
    size_t res_count, res_cap;
    uint64_t last_cost, last_cost_fence;
    bool session_valid, attached;
    uint64_t violations, destroyed;
    SimSubmitFault submit_fault;
    uint32_t destroy_faults;
} SimBackend;



typedef struct { LibreGPUBackend base; SimBackend sim; } SimHandle;
_Static_assert(offsetof(SimHandle, base) == 0, "LibreGPUBackend must be the first member of SimHandle");




static void sim_pixel_op(SimGpu *g, const LibreGPURawOp *op, const LibreGPURect *scissor)
{
    switch (op->kind) {
    case LIBREGPU_OP_DRAW:
    case LIBREGPU_OP_DISPATCH:
    case LIBREGPU_OP_RES_COPY: {
        bool writes_target = false, reads_shadow = false;
        for (uint8_t i = 0; i < op->write_count; i++) {
            if (op->writes[i].id == SIM_TARGET_ID) writes_target = true;
            if (op->writes[i].id == SIM_SHADOW_ID) g->shadow_value = op->payload_hash;
        }
        for (uint8_t i = 0; i < op->read_count; i++)
            if (op->reads[i].id == SIM_SHADOW_ID) reads_shadow = true;
        g->cost += 400;
        if (!writes_target) break;

        const bool clears = (op->flags & LIBREGPU_OPF_CLEARS_TARGET) != 0;
        LibreGPURect r = clears ? libregpu_rect_make(0, 0, SIM_W, SIM_H) : op->region;
        r = libregpu_rect_intersect(r, libregpu_rect_make(0, 0, SIM_W, SIM_H));
        if (scissor) r = libregpu_rect_intersect(r, *scissor);
        uint32_t color = (uint32_t)(op->payload_hash >> 16) | 0xFF000000u;
        if (reads_shadow) color ^= (uint32_t)g->shadow_value;
        for (int32_t y = r.y0; y < r.y1; y++) {
            for (int32_t x = r.x0; x < r.x1; x++) {
                uint32_t *px = &g->fb[y * SIM_W + x];
                *px = clears ? color : (*px * 1664525u + color + 1013904223u);   
            }
        }
        g->cost += (uint64_t)libregpu_rect_area(r) * 8u;
        break;
    }
    case LIBREGPU_OP_PRESENT:
        memcpy(g->display, g->fb, sizeof g->display);
        g->presented++;
        g->cost += 200;
        break;
    default:
        g->cost += 50;
        break;
    }
}

static void sim_reproject(SimGpu *g, int32_t sx, int32_t sy)
{
    uint32_t tmp[PIXELS];
    memcpy(tmp, g->fb, sizeof tmp);
    for (int32_t y = 0; y < SIM_H; y++) {
        for (int32_t x = 0; x < SIM_W; x++) {
            const int32_t ox = x - sx, oy = y - sy;
            if (ox >= 0 && ox < SIM_W && oy >= 0 && oy < SIM_H) g->fb[y * SIM_W + x] = tmp[oy * SIM_W + ox];
        }
    }
    g->cost += (uint64_t)PIXELS * 2u;
}

LibreGPUStatus libregpu_sim_render_reference(const LibreGPURawDataset *ds, uint32_t *fb_out)
{
    if (!ds || !fb_out) return LIBREGPU_ERR_INVALID_ARG;
    SimGpu *g = (SimGpu *)calloc(1, sizeof *g);
    if (!g) return LIBREGPU_ERR_NO_MEMORY;
    for (size_t i = 0; i < ds->op_count; i++) sim_pixel_op(g, &ds->ops[i], NULL);
    memcpy(fb_out, g->display, sizeof g->display);
    free(g);
    return LIBREGPU_OK;
}




static SimRes *sim_res_find(SimBackend *s, LibreGPUResourceId id)
{
    const uint64_t key = libregpu_res_key(id);
    for (size_t i = 0; i < s->res_count; i++) if (s->res[i].key == key) return &s->res[i];
    return NULL;
}

static SimRes *sim_res_add(SimBackend *s, LibreGPUResourceId id)
{
    if (s->res_count == s->res_cap) {
        size_t nc = s->res_cap ? s->res_cap * 2 : 32;
        SimRes *n = (SimRes *)realloc(s->res, nc * sizeof *n);
        if (!n) return NULL;
        s->res = n; s->res_cap = nc;
    }
    SimRes *r = &s->res[s->res_count++];
    r->key = libregpu_res_key(id); r->alive = true; r->last_use_fence = 0;
    return r;
}

static void sim_import(SimBackend *s, uint32_t id)            
{
    LibreGPUResourceId rid = { id, 1 };
    if (!sim_res_find(s, rid)) sim_res_add(s, rid);
}

static void sim_touch(SimBackend *s, LibreGPUResourceId id, uint64_t fence)
{
    SimRes *r = sim_res_find(s, id);
    if (!r) {                                  
        s->violations++;
        r = sim_res_add(s, id);                
        if (!r) return;
    }
    if (!r->alive) s->violations++;            
    r->last_use_fence = fence;
}

static void sim_exec_op(SimBackend *s, const LibreGPURawOp *op, const LibreGPURect *scissor, uint64_t fence)
{
    if (op->kind == LIBREGPU_OP_RES_DESTROY) return;                 
    if (op->kind == LIBREGPU_OP_RES_CREATE) {
        for (uint8_t i = 0; i < op->write_count; i++) {
            SimRes *r = sim_res_find(s, op->writes[i]);
            if (r && r->alive) s->violations++;                   
            if (!r) r = sim_res_add(s, op->writes[i]);
            if (r) { r->alive = true; r->last_use_fence = fence; }
        }
    } else {
        for (uint8_t i = 0; i < op->read_count; i++)  sim_touch(s, op->reads[i], fence);
        for (uint8_t i = 0; i < op->write_count; i++) sim_touch(s, op->writes[i], fence);
    }
    sim_pixel_op(&s->gpu, op, scissor);
}




static LibreGPUStatus sim_attach(void *ctx) { ((SimBackend *)ctx)->attached = true; return LIBREGPU_OK; }
static void sim_detach(void *ctx)        { ((SimBackend *)ctx)->attached = false; }
static bool sim_session_valid(void *ctx) { return ((SimBackend *)ctx)->session_valid; }

static LibreGPUStatus sim_next_dataset(void *ctx, LibreGPURawDataset *out)
{
    SimBackend *s = (SimBackend *)ctx;
    if (s->q_count == 0) return LIBREGPU_NO_DATA;
    *out = s->queue[s->q_head];
    memset(&s->queue[s->q_head], 0, sizeof s->queue[0]);
    s->q_head = (s->q_head + 1) % s->q_cap;
    s->q_count--;
    return LIBREGPU_OK;
}

static LibreGPUStatus sim_prevalidate(const LibreGPURawDataset *ds, const LibreGPUUpdateWork *w)
{
    if (!ds || !ds->complete || ds->corrupt) return LIBREGPU_ERR_VALIDATION;
    if (!w) return LIBREGPU_OK;
    if (w->region_count > LIBREGPU_MAX_REGIONS) return LIBREGPU_ERR_VALIDATION;
    if (w->op_count > 0 && !w->ops) return LIBREGPU_ERR_VALIDATION;
    for (size_t i = 0; i < w->op_count; i++) {
        if (w->ops[i].op_index >= ds->op_count) return LIBREGPU_ERR_VALIDATION;
        if (w->ops[i].scissored && w->region_count == 0) return LIBREGPU_ERR_VALIDATION;
    }
    return LIBREGPU_OK;
}

static void sim_run(SimBackend *s, const LibreGPURawDataset *ds, const LibreGPUUpdateWork *w,
                    uint64_t fence, size_t limit_ops)
{
    if (!w) {                                                     
        for (size_t i = 0; i < ds->op_count && i < limit_ops; i++) sim_exec_op(s, &ds->ops[i], NULL, fence);
        return;
    }
    if (w->has_scratch) sim_touch(s, w->scratch, fence);
    if (w->reproject) sim_reproject(&s->gpu, w->reprojection.shift_x, w->reprojection.shift_y);
    for (size_t i = 0; i < w->op_count && i < limit_ops; i++) {
        const LibreGPUReplayOp *r = &w->ops[i];
        const LibreGPURawOp *op = &ds->ops[r->op_index];
        if (r->scissored) {
            for (uint8_t k = 0; k < w->region_count; k++) sim_exec_op(s, op, &w->regions[k], fence);
        } else {
            sim_exec_op(s, op, NULL, fence);
        }
    }
}

static LibreGPUStatus sim_submit(void *ctx, const LibreGPURawDataset *ds, const LibreGPUUpdateWork *w,
                              uint64_t *fence_out)
{
    SimBackend *s = (SimBackend *)ctx;
    if (!s->session_valid) return LIBREGPU_ERR_SESSION_INVALID;
    LibreGPUStatus st = sim_prevalidate(ds, w);                      
    if (st != LIBREGPU_OK) return st;

    if (s->submit_fault.skip > 0) {
        s->submit_fault.skip--;
    } else if (s->submit_fault.count > 0) {
        s->submit_fault.count--;
        if (s->submit_fault.status == LIBREGPU_ERR_BACKEND) {        
            const uint64_t fence = ++s->submitted;
            s->gpu.cost = 0;
            sim_run(s, ds, w, fence, (w ? w->op_count : ds->op_count) / 2);
            return LIBREGPU_ERR_BACKEND;
        }
        return s->submit_fault.status;                            
    }

    const uint64_t fence = ++s->submitted;
    s->gpu.cost = 0;
    sim_run(s, ds, w, fence, SIZE_MAX);
    s->last_cost = s->gpu.cost;
    s->last_cost_fence = fence;
    const uint64_t done = s->submitted > s->latency ? s->submitted - s->latency : 0;
    if (done > s->completed) s->completed = done;
    if (fence_out) *fence_out = fence;
    return LIBREGPU_OK;
}

static uint64_t sim_completed_fence(void *ctx) { return ((SimBackend *)ctx)->completed; }
static void sim_wait_idle(void *ctx) { SimBackend *s = (SimBackend *)ctx; s->completed = s->submitted; }

static LibreGPUStatus sim_create_scratch(void *ctx, LibreGPUResourceId id, uint64_t bytes)
{
    (void)bytes;
    SimBackend *s = (SimBackend *)ctx;
    if (sim_res_find(s, id)) return LIBREGPU_ERR_BACKEND;            
    return sim_res_add(s, id) ? LIBREGPU_OK : LIBREGPU_ERR_NO_MEMORY;
}

static LibreGPUStatus sim_destroy_resource(void *ctx, LibreGPUResourceId id)
{
    SimBackend *s = (SimBackend *)ctx;
    if (s->destroy_faults > 0) { s->destroy_faults--; return LIBREGPU_ERR_BACKEND; }   
    SimRes *r = sim_res_find(s, id);
    if (!r) return LIBREGPU_ERR_BACKEND;
    if (!r->alive) { s->violations++; return LIBREGPU_ERR_BACKEND; }                  
    if (r->last_use_fence > s->completed) s->violations++;        
    r->alive = false;
    s->destroyed++;
    return LIBREGPU_OK;
}

static uint64_t sim_gpu_time(void *ctx, uint64_t fence)
{
    SimBackend *s = (SimBackend *)ctx;
    return fence == s->last_cost_fence ? s->last_cost : 0;
}
static uint64_t sim_presented(void *ctx)  { return ((SimBackend *)ctx)->gpu.presented; }
static uint64_t sim_errors(void *ctx)     { return ((SimBackend *)ctx)->violations; }

LibreGPUBackend *libregpu_sim_backend_create(uint32_t fence_latency)
{
    SimHandle *h = (SimHandle *)calloc(1, sizeof *h);
    if (!h) return NULL;
    h->sim.latency = fence_latency;
    h->sim.session_valid = true;
    h->sim.q_cap = 256;
    h->sim.queue = (LibreGPURawDataset *)calloc(h->sim.q_cap, sizeof *h->sim.queue);
    if (!h->sim.queue) { free(h); return NULL; }
    sim_import(&h->sim, SIM_TARGET_ID);                           
    sim_import(&h->sim, SIM_SHADOW_ID);                           
    sim_import(&h->sim, SIM_TEXTURE_ID);

    LibreGPUBackend *b = &h->base;
    b->name = "simulated";
    b->ctx = &h->sim;
    b->caps.supports_retained_results = true;
    b->caps.supports_scissor_replay   = true;
    b->caps.supports_reproject        = true;
    b->caps.supports_gpu_timing       = true;
    b->attach = sim_attach;
    b->detach = sim_detach;
    b->session_valid = sim_session_valid;
    b->next_dataset = sim_next_dataset;
    b->submit = sim_submit;
    b->completed_fence = sim_completed_fence;
    b->wait_idle = sim_wait_idle;
    b->create_scratch = sim_create_scratch;
    b->destroy_resource = sim_destroy_resource;
    b->gpu_time_ns = sim_gpu_time;
    b->presented_count = sim_presented;
    b->error_count = sim_errors;
    b->destroy = libregpu_sim_backend_destroy;
    return (LibreGPUBackend *)h;                    
}

void libregpu_sim_backend_destroy(LibreGPUBackend *be)
{
    if (!be) return;
    SimHandle *h = (SimHandle *)be;
    for (size_t i = 0; i < h->sim.q_cap; i++) libregpu_dataset_release(&h->sim.queue[i]);
    free(h->sim.queue);
    free(h->sim.res);
    free(h);
}

LibreGPUStatus libregpu_sim_enqueue(LibreGPUBackend *be, LibreGPURawDataset *ds)
{
    if (!be || !ds) return LIBREGPU_ERR_INVALID_ARG;
    SimBackend *s = &((SimHandle *)be)->sim;
    if (s->q_count == s->q_cap) return LIBREGPU_ERR_NO_MEMORY;
    s->queue[(s->q_head + s->q_count) % s->q_cap] = *ds;           
    s->q_count++;
    memset(ds, 0, sizeof *ds);
    return LIBREGPU_OK;
}

void libregpu_sim_set_session_valid(LibreGPUBackend *be, bool valid)
{
    ((SimHandle *)be)->sim.session_valid = valid;
}

const uint32_t *libregpu_sim_display(const LibreGPUBackend *be)
{
    return ((const SimHandle *)be)->sim.gpu.display;
}

uint64_t libregpu_sim_destroyed_count(const LibreGPUBackend *be)
{
    return ((const SimHandle *)be)->sim.destroyed;
}

void libregpu_sim_inject_submit_fault(LibreGPUBackend *be, SimSubmitFault f)
{
    ((SimHandle *)be)->sim.submit_fault = f;
}

void libregpu_sim_inject_destroy_faults(LibreGPUBackend *be, uint32_t count)
{
    ((SimHandle *)be)->sim.destroy_faults = count;
}




static LibreGPURawOp sim_op_new(LibreGPUOpKind k)
{
    LibreGPURawOp o;
    memset(&o, 0, sizeof o);
    o.kind = k;
    return o;
}

static LibreGPUResourceId sim_rid(uint32_t id) { LibreGPUResourceId r = { id, 1 }; return r; }

void libregpu_sim_scene_defaults(SimScene *s)
{
    memset(s, 0, sizeof *s);
    s->camera_enabled = true;
    s->projection = LIBREGPU_PROJ_PAN_2D;
    s->object_count = 3;
    s->objects[0] = (SimObject){  6,  5, 14, 10, 0xAA };
    s->objects[1] = (SimObject){ 16, 10, 14, 10, 0xBB };   
    s->objects[2] = (SimObject){ 46,  6, 10, 12, 0xCC };
}

LibreGPUStatus libregpu_sim_build_dataset(const SimScene *s, LibreGPURawDataset *out)
{
    if (!s || !out) return LIBREGPU_ERR_INVALID_ARG;
    libregpu_dataset_init(out);
    if (s->object_count > SIM_MAX_OBJECTS) return LIBREGPU_ERR_INVALID_ARG;

    LibreGPUCapture cap;
    LibreGPUStatus st = LIBREGPU_OK;
    bool ready = false;
    libregpu_capture_init(&cap);

#define FEED(opv) do { st = libregpu_capture_feed(&cap, &(opv), out, &ready); if (st != LIBREGPU_OK) goto fail; } while (0)

    if (s->camera_enabled) {
        LibreGPURawOp o = sim_op_new(LIBREGPU_OP_SET_CAMERA);
        o.camera_valid = true;
        o.camera.projection = s->projection;
        o.camera.fov_x = 1.0f;
        o.camera.fov_y = (float)SIM_H / (float)SIM_W;
        o.camera.surface_w = SIM_W;
        o.camera.surface_h = SIM_H;
        o.camera.yaw   = ((float)s->cam_yaw_px + s->cam_yaw_sub_px) * o.camera.fov_x / (float)SIM_W;
        o.camera.pitch = (float)s->cam_pitch_px * o.camera.fov_y / (float)SIM_H;
        FEED(o);
    }
    if (s->transient_id) {
        LibreGPURawOp o = sim_op_new(LIBREGPU_OP_RES_CREATE);
        o.writes[0] = sim_rid(s->transient_id); o.write_count = 1;
        o.size_bytes = 4096;
        o.payload_hash = libregpu_hash_combine(0x7EA5, s->transient_id);
        FEED(o);
    }
    if (s->shadow_pass) {
        LibreGPURawOp o = sim_op_new(LIBREGPU_OP_DRAW);
        o.flags = LIBREGPU_OPF_CLEARS_TARGET;                         
        o.writes[0] = sim_rid(SIM_SHADOW_ID); o.write_count = 1;
        o.payload_hash = libregpu_hash_combine(0x5AD0, s->shadow_param);
        FEED(o);
    }
    {
        LibreGPURawOp o = sim_op_new(LIBREGPU_OP_DRAW);                      
        o.flags = LIBREGPU_OPF_CLEARS_TARGET;
        o.writes[0] = sim_rid(SIM_TARGET_ID); o.write_count = 1;
        o.payload_hash = 0xC1EA;
        o.region = libregpu_rect_make(0, 0, SIM_W, SIM_H); o.region_valid = true;
        FEED(o);
    }
    for (size_t i = 0; i < s->object_count; i++) {
        const SimObject *ob = &s->objects[i];
        LibreGPURawOp o = sim_op_new(LIBREGPU_OP_DRAW);
        o.reads[o.read_count++] = sim_rid(SIM_TEXTURE_ID);
        if (s->shadow_pass) o.reads[o.read_count++] = sim_rid(SIM_SHADOW_ID);
        if (s->transient_id && i == 0) o.reads[o.read_count++] = sim_rid(s->transient_id);
        o.writes[0] = sim_rid(SIM_TARGET_ID); o.write_count = 1;
        uint64_t h = libregpu_hash_combine(0x0B7EC7, (uint64_t)(uint32_t)ob->x);          
        h = libregpu_hash_combine(h, (uint64_t)(uint32_t)ob->y);
        h = libregpu_hash_combine(h, (uint64_t)(uint32_t)ob->w);
        h = libregpu_hash_combine(h, (uint64_t)(uint32_t)ob->h);
        h = libregpu_hash_combine(h, ob->color);
        o.payload_hash = h;
        o.region = libregpu_rect_make(ob->x - s->cam_yaw_px, ob->y + s->cam_pitch_px,
                                   ob->x - s->cam_yaw_px + ob->w, ob->y + s->cam_pitch_px + ob->h);
        o.region_valid = true;
        FEED(o);
        if (s->shadow_pass && s->shadow_overwrite && i == 0) {      
            LibreGPURawOp w = sim_op_new(LIBREGPU_OP_DRAW);
            w.flags = LIBREGPU_OPF_CLEARS_TARGET;
            w.writes[0] = sim_rid(SIM_SHADOW_ID); w.write_count = 1;
            w.payload_hash = libregpu_hash_combine(0x5AD1, s->shadow_param);
            FEED(w);
        }
    }
    if (s->inject_opaque) {
        static const uint8_t blob[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01 };
        LibreGPURawOp o = sim_op_new(LIBREGPU_OP_OPAQUE);
        o.blob = (uint8_t *)blob; o.blob_size = sizeof blob;
        FEED(o);
    }
    if (s->transient_id) {
        LibreGPURawOp o = sim_op_new(LIBREGPU_OP_RES_DESTROY);
        o.writes[0] = sim_rid(s->transient_id); o.write_count = 1;
        FEED(o);
    }
    {
        LibreGPURawOp o = sim_op_new(LIBREGPU_OP_PRESENT);
        o.reads[0] = sim_rid(SIM_TARGET_ID); o.read_count = 1;
        FEED(o);
    }
#undef FEED
    libregpu_capture_shutdown(&cap);
    return ready ? LIBREGPU_OK : LIBREGPU_ERR_VALIDATION;
fail:
    libregpu_capture_shutdown(&cap);
    libregpu_dataset_release(out);
    return st;
}



LibreGPUStatus libregpu_sim_demo_scenario(LibreGPUBackend *be, size_t frames)
{
    for (size_t f = 0; f < frames; f++) {
        SimScene sc;
        libregpu_sim_scene_defaults(&sc);
        sc.cam_yaw_px = (int32_t)((f / 6) % 24);
        sc.objects[0].x = 6 + (int32_t)((f / 3) % 12);
        if (f % 50 >= 45) {                                        
            sc.objects[sc.object_count++] = (SimObject){ 20, 28, 8, 6, 0xDD };
        }
        LibreGPURawDataset ds;
        LibreGPUStatus st = libregpu_sim_build_dataset(&sc, &ds);
        if (st != LIBREGPU_OK) return st;
        st = libregpu_sim_enqueue(be, &ds);
        if (st != LIBREGPU_OK) { libregpu_dataset_release(&ds); return st; }
    }
    return LIBREGPU_OK;
}


























void libregpu_metrics_init(LibreGPUMetrics *m) { memset(m, 0, sizeof *m); }

void libregpu_metrics_note_cycle_time(LibreGPUMetrics *m, uint64_t now_ns)
{
    if (m->last_cycle_ns) {                                  
        const double dt = (double)(now_ns - m->last_cycle_ns);
        m->interval_count++;
        const double delta = dt - m->interval_mean_ns;
        m->interval_mean_ns += delta / (double)m->interval_count;
        m->interval_m2 += delta * (dt - m->interval_mean_ns);
    }
    m->last_cycle_ns = now_ns;
}

static double avg(uint64_t total, uint64_t n) { return n ? (double)total / (double)n : 0.0; }

void libregpu_metrics_report(const LibreGPUMetrics *m, const LibreGPUGovernor *gov, FILE *out)
{
    fprintf(out, "LIBREGPU measurements\n");
    

    fprintf(out, "  cycles                : %llu  (baseline %llu, incremental %llu, no-impact %llu, ordinary %llu)\n",
            (unsigned long long)m->cycles, (unsigned long long)m->baseline_cycles,
            (unsigned long long)m->incremental_cycles, (unsigned long long)m->no_impact_cycles,
            (unsigned long long)m->fallback_cycles);
    const double fb = m->cycles ? 100.0 * (double)m->fallback_cycles / (double)m->cycles : 0.0;
    fprintf(out, "  fallback frequency    : %.1f%% of cycles; failed rebuilds: %llu\n",
            fb, (unsigned long long)m->failed_rebuilds);
    fprintf(out, "  operations submitted  : %llu of %llu  (an operation COUNT, not GPU time)\n",
            (unsigned long long)m->ops_submitted, (unsigned long long)m->ops_total);
    fprintf(out, "  dirty pixels (bound)  : %llu pixels covered by scissor regions (upper bound of re-rendered area)\n",
            (unsigned long long)m->dirty_pixels_total);
    fprintf(out, "  CPU time per cycle    : avg %.1f us, max %.1f us (interception, GIR, compare, decide, rebuild, submit)\n",
            avg(m->cpu_ns_total, m->cycles) / 1e3, (double)m->cpu_ns_max / 1e3);

    const double inc = avg(m->gpu_ns_incremental, m->gpu_samples_incremental);
    const double ord = avg(m->gpu_ns_ordinary, m->gpu_samples_ordinary);
    const double base = avg(m->gpu_ns_baseline, m->gpu_samples_baseline);
    fprintf(out, "  GPU time per cycle    : optimized %.0f ns (%llu samples), ordinary %.0f ns (%llu samples)\n",
            inc, (unsigned long long)m->gpu_samples_incremental, ord, (unsigned long long)m->gpu_samples_ordinary);
    if (m->gpu_samples_baseline >= 4 && m->gpu_samples_incremental > 0 && base > 0.0) {
        fprintf(out, "  GPU saving            : %.1f%% (optimized vs %llu forced ordinary samples; indicative, frames differ)\n",
                100.0 * (base - inc) / base, (unsigned long long)m->gpu_samples_baseline);
    } else if (m->gpu_samples_incremental == 0 && m->gpu_samples_ordinary == 0) {
        fprintf(out, "  GPU saving            : NOT MEASURED (backend reports no GPU timing)\n");
    } else {
        fprintf(out, "  GPU saving            : NOT DEMONSTRATED (needs forced-ordinary baseline samples)\n");
    }

    fprintf(out, "  VRAM tracked peak/avg : %llu / %.0f bytes (all tracked resources)\n",
            (unsigned long long)m->vram_peak, avg(m->vram_sum, m->vram_samples));
    fprintf(out, "  VRAM LIBREGPU-owned peak : %llu bytes (the budget applies to this)\n",
            (unsigned long long)m->libregpu_vram_peak);
    if (gov) {
        fprintf(out, "  loop rate             : %.1f Hz (target %s%.1f)\n", gov_loop_rate(gov),
                gov->enabled ? "" : "unlimited ", gov->enabled ? gov->target_hz : 0.0);
        fprintf(out, "  presented rate        : %.1f Hz (measured separately from loop rate)\n", gov_present_rate(gov));
    }
    const double var = m->interval_count > 1 ? m->interval_m2 / (double)(m->interval_count - 1) : 0.0;
    fprintf(out, "  frame-time variance   : mean %.1f us, std-dev %.1f us\n",
            m->interval_mean_ns / 1e3, sqrt(var) / 1e3);
    fprintf(out, "  resource-lifetime err : %llu\n", (unsigned long long)m->lifetime_errors);
    if (m->corrupt_datasets || m->bookkeeping_errors || m->baseline_losses)
        fprintf(out, "  degradations          : corrupt datasets %llu, bookkeeping errors %llu, baselines lost %llu\n",
                (unsigned long long)m->corrupt_datasets, (unsigned long long)m->bookkeeping_errors,
                (unsigned long long)m->baseline_losses);
    fprintf(out, "  output correctness    : verified offline by `--selftest`, not at runtime\n");
}




typedef struct {
    LibreGPUBackend *be;
    LibreGPURuntime  rt;
    FILE         *out;
    int           failures, checks;
    bool          open;
} H;

#define CHECK(h, cond, ...) \
    do { (h)->checks++; if (!(cond)) { (h)->failures++; \
         fprintf((h)->out, "    FAIL (validation.c:%d): ", __LINE__); \
         fprintf((h)->out, __VA_ARGS__); fputc('\n', (h)->out); } } while (0)

typedef struct { int *failures, *checks; } Totals;

static void h_fold(H *h, int *failures, int *checks)
{
    *failures += h->failures; *checks += h->checks;
    h->failures = h->checks = 0;
}



static bool h_open(H *h, FILE *out, uint32_t latency, const LibreGPUConfig *cfg)
{
    memset(h, 0, sizeof *h);
    h->out = out;
    h->be = libregpu_sim_backend_create(latency);
    if (!h->be) { h->failures++; fprintf(out, "    FAIL: cannot create simulated backend\n"); return false; }
    LibreGPUStatus st = libregpu_runtime_init(&h->rt, cfg, h->be);
    if (st != LIBREGPU_OK) {
        h->failures++;
        fprintf(out, "    FAIL: runtime init: %s\n", libregpu_status_str(st));
        libregpu_backend_destroy(h->be);
        h->be = NULL;
        return false;
    }
    st = libregpu_runtime_start(&h->rt);
    if (st != LIBREGPU_OK) {
        h->failures++;
        fprintf(out, "    FAIL: runtime start: %s\n", libregpu_status_str(st));
        libregpu_runtime_stop(&h->rt);                  
        libregpu_backend_destroy(h->be);
        h->be = NULL;
        return false;
    }
    h->open = true;
    return true;
}

static void h_close(H *h)
{
    if (!h->open) return;
    libregpu_runtime_stop(&h->rt);
    h->checks++;
    if (h->be->error_count(h->be->ctx) != 0) {
        h->failures++;
        fprintf(h->out, "    FAIL: %llu resource-lifetime violation(s)\n",
                (unsigned long long)h->be->error_count(h->be->ctx));
    }
    libregpu_backend_destroy(h->be);
    h->be = NULL;
    h->open = false;
}



static bool h_frame(H *h, const SimScene *sc, LibreGPUCycleInfo *ci, bool *match)
{
    LibreGPURawDataset ds;
    uint32_t ref[SIM_W * SIM_H];
    *match = false;
    LibreGPUStatus st = libregpu_sim_build_dataset(sc, &ds);
    if (st != LIBREGPU_OK) { CHECK(h, false, "build dataset: %s", libregpu_status_str(st)); return false; }
    st = libregpu_sim_render_reference(&ds, ref);
    if (st != LIBREGPU_OK) {
        CHECK(h, false, "reference render: %s", libregpu_status_str(st));
        libregpu_dataset_release(&ds);
        return false;
    }
    st = libregpu_sim_enqueue(h->be, &ds);
    if (st != LIBREGPU_OK) {
        CHECK(h, false, "enqueue: %s", libregpu_status_str(st));
        libregpu_dataset_release(&ds);                  
        return false;
    }
    st = libregpu_runtime_step(&h->rt, ci);
    if (st != LIBREGPU_OK) { CHECK(h, false, "step: %s", libregpu_status_str(st)); return false; }
    *match = memcmp(ref, libregpu_sim_display(h->be), sizeof ref) == 0;
    return true;
}

static const char *mode_str(LibreGPUZMode m)
{
    return m == LIBREGPU_Z_INCREMENTAL ? "incremental" : (m == LIBREGPU_Z_NO_IMPACT ? "no-impact" : "ordinary");
}

#define EXPECT_FRAME(h, name, scene, want_decision, want_mode)                                   \
    do {                                                                                          \
        LibreGPUCycleInfo ci_; bool match_ = false;                                                  \
        memset(&ci_, 0, sizeof ci_);                                                              \
        const bool ok_ = h_frame((h), (scene), &ci_, &match_);                                    \
        last_info = ci_;                                                                          \
        if (ok_) {                                                                                \
            fprintf((h)->out, "  %-36s %-20s %-12s ops %3zu/%-3zu %s\n", (name),                 \
                    libregpu_stream_action_str(ci_.decision), mode_str(ci_.submitted_mode),         \
                    ci_.ops_submitted, ci_.ops_total, match_ ? "image OK" : "IMAGE MISMATCH");   \
            CHECK((h), match_, "%s: output differs from the reference renderer", (name));         \
            CHECK((h), ci_.decision == (want_decision), "%s: decision was %s", (name),           \
                  libregpu_stream_action_str(ci_.decision));                                         \
            CHECK((h), ci_.submitted_mode == (want_mode), "%s: submitted mode was %s", (name),   \
                  mode_str(ci_.submitted_mode));                                                  \
        } else {                                                                                  \
            fprintf((h)->out, "  %-36s FRAME FAILED (see above)\n", (name));                      \
        }                                                                                         \
    } while (0)

static LibreGPUConfig test_config(void)
{
    LibreGPUConfig c;
    libregpu_config_defaults(&c);
    c.micro_pixel_max_area_fraction = 0.9f;
    return c;
}


static void test_pipeline(FILE *out, int *failures, int *checks)
{
    fprintf(out, "[pipeline] X/Y/Z cycle, every Streamline outcome vs reference image\n");
    LibreGPUConfig cfg = test_config();
    H h;
    LibreGPUCycleInfo last_info;
    memset(&last_info, 0, sizeof last_info);
    if (!h_open(&h, out, 0, &cfg)) { h_fold(&h, failures, checks); return; }

    SimScene s;
    libregpu_sim_scene_defaults(&s);

    EXPECT_FRAME(&h, "initial baseline (X)", &s, LIBREGPU_STREAM_FALLBACK_ORDINARY, LIBREGPU_Z_ORDINARY);
    CHECK(&h, last_info.was_baseline, "first dataset must be the baseline");

    EXPECT_FRAME(&h, "identical frame", &s, LIBREGPU_STREAM_FALLBACK_NO_IMPACT, LIBREGPU_Z_NO_IMPACT);
    CHECK(&h, last_info.ops_submitted < last_info.ops_total, "no-impact frame should submit fewer operations");

    s.objects[0].x += 5; s.objects[0].y += 2;
    EXPECT_FRAME(&h, "object moved (micro-pixel)", &s, LIBREGPU_STREAM_MICRO_PIXEL, LIBREGPU_Z_INCREMENTAL);
    CHECK(&h, last_info.ops_submitted < last_info.ops_total, "micro-pixel frame should replay fewer operations");
    CHECK(&h, last_info.dirty_pixels > 0, "micro-pixel frame should report its dirty-pixel bound");

    s.cam_yaw_px = 3;
    EXPECT_FRAME(&h, "camera pan right 3px (camera)", &s, LIBREGPU_STREAM_CAMERA, LIBREGPU_Z_INCREMENTAL);

    s.cam_yaw_px = 5; s.objects[1].x += 4;
    EXPECT_FRAME(&h, "pan + object moved (both)", &s, LIBREGPU_STREAM_BOTH, LIBREGPU_Z_INCREMENTAL);
    {
        LibreGPUMemStats ms;
        mem_get_stats(&h.rt.mem, &ms);
        CHECK(&h, ms.pool_reuse_count >= 1, "reprojection scratch should be reused from the pool");
    }

    s.shadow_pass = true; s.shadow_param = 1;
    EXPECT_FRAME(&h, "shadow pass added (structure)", &s, LIBREGPU_STREAM_FALLBACK_ORDINARY, LIBREGPU_Z_ORDINARY);

    s.shadow_param = 7;
    EXPECT_FRAME(&h, "shadow changed (intermediate pass)", &s, LIBREGPU_STREAM_MICRO_PIXEL, LIBREGPU_Z_INCREMENTAL);

    s.cam_yaw_px = 35;
    EXPECT_FRAME(&h, "pan beyond reuse limit", &s, LIBREGPU_STREAM_FALLBACK_ORDINARY, LIBREGPU_Z_ORDINARY);

    s.shadow_pass = false;
    s.objects[s.object_count++] = (SimObject){ 20, 28, 8, 6, 0xDD };
    EXPECT_FRAME(&h, "object added (structure)", &s, LIBREGPU_STREAM_FALLBACK_ORDINARY, LIBREGPU_Z_ORDINARY);

    s.inject_opaque = true;
    EXPECT_FRAME(&h, "opaque operation present", &s, LIBREGPU_STREAM_FALLBACK_ORDINARY, LIBREGPU_Z_ORDINARY);
    s.inject_opaque = false;
    EXPECT_FRAME(&h, "opaque removed (structure)", &s, LIBREGPU_STREAM_FALLBACK_ORDINARY, LIBREGPU_Z_ORDINARY);

    s.cam_yaw_px = 33; s.cam_pitch_px = 2;
    EXPECT_FRAME(&h, "pan left + look up (two strips)", &s, LIBREGPU_STREAM_CAMERA, LIBREGPU_Z_INCREMENTAL);

    s.objects[2].color = 0x77;
    EXPECT_FRAME(&h, "object recoloured (micro-pixel)", &s, LIBREGPU_STREAM_MICRO_PIXEL, LIBREGPU_Z_INCREMENTAL);

    for (int i = 0; i < 20; i++) {                        
        s.objects[0].x += (i % 2) ? -1 : 2;
        s.cam_yaw_px += (i % 3 == 0) ? 1 : 0;
        LibreGPUCycleInfo ci; bool match;
        if (h_frame(&h, &s, &ci, &match)) CHECK(&h, match, "chained frame %d drifted from the reference", i);
    }
    fprintf(out, "  20 chained frames with mixed pan/object motion: no drift checked\n");
    CHECK(&h, h.rt.last_cycle_z_built || !h.rt.last_cycle_optimized, "z_constructed must imply an accepted Z");

    h_fold(&h, failures, checks);
    h_close(&h);
    h_fold(&h, failures, checks);
}


static void test_camera_model(FILE *out, int *failures, int *checks)
{
    fprintf(out, "[camera] exact reprojection only: whole-pixel PAN_2D shifts, nothing else\n");
    LibreGPUConfig cfg = test_config();
    H h;
    LibreGPUCycleInfo last_info;
    memset(&last_info, 0, sizeof last_info);
    if (!h_open(&h, out, 0, &cfg)) { h_fold(&h, failures, checks); return; }
    SimScene s;
    libregpu_sim_scene_defaults(&s);

    EXPECT_FRAME(&h, "baseline", &s, LIBREGPU_STREAM_FALLBACK_ORDINARY, LIBREGPU_Z_ORDINARY);
    s.cam_yaw_px = 3;
    EXPECT_FRAME(&h, "exact 3px pan", &s, LIBREGPU_STREAM_CAMERA, LIBREGPU_Z_INCREMENTAL);

    s.cam_yaw_sub_px = 0.5f;                         
    EXPECT_FRAME(&h, "half-pixel pan", &s, LIBREGPU_STREAM_FALLBACK_ORDINARY, LIBREGPU_Z_ORDINARY);
    CHECK(&h, last_info.camera_reason && strstr(last_info.camera_reason, "whole-pixel"),
          "sub-pixel rotation must be refused, reason was '%s'", last_info.camera_reason ? last_info.camera_reason : "");

    s.cam_yaw_sub_px = 0.0f;                         
    EXPECT_FRAME(&h, "back to a whole pixel", &s, LIBREGPU_STREAM_FALLBACK_ORDINARY, LIBREGPU_Z_ORDINARY);

    s.cam_yaw_px = 4;
    EXPECT_FRAME(&h, "exact 1px pan after recovery", &s, LIBREGPU_STREAM_CAMERA, LIBREGPU_Z_INCREMENTAL);

    s.projection = LIBREGPU_PROJ_PERSPECTIVE; s.cam_yaw_px = 5;
    EXPECT_FRAME(&h, "perspective camera", &s, LIBREGPU_STREAM_FALLBACK_ORDINARY, LIBREGPU_Z_ORDINARY);
    CHECK(&h, last_info.camera_reason && strstr(last_info.camera_reason, "perspective"),
          "perspective reprojection must be refused, reason was '%s'", last_info.camera_reason ? last_info.camera_reason : "");

    s.projection = LIBREGPU_PROJ_PAN_2D; s.cam_yaw_px = 6;
    EXPECT_FRAME(&h, "pan after perspective baseline", &s, LIBREGPU_STREAM_FALLBACK_ORDINARY, LIBREGPU_Z_ORDINARY);
    s.cam_yaw_px = 7;
    EXPECT_FRAME(&h, "pan, pan model restored", &s, LIBREGPU_STREAM_CAMERA, LIBREGPU_Z_INCREMENTAL);

    s.projection = LIBREGPU_PROJ_UNKNOWN; s.cam_yaw_px = 8;
    EXPECT_FRAME(&h, "projection model undeclared", &s, LIBREGPU_STREAM_FALLBACK_ORDINARY, LIBREGPU_Z_ORDINARY);
    CHECK(&h, last_info.camera_reason && strstr(last_info.camera_reason, "not declared"),
          "undeclared projection must disable the camera unit, reason was '%s'",
          last_info.camera_reason ? last_info.camera_reason : "");

    h_fold(&h, failures, checks);
    h_close(&h);
    h_fold(&h, failures, checks);
}


static void test_dependency_integrity(FILE *out, int *failures, int *checks)
{
    fprintf(out, "[dependencies] skipping a producer is only legal while its output is still the retained one (Contract E)\n");
    LibreGPUConfig cfg = test_config();
    H h;
    LibreGPUCycleInfo last_info;
    memset(&last_info, 0, sizeof last_info);
    if (!h_open(&h, out, 0, &cfg)) { h_fold(&h, failures, checks); return; }

    SimScene s;
    libregpu_sim_scene_defaults(&s);
    s.shadow_pass = true; s.shadow_overwrite = true; s.shadow_param = 3;
    EXPECT_FRAME(&h, "baseline with rewritten shadow map", &s, LIBREGPU_STREAM_FALLBACK_ORDINARY, LIBREGPU_Z_ORDINARY);

    s.objects[0].x += 4;      
    EXPECT_FRAME(&h, "move consumer of stale producer", &s, LIBREGPU_STREAM_FALLBACK_ORDINARY, LIBREGPU_Z_ORDINARY);

    s.objects[2].x += 3;      
    EXPECT_FRAME(&h, "move consumer of final producer", &s, LIBREGPU_STREAM_MICRO_PIXEL, LIBREGPU_Z_INCREMENTAL);

    s.shadow_param = 9;       
    EXPECT_FRAME(&h, "change both shadow producers", &s, LIBREGPU_STREAM_MICRO_PIXEL, LIBREGPU_Z_INCREMENTAL);

    h_fold(&h, failures, checks);
    h_close(&h);
    h_fold(&h, failures, checks);
}


static void test_lifetime(FILE *out, int *failures, int *checks)
{
    fprintf(out, "[lifetime] deferred reclamation, bookkeeping on every path (Contract G)\n");
    LibreGPUConfig cfg = test_config();
    H h;
    LibreGPUCycleInfo ci;
    bool match;
    SimScene s;
    libregpu_sim_scene_defaults(&s);

    
    if (!h_open(&h, out, 2, &cfg)) { h_fold(&h, failures, checks); return; }
    h_frame(&h, &s, &ci, &match);
    s.transient_id = 10;
    h_frame(&h, &s, &ci, &match);                         
    CHECK(&h, match, "transient frame image");
    CHECK(&h, libregpu_sim_destroyed_count(h.be) == 0, "game destroy must not hit the driver yet");
    s.transient_id = 0;
    h_frame(&h, &s, &ci, &match);
    CHECK(&h, libregpu_sim_destroyed_count(h.be) == 0, "GPU work may still use the resource");
    h_frame(&h, &s, &ci, &match);
    h_frame(&h, &s, &ci, &match);
    CHECK(&h, libregpu_sim_destroyed_count(h.be) == 1, "resource must be reclaimed once the fence completed");
    fprintf(out, "  destroy deferred until references dropped and fence completed: ok=%d\n",
            libregpu_sim_destroyed_count(h.be) == 1);
    h_fold(&h, failures, checks);
    h_close(&h);
    h_fold(&h, failures, checks);

    
    libregpu_sim_scene_defaults(&s);
    if (!h_open(&h, out, 3, &cfg)) { h_fold(&h, failures, checks); return; }
    h_frame(&h, &s, &ci, &match);                         
    s.transient_id = 10;
    LibreGPURawDataset bad;
    if (libregpu_sim_build_dataset(&s, &bad) == LIBREGPU_OK) {
        bad.ops[0].region_valid = true;                   
        bad.ops[0].region = libregpu_rect_make(10, 10, 5, 5);
        if (libregpu_sim_enqueue(h.be, &bad) != LIBREGPU_OK) libregpu_dataset_release(&bad);
    }
    memset(&ci, 0, sizeof ci);
    LibreGPUStatus st = libregpu_runtime_step(&h.rt, &ci);      
    CHECK(&h, st == LIBREGPU_OK, "unrepresentable dataset must still be submitted: %s", libregpu_status_str(st));
    CHECK(&h, ci.reason && strstr(ci.reason, "representation"), "reason should say GIR failed");
    CHECK(&h, !h.rt.have_baseline, "baseline must be voided when GIR cannot represent Y");
    CHECK(&h, libregpu_sim_destroyed_count(h.be) == 0, "destroy must wait for the fence of the ORDINARY submission");
    s.transient_id = 0;
    for (int i = 0; i < 3; i++) {                         
        h_frame(&h, &s, &ci, &match);
        if (i < 2) CHECK(&h, libregpu_sim_destroyed_count(h.be) == 0, "still in flight after %d more frames", i + 1);
    }
    CHECK(&h, libregpu_sim_destroyed_count(h.be) == 1, "reclaimed only after its fence completed");
    h_fold(&h, failures, checks);
    h_close(&h);                                          
    h_fold(&h, failures, checks);

    
    libregpu_sim_scene_defaults(&s);
    if (!h_open(&h, out, 0, &cfg)) { h_fold(&h, failures, checks); return; }
    h_frame(&h, &s, &ci, &match);
    s.transient_id = 10;
    h_frame(&h, &s, &ci, &match);
    libregpu_sim_inject_destroy_faults(h.be, 2);
    s.transient_id = 0;
    h_frame(&h, &s, &ci, &match);                         
    CHECK(&h, libregpu_sim_destroyed_count(h.be) == 0, "refused destroy must not be counted as done");
    h_frame(&h, &s, &ci, &match);                         
    CHECK(&h, libregpu_sim_destroyed_count(h.be) == 0, "still refused");
    h_frame(&h, &s, &ci, &match);                         
    CHECK(&h, libregpu_sim_destroyed_count(h.be) == 1, "destroy retried until the backend accepts");
    {
        LibreGPUMemStats ms;
        mem_get_stats(&h.rt.mem, &ms);
        CHECK(&h, ms.destroy_failures == 2, "both refusals must be counted (got %llu)",
              (unsigned long long)ms.destroy_failures);
    }
    fprintf(out, "  refused destroys are retried, never forgotten\n");
    h_fold(&h, failures, checks);
    h_close(&h);
    h_fold(&h, failures, checks);

    
    libregpu_sim_scene_defaults(&s);
    if (!h_open(&h, out, 0, &cfg)) { h_fold(&h, failures, checks); return; }
    h_frame(&h, &s, &ci, &match);
    s.transient_id = 10;                                  
    {
        LibreGPURawDataset ds;
        uint32_t ref[SIM_W * SIM_H];
        if (libregpu_sim_build_dataset(&s, &ds) == LIBREGPU_OK && libregpu_sim_render_reference(&ds, ref) == LIBREGPU_OK &&
            libregpu_sim_enqueue(h.be, &ds) == LIBREGPU_OK) {
            memset(&ci, 0, sizeof ci);
            libregpu_alloc_fault_arm(0);                     
            LibreGPUStatus step_st = libregpu_runtime_step(&h.rt, &ci);
            libregpu_alloc_fault_disarm();
            CHECK(&h, step_st == LIBREGPU_OK, "the frame must still be submitted (%s)", libregpu_status_str(step_st));
            CHECK(&h, memcmp(ref, libregpu_sim_display(h.be), sizeof ref) == 0, "and be correct");
            CHECK(&h, h.rt.metrics.bookkeeping_errors >= 1, "the failure must be counted");
            CHECK(&h, ci.baseline_lost && !h.rt.have_baseline && ci.submitted_mode == LIBREGPU_Z_ORDINARY,
                  "optimisation continuity is dropped, visibly");
        } else {
            CHECK(&h, false, "setup of the tracking-failure test failed");
        }
    }
    s.transient_id = 0;
    if (h_frame(&h, &s, &ci, &match)) CHECK(&h, ci.was_baseline && match, "a fresh baseline follows");
    fprintf(out, "  untrackable resource -> ordinary cycle, baseline voided, counted, still correct\n");
    h_fold(&h, failures, checks);
    h_close(&h);
    h_fold(&h, failures, checks);
}


static void test_backend_failures(FILE *out, int *failures, int *checks)
{
    fprintf(out, "[backend] failed submissions: rejection vs unknown state (Contract F)\n");
    LibreGPUConfig cfg = test_config();
    H h;
    LibreGPUCycleInfo ci;
    bool match;
    SimScene s;

    
    libregpu_sim_scene_defaults(&s);
    if (!h_open(&h, out, 0, &cfg)) { h_fold(&h, failures, checks); return; }
    h_frame(&h, &s, &ci, &match);
    s.objects[0].x += 4;
    libregpu_sim_inject_submit_fault(h.be, (SimSubmitFault){ 0, 1, LIBREGPU_ERR_NO_MEMORY });
    memset(&ci, 0, sizeof ci);
    if (h_frame(&h, &s, &ci, &match)) {
        CHECK(&h, match, "image after fallback must still match");
        CHECK(&h, ci.fell_back_after_failure && ci.submitted_mode == LIBREGPU_Z_ORDINARY,
              "a rejected rebuild must fall back to the ordinary path");
        CHECK(&h, h.rt.have_baseline, "a clean rejection leaves the retained state, and the baseline, intact");
    }
    s.objects[0].x += 1;
    if (h_frame(&h, &s, &ci, &match)) {
        CHECK(&h, match && ci.submitted_mode == LIBREGPU_Z_INCREMENTAL, "optimisation resumes afterwards");
    }
    fprintf(out, "  rejected rebuild -> ordinary retry, baseline kept, optimisation resumes\n");
    h_fold(&h, failures, checks);
    h_close(&h);
    h_fold(&h, failures, checks);

    
    libregpu_sim_scene_defaults(&s);
    if (!h_open(&h, out, 0, &cfg)) { h_fold(&h, failures, checks); return; }
    h_frame(&h, &s, &ci, &match);
    s.objects[0].x += 4;
    libregpu_sim_inject_submit_fault(h.be, (SimSubmitFault){ 0, 1, LIBREGPU_ERR_BACKEND });
    {
        LibreGPURawDataset ds;
        if (libregpu_sim_build_dataset(&s, &ds) == LIBREGPU_OK && libregpu_sim_enqueue(h.be, &ds) != LIBREGPU_OK)
            libregpu_dataset_release(&ds);
        memset(&ci, 0, sizeof ci);
        LibreGPUStatus st = libregpu_runtime_step(&h.rt, &ci);
        CHECK(&h, st == LIBREGPU_ERR_BACKEND, "unknown-state failure must be reported (got %s)", libregpu_status_str(st));
        CHECK(&h, !h.rt.have_baseline && ci.baseline_lost, "baseline must be voided after an unknown-state failure");
    }
    s.objects[1].x += 2;
    if (h_frame(&h, &s, &ci, &match))
        CHECK(&h, ci.was_baseline && match, "next dataset must start a fresh baseline and be correct");
    fprintf(out, "  unknown-state failure -> no retry, baseline voided, fresh baseline restores correctness\n");
    h_fold(&h, failures, checks);
    h_close(&h);
    h_fold(&h, failures, checks);

    
    libregpu_sim_scene_defaults(&s);
    if (!h_open(&h, out, 0, &cfg)) { h_fold(&h, failures, checks); return; }
    h_frame(&h, &s, &ci, &match);
    s.objects[0].x += 4;
    libregpu_sim_inject_submit_fault(h.be, (SimSubmitFault){ 0, 2, LIBREGPU_ERR_NO_MEMORY });
    {
        LibreGPURawDataset ds;
        if (libregpu_sim_build_dataset(&s, &ds) == LIBREGPU_OK && libregpu_sim_enqueue(h.be, &ds) != LIBREGPU_OK)
            libregpu_dataset_release(&ds);
        LibreGPUStatus st = libregpu_runtime_step(&h.rt, &ci);
        CHECK(&h, st == LIBREGPU_ERR_NO_MEMORY, "double rejection must surface (got %s)", libregpu_status_str(st));
        CHECK(&h, h.rt.have_baseline, "rejected work leaves X describing what is still displayed");
    }
    s.objects[0].x += 1;
    if (h_frame(&h, &s, &ci, &match))
        CHECK(&h, match && ci.submitted_mode == LIBREGPU_Z_INCREMENTAL, "the next frame is optimised against the intact baseline");
    h_fold(&h, failures, checks);
    h_close(&h);
    h_fold(&h, failures, checks);

    
    libregpu_sim_scene_defaults(&s);
    if (!h_open(&h, out, 0, &cfg)) { h_fold(&h, failures, checks); return; }
    libregpu_sim_inject_submit_fault(h.be, (SimSubmitFault){ 0, 1, LIBREGPU_ERR_NO_MEMORY });
    {
        LibreGPURawDataset ds;
        if (libregpu_sim_build_dataset(&s, &ds) == LIBREGPU_OK && libregpu_sim_enqueue(h.be, &ds) != LIBREGPU_OK)
            libregpu_dataset_release(&ds);
        LibreGPUStatus st = libregpu_runtime_step(&h.rt, &ci);
        CHECK(&h, st != LIBREGPU_OK && !h.rt.have_baseline, "a rejected baseline submission leaves no baseline");
    }
    if (h_frame(&h, &s, &ci, &match)) CHECK(&h, ci.was_baseline && match, "baseline established on the next dataset");
    h_fold(&h, failures, checks);
    h_close(&h);
    h_fold(&h, failures, checks);
}


static void test_capture(FILE *out, int *failures, int *checks)
{
    fprintf(out, "[capture] corrupt datasets are never trusted; input is validated, not truncated\n");
    H h;
    memset(&h, 0, sizeof h);
    h.out = out;
    LibreGPUCapture cap;
    LibreGPURawDataset ds;
    bool ready;
    LibreGPURawOp op;

    
    libregpu_capture_init(&cap);
    libregpu_dataset_init(&ds);
    memset(&op, 0, sizeof op);
    op.kind = LIBREGPU_OP_OPAQUE; op.blob = NULL; op.blob_size = 16;
    CHECK(&h, libregpu_capture_feed(&cap, &op, &ds, &ready) == LIBREGPU_ERR_INVALID_ARG && !ready,
          "blob_size > 0 with a NULL blob must be rejected");
    memset(&op, 0, sizeof op);
    op.kind = LIBREGPU_OP_PRESENT; op.reads[0] = (LibreGPUResourceId){ 1, 1 }; op.read_count = 1;
    CHECK(&h, libregpu_capture_feed(&cap, &op, &ds, &ready) == LIBREGPU_OK && ready, "PRESENT closes the dataset");
    CHECK(&h, ds.complete && ds.corrupt, "a dataset that lost an operation must be flagged corrupt");
    libregpu_dataset_release(&ds);

    
    libregpu_dataset_init(&ds);
    CHECK(&h, libregpu_capture_feed(&cap, &op, &ds, &ready) == LIBREGPU_OK && ready && ds.complete && !ds.corrupt,
          "the dataset after a corrupt one starts clean");
    
    LibreGPURawDataset again = ds;
    CHECK(&h, libregpu_capture_feed(&cap, &op, &again, &ready) == LIBREGPU_ERR_INVALID_ARG,
          "an output dataset that still owns operations must be refused");
    libregpu_dataset_release(&ds);

    
    libregpu_dataset_init(&ds);
    memset(&op, 0, sizeof op);
    op.kind = LIBREGPU_OP_DRAW; op.writes[0] = (LibreGPUResourceId){ LIBREGPU_SCRATCH_ID_BASE + 5, 1 }; op.write_count = 1;
    CHECK(&h, libregpu_dataset_append(&ds, &op) == LIBREGPU_ERR_INVALID_ARG, "ids in the scratch range must be rejected");
    memset(&op, 0, sizeof op);
    op.kind = LIBREGPU_OP_DRAW; op.write_count = 9;
    CHECK(&h, libregpu_dataset_append(&ds, &op) == LIBREGPU_ERR_INVALID_ARG, "reference lists are never truncated");
    libregpu_dataset_release(&ds);
    libregpu_capture_shutdown(&cap);

    
    libregpu_capture_init(&cap);
    libregpu_dataset_init(&ds);
    memset(&op, 0, sizeof op);
    op.kind = LIBREGPU_OP_DRAW;
    libregpu_alloc_fault_arm(0);                              
    LibreGPUStatus st = libregpu_capture_feed(&cap, &op, &ds, &ready);
    libregpu_alloc_fault_disarm();
    CHECK(&h, st == LIBREGPU_ERR_NO_MEMORY && cap.current.corrupt, "allocation failure must poison the dataset");
    memset(&op, 0, sizeof op);
    op.kind = LIBREGPU_OP_PRESENT; op.reads[0] = (LibreGPUResourceId){ 1, 1 }; op.read_count = 1;
    st = libregpu_capture_feed(&cap, &op, &ds, &ready);
    CHECK(&h, st == LIBREGPU_OK && ready && ds.corrupt, "the corrupt dataset is still emitted at its boundary");
    libregpu_dataset_release(&ds);
    libregpu_capture_shutdown(&cap);

    
    LibreGPUConfig cfg = test_config();
    LibreGPUCycleInfo ci;
    bool match;
    SimScene s;
    libregpu_sim_scene_defaults(&s);
    H rh;
    if (h_open(&rh, out, 0, &cfg)) {
        h_frame(&rh, &s, &ci, &match);
        LibreGPURawDataset c;
        if (libregpu_sim_build_dataset(&s, &c) == LIBREGPU_OK) {
            c.corrupt = true;
            if (libregpu_sim_enqueue(rh.be, &c) != LIBREGPU_OK) libregpu_dataset_release(&c);
        }
        memset(&ci, 0, sizeof ci);
        st = libregpu_runtime_step(&rh.rt, &ci);
        CHECK(&rh, st == LIBREGPU_ERR_VALIDATION && rh.rt.metrics.corrupt_datasets == 1 && !rh.rt.have_baseline,
              "corrupt dataset: refused, counted, baseline voided (got %s)", libregpu_status_str(st));
        if (h_frame(&rh, &s, &ci, &match)) CHECK(&rh, ci.was_baseline && match, "recovers with a fresh baseline");
        fprintf(out, "  corrupt dataset refused and counted; next dataset is a clean baseline\n");
        h_fold(&rh, &h.failures, &h.checks);
        h_close(&rh);
        h_fold(&rh, &h.failures, &h.checks);
    } else {
        h.failures += rh.failures;
    }
    h_fold(&h, failures, checks);
}


static void test_rebuilder_validation(FILE *out, int *failures, int *checks)
{
    fprintf(out, "[rebuilder] the last validation boundary refuses every unsafe plan\n");
    H h;
    memset(&h, 0, sizeof h);
    h.out = out;
    LibreGPUBackend *be = libregpu_sim_backend_create(0);
    if (!be) { h.failures++; h_fold(&h, failures, checks); return; }

    SimScene s;
    libregpu_sim_scene_defaults(&s);                            
    LibreGPURawDataset ds;
    if (libregpu_sim_build_dataset(&s, &ds) != LIBREGPU_OK) { h.failures++; libregpu_backend_destroy(be); h_fold(&h, failures, checks); return; }
    LibreGPURebuildResult rr;
    LibreGPUZPlan p;
    const LibreGPUResourceId scratch = { SIM_TEXTURE_ID, 1 };

#define PLAN(m_, arr_, n_) do { memset(&p, 0, sizeof p); p.mode = (m_); p.replay = (arr_); p.replay_count = (n_); } while (0)

    LibreGPUReplayOp good[] = { { 0, false }, { 5, false } };
    PLAN(LIBREGPU_Z_NO_IMPACT, good, 2);
    CHECK(&h, dataset_rebuilder_execute(&p, &ds, be, NULL, &rr) == LIBREGPU_OK && rr.ops_submitted == 2,
          "a correct no-impact plan is accepted");

    LibreGPUReplayOp no_camera[] = { { 5, false } };
    PLAN(LIBREGPU_Z_NO_IMPACT, no_camera, 1);
    CHECK(&h, dataset_rebuilder_execute(&p, &ds, be, NULL, &rr) == LIBREGPU_ERR_VALIDATION, "dropping the camera operation is refused");

    LibreGPUReplayOp with_draw[] = { { 0, false }, { 1, false }, { 5, false } };
    PLAN(LIBREGPU_Z_NO_IMPACT, with_draw, 3);
    CHECK(&h, dataset_rebuilder_execute(&p, &ds, be, NULL, &rr) == LIBREGPU_ERR_VALIDATION, "no-impact plan containing a draw is refused");

    PLAN(LIBREGPU_Z_NO_IMPACT, NULL, 2);
    CHECK(&h, dataset_rebuilder_execute(&p, &ds, be, NULL, &rr) == LIBREGPU_ERR_VALIDATION, "NULL replay with a count is refused");

    LibreGPUReplayOp no_present[] = { { 0, false } };
    PLAN(LIBREGPU_Z_NO_IMPACT, no_present, 1);
    CHECK(&h, dataset_rebuilder_execute(&p, &ds, be, NULL, &rr) == LIBREGPU_ERR_VALIDATION, "a plan that never presents is refused");

    PLAN((LibreGPUZMode)99, good, 2);
    CHECK(&h, dataset_rebuilder_execute(&p, &ds, be, NULL, &rr) == LIBREGPU_ERR_VALIDATION, "an unknown plan mode is refused");

    LibreGPUReplayOp unordered[] = { { 5, false }, { 0, false } };
    PLAN(LIBREGPU_Z_NO_IMPACT, unordered, 2);
    CHECK(&h, dataset_rebuilder_execute(&p, &ds, be, NULL, &rr) == LIBREGPU_ERR_VALIDATION, "out-of-order replay is refused");

    LibreGPUReplayOp all[] = { { 0, false }, { 1, true }, { 2, true }, { 3, true }, { 4, true }, { 5, false } };
    PLAN(LIBREGPU_Z_INCREMENTAL, all, 6);
    p.reproject = true;
    p.regions[0] = libregpu_rect_make(0, 0, 8, 8); p.region_count = 1;
    CHECK(&h, dataset_rebuilder_execute(&p, &ds, be, NULL, &rr) == LIBREGPU_ERR_VALIDATION, "reprojection without a scratch destination is refused");
    CHECK(&h, dataset_rebuilder_execute(&p, &ds, be, &scratch, &rr) == LIBREGPU_OK, "reprojection with a scratch destination is accepted");

    PLAN(LIBREGPU_Z_INCREMENTAL, all, 6);
    p.regions[0] = libregpu_rect_make(0, 0, 8, 8); p.regions[1] = libregpu_rect_make(4, 4, 12, 12); p.region_count = 2;
    CHECK(&h, dataset_rebuilder_execute(&p, &ds, be, NULL, &rr) == LIBREGPU_ERR_VALIDATION, "overlapping regions are refused");

    PLAN(LIBREGPU_Z_INCREMENTAL, all, 6);                       
    CHECK(&h, dataset_rebuilder_execute(&p, &ds, be, NULL, &rr) == LIBREGPU_ERR_VALIDATION, "scissored work without a region is refused");

    ds.corrupt = true;
    PLAN(LIBREGPU_Z_ORDINARY, NULL, 0);
    CHECK(&h, dataset_rebuilder_execute(&p, &ds, be, NULL, &rr) == LIBREGPU_ERR_VALIDATION, "a corrupt dataset is never submitted");
    ds.corrupt = false;
    ds.complete = false;
    CHECK(&h, dataset_rebuilder_execute(&p, &ds, be, NULL, &rr) == LIBREGPU_ERR_VALIDATION, "an incomplete dataset is never submitted");
    ds.complete = true;
    CHECK(&h, dataset_rebuilder_execute(&p, &ds, be, NULL, &rr) == LIBREGPU_OK, "the ordinary path accepts a valid dataset");
    libregpu_dataset_release(&ds);

    s.transient_id = 10;                                     
    if (libregpu_sim_build_dataset(&s, &ds) == LIBREGPU_OK) {
        size_t destroy_idx = 0, create_idx = 0, present_idx = ds.op_count - 1;
        for (size_t i = 0; i < ds.op_count; i++) {
            if (ds.ops[i].kind == LIBREGPU_OP_RES_DESTROY) destroy_idx = i;
            if (ds.ops[i].kind == LIBREGPU_OP_RES_CREATE) create_idx = i;
        }
        LibreGPUReplayOp with_destroy[] = { { 0, false }, { (uint32_t)create_idx, false },
                                         { (uint32_t)destroy_idx, false }, { (uint32_t)present_idx, false } };
        PLAN(LIBREGPU_Z_NO_IMPACT, with_destroy, 4);
        CHECK(&h, dataset_rebuilder_execute(&p, &ds, be, NULL, &rr) == LIBREGPU_ERR_VALIDATION, "destroy operations are never replayed");
        libregpu_dataset_release(&ds);
    }

    s.transient_id = 0; s.inject_opaque = true;
    if (libregpu_sim_build_dataset(&s, &ds) == LIBREGPU_OK) {
        LibreGPUReplayOp mand[] = { { 0, false }, { (uint32_t)(ds.op_count - 1), false } };
        PLAN(LIBREGPU_Z_NO_IMPACT, mand, 2);
        CHECK(&h, dataset_rebuilder_execute(&p, &ds, be, NULL, &rr) == LIBREGPU_ERR_VALIDATION,
              "a non-ordinary plan over a dataset with an opaque operation is refused");
        PLAN(LIBREGPU_Z_ORDINARY, NULL, 0);
        CHECK(&h, dataset_rebuilder_execute(&p, &ds, be, NULL, &rr) == LIBREGPU_OK, "...but the ordinary path still forwards it unchanged");
        libregpu_dataset_release(&ds);
    }
#undef PLAN
    CHECK(&h, be->error_count(be->ctx) == 0, "validation tests must not cause lifetime violations");
    libregpu_backend_destroy(be);
    h_fold(&h, failures, checks);
}


static void test_memory_manager(FILE *out, int *failures, int *checks)
{
    fprintf(out, "[memory] transactional references, budget scope, backend contract\n");
    H h;
    memset(&h, 0, sizeof h);
    h.out = out;
    LibreGPUBackend *be = libregpu_sim_backend_create(0);
    if (!be) { h.failures++; h_fold(&h, failures, checks); return; }

    
    LibreGPUBackend bad = *be;
    LibreGPUMemoryManager m;
    bad.completed_fence = NULL;
    CHECK(&h, mem_init(&m, &bad, 0) == LIBREGPU_ERR_INVALID_ARG, "a backend without completed_fence must be refused");
    bad = *be; bad.wait_idle = NULL;
    CHECK(&h, mem_init(&m, &bad, 0) == LIBREGPU_ERR_INVALID_ARG, "a backend without wait_idle must be refused");
    bad = *be; bad.destroy_resource = NULL;
    CHECK(&h, mem_init(&m, &bad, 0) == LIBREGPU_ERR_INVALID_ARG, "a backend without destroy_resource must be refused");
    {
        LibreGPUConfig cfg = test_config();
        LibreGPURuntime rt;
        bad = *be; bad.completed_fence = NULL;
        CHECK(&h, libregpu_runtime_init(&rt, &cfg, &bad) == LIBREGPU_ERR_INVALID_ARG, "the runtime refuses such a backend too");
    }
    {
        

        LibreGPUMemoryManager um;
        CHECK(&h, mem_init_unlimited(&um, be) == LIBREGPU_OK,
              "the rc0-2 two-argument mem_init form remains available");
        if (um.buckets) mem_shutdown(&um);
    }

    
    SimScene s;
    libregpu_sim_scene_defaults(&s);
    LibreGPURawDataset ra, rb;
    GirDataset ga, gb;
    s.transient_id = 10;
    LibreGPUStatus st = libregpu_sim_build_dataset(&s, &ra);
    s.transient_id = 11;
    LibreGPUStatus st2 = libregpu_sim_build_dataset(&s, &rb);
    if (st != LIBREGPU_OK || st2 != LIBREGPU_OK || gir_build(&ga, &ra) != LIBREGPU_OK || gir_build(&gb, &rb) != LIBREGPU_OK ||
        mem_init(&m, be, 0) != LIBREGPU_OK) {
        CHECK(&h, false, "setup of the transactional test failed");
        libregpu_backend_destroy(be);
        h_fold(&h, failures, checks);
        return;
    }
    uint64_t fence = 0;
    CHECK(&h, be->submit(be->ctx, &ra, NULL, &fence) == LIBREGPU_OK, "submit dataset A");
    CHECK(&h, mem_track_raw(&m, &ra) == LIBREGPU_OK, "track A");
    mem_note_raw_submission(&m, &ra, fence);
    CHECK(&h, mem_sync_owner_refs(&m, LIBREGPU_OWNER_X, &ga) == LIBREGPU_OK, "X := A");
    CHECK(&h, mem_request_release(&m, (LibreGPUResourceId){ 10, 1 }) == LIBREGPU_OK, "game destroys resource 10");
    mem_collect(&m);
    CHECK(&h, libregpu_sim_destroyed_count(be) == 0, "a referenced resource is not reclaimed");
    libregpu_alloc_fault_arm(0);                                
    st = mem_sync_owner_refs(&m, LIBREGPU_OWNER_X, &gb);
    libregpu_alloc_fault_disarm();
    CHECK(&h, st == LIBREGPU_ERR_NO_MEMORY, "the sync must report the failure (got %s)", libregpu_status_str(st));
    mem_collect(&m);
    CHECK(&h, libregpu_sim_destroyed_count(be) == 0, "a failed sync must not drop X's references");
    CHECK(&h, mem_sync_owner_refs(&m, LIBREGPU_OWNER_X, &gb) == LIBREGPU_OK, "the retry succeeds");
    mem_collect(&m);
    CHECK(&h, libregpu_sim_destroyed_count(be) == 1, "after a successful sync the resource is reclaimed");

    
    mem_shutdown(&m);
    CHECK(&h, mem_init(&m, be, 1000) == LIBREGPU_OK, "mem_init with a budget");
    CHECK(&h, mem_track(&m, (LibreGPUResourceId){ 20, 1 }, 1000000000ULL, false) == LIBREGPU_OK, "track a 1 GB game resource");
    LibreGPUResourceId r1, r2, r3;
    CHECK(&h, mem_acquire_scratch(&m, 600, &r1) == LIBREGPU_OK, "game bytes do not count against the budget");
    CHECK(&h, mem_acquire_scratch(&m, 600, &r2) == LIBREGPU_ERR_NO_MEMORY, "LIBREGPU-owned bytes do");
    CHECK(&h, mem_request_release(&m, r1) == LIBREGPU_OK, "release scratch");
    mem_collect(&m);
    {
        LibreGPUMemStats ms;
        mem_get_stats(&m, &ms);
        CHECK(&h, ms.resources_pooled == 1 && ms.libregpu_bytes == 600, "released scratch is pooled, still LIBREGPU-owned");
    }
    CHECK(&h, mem_acquire_scratch(&m, 600, &r3) == LIBREGPU_OK && libregpu_res_equal(r1, r3), "pooled scratch is reused");
    CHECK(&h, mem_acquire_scratch(&m, UINT64_MAX, &r2) == LIBREGPU_ERR_NO_MEMORY, "huge request: saturating arithmetic, no wrap");
    CHECK(&h, mem_track(&m, (LibreGPUResourceId){ LIBREGPU_SCRATCH_ID_BASE + 9, 1 }, 1, false) == LIBREGPU_ERR_INVALID_ARG,
          "a game resource cannot be tracked inside the scratch namespace");
    mem_shutdown(&m);

    gir_release(&ga); gir_release(&gb);
    libregpu_dataset_release(&ra); libregpu_dataset_release(&rb);
    CHECK(&h, be->error_count(be->ctx) == 0, "no lifetime violations");
    libregpu_backend_destroy(be);
    h_fold(&h, failures, checks);
}


static void test_session_and_modes(FILE *out, int *failures, int *checks)
{
    fprintf(out, "[session] invalidation and decision-only mode\n");
    LibreGPUConfig cfg = test_config();
    H h;
    LibreGPUCycleInfo ci;
    bool match;
    SimScene s;
    libregpu_sim_scene_defaults(&s);

    if (!h_open(&h, out, 0, &cfg)) { h_fold(&h, failures, checks); return; }
    h_frame(&h, &s, &ci, &match);
    h_frame(&h, &s, &ci, &match);
    libregpu_sim_set_session_valid(h.be, false);
    LibreGPUStatus st = libregpu_runtime_step(&h.rt, &ci);
    CHECK(&h, st == LIBREGPU_ERR_SESSION_INVALID, "invalid session must be reported");
    CHECK(&h, !h.rt.have_baseline, "baseline must be voided with the session");
    libregpu_sim_set_session_valid(h.be, true);
    if (h_frame(&h, &s, &ci, &match))
        CHECK(&h, ci.was_baseline && match, "a new baseline must be established before optimizing again");
    h_fold(&h, failures, checks);
    h_close(&h);
    h_fold(&h, failures, checks);

    cfg.mode = LIBREGPU_MODE_COMPARE;
    if (!h_open(&h, out, 0, &cfg)) { h_fold(&h, failures, checks); return; }
    h_frame(&h, &s, &ci, &match);
    if (h_frame(&h, &s, &ci, &match)) {
        CHECK(&h, ci.decision == LIBREGPU_STREAM_FALLBACK_NO_IMPACT && ci.submitted_mode == LIBREGPU_Z_ORDINARY && match,
              "compare mode must decide but still submit the original dataset");
        CHECK(&h, h.rt.metrics.would_optimize_cycles == 1, "compare mode should count would-be optimizations");
    }
    fprintf(out, "  compare mode: decisions logged, original datasets submitted\n");
    h_fold(&h, failures, checks);
    h_close(&h);
    h_fold(&h, failures, checks);

    fprintf(out, "[honesty] forced-ordinary baseline sampling (Contract H)\n");
    cfg = test_config();
    cfg.baseline_sample_interval = 3;
    if (!h_open(&h, out, 0, &cfg)) { h_fold(&h, failures, checks); return; }
    for (int i = 0; i < 14; i++) h_frame(&h, &s, &ci, &match);
    CHECK(&h, h.rt.metrics.gpu_samples_baseline >= 4, "baseline samples should have been collected");
    libregpu_metrics_report(&h.rt.metrics, &h.rt.gov, out);
    h_fold(&h, failures, checks);
    h_close(&h);
    h_fold(&h, failures, checks);
}


static void test_units(FILE *out, int *failures, int *checks)
{
    fprintf(out, "[units] GIR, numerics, launcher input, governor\n");
    H h;
    memset(&h, 0, sizeof h);
    h.out = out;

    
    SimScene s;
    libregpu_sim_scene_defaults(&s);
    LibreGPURawDataset a, b, c;
    GirDataset ga, gb, gc;
    GirDelta d;
    CHECK(&h, libregpu_sim_build_dataset(&s, &a) == LIBREGPU_OK, "build a");
    s.objects[1].x += 3;
    CHECK(&h, libregpu_sim_build_dataset(&s, &b) == LIBREGPU_OK, "build b");
    s.object_count--;
    CHECK(&h, libregpu_sim_build_dataset(&s, &c) == LIBREGPU_OK, "build c");
    CHECK(&h, gir_build(&ga, &a) == LIBREGPU_OK && gir_build(&gb, &b) == LIBREGPU_OK &&
              gir_build(&gc, &c) == LIBREGPU_OK, "gir_build");
    CHECK(&h, ga.has_present_target && ga.has_surface && ga.has_camera && ga.camera_count == 1,
          "GIR facts: target, surface, exactly one camera");
    CHECK(&h, ga.dep_count > 0 && gir_find_resource(&ga, (LibreGPUResourceId){ SIM_TARGET_ID, 1 }) != NULL,
          "GIR dependencies and resource table");
    CHECK(&h, gir_compare(&ga, &ga, &d) == LIBREGPU_OK && d.identical, "identical datasets compare equal");
    gir_delta_release(&d);
    CHECK(&h, gir_compare(&ga, &gb, &d) == LIBREGPU_OK && !d.identical && d.change_count == 1 &&
              d.changes[0].kind == GIR_CHG_MODIFIED && d.non_camera_change && !d.camera_changed,
          "one modified draw is reported as exactly one change");
    gir_delta_release(&d);
    CHECK(&h, gir_compare(&gb, &gc, &d) == LIBREGPU_OK && d.structure_changed && d.change_count >= 1,
          "removed draw is a structural change");
    gir_delta_release(&d);
    gir_release(&ga); gir_release(&gb); gir_release(&gc);

    
    {
        GirDataset g;
        b.ops[1].reads[0] = (LibreGPUResourceId){ LIBREGPU_SCRATCH_ID_BASE, 1 }; b.ops[1].read_count = 1;
        CHECK(&h, gir_build(&g, &b) == LIBREGPU_ERR_VALIDATION, "GIR rejects the reserved id range");
        b.ops[1].read_count = 0;
        b.ops[1].region_valid = true; b.ops[1].region = libregpu_rect_make(0, 0, INT32_MAX, 4);
        CHECK(&h, gir_build(&g, &b) == LIBREGPU_ERR_VALIDATION, "GIR rejects out-of-range footprints");
        b.ops[1].region = libregpu_rect_make(0, 0, SIM_W, SIM_H);
        b.corrupt = true;
        CHECK(&h, gir_build(&g, &b) == LIBREGPU_ERR_VALIDATION, "GIR rejects corrupt datasets");
        b.corrupt = false;
        b.ops[1].region.x0 = 1;                             
        CHECK(&h, gir_build(&g, &b) == LIBREGPU_OK && !g.has_surface, "surface stays unknown unless the clear is a full-target clear");
        gir_release(&g);
    }
    libregpu_dataset_release(&a); libregpu_dataset_release(&b); libregpu_dataset_release(&c);

    
    {
        SimScene cs;
        libregpu_sim_scene_defaults(&cs);
        LibreGPURawDataset x, y;
        GirDataset gx, gy;
        GirDelta sd;
        LibreGPUStreamlineResult sr;
        memset(&x, 0, sizeof x); memset(&y, 0, sizeof y);
        memset(&gx, 0, sizeof gx); memset(&gy, 0, sizeof gy);
        memset(&sd, 0, sizeof sd); memset(&sr, 0, sizeof sr);
        bool ok = libregpu_sim_build_dataset(&cs, &x) == LIBREGPU_OK &&
                  libregpu_sim_build_dataset(&cs, &y) == LIBREGPU_OK;
        if (ok) {
            for (int k = 0; k < 2; k++) {
                LibreGPURawDataset *ds = k ? &y : &x;
                ds->ops[0].camera.fov_x = 1e-12f;
                ds->ops[0].camera.fov_y = 1e-12f;
                if (k) ds->ops[0].camera.yaw = 0.1f;
            }
            ok = gir_build(&gx, &x) == LIBREGPU_OK && gir_build(&gy, &y) == LIBREGPU_OK &&
                 gir_compare(&gx, &gy, &sd) == LIBREGPU_OK;
        }
        if (ok) {
            LibreGPUConfig cfg = test_config();
            const LibreGPUBackendCaps caps = { true, true, true, true };
            const LibreGPUStatus st = streamline_decide(&cfg, &caps, &gx, &gy, &sd, 0.0, &sr);
            CHECK(&h, st == LIBREGPU_OK && sr.action == LIBREGPU_STREAM_FALLBACK_ORDINARY &&
                      sr.camera_reason && strstr(sr.camera_reason, "out of range"),
                  "an extreme reprojection shift is refused, not rounded");
            streamline_result_release(&sr);
            gir_delta_release(&sd);
        } else {
            CHECK(&h, false, "setup of the extreme-shift test failed");
        }
        gir_release(&gx); gir_release(&gy);
        libregpu_dataset_release(&x); libregpu_dataset_release(&y);
    }

    
    {
        SimScene w;
        libregpu_sim_scene_defaults(&w);
        w.shadow_pass = true; w.shadow_overwrite = true;
        LibreGPURawDataset r;
        GirDataset g;
        bool war = false, raw_dep = false;
        if (libregpu_sim_build_dataset(&w, &r) == LIBREGPU_OK && gir_build(&g, &r) == LIBREGPU_OK) {
            for (size_t i = 0; i < g.dep_count; i++) {
                if (g.deps[i].kind == GIR_DEP_WAR) war = true;
                if (g.deps[i].kind == GIR_DEP_RAW) raw_dep = true;
                if (i && g.deps[i].to < g.deps[i - 1].to) CHECK(&h, false, "dependencies must stay sorted by consumer");
            }
            CHECK(&h, war && raw_dep, "GIR records WAR (writer after reader) as well as RAW edges");
            gir_release(&g);
            libregpu_dataset_release(&r);
        } else {
            CHECK(&h, false, "could not build the dependency scene");
        }

        LibreGPUCapture cap;
        libregpu_capture_init(&cap);
        libregpu_dataset_init(&r);
        bool ready = false;
        LibreGPURawOp cam;
        memset(&cam, 0, sizeof cam);
        cam.kind = LIBREGPU_OP_SET_CAMERA; cam.camera_valid = true;
        cam.camera.projection = LIBREGPU_PROJ_PAN_2D; cam.camera.fov_x = 1; cam.camera.fov_y = 0.5f;
        cam.camera.surface_w = SIM_W; cam.camera.surface_h = SIM_H;
        LibreGPURawOp pr;
        memset(&pr, 0, sizeof pr);
        pr.kind = LIBREGPU_OP_PRESENT; pr.reads[0] = (LibreGPUResourceId){ SIM_TARGET_ID, 1 }; pr.read_count = 1;
        libregpu_capture_feed(&cap, &cam, &r, &ready);
        libregpu_capture_feed(&cap, &cam, &r, &ready);
        libregpu_capture_feed(&cap, &pr, &r, &ready);
        CHECK(&h, ready && gir_build(&g, &r) == LIBREGPU_OK && g.camera_count == 2 && !g.has_camera,
              "two camera records mean no authoritative pose: the camera unit is off");
        gir_release(&g);
        libregpu_dataset_release(&r);
        libregpu_capture_shutdown(&cap);

        
        LibreGPURawDataset c1, c2;
        GirDataset g1, g2;
        bool built = true;
        LibreGPURawOp mk;
        memset(&mk, 0, sizeof mk);
        mk.kind = LIBREGPU_OP_RES_CREATE; mk.writes[0] = (LibreGPUResourceId){ 50, 1 }; mk.write_count = 1;
        mk.size_bytes = 100; mk.payload_hash = 7;
        for (int k = 0; k < 2; k++) {
            LibreGPURawDataset *dst = k ? &c2 : &c1;
            libregpu_capture_init(&cap);
            libregpu_dataset_init(dst);
            mk.size_bytes = k ? 200 : 100;
            built = built && libregpu_capture_feed(&cap, &mk, dst, &ready) == LIBREGPU_OK;
            built = built && libregpu_capture_feed(&cap, &pr, dst, &ready) == LIBREGPU_OK && ready;
            libregpu_capture_shutdown(&cap);
        }
        CHECK(&h, built && gir_build(&g1, &c1) == LIBREGPU_OK && gir_build(&g2, &c2) == LIBREGPU_OK &&
                  g1.records[0].sig != g2.records[0].sig, "allocation size must change the operation signature");
        gir_release(&g1); gir_release(&g2);
        libregpu_dataset_release(&c1); libregpu_dataset_release(&c2);
    }

    
    {
        const LibreGPURect big = { INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX };
        CHECK(&h, libregpu_rect_area(big) == INT64_MAX, "area of the largest rectangle saturates instead of wrapping");
        const LibreGPURect edge = { INT32_MAX - 1, 0, INT32_MAX, 1 };
        CHECK(&h, libregpu_rect_translate(edge, 100, 0).x1 == INT32_MAX, "translate saturates");
        const LibreGPURect low = { INT32_MIN, 0, INT32_MIN + 1, 1 };
        CHECK(&h, libregpu_rect_inflate(low, 1000).x0 == INT32_MIN, "inflate saturates");
        CHECK(&h, libregpu_sat_add_u64(UINT64_MAX, 5) == UINT64_MAX, "saturating add");
    }

    
    {
        LibreGPUConfig cfg = test_config();
        CHECK(&h, libregpu_config_validate(&cfg) == LIBREGPU_OK, "default configuration is valid");
        cfg.micro_pixel_max_area_fraction = NAN;
        CHECK(&h, libregpu_config_validate(&cfg) == LIBREGPU_ERR_INVALID_ARG, "NaN in the configuration is rejected");
        cfg = test_config(); cfg.target_loops_per_second = 1e-10;
        CHECK(&h, libregpu_config_validate(&cfg) == LIBREGPU_ERR_INVALID_ARG,
              "a loop rate below the governor minimum is rejected");
        cfg = test_config(); cfg.target_loops_per_second = LIBREGPU_GOV_MIN_HZ;
        CHECK(&h, libregpu_config_validate(&cfg) == LIBREGPU_OK,
              "the governor minimum is accepted");
        cfg = test_config(); cfg.target_loops_per_second = -1.0;
        CHECK(&h, libregpu_config_validate(&cfg) == LIBREGPU_ERR_INVALID_ARG,
              "a negative loop rate is rejected");
        cfg = test_config(); cfg.target_loops_per_second = INFINITY;
        LibreGPUBackend *be = libregpu_sim_backend_create(0);
        LibreGPURuntime rt;
        CHECK(&h, be && libregpu_runtime_init(&rt, &cfg, be) == LIBREGPU_ERR_INVALID_ARG, "runtime refuses a non-finite loop rate");
        libregpu_backend_destroy(be);

        LibreGPUGovernor g;
        CHECK(&h, gov_init(&g, NAN) == LIBREGPU_ERR_INVALID_ARG && !g.enabled, "governor rejects NaN");
        CHECK(&h, gov_init(&g, -5.0) == LIBREGPU_ERR_INVALID_ARG && !g.enabled, "governor rejects negative rates");
        CHECK(&h, gov_init(&g, 1e-10) == LIBREGPU_ERR_INVALID_ARG && !g.enabled,
              "governor rejects rates below the minimum");
        CHECK(&h, gov_init(&g, 5e-324) == LIBREGPU_ERR_INVALID_ARG && !g.enabled,
              "governor rejects denormal rates that would overflow the interval");
        CHECK(&h, gov_init(&g, LIBREGPU_GOV_MIN_HZ) == LIBREGPU_OK && g.enabled && g.interval_ns == 1000000000ULL,
              "the governor minimum is accepted with a 1 s interval");
        CHECK(&h, gov_init(&g, 1e12) == LIBREGPU_ERR_INVALID_ARG && !g.enabled, "governor rejects absurd rates");
        CHECK(&h, gov_init(&g, 0.0) == LIBREGPU_OK && !g.enabled, "0 disables pacing");
        CHECK(&h, gov_init(&g, 60.0) == LIBREGPU_OK && g.enabled && g.interval_ns > 0, "60 Hz is accepted");

        

        gov_init(&g, 300.0);
        const uint64_t t0 = libregpu_now_ns();
        for (int i = 0; i < 30; i++) gov_wait(&g);
        const double ms_elapsed = (double)(libregpu_now_ns() - t0) / 1e6;
        CHECK(&h, ms_elapsed >= 90.0 && ms_elapsed < 400.0, "governor: 30 loops at 300 Hz took %.1f ms", ms_elapsed);
        fprintf(out, "  governor: 30 loops at 300 Hz took %.1f ms (expected ~100)\n", ms_elapsed);
    }

    
    {
        LibreGPULauncherSettings st;
        char err[200];
        char *nan_args[]   = { "libregpu", "--fps", "nan" };
        char *inf_args[]   = { "libregpu", "--fps", "inf" };
        char *tiny_args[]  = { "libregpu", "--fps", "1e-10" };
        char *denorm_args[] = { "libregpu", "--fps", "5e-324" };
        char *min_args[]   = { "libregpu", "--fps", "1" };
        char *miss_args[]  = { "libregpu", "--game", "--demo" };
        char *nothing[]    = { "libregpu", "--fps" };
        char *unknown[]    = { "libregpu", "--bogus" };
        char *good[]       = { "libregpu", "--fps", "60", "--mode", "compare", "--frames", "10", "--demo", "--quiet" };
        char longpath[700];
        memset(longpath, 'a', sizeof longpath - 1);
        longpath[sizeof longpath - 1] = '\0';
        char *long_args[]  = { "libregpu", "--game", longpath };
        CHECK(&h, ui_parse_args(&st, 3, nan_args, err, sizeof err) == LIBREGPU_ERR_INVALID_ARG, "--fps nan is rejected");
        CHECK(&h, ui_parse_args(&st, 3, inf_args, err, sizeof err) == LIBREGPU_ERR_INVALID_ARG, "--fps inf is rejected");
        CHECK(&h, ui_parse_args(&st, 3, tiny_args, err, sizeof err) == LIBREGPU_ERR_INVALID_ARG,
              "--fps below the minimum is rejected");
        CHECK(&h, ui_parse_args(&st, 3, denorm_args, err, sizeof err) == LIBREGPU_ERR_INVALID_ARG,
              "--fps denormal is rejected");
        CHECK(&h, ui_parse_args(&st, 3, min_args, err, sizeof err) == LIBREGPU_OK &&
                  st.config.target_loops_per_second == LIBREGPU_GOV_MIN_HZ,
              "--fps at the governor minimum is accepted");
        CHECK(&h, ui_parse_args(&st, 3, miss_args, err, sizeof err) == LIBREGPU_ERR_INVALID_ARG && !st.demo,
              "an option is not accepted as another option's value");
        CHECK(&h, ui_parse_args(&st, 2, nothing, err, sizeof err) == LIBREGPU_ERR_INVALID_ARG, "a missing value is reported");
        CHECK(&h, ui_parse_args(&st, 2, unknown, err, sizeof err) == LIBREGPU_ERR_INVALID_ARG, "unknown options are reported");
        CHECK(&h, ui_parse_args(&st, 3, long_args, err, sizeof err) == LIBREGPU_ERR_INVALID_ARG && !st.game_selected,
              "a game path that does not fit is rejected, not truncated");
        CHECK(&h, ui_parse_args(NULL, 1, good, err, sizeof err) == LIBREGPU_ERR_INVALID_ARG, "NULL settings are rejected");
        CHECK(&h, ui_parse_args(&st, 9, good, err, sizeof err) == LIBREGPU_OK && st.demo && st.quiet &&
                  st.demo_frames == 10 && st.config.mode == LIBREGPU_MODE_COMPARE &&
                  st.config.target_loops_per_second == 60.0, "a valid command line parses");
    }

    
    {
        FILE *f = tmpfile();
        if (f) {
            char buf[256];
            ui_print_banner(f);
            rewind(f);
            const size_t got = fread(buf, 1, sizeof buf - 1, f);
            buf[got] = '\0';
            CHECK(&h, strstr(buf, LIBREGPU_CODE_NAME) != NULL && strstr(buf, LIBREGPU_VERSION) != NULL,
                  "the launcher banner shows the code name and version (%s %s)",
                  LIBREGPU_CODE_NAME, LIBREGPU_VERSION);
            fclose(f);
        } else {
            CHECK(&h, false, "could not create a temporary file for the banner check");
        }
    }
    h_fold(&h, failures, checks);
}




#define SWEEP_FRAMES 10

static void sweep_scenes(SimScene *sc)
{
    libregpu_sim_scene_defaults(&sc[0]);                                    
    sc[1] = sc[0];                                                       
    sc[2] = sc[1]; sc[2].objects[0].x += 4; sc[2].objects[0].y += 2;     
    sc[3] = sc[2]; sc[3].cam_yaw_px = 2;                                 
    sc[4] = sc[3]; sc[4].cam_yaw_px = 4; sc[4].objects[1].x += 3;        
    sc[5] = sc[4]; sc[5].shadow_pass = true; sc[5].shadow_param = 1;     
    sc[6] = sc[5]; sc[6].shadow_param = 5;                               
    sc[7] = sc[6]; sc[7].shadow_pass = false; sc[7].transient_id = 10;   
    sc[8] = sc[7]; sc[8].transient_id = 0;                               
    sc[9] = sc[8];                                                       
}





static int sweep_run(FILE *out, long fail_at, long *allocs, int *first_bad_frame, int *step_errors, int *degraded)
{
    LibreGPUConfig cfg = test_config();
    SimScene sc[SWEEP_FRAMES];
    sweep_scenes(sc);
    H h;
    int problems = 0;
    *first_bad_frame = -1;
    *step_errors = 0;
    *degraded = 0;
    if (!h_open(&h, out, 1, &cfg)) return 1000;

    libregpu_alloc_fault_arm(fail_at);
    libregpu_alloc_fault_pause(true);                       
    for (int f = 0; f < SWEEP_FRAMES; f++) {
        LibreGPURawDataset ds;
        uint32_t ref[SIM_W * SIM_H];
        if (libregpu_sim_build_dataset(&sc[f], &ds) != LIBREGPU_OK) { problems++; continue; }
        if (libregpu_sim_render_reference(&ds, ref) != LIBREGPU_OK) { libregpu_dataset_release(&ds); problems++; continue; }
        if (libregpu_sim_enqueue(h.be, &ds) != LIBREGPU_OK) { libregpu_dataset_release(&ds); problems++; continue; }
        LibreGPUCycleInfo ci;
        libregpu_alloc_fault_pause(false);
        const LibreGPUStatus st = libregpu_runtime_step(&h.rt, &ci);
        libregpu_alloc_fault_pause(true);
        if (st != LIBREGPU_OK) (*step_errors)++;
        else if (ci.baseline_lost || ci.fell_back_after_failure) (*degraded)++;
        if (st == LIBREGPU_OK && memcmp(ref, libregpu_sim_display(h.be), sizeof ref) != 0) {
            problems++;
            if (*first_bad_frame < 0) *first_bad_frame = f;
        }
    }
    *allocs = libregpu_alloc_count();
    libregpu_alloc_fault_disarm();
    h_close(&h);                                          
    problems += h.failures;
    return problems;
}

static void test_alloc_sweep(FILE *out, int *failures, int *checks)
{
    fprintf(out, "[alloc-sweep] one allocation failure at EVERY allocation point of a %d-frame scenario\n", SWEEP_FRAMES);
    long total = 0;
    int bad_frame = -1, errs = 0, deg = 0;
    int base = sweep_run(out, -1, &total, &bad_frame, &errs, &deg);
    (*checks)++;
    if (base != 0 || total <= 0 || errs != 0) {
        (*failures)++;
        fprintf(out, "    FAIL: the fault-free counting run itself had problems (%d, %d step errors)\n", base, errs);
        return;
    }
    int broken_runs = 0, runs_with_error = 0, runs_degraded = 0;
    for (long k = 0; k < total; k++) {
        long ignored = 0;
        int e = 0, d = 0;
        const int p = sweep_run(out, k, &ignored, &bad_frame, &e, &d);
        if (e) runs_with_error++;
        if (d) runs_degraded++;
        if (p) {
            broken_runs++;
            if (broken_runs == 1)
                fprintf(out, "    first broken run: failing allocation #%ld (frame %d)\n", k, bad_frame);
        }
    }
    (*checks)++;
    if (broken_runs) { (*failures)++; fprintf(out, "    FAIL: %d of %ld injected failures broke an invariant\n", broken_runs, total); }
    
    (*checks)++;
    if (runs_with_error + runs_degraded == 0) { (*failures)++; fprintf(out, "    FAIL: no injected failure had any visible effect\n"); }
    fprintf(out, "  %ld allocation points swept: %d broke an invariant\n", total, broken_runs);
    fprintf(out, "  injections that surfaced as a failed step: %d; as a degraded-but-correct cycle: %d; absorbed silently: %ld\n",
            runs_with_error, runs_degraded, total - runs_with_error - runs_degraded);
}

int libregpu_selftest(FILE *out)
{
    int failures = 0, checks = 0;
    fprintf(out, "%s %s self-test (simulated backend; GPU time is a cost model)\n\n",
            LIBREGPU_CODE_NAME, LIBREGPU_VERSION);
    test_pipeline(out, &failures, &checks);
    test_camera_model(out, &failures, &checks);
    test_dependency_integrity(out, &failures, &checks);
    test_lifetime(out, &failures, &checks);
    test_backend_failures(out, &failures, &checks);
    test_capture(out, &failures, &checks);
    test_rebuilder_validation(out, &failures, &checks);
    test_memory_manager(out, &failures, &checks);
    test_session_and_modes(out, &failures, &checks);
    test_units(out, &failures, &checks);
    test_alloc_sweep(out, &failures, &checks);
    fprintf(out, "\n%d checks, %d failed\n", checks, failures);
    return failures;
}

