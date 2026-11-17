










#define LIBREGPU_LIB_DECLARATIONS_ONLY
#define LIBREGPU_UI_DECLARATIONS_ONLY
#include "lib.c"
#include "ui.c"       

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>









#define SIM_W 64
#define SIM_H 36
#define SIM_TARGET_ID 1u
#define SIM_SHADOW_ID 2u
#define SIM_TEXTURE_ID 3u
#define SIM_MAX_OBJECTS 8

typedef struct { int32_t x, y, w, h; uint32_t color; } SimObject;   

typedef struct {
    bool      camera_enabled;
    int32_t   cam_yaw_px, cam_pitch_px;     
    float     cam_yaw_sub_px;               

    LibreGPUProjection projection;             
    SimObject objects[SIM_MAX_OBJECTS];     size_t object_count;
    bool      shadow_pass;  uint32_t shadow_param;
    bool      shadow_overwrite;             
    uint32_t  transient_id;                 
    bool      inject_opaque;
} SimScene;

typedef struct {                            
    uint32_t    skip;                       
    uint32_t    count;                      
    LibreGPUStatus status;                     
} SimSubmitFault;



typedef struct {
    uint64_t submits_ordinary;              
    uint64_t submits_update;                
    uint64_t max_fence, completed_fence;
} LibreGPUSimStats;

LibreGPUBackend  *libregpu_sim_backend_create(uint32_t fence_latency);
void           libregpu_sim_backend_destroy(LibreGPUBackend *be);
LibreGPUStatus    libregpu_sim_enqueue(LibreGPUBackend *be, LibreGPURawDataset *ds);   
void           libregpu_sim_set_session_valid(LibreGPUBackend *be, bool valid);
const uint32_t *libregpu_sim_display(const LibreGPUBackend *be);
uint64_t       libregpu_sim_destroyed_count(const LibreGPUBackend *be);
void           libregpu_sim_stats(const LibreGPUBackend *be, LibreGPUSimStats *out);
void           libregpu_sim_inject_submit_fault(LibreGPUBackend *be, SimSubmitFault f);
void           libregpu_sim_scene_defaults(SimScene *s);
LibreGPUStatus    libregpu_sim_build_dataset(const SimScene *s, LibreGPURawDataset *out);
LibreGPUStatus    libregpu_sim_render_reference(const LibreGPURawDataset *ds, uint32_t *fb_out);
LibreGPUStatus    libregpu_sim_demo_scenario(LibreGPUBackend *be, size_t frames);





























void libregpu_config_defaults(LibreGPUConfig *c)
{
    memset(c, 0, sizeof *c);
    c->target_loops_per_second = 0.0;
    c->enable_micro_pixel = true;
    c->enable_camera = false;                 
    c->camera_max_reuse_angle = 0.35f;
    c->camera_position_epsilon = 1e-4f;
    c->camera_roll_epsilon = 1e-4f;
    c->camera_shift_tolerance_px = 1e-3f;
    c->exposed_margin_px = 2;
    c->micro_pixel_max_area_fraction = 0.5f;
    c->baseline_sample_interval = 0;
    c->full_refresh_interval = 600;
}

LibreGPUStatus libregpu_config_validate(const LibreGPUConfig *c)
{
    if (!c) return LIBREGPU_ERR_INVALID_ARG;
    if (!isfinite(c->target_loops_per_second) || c->target_loops_per_second < 0.0 ||
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
    st = mem_init(&rt->mem, backend);
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
    rt->metrics.mem_saved = ms.saved_count;
    rt->metrics.mem_carved = ms.carved_count;
    rt->metrics.mem_carved_bytes = ms.carved_bytes;
}







static LibreGPUStatus mem_cycle_prepare(LibreGPURuntime *rt, const GirDataset *y,
                                     const LibreGPUStreamlineResult *decision,
                                     LibreGPUMemSave *save)
{
    memset(save, 0, sizeof *save);
    mem_collect(&rt->mem);                                  
    LibreGPUMemInput in;
    in.x = rt->have_baseline ? &rt->gir_x : NULL;
    in.y = y;
    in.decision = decision;
    return memalloc(&rt->mem, &in, save);
}




static bool fence_advances(const LibreGPURuntime *rt, uint64_t fence)
{
    return fence != 0 && fence > rt->last_fence;
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
    m->changed_pixels_total = libregpu_sat_add_u64(m->changed_pixels_total, info->changed_pixels);
    m->affected_pixels_total = libregpu_sat_add_u64(m->affected_pixels_total, info->affected_pixels);
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
    track_dataset(rt, raw);                                
    info->reason = reason;
    info->decision = LIBREGPU_STREAM_FALLBACK_ORDINARY;

    const bool had_baseline = rt->have_baseline;
    LibreGPUStatus st = dataset_rebuilder_execute(&plan, raw, rt->backend, NULL, &rr);
    if (st != LIBREGPU_OK) {
        if (st == LIBREGPU_ERR_BACKEND) {                     
            info->baseline_lost = had_baseline;
            invalidate_baseline(rt);
        }
        return st;                                         
    }
    if (!fence_advances(rt, rr.fence)) {                   
        info->baseline_lost = had_baseline;
        invalidate_baseline(rt);
        return LIBREGPU_ERR_BACKEND;
    }
    mem_note_raw_submission(&rt->mem, raw, rr.fence);      
    if (had_baseline) {
        info->baseline_lost = true;                        
        invalidate_baseline(rt);
    }
    request_destroys(rt, raw);
    info->submitted_mode = rr.mode;
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

    


    if (raw.already_forwarded) {
        rt->metrics.forwarded_datasets++;
        if (rt->have_baseline) info->baseline_lost = true;
        invalidate_baseline(rt);
        info->reason = "dataset already forwarded by capture: never re-submitted";
        rt->processing_y = false;
        libregpu_dataset_release(&raw);
        return LIBREGPU_ERR_VALIDATION;
    }
    if (raw.capture_failed) {
        rt->metrics.capture_failed_datasets++;
        if (rt->have_baseline) info->baseline_lost = true;
        invalidate_baseline(rt);
        info->reason = "capture failure: dataset incomplete, nothing submitted";
        rt->processing_y = false;
        libregpu_dataset_release(&raw);
        return LIBREGPU_ERR_VALIDATION;
    }

    double dt = 0.0;                                       
    if (raw.timestamp_ns && rt->last_dataset_ts_ns && raw.timestamp_ns > rt->last_dataset_ts_ns)
        dt = (double)(raw.timestamp_ns - rt->last_dataset_ts_ns) * 1e-9;
    rt->last_dataset_ts_ns = raw.timestamp_ns;

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
        if (!fence_advances(rt, rr.fence)) {
            info->baseline_lost = true;
            info->reason = "backend returned a non-monotonic fence";
            invalidate_baseline(rt);
            result = LIBREGPU_ERR_BACKEND;
            goto done;
        }
        mem_note_raw_submission(&rt->mem, &raw, rr.fence);
        request_destroys(rt, &raw);
        const bool kept = promote(rt, &y);
        info->was_baseline = true;
        info->baseline_lost = !kept;
        info->decision = LIBREGPU_STREAM_FALLBACK_ORDINARY;
        info->submitted_mode = rr.mode;
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
    info->micro_pixel = sr.micro_pixel;
    info->camera = sr.camera;
    info->micro_reason = sr.micro_reason;
    info->camera_reason = sr.camera_reason;
    info->changes = delta.change_count;
    info->record_changes_per_second = sr.record_changes_per_second;
    info->reason = sr.fallback_reason ? sr.fallback_reason
                                      : (sr.micro_pixel ? sr.micro_reason : sr.camera_reason);

    
    {
        const LibreGPUConfig *cfg = &rt->config;
        const char *force = NULL;
        if (cfg->baseline_sample_interval && rt->cycle % cfg->baseline_sample_interval == 0)
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

    
    {
        LibreGPUMemSave save;
        const LibreGPUStatus ms = mem_cycle_prepare(rt, &y, &sr, &save);
        if (save.have_scratch) {
            scratch = save.scratch;
            has_scratch = true;
        } else if (sr.plan.mode == LIBREGPU_Z_INCREMENTAL && sr.plan.reproject) {
            streamline_force_ordinary(&sr, ms != LIBREGPU_OK
                                             ? "memalloc could not save the reprojection target"
                                             : "reprojection target unavailable");
            info->reason = "reprojection target unavailable";
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
    if (st == LIBREGPU_OK && !fence_advances(rt, rr.fence)) {
        if (has_scratch) { mem_request_release(&rt->mem, scratch); has_scratch = false; }
        info->baseline_lost = true;
        info->reason = "backend returned a non-monotonic fence";
        invalidate_baseline(rt);
        result = LIBREGPU_ERR_BACKEND;
        goto done;
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
        const LibreGPUZMode mode = rr.mode;                   
        info->submitted_mode = mode;
        info->ops_submitted = rr.ops_submitted;
        info->changed_pixels = (mode == LIBREGPU_Z_INCREMENTAL) ? sr.changed_pixels : 0;
        info->affected_pixels = (mode == LIBREGPU_Z_INCREMENTAL) ? sr.affected_pixels : 0;
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
    info->forced_baseline = forced_baseline;
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
    uint64_t submits_ordinary, submits_update;
    SimSubmitFault submit_fault;
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
    if (!libregpu_dataset_is_submittable(ds)) return LIBREGPU_ERR_VALIDATION;
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
            if (w) s->submits_update++; else s->submits_ordinary++;
            s->gpu.cost = 0;
            sim_run(s, ds, w, fence, (w ? w->op_count : ds->op_count) / 2);
            return LIBREGPU_ERR_BACKEND;
        }
        return s->submit_fault.status;                            
    }

    const uint64_t fence = ++s->submitted;
    if (w) s->submits_update++; else s->submits_ordinary++;
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

void libregpu_sim_stats(const LibreGPUBackend *be, LibreGPUSimStats *out)
{
    if (!out) return;
    memset(out, 0, sizeof *out);
    if (!be) return;
    const SimBackend *s = &((const SimHandle *)be)->sim;
    out->submits_ordinary = s->submits_ordinary;
    out->submits_update   = s->submits_update;
    out->max_fence        = s->submitted;
    out->completed_fence  = s->completed;
}

void libregpu_sim_inject_submit_fault(LibreGPUBackend *be, SimSubmitFault f)
{
    ((SimHandle *)be)->sim.submit_fault = f;
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
    fprintf(out, "  changed pixels (bound): %llu  (union of changed operation footprints; a PIXEL count)\n",
            (unsigned long long)m->changed_pixels_total);
    fprintf(out, "  affected pixels (bound): %llu  (scissor regions Z re-rendered; upper bound)\n",
            (unsigned long long)m->affected_pixels_total);
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

    fprintf(out, "  VRAM tracked peak/avg : %llu / %.0f bytes\n",
            (unsigned long long)m->vram_peak, avg(m->vram_sum, m->vram_samples));
    fprintf(out, "  memalloc saved/carved : %llu resources / %llu blocks (%llu bytes)\n",
            (unsigned long long)m->mem_saved, (unsigned long long)m->mem_carved,
            (unsigned long long)m->mem_carved_bytes);
    if (gov) {
        fprintf(out, "  loop rate             : %.1f Hz (target %s%.1f)\n", gov_loop_rate(gov),
                gov->enabled ? "" : "unlimited ", gov->enabled ? gov->target_hz : 0.0);
        fprintf(out, "  presented rate        : %.1f Hz (measured separately from loop rate)\n", gov_present_rate(gov));
    }
    const double var = m->interval_count > 1 ? m->interval_m2 / (double)(m->interval_count - 1) : 0.0;
    fprintf(out, "  frame-time variance   : mean %.1f us, std-dev %.1f us\n",
            m->interval_mean_ns / 1e3, sqrt(var) / 1e3);
    fprintf(out, "  resource-lifetime err : %llu\n", (unsigned long long)m->lifetime_errors);
    if (m->capture_failed_datasets || m->forwarded_datasets ||
        m->bookkeeping_errors || m->baseline_losses)
        fprintf(out, "  degradations          : capture failures %llu, already forwarded %llu, "
                     "bookkeeping errors %llu, baselines lost %llu\n",
                (unsigned long long)m->capture_failed_datasets,
                (unsigned long long)m->forwarded_datasets,
                (unsigned long long)m->bookkeeping_errors, (unsigned long long)m->baseline_losses);
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






static bool h_frame_fb(H *h, const SimScene *sc, LibreGPUCycleInfo *ci, bool *match,
                       uint32_t *ref_out, uint32_t *disp_out)
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
    if (ref_out)  memcpy(ref_out, ref, sizeof ref);
    if (disp_out) memcpy(disp_out, libregpu_sim_display(h->be), sizeof ref);
    return true;
}

static bool h_frame(H *h, const SimScene *sc, LibreGPUCycleInfo *ci, bool *match)
{
    return h_frame_fb(h, sc, ci, match, NULL, NULL);
}



static uint64_t frame_diff(const uint32_t *a, const uint32_t *b)
{
    uint64_t n = 0;
    for (size_t i = 0; i < (size_t)SIM_W * SIM_H; i++) if (a[i] != b[i]) n++;
    return n;
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
    c.enable_camera = true;             
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
    CHECK(&h, last_info.changed_pixels > 0 && last_info.affected_pixels > 0,
          "micro-pixel frame should report pixel-scale changed and affected bounds");

    s.cam_yaw_px = 3;
    EXPECT_FRAME(&h, "camera pan right 3px (camera)", &s, LIBREGPU_STREAM_CAMERA, LIBREGPU_Z_INCREMENTAL);

    s.cam_yaw_px = 5; s.objects[1].x += 4;
    EXPECT_FRAME(&h, "pan + object moved (both)", &s, LIBREGPU_STREAM_BOTH, LIBREGPU_Z_INCREMENTAL);
    CHECK(&h, h.rt.metrics.mem_saved > 0, "memalloc saved the resources the next X needs");
    CHECK(&h, h.rt.metrics.mem_carved >= 2,
          "memalloc carved a reprojection target in order for every camera update");

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

}


static void test_capture(FILE *out, int *failures, int *checks)
{
    fprintf(out, "[capture] lost and already-forwarded datasets are distinct and never trusted\n");
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
    CHECK(&h, ds.complete && ds.capture_failed && !ds.already_forwarded,
          "a dataset that lost an operation is capture_failed, not silently usable");
    CHECK(&h, !libregpu_dataset_is_submittable(&ds), "a capture_failed dataset is never submittable");
    libregpu_dataset_release(&ds);

    
    libregpu_dataset_init(&ds);
    CHECK(&h, libregpu_capture_feed(&cap, &op, &ds, &ready) == LIBREGPU_OK && ready && ds.complete &&
                  !ds.capture_failed && !ds.already_forwarded && libregpu_dataset_is_submittable(&ds),
          "the dataset after a failed one starts clean and submittable");
    
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
            libregpu_dataset_mark_forwarded(&c);                  
            if (libregpu_sim_enqueue(rh.be, &c) != LIBREGPU_OK) libregpu_dataset_release(&c);
        }
        memset(&ci, 0, sizeof ci);
        const LibreGPUStatus st = libregpu_runtime_step(&rh.rt, &ci);
        CHECK(&rh, st == LIBREGPU_ERR_VALIDATION && rh.rt.metrics.forwarded_datasets == 1 &&
                   !rh.rt.have_baseline,
              "already-forwarded dataset: refused, counted, baseline voided (got %s)", libregpu_status_str(st));
        if (h_frame(&rh, &s, &ci, &match)) CHECK(&rh, ci.was_baseline && match, "recovers with a fresh baseline");

        LibreGPURawDataset lost;
        if (libregpu_sim_build_dataset(&s, &lost) == LIBREGPU_OK) {
            lost.capture_failed = true;                         
            if (libregpu_sim_enqueue(rh.be, &lost) != LIBREGPU_OK) libregpu_dataset_release(&lost);
        }
        memset(&ci, 0, sizeof ci);
        const LibreGPUStatus st2 = libregpu_runtime_step(&rh.rt, &ci);
        CHECK(&rh, st2 == LIBREGPU_ERR_VALIDATION && rh.rt.metrics.capture_failed_datasets == 1 &&
                   !rh.rt.have_baseline,
              "capture_failed without forwarding: refused separately, baseline voided");
        fprintf(out, "  both capture states refused and counted; next dataset is a clean baseline\n");
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

    ds.capture_failed = true;
    PLAN(LIBREGPU_Z_ORDINARY, NULL, 0);
    CHECK(&h, dataset_rebuilder_execute(&p, &ds, be, NULL, &rr) == LIBREGPU_ERR_VALIDATION, "a capture_failed dataset is never submitted");
    ds.capture_failed = false;
    ds.already_forwarded = true;
    CHECK(&h, dataset_rebuilder_execute(&p, &ds, be, NULL, &rr) == LIBREGPU_ERR_VALIDATION, "an already-forwarded dataset is never submitted");
    ds.already_forwarded = false;
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
    fprintf(out, "[gc] resource ownership and fence-based lifetime\n");
    H h;
    memset(&h, 0, sizeof h);
    h.out = out;
    LibreGPUBackend *be = libregpu_sim_backend_create(0);
    if (!be) { h.failures++; h_fold(&h, failures, checks); return; }

    
    LibreGPUBackend bad = *be;
    LibreGPUMemoryManager m;
    bad.completed_fence = NULL;
    CHECK(&h, mem_init(&m, &bad) == LIBREGPU_ERR_INVALID_ARG, "a backend without completed_fence must be refused");
    bad = *be; bad.wait_idle = NULL;
    CHECK(&h, mem_init(&m, &bad) == LIBREGPU_ERR_INVALID_ARG, "a backend without wait_idle must be refused");
    bad = *be; bad.destroy_resource = NULL;
    CHECK(&h, mem_init(&m, &bad) == LIBREGPU_ERR_INVALID_ARG, "a backend without destroy_resource must be refused");
    {
        LibreGPUConfig cfg = test_config();
        LibreGPURuntime rt;
        bad = *be; bad.completed_fence = NULL;
        CHECK(&h, libregpu_runtime_init(&rt, &cfg, &bad) == LIBREGPU_ERR_INVALID_ARG, "the runtime refuses such a backend too");
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
        mem_init(&m, be) != LIBREGPU_OK) {
        CHECK(&h, false, "setup of the ownership test failed");
        libregpu_backend_destroy(be);
        h_fold(&h, failures, checks);
        return;
    }
    uint64_t fence = 0;
    CHECK(&h, be->submit(be->ctx, &ra, NULL, &fence) == LIBREGPU_OK, "submit dataset A");
    CHECK(&h, mem_track_raw(&m, &ra) == LIBREGPU_OK, "track A");
    mem_note_raw_submission(&m, &ra, fence);
    CHECK(&h, mem_sync_owner_refs(&m, LIBREGPU_OWNER_X, &ga) == LIBREGPU_OK, "X := A");
    CHECK(&h, mem_request_release(&m, (LibreGPUResourceId){ 10, 1 }) == LIBREGPU_OK, "the game destroys resource 10");
    mem_collect(&m);
    CHECK(&h, libregpu_sim_destroyed_count(be) == 0, "a referenced resource is not reclaimed");
    CHECK(&h, mem_sync_owner_refs(&m, LIBREGPU_OWNER_X, &gb) == LIBREGPU_OK, "X := B (A is no longer referenced)");
    mem_collect(&m);
    CHECK(&h, libregpu_sim_destroyed_count(be) == 1, "once unreferenced and the fence has completed, it is reclaimed");

    
    CHECK(&h, mem_track(&m, (LibreGPUResourceId){ LIBREGPU_SCRATCH_ID_BASE + 9, 1 }, 1, false) == LIBREGPU_ERR_INVALID_ARG,
          "a game resource cannot be tracked inside the scratch namespace");

    
    LibreGPUResourceId scratch;
    CHECK(&h, mem_acquire_scratch(&m, 600, &scratch) == LIBREGPU_OK && scratch.id >= LIBREGPU_SCRATCH_ID_BASE, "acquire scratch");
    CHECK(&h, mem_request_release(&m, scratch) == LIBREGPU_OK, "release scratch");
    mem_collect(&m);
    CHECK(&h, libregpu_sim_destroyed_count(be) == 2, "scratch is destroyed once released and idle");

    

    {
        LibreGPUBackend *be2 = libregpu_sim_backend_create(0);
        LibreGPUMemoryManager mm;
        if (!be2 || mem_init(&mm, be2) != LIBREGPU_OK) {
            CHECK(&h, false, "memalloc manager init failed");
            if (be2) libregpu_backend_destroy(be2);
        } else {
            LibreGPURawDataset ry, rp;
            GirDataset gy;
            LibreGPURawOp op;
            libregpu_dataset_init(&ry);
            libregpu_dataset_init(&rp);
            memset(&gy, 0, sizeof gy);
            memset(&op, 0, sizeof op);
            op.kind = LIBREGPU_OP_RES_CREATE;
            op.write_count = 1;
            op.writes[0] = (LibreGPUResourceId){ 20, 1 };
            op.size_bytes = 4096;
            LibreGPUStatus bst = libregpu_dataset_append(&ry, &op);
            memset(&op, 0, sizeof op);                       
            op.kind = LIBREGPU_OP_RES_CREATE;
            op.write_count = 1;
            op.writes[0] = (LibreGPUResourceId){ 21, 1 };
            if (bst == LIBREGPU_OK) bst = libregpu_dataset_append(&ry, &op);
            memset(&op, 0, sizeof op);
            op.kind = LIBREGPU_OP_RES_DESTROY;
            op.write_count = 1;
            op.writes[0] = (LibreGPUResourceId){ 21, 1 };
            if (bst == LIBREGPU_OK) bst = libregpu_dataset_append(&ry, &op);
            if (bst == LIBREGPU_OK) { ry.complete = true; bst = gir_build(&gy, &ry); }

            
            memset(&op, 0, sizeof op);
            op.kind = LIBREGPU_OP_RES_CREATE;
            op.write_count = 1;
            op.writes[0] = (LibreGPUResourceId){ 30, 1 };
            op.size_bytes = 1024;
            if (bst == LIBREGPU_OK) bst = libregpu_dataset_append(&rp, &op);
            uint64_t pfence = 0;
            if (bst == LIBREGPU_OK) {
                rp.complete = true;
                bst = be2->submit(be2->ctx, &rp, NULL, &pfence);
            }

            if (bst != LIBREGPU_OK) {
                CHECK(&h, false, "memalloc test dataset setup failed");
            } else {
                CHECK(&h, mem_track_raw(&mm, &ry) == LIBREGPU_OK, "memalloc sees the incoming sizes");
                CHECK(&h, mem_track(&mm, (LibreGPUResourceId){ 30, 1 }, 1024, false) == LIBREGPU_OK,
                      "the unwanted resource is tracked");
                mem_note_use(&mm, (LibreGPUResourceId){ 30, 1 }, pfence);
                CHECK(&h, mem_request_release(&mm, (LibreGPUResourceId){ 30, 1 }) == LIBREGPU_OK,
                      "an unwanted resource is queued for the purge");
                LibreGPUMemStats before, after;
                mem_get_stats(&mm, &before);
                mem_collect(&mm);
                mem_get_stats(&mm, &after);
                CHECK(&h, after.resources_tracked + 1 == before.resources_tracked,
                      "the manager purges the unwanted resource before memalloc saves");

                LibreGPUStreamlineResult sr;
                memset(&sr, 0, sizeof sr);
                sr.plan.mode = LIBREGPU_Z_INCREMENTAL;
                sr.plan.reproject = true;
                sr.plan.reprojection.surface_w = 32;
                sr.plan.reprojection.surface_h = 16;

                LibreGPUMemInput mi;
                memset(&mi, 0, sizeof mi);
                mi.y = &gy;
                mi.decision = &sr;
                LibreGPUMemSave save;
                CHECK(&h, memalloc(&mm, &mi, &save) == LIBREGPU_OK, "memalloc completes a save pass");
                CHECK(&h, save.saved_count == 1 && save.saved_bytes == 4096,
                      "only the resource that reaches the next X is saved");
                CHECK(&h, save.have_scratch && save.scratch.id >= LIBREGPU_SCRATCH_ID_BASE &&
                          save.carved_count == 1 && save.scratch_bytes == 32u * 16u * 4u,
                      "memalloc carves exactly the block the plan needs");
                CHECK(&h, save.cursor_end == save.scratch_bytes,
                      "the save cursor starts at the bottom of the space the purge freed");
                CHECK(&h, mem_request_release(&mm, save.scratch) == LIBREGPU_OK, "the carved block is released");
                mem_collect(&mm);
                CHECK(&h, be2->error_count(be2->ctx) == 0, "the save pass caused no lifetime violations");
            }
            gir_release(&gy);
            libregpu_dataset_release(&ry);
            libregpu_dataset_release(&rp);
            mem_shutdown(&mm);
            libregpu_backend_destroy(be2);
        }
    }
    mem_shutdown(&m);

    gir_release(&ga); gir_release(&gb);
    libregpu_dataset_release(&ra); libregpu_dataset_release(&rb);
    CHECK(&h, be->error_count(be->ctx) == 0, "no lifetime violations");
    libregpu_backend_destroy(be);
    h_fold(&h, failures, checks);
}


static void test_session(FILE *out, int *failures, int *checks)
{
    fprintf(out, "[session] invalidation, and forced-ordinary baseline sampling\n");
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


static void test_contracts(FILE *out, int *failures, int *checks)
{
    fprintf(out, "[contract] Step 2: finalised dataset, submission, backend, Z and GC rules\n");
    H h;
    memset(&h, 0, sizeof h);
    h.out = out;

    
    {
        LibreGPUCapture cap;
        LibreGPURawDataset ds;
        LibreGPURawOp op;
        bool ready = false;
        uint64_t first_ts = 0, first_seq = 0;
        libregpu_capture_init(&cap);

        libregpu_dataset_init(&ds);
        memset(&op, 0, sizeof op);
        op.kind = LIBREGPU_OP_DRAW;
        op.writes[0] = (LibreGPUResourceId){ SIM_TARGET_ID, 1 }; op.write_count = 1;
        CHECK(&h, libregpu_capture_feed(&cap, &op, &ds, &ready) == LIBREGPU_OK && !ready &&
                  ds.op_count == 0 && !ds.complete,
              "operations accumulate in the capture; PRESENT alone closes and emits the dataset");
        memset(&op, 0, sizeof op);
        op.kind = LIBREGPU_OP_PRESENT;
        op.reads[0] = (LibreGPUResourceId){ SIM_TARGET_ID, 1 }; op.read_count = 1;
        CHECK(&h, libregpu_capture_feed(&cap, &op, &ds, &ready) == LIBREGPU_OK && ready &&
                  ds.complete && ds.op_count == 2 &&
                  ds.ops[0].kind == LIBREGPU_OP_DRAW && ds.ops[1].kind == LIBREGPU_OP_PRESENT,
              "PRESENT closes the dataset without reordering its operations");
        CHECK(&h, ds.sequence == 1 && ds.timestamp_ns > 0,
              "the closing PRESENT stamps a monotonic-ns sequence and timestamp");
        first_ts = ds.timestamp_ns; first_seq = ds.sequence;
        CHECK(&h, libregpu_dataset_append(&ds, &op) == LIBREGPU_ERR_STATE,
              "a dataset closed at PRESENT cannot be appended to");
        libregpu_dataset_release(&ds);

        libregpu_dataset_init(&ds); ready = false;
        CHECK(&h, libregpu_capture_feed(&cap, &op, &ds, &ready) == LIBREGPU_OK && ready &&
                  ds.sequence == first_seq + 1 && ds.timestamp_ns >= first_ts,
              "sequence numbers stay monotonic within a capture session");
        libregpu_dataset_release(&ds);
        libregpu_capture_shutdown(&cap);
    }

    
    {
        uint8_t src[3] = { 1, 2, 3 };
        LibreGPURawOp op;
        LibreGPURawDataset ds;
        libregpu_dataset_init(&ds);
        memset(&op, 0, sizeof op);
        op.kind = LIBREGPU_OP_OPAQUE; op.blob = src; op.blob_size = sizeof src;
        CHECK(&h, libregpu_dataset_append(&ds, &op) == LIBREGPU_OK && ds.ops[0].blob != NULL &&
                  ds.ops[0].blob != src,
              "append deep-copies original bytes instead of retaining caller memory");
        src[0] = 0xEE;
        CHECK(&h, ds.ops[0].blob[0] == 1 && ds.ops[0].blob_size == sizeof src,
              "the dataset owns its copy; later caller writes cannot alias it");
        CHECK(&h, ds.ops[0].payload_hash != 0, "unhashed original bytes are hashed once on append");
        libregpu_dataset_release(&ds);
        CHECK(&h, ds.ops == NULL && ds.op_count == 0 && ds.op_capacity == 0,
              "release frees the operation array and returns the dataset to empty");
    }

    
    {
        LibreGPUConfig cfg = test_config();
        H oh;
        SimScene s;
        LibreGPUCycleInfo ci;
        bool match;
        libregpu_sim_scene_defaults(&s);
        s.inject_opaque = true;
        if (h_open(&oh, out, 0, &cfg)) {
            if (h_frame(&oh, &s, &ci, &match))
                CHECK(&oh, ci.was_baseline && match, "baseline containing opaque data");
            if (h_frame(&oh, &s, &ci, &match)) {
                CHECK(&oh, ci.decision == LIBREGPU_STREAM_FALLBACK_ORDINARY,
                      "opaque data disables the optimised path (decision was %s)",
                      libregpu_stream_action_str(ci.decision));
                CHECK(&oh, ci.submitted_mode == LIBREGPU_Z_ORDINARY && ci.ops_submitted == ci.ops_total,
                      "the ordinary path forwards the complete dataset unchanged");
                CHECK(&oh, match, "opaque data still renders correctly");
            }
            h_fold(&oh, failures, checks);
            h_close(&oh);
            h_fold(&oh, failures, checks);
        } else h.failures++;
    }

    
    {
        LibreGPUCapture cap;
        LibreGPURawDataset ds;
        LibreGPURawOp op;
        bool ready = false;
        libregpu_capture_init(&cap);
        libregpu_dataset_init(&ds);
        memset(&op, 0, sizeof op);
        op.kind = LIBREGPU_OP_DRAW; op.write_count = LIBREGPU_MAX_OP_RES + 1;   
        CHECK(&h, libregpu_capture_feed(&cap, &op, &ds, &ready) != LIBREGPU_OK && !ready,
              "an operation that cannot be preserved is reported, not truncated");
        libregpu_capture_mark_forwarded(&cap);                 
        memset(&op, 0, sizeof op);
        op.kind = LIBREGPU_OP_PRESENT;
        CHECK(&h, libregpu_capture_feed(&cap, &op, &ds, &ready) == LIBREGPU_OK && ready &&
                  ds.complete && ds.capture_failed && ds.already_forwarded,
              "the boundary is still found; loss and forwarding are recorded separately");
        CHECK(&h, !libregpu_dataset_is_submittable(&ds),
              "a forwarded dataset is never submittable");
        libregpu_capture_shutdown(&cap);
        libregpu_dataset_release(&ds);

        LibreGPUConfig cfg = test_config();
        SimScene s;
        LibreGPUCycleInfo ci;
        bool match;
        libregpu_sim_scene_defaults(&s);
        H ch;
        if (h_open(&ch, out, 0, &cfg)) {
            h_frame(&ch, &s, &ci, &match);
            LibreGPUSimStats before, after;
            libregpu_sim_stats(ch.be, &before);
            LibreGPURawDataset c;
            if (libregpu_sim_build_dataset(&s, &c) == LIBREGPU_OK) {
                libregpu_dataset_mark_forwarded(&c);              
                if (libregpu_sim_enqueue(ch.be, &c) != LIBREGPU_OK) libregpu_dataset_release(&c);
            }
            memset(&ci, 0, sizeof ci);
            const LibreGPUStatus st = libregpu_runtime_step(&ch.rt, &ci);
            libregpu_sim_stats(ch.be, &after);
            CHECK(&ch, st == LIBREGPU_ERR_VALIDATION && !ch.rt.have_baseline &&
                       ch.rt.metrics.forwarded_datasets == 1,
                  "an already-forwarded dataset is refused and X is voided");
            CHECK(&ch, before.submits_ordinary == after.submits_ordinary &&
                      before.submits_update == after.submits_update,
                  "LIBREGPU never re-submits a dataset the interposer already forwarded");
            if (h_frame(&ch, &s, &ci, &match))
                CHECK(&ch, ci.was_baseline && match, "the next complete dataset becomes a fresh baseline");
            h_fold(&ch, failures, checks);
            h_close(&ch);
            h_fold(&ch, failures, checks);
        } else h.failures++;
    }

    
    {
        LibreGPUConfig cfg = test_config();
        SimScene s;
        LibreGPUCycleInfo ci;
        bool match;
        libregpu_sim_scene_defaults(&s);
        H rh;
        if (h_open(&rh, out, 0, &cfg)) {
            LibreGPUSimStats s0, s1, s2;
            if (h_frame(&rh, &s, &ci, &match))
                CHECK(&rh, ci.was_baseline && ci.submitted_mode == LIBREGPU_Z_ORDINARY,
                      "initial X goes through the ordinary path and is retained");
            libregpu_sim_stats(rh.be, &s0);

            s.objects[0].x += 4;                              
            if (h_frame(&rh, &s, &ci, &match)) {
                libregpu_sim_stats(rh.be, &s1);
                CHECK(&rh, ci.decision == LIBREGPU_STREAM_MICRO_PIXEL &&
                          ci.submitted_mode == LIBREGPU_Z_INCREMENTAL && match,
                      "a micro-pixel decision submits Z, and the image matches Y");
                CHECK(&rh, s1.submits_update == s0.submits_update + 1 &&
                          s1.submits_ordinary == s0.submits_ordinary,
                      "the incremental cycle uses exactly one route: Z, not full Y");
                CHECK(&rh, ci.ops_submitted < ci.ops_total,
                      "the full Y dataset was not submitted before the Z decision");
            }

            s.shadow_pass = true;                             
            if (h_frame(&rh, &s, &ci, &match)) {
                libregpu_sim_stats(rh.be, &s2);
                CHECK(&rh, ci.submitted_mode == LIBREGPU_Z_ORDINARY &&
                          ci.ops_submitted == ci.ops_total && match,
                      "a fallback cycle forwards the original Y dataset unchanged");
                CHECK(&rh, s2.submits_ordinary == s1.submits_ordinary + 1 &&
                          s2.submits_update == s1.submits_update,
                      "the fallback cycle also uses exactly one route: ordinary Y");
            }

            s.objects[1].x += 3;
            if (h_frame(&rh, &s, &ci, &match))
                CHECK(&rh, ci.submitted_mode == LIBREGPU_Z_INCREMENTAL && match && rh.rt.have_baseline &&
                          !ci.baseline_lost,
                      "retained X survives a consecutive incremental submission");
            s.objects[1].x += 1;
            if (h_frame(&rh, &s, &ci, &match))
                CHECK(&rh, ci.submitted_mode == LIBREGPU_Z_INCREMENTAL && match && rh.rt.have_baseline &&
                          !ci.baseline_lost,
                      "retained X survives a second consecutive incremental submission");
            h_fold(&rh, failures, checks);
            h_close(&rh);
            h_fold(&rh, failures, checks);
        } else h.failures++;
    }

    
    {
        LibreGPUConfig cfg = test_config();
        SimScene s;
        LibreGPUCycleInfo ci;
        bool match;
        libregpu_sim_scene_defaults(&s);
        H ih;
        if (h_open(&ih, out, 0, &cfg)) {
            h_frame(&ih, &s, &ci, &match);
            s.cam_yaw_px = 3; s.objects[0].x += 3;
            if (h_frame(&ih, &s, &ci, &match))
                CHECK(&ih, ci.micro_pixel && ci.camera &&
                           ci.decision == LIBREGPU_STREAM_BOTH && match,
                      "micro-pixel and camera both answer Y independently and combine");

            ih.rt.config.enable_camera = false;     
            s.cam_yaw_px = 6; s.objects[0].x += 3;
            if (h_frame(&ih, &s, &ci, &match))
                CHECK(&ih, ci.micro_pixel && !ci.camera &&
                           ci.decision == LIBREGPU_STREAM_FALLBACK_ORDINARY && match,
                      "micro-pixel keeps its Y verdict when the camera unit answers N");
            h_fold(&ih, failures, checks);
            h_close(&ih);
            h_fold(&ih, failures, checks);
        } else h.failures++;
    }

    
    {
        LibreGPUConfig cfg = test_config();
        SimScene s;
        LibreGPUCycleInfo ci;
        bool match;
        libregpu_sim_scene_defaults(&s);
        H oh;
        if (h_open(&oh, out, 0, &cfg)) {
            h_frame(&oh, &s, &ci, &match);
            LibreGPURawDataset bad;
            if (libregpu_sim_build_dataset(&s, &bad) == LIBREGPU_OK) {
                bad.ops[1].kind = (LibreGPUOpKind)99;          
                if (libregpu_sim_enqueue(oh.be, &bad) != LIBREGPU_OK) libregpu_dataset_release(&bad);
            }
            libregpu_sim_inject_submit_fault(oh.be, (SimSubmitFault){ 0, 1, LIBREGPU_ERR_NO_MEMORY });
            memset(&ci, 0, sizeof ci);
            const LibreGPUStatus st = libregpu_runtime_step(&oh.rt, &ci);
            CHECK(&oh, st == LIBREGPU_ERR_NO_MEMORY && oh.rt.have_baseline && !ci.baseline_lost,
                  "a clean rejection on the recovery path leaves the retained X valid");
            if (h_frame(&oh, &s, &ci, &match))
                CHECK(&oh, !ci.was_baseline && ci.submitted_mode == LIBREGPU_Z_NO_IMPACT && match,
                      "the preserved X is still the comparison baseline afterwards");
            h_fold(&oh, failures, checks);
            h_close(&oh);
            h_fold(&oh, failures, checks);
        } else h.failures++;
    }

    
    {
        LibreGPUConfig cfg = test_config();
        SimScene s;
        LibreGPUCycleInfo ci;
        bool match;
        libregpu_sim_scene_defaults(&s);
        H fh;
        if (h_open(&fh, out, 2, &cfg)) {
            uint64_t prev_completed = fh.be->completed_fence(fh.be->ctx);
            for (int i = 0; i < 3; i++) {
                h_frame(&fh, &s, &ci, &match);
                const uint64_t now = fh.be->completed_fence(fh.be->ctx);
                CHECK(&fh, now >= prev_completed, "completed_fence never moves backwards");
                prev_completed = now;
            }
            LibreGPUSimStats fs;
            libregpu_sim_stats(fh.be, &fs);
            CHECK(&fh, fs.max_fence > 0 && fh.rt.last_fence == fs.max_fence,
                  "successful submissions return strictly advancing fences");
            CHECK(&fh, fs.completed_fence <= fs.max_fence,
                  "completed_fence reports only work that has actually finished");

            s.transient_id = 10;
            h_frame(&fh, &s, &ci, &match);                     
            CHECK(&fh, match && libregpu_sim_destroyed_count(fh.be) == 0,
                  "a released resource is not destroyed while its submission is in flight");
            s.transient_id = 0;
            h_frame(&fh, &s, &ci, &match);
            h_frame(&fh, &s, &ci, &match);
            h_frame(&fh, &s, &ci, &match);
            CHECK(&fh, libregpu_sim_destroyed_count(fh.be) == 1,
                  "destruction happens only after release and fence completion");

            fh.be->wait_idle(fh.be->ctx);
            libregpu_sim_stats(fh.be, &fs);
            CHECK(&fh, fs.completed_fence == fs.max_fence,
                  "wait_idle completes every accepted submission");
            h_fold(&fh, failures, checks);
            h_close(&fh);
            h_fold(&fh, failures, checks);
        } else h.failures++;
    }

    
    {
        LibreGPUConfig cfg = test_config();
        SimScene s;
        LibreGPUCycleInfo ci;
        bool match;
        libregpu_sim_scene_defaults(&s);
        H bh;
        if (h_open(&bh, out, 0, &cfg)) {
            h_frame(&bh, &s, &ci, &match);
            s.objects[0].x += 4;
            libregpu_sim_inject_submit_fault(bh.be, (SimSubmitFault){ 0, 1, LIBREGPU_ERR_NO_MEMORY });
            memset(&ci, 0, sizeof ci);
            if (h_frame(&bh, &s, &ci, &match)) {
                CHECK(&bh, ci.fell_back_after_failure && ci.submitted_mode == LIBREGPU_Z_ORDINARY && match,
                      "a clean rejection leaves X valid and retries the ordinary path");
                CHECK(&bh, bh.rt.have_baseline, "a clean rejection never voids the baseline");
            }
            s.objects[0].x += 1;
            libregpu_sim_inject_submit_fault(bh.be, (SimSubmitFault){ 0, 1, LIBREGPU_ERR_BACKEND });
            {
                LibreGPURawDataset ds;
                if (libregpu_sim_build_dataset(&s, &ds) == LIBREGPU_OK &&
                    libregpu_sim_enqueue(bh.be, &ds) != LIBREGPU_OK)
                    libregpu_dataset_release(&ds);
            }
            memset(&ci, 0, sizeof ci);
            const LibreGPUStatus st = libregpu_runtime_step(&bh.rt, &ci);
            CHECK(&bh, st == LIBREGPU_ERR_BACKEND && !bh.rt.have_baseline && ci.baseline_lost,
                  "an unknown-effect failure voids X and is not retried");
            if (h_frame(&bh, &s, &ci, &match))
                CHECK(&bh, ci.was_baseline && match, "recovery starts from a fresh baseline");
            h_fold(&bh, failures, checks);
            h_close(&bh);
            h_fold(&bh, failures, checks);
        } else h.failures++;
    }

    
    {
        LibreGPUBackend *be = libregpu_sim_backend_create(0);
        SimScene s;
        libregpu_sim_scene_defaults(&s);
        LibreGPURawDataset ds;
        LibreGPUZPlan p;
        LibreGPURebuildResult rr;
        LibreGPUSimStats b0, b1;
        if (!be || libregpu_sim_build_dataset(&s, &ds) != LIBREGPU_OK) {
            CHECK(&h, false, "could not assemble the invalid-plan fixture");
        } else {
            memset(&p, 0, sizeof p);
            p.mode = LIBREGPU_Z_NO_IMPACT;
            LibreGPUReplayOp missing_present[] = { { 0, false } };   
            p.replay = missing_present;
            p.replay_count = 1;
            libregpu_sim_stats(be, &b0);
            const LibreGPUStatus st = dataset_rebuilder_execute(&p, &ds, be, NULL, &rr);
            libregpu_sim_stats(be, &b1);
            CHECK(&h, st == LIBREGPU_ERR_VALIDATION,
                  "a Z plan that drops a mandatory operation is refused");
            CHECK(&h, b0.submits_ordinary == b1.submits_ordinary &&
                      b0.submits_update == b1.submits_update,
                  "an invalid Z is rejected before the backend is called");
            libregpu_dataset_release(&ds);
        }
        if (be) libregpu_backend_destroy(be);
    }

    h_fold(&h, failures, checks);
}




static void test_m2_lifecycle(FILE *out, int *failures, int *checks)
{
    fprintf(out, "[m2] complete X/Y/Z lifecycle: nine required scenarios, pixels vs offline truth\n");
    LibreGPUConfig cfg = test_config();
    H h;
    LibreGPUCycleInfo ci;
    bool match;
    SimScene s;
    uint32_t ref_prev[SIM_W * SIM_H], ref_cur[SIM_W * SIM_H];
    const uint64_t surface = (uint64_t)SIM_W * (uint64_t)SIM_H;
    libregpu_sim_scene_defaults(&s);

    if (!h_open(&h, out, 2, &cfg)) { h_fold(&h, failures, checks); return; }

    
    memset(&ci, 0, sizeof ci);
    if (h_frame_fb(&h, &s, &ci, &match, ref_cur, NULL)) {
        CHECK(&h, match, "initial X: display differs from the reference");
        CHECK(&h, ci.was_baseline && ci.decision == LIBREGPU_STREAM_FALLBACK_ORDINARY &&
                  ci.submitted_mode == LIBREGPU_Z_ORDINARY,
              "initial X must be submitted as the baseline through the ordinary path");
        CHECK(&h, h.rt.have_baseline, "the first dataset becomes X");
    }
    memcpy(ref_prev, ref_cur, sizeof ref_prev);

    
    memset(&ci, 0, sizeof ci);
    if (h_frame_fb(&h, &s, &ci, &match, ref_cur, NULL)) {
        CHECK(&h, match, "unchanged frame: display differs from the reference");
        CHECK(&h, ci.decision == LIBREGPU_STREAM_FALLBACK_NO_IMPACT &&
                  ci.submitted_mode == LIBREGPU_Z_NO_IMPACT,
              "unchanged Y must take the no-impact fallback");
        CHECK(&h, frame_diff(ref_prev, ref_cur) == 0 && ci.changed_pixels == 0 &&
                  ci.affected_pixels == 0,
              "unchanged Y: no changed and no affected pixels");
    }
    memcpy(ref_prev, ref_cur, sizeof ref_prev);

    
    s.objects[0].x += 3;
    memset(&ci, 0, sizeof ci);
    if (h_frame_fb(&h, &s, &ci, &match, ref_cur, NULL)) {
        const uint64_t truth = frame_diff(ref_prev, ref_cur);
        CHECK(&h, match, "micro-pixel-only: display differs from the reference");
        CHECK(&h, ci.decision == LIBREGPU_STREAM_MICRO_PIXEL && ci.micro_pixel && !ci.camera,
              "a moved object must be a Micro-Pixel-only decision");
        CHECK(&h, ci.submitted_mode == LIBREGPU_Z_INCREMENTAL && ci.ops_submitted < ci.ops_total,
              "Micro-Pixel must submit a selective Z update, not the whole frame");
        CHECK(&h, truth > 0, "offline truth: moving an object changes pixels");
        CHECK(&h, ci.changed_pixels >= truth && ci.changed_pixels > ci.changes &&
                  ci.changed_pixels <= surface,
              "changed_pixels (%llu) must be a conservative pixel count >= offline diff (%llu), "
              "not a record count (%zu)",
              (unsigned long long)ci.changed_pixels, (unsigned long long)truth, ci.changes);
        CHECK(&h, ci.affected_pixels >= truth && ci.affected_pixels <= surface,
              "affected_pixels (%llu) must cover the offline diff (%llu) and stay on the surface",
              (unsigned long long)ci.affected_pixels, (unsigned long long)truth);
    }
    memcpy(ref_prev, ref_cur, sizeof ref_prev);

    
    s.cam_yaw_px += 3;
    memset(&ci, 0, sizeof ci);
    if (h_frame_fb(&h, &s, &ci, &match, ref_cur, NULL)) {
        CHECK(&h, match, "camera-only: display differs from the reference");
        CHECK(&h, ci.decision == LIBREGPU_STREAM_CAMERA && !ci.micro_pixel && ci.camera,
              "a pure camera pan must be a Camera-only decision");
        CHECK(&h, ci.submitted_mode == LIBREGPU_Z_INCREMENTAL, "Camera must submit an incremental Z update");
        CHECK(&h, ci.changed_pixels == 0 && ci.affected_pixels > 0 && ci.affected_pixels <= surface,
              "no operation changed, so changed_pixels is 0; the reprojection strips are affected");
    }
    memcpy(ref_prev, ref_cur, sizeof ref_prev);

    
    s.objects[1].x += 2;
    s.cam_yaw_px += 1;
    memset(&ci, 0, sizeof ci);
    if (h_frame_fb(&h, &s, &ci, &match, ref_cur, NULL)) {
        CHECK(&h, match, "combined: display differs from the reference");
        CHECK(&h, ci.decision == LIBREGPU_STREAM_BOTH && ci.micro_pixel && ci.camera,
              "a compatible object move plus whole-pixel pan must be a combined update");
        CHECK(&h, ci.submitted_mode == LIBREGPU_Z_INCREMENTAL, "combined update must submit an incremental Z");
        CHECK(&h, ci.changed_pixels > 0 && ci.affected_pixels > 0 &&
                  ci.changed_pixels <= surface && ci.affected_pixels <= surface,
              "combined update reports both pixel measures, bounded by the surface");
    }
    memcpy(ref_prev, ref_cur, sizeof ref_prev);

    
    s.shadow_pass = true;                       
    memset(&ci, 0, sizeof ci);
    if (h_frame_fb(&h, &s, &ci, &match, ref_cur, NULL)) {
        CHECK(&h, match, "N/N fallback: display differs from the reference");
        CHECK(&h, !ci.micro_pixel && !ci.camera &&
                  ci.decision == LIBREGPU_STREAM_FALLBACK_ORDINARY &&
                  ci.submitted_mode == LIBREGPU_Z_ORDINARY,
              "a structural change must fall back to ordinary execution");
        CHECK(&h, ci.changed_pixels == 0 && ci.affected_pixels == 0,
              "an ordinary fallback claims no pixel savings");
    }
    memcpy(ref_prev, ref_cur, sizeof ref_prev);

    
    s.objects[2].y += 4;
    memset(&ci, 0, sizeof ci);
    if (h_frame_fb(&h, &s, &ci, &match, ref_cur, NULL)) {
        CHECK(&h, match, "Z: the rebuilt update differs from the reference render");
        CHECK(&h, ci.submitted_mode == LIBREGPU_Z_INCREMENTAL && ci.ops_submitted < ci.ops_total &&
                  ci.decision == LIBREGPU_STREAM_MICRO_PIXEL,
              "the update after a fallback is a real Z rebuild (selective ops), not ordinary");
    }
    memcpy(ref_prev, ref_cur, sizeof ref_prev);

    
    {
        LibreGPUSimStats st;
        libregpu_sim_stats(h.be, &st);
        CHECK(&h, h.rt.metrics.lifetime_errors == 0, "no lifetime errors were recorded");
        CHECK(&h, st.submits_update > 0, "incremental updates went through the update submission route");
        CHECK(&h, st.completed_fence <= st.max_fence, "completion never outruns submission");
    }
    fprintf(out, "  lifecycle: X -> unchanged -> micro -> camera -> both -> fallback -> Z, all images OK\n");

    h_fold(&h, failures, checks);
    h_close(&h);                                 
    h_fold(&h, failures, checks);

    
    
    {
        H fh;
        libregpu_sim_scene_defaults(&s);
        if (h_open(&fh, out, 1, &cfg)) {
            h_frame(&fh, &s, &ci, &match);
            s.objects[0].x += 4;
            libregpu_sim_inject_submit_fault(fh.be, (SimSubmitFault){ 0, 1, LIBREGPU_ERR_NO_MEMORY });
            memset(&ci, 0, sizeof ci);
            if (h_frame(&fh, &s, &ci, &match)) {
                CHECK(&fh, match && ci.fell_back_after_failure &&
                          ci.submitted_mode == LIBREGPU_Z_ORDINARY && fh.rt.have_baseline,
                      "submission: a clean rejection retries ordinary and keeps X");
            }
            s.objects[0].x += 1;
            memset(&ci, 0, sizeof ci);
            if (h_frame(&fh, &s, &ci, &match))
                CHECK(&fh, match && ci.submitted_mode == LIBREGPU_Z_INCREMENTAL,
                      "submission: optimisation resumes after a clean rejection");

            s.objects[1].x += 5;
            libregpu_sim_inject_submit_fault(fh.be, (SimSubmitFault){ 0, 1, LIBREGPU_ERR_BACKEND });
            {
                LibreGPURawDataset ds;
                if (libregpu_sim_build_dataset(&s, &ds) == LIBREGPU_OK &&
                    libregpu_sim_enqueue(fh.be, &ds) != LIBREGPU_OK) libregpu_dataset_release(&ds);
                memset(&ci, 0, sizeof ci);
                const LibreGPUStatus st = libregpu_runtime_step(&fh.rt, &ci);
                CHECK(&fh, st == LIBREGPU_ERR_BACKEND && !fh.rt.have_baseline && ci.baseline_lost,
                      "submission: an unknown-state failure voids X and is not retried");
            }
            s.objects[2].x += 2;
            memset(&ci, 0, sizeof ci);
            if (h_frame(&fh, &s, &ci, &match))
                CHECK(&fh, match && ci.was_baseline, "submission: recovery starts from a fresh baseline");
            fprintf(out, "  submission failure: clean rejection retries, unknown state restarts from X\n");
            h_fold(&fh, failures, checks);
            h_close(&fh);
            h_fold(&fh, failures, checks);
        } else {
            h_fold(&fh, failures, checks);
        }
    }

    
    {
        H ch;
        libregpu_sim_scene_defaults(&s);
        if (h_open(&ch, out, 0, &cfg)) {
            h_frame(&ch, &s, &ci, &match);

            LibreGPURawDataset lost;
            if (libregpu_sim_build_dataset(&s, &lost) == LIBREGPU_OK) {
                lost.capture_failed = true;
                if (libregpu_sim_enqueue(ch.be, &lost) != LIBREGPU_OK) libregpu_dataset_release(&lost);
            }
            memset(&ci, 0, sizeof ci);
            LibreGPUStatus st = libregpu_runtime_step(&ch.rt, &ci);
            CHECK(&ch, st == LIBREGPU_ERR_VALIDATION && ch.rt.metrics.capture_failed_datasets == 1 &&
                       !ch.rt.have_baseline,
                  "capture: a lost dataset is refused, counted and voids X");
            memset(&ci, 0, sizeof ci);
            if (h_frame(&ch, &s, &ci, &match))
                CHECK(&ch, match && ci.was_baseline, "capture: recovery after a lost dataset");

            LibreGPURawDataset fwd;
            if (libregpu_sim_build_dataset(&s, &fwd) == LIBREGPU_OK) {
                libregpu_dataset_mark_forwarded(&fwd);
                if (libregpu_sim_enqueue(ch.be, &fwd) != LIBREGPU_OK) libregpu_dataset_release(&fwd);
            }
            memset(&ci, 0, sizeof ci);
            st = libregpu_runtime_step(&ch.rt, &ci);
            CHECK(&ch, st == LIBREGPU_ERR_VALIDATION && ch.rt.metrics.forwarded_datasets == 1 &&
                       !ch.rt.have_baseline,
                  "capture: an already-forwarded dataset is refused distinctly and voids X");
            memset(&ci, 0, sizeof ci);
            if (h_frame(&ch, &s, &ci, &match))
                CHECK(&ch, match && ci.was_baseline, "capture: recovery after a forwarded dataset");
            fprintf(out, "  capture failure: lost and forwarded refused distinctly; next frame is a baseline\n");
            h_fold(&ch, failures, checks);
            h_close(&ch);
            h_fold(&ch, failures, checks);
        } else {
            h_fold(&ch, failures, checks);
        }
    }
}



static void test_m2_gir(FILE *out, int *failures, int *checks)
{
    fprintf(out, "[m2-gir] roles, dependency closure and exact pixel-union measurement\n");
    H h;
    memset(&h, 0, sizeof h);
    h.out = out;

    
    {
        SimScene s;
        libregpu_sim_scene_defaults(&s);
        LibreGPURawDataset r;
        GirDataset g;
        if (libregpu_sim_build_dataset(&s, &r) == LIBREGPU_OK && gir_build(&g, &r) == LIBREGPU_OK) {
            CHECK(&h, gir_record_has(&g.records[g.camera_record], GIR_ROLE_CAMERA) &&
                      gir_record_has(&g.records[g.camera_record], GIR_ROLE_MANDATORY),
                  "the camera record is mandatory and classified as the camera");
            const size_t clear = 1, present = g.record_count - 1;
            CHECK(&h, gir_record_has(&g.records[clear], GIR_ROLE_TARGET_WRITER) &&
                      gir_record_has(&g.records[clear], GIR_ROLE_CLEARS_TARGET) &&
                      !gir_record_has(&g.records[clear], GIR_ROLE_INTER_WRITER),
                  "the full frame clear is a target writer with the clear flag");
            CHECK(&h, gir_record_has(&g.records[present], GIR_ROLE_PRESENT) &&
                      gir_record_has(&g.records[present], GIR_ROLE_READS_TARGET) &&
                      !gir_record_has(&g.records[present], GIR_ROLE_TARGET_WRITER),
                  "the present record reads the target and is mandatory");
            const GirResource *t = gir_find_resource(&g, g.present_target);
            CHECK(&h, t && gir_resource_has(t, GIR_RES_TARGET) && gir_resource_has(t, GIR_RES_WRITTEN),
                  "the present target resource is classified as the target");
            CHECK(&h, gir_record_has(&g.records[2], GIR_ROLE_REPLAYABLE) &&
                      gir_record_has(&g.records[2], GIR_ROLE_TARGET_WRITER) &&
                      !gir_record_has(&g.records[2], GIR_ROLE_READS_TARGET),
                  "an object draw is a replayable target writer, not a target reader");
            gir_release(&g);
        } else {
            CHECK(&h, false, "could not build the default GIR");
        }
        libregpu_dataset_release(&r);
    }

    
    {
        SimScene s;
        libregpu_sim_scene_defaults(&s);
        s.shadow_pass = true;
        s.shadow_overwrite = true;
        LibreGPURawDataset r;
        GirDataset g;
        if (libregpu_sim_build_dataset(&s, &r) == LIBREGPU_OK && gir_build(&g, &r) == LIBREGPU_OK) {
            
            const size_t shadow = 1;
            const size_t present = g.record_count - 1;
            const GirResource *sr = gir_find_resource(&g, (LibreGPUResourceId){ SIM_SHADOW_ID, 1 });
            CHECK(&h, gir_record_has(&g.records[shadow], GIR_ROLE_INTER_WRITER) &&
                      !gir_record_has(&g.records[shadow], GIR_ROLE_TARGET_WRITER),
                  "a shadow producer is an intermediate writer, not a target writer");
            CHECK(&h, sr && gir_resource_has(sr, GIR_RES_WRITTEN) &&
                      !gir_resource_has(sr, GIR_RES_TARGET),
                  "the shadow resource is written but is not the present target");

            uint8_t *seed = (uint8_t *)calloc(g.record_count, 1);
            uint8_t *marked = (uint8_t *)calloc(g.record_count, 1);
            if (seed && marked) {
                seed[shadow] = 1;
                (void)gir_propagate_affected(&g, seed, marked);
                bool consumers = true;
                for (size_t i = 0; i < g.record_count; i++) {
                    bool reads_shadow = false;
                    for (uint8_t k = 0; k < g.records[i].read_count; k++)
                        if (libregpu_res_equal(g.records[i].reads[k],
                                            (LibreGPUResourceId){ SIM_SHADOW_ID, 1 }))
                            reads_shadow = true;
                    if (reads_shadow && !marked[i]) consumers = false;
                }
                CHECK(&h, consumers, "seeding a shadow producer affects every consumer of the shadow");
                CHECK(&h, marked[shadow], "the seed itself stays marked");
                CHECK(&h, !marked[present],
                      "target edges are not propagated: the present record is not affected");
                CHECK(&h, !marked[g.camera_record], "unrelated records (camera) stay unmarked");
                free(seed);
                free(marked);
            } else {
                free(seed); free(marked);
                CHECK(&h, false, "out of memory in the closure test");
            }
            gir_release(&g);
        } else {
            CHECK(&h, false, "could not build the shadow GIR");
        }
        libregpu_dataset_release(&r);
    }

    
    {
        const LibreGPURect clip = libregpu_rect_make(0, 0, SIM_W, SIM_H);
        const LibreGPURect rects[] = {
            {  0,  0, 10, 10 },
            {  5,  5, 15, 15 },
            {  8,  8, 12, 12 },        
            { -5, -5,  3,  3 },        
            { 60, 30, 70, 40 }         
        };
        
        CHECK(&h, libregpu_rect_union_area(rects, 5, clip) == 199,
              "union area counts overlaps once and clips to the surface (got %llu)",
              (unsigned long long)libregpu_rect_union_area(rects, 5, clip));
        CHECK(&h, libregpu_rect_union_area(NULL, 0, clip) == 0, "an empty union is zero");
        const LibreGPURect outside[] = { { -50, -50, -10, -10 } };
        CHECK(&h, libregpu_rect_union_area(outside, 1, clip) == 0,
              "a fully clipped rectangle contributes nothing");

        LibreGPURect many[600];
        for (size_t i = 0; i < 600; i++) many[i] = clip;    
        CHECK(&h, libregpu_rect_union_area(many, 600, clip) ==
                  (uint64_t)SIM_W * (uint64_t)SIM_H,
              "above the sweep cap the bound is conservative but never exceeds the surface");
    }

    h_fold(&h, failures, checks);
}

static void test_defaults_and_version(FILE *out, int *failures, int *checks)
{
    fprintf(out, "[defaults] camera and governor off, micro-pixel on; one version string\n");
    H h;
    memset(&h, 0, sizeof h);
    h.out = out;
    LibreGPUConfig c;
    libregpu_config_defaults(&c);
    CHECK(&h, !c.enable_camera, "the Camera Update Unit is OFF by default");
    CHECK(&h, c.enable_micro_pixel, "Micro-Pixel Update is on by default");
    CHECK(&h, !(c.target_loops_per_second > 0.0), "the Passive FPS Governor is off by default");

    LibreGPUCycleInfo last_info;
    memset(&last_info, 0, sizeof last_info);
    SimScene s;
    libregpu_sim_scene_defaults(&s);
    H ph;
    if (h_open(&ph, out, 0, &c)) {
        EXPECT_FRAME(&ph, "baseline (default configuration)", &s, LIBREGPU_STREAM_FALLBACK_ORDINARY, LIBREGPU_Z_ORDINARY);
        s.cam_yaw_px = 3;
        EXPECT_FRAME(&ph, "pan 3px: camera unit is off -> ordinary", &s, LIBREGPU_STREAM_FALLBACK_ORDINARY, LIBREGPU_Z_ORDINARY);
        CHECK(&ph, last_info.camera_reason && strstr(last_info.camera_reason, "disabled"),
              "the reason says the camera unit is disabled");
        s.objects[0].x += 5; s.objects[0].y += 2;
        EXPECT_FRAME(&ph, "moved object: micro-pixel works by default", &s, LIBREGPU_STREAM_MICRO_PIXEL, LIBREGPU_Z_INCREMENTAL);
        h_fold(&ph, &h.failures, &h.checks);
        h_close(&ph);
        h_fold(&ph, &h.failures, &h.checks);
    } else {
        h.failures += ph.failures;
    }

    FILE *f = tmpfile();
    if (f) {
        char buf[256];
        ui_print_banner(f);
        rewind(f);
        const size_t got = fread(buf, 1, sizeof buf - 1, f);
        buf[got] = '\0';
        CHECK(&h, strstr(buf, LIBREGPU_VERSION) != NULL, "the launcher banner shows the version (%s)", LIBREGPU_VERSION);
        CHECK(&h, strstr(LIBREGPU_VERSION, "2.0.0") != NULL, "the version string is 2.0.0");
        fclose(f);
    } else {
        CHECK(&h, false, "could not create a temporary file for the banner check");
    }
    h_fold(&h, failures, checks);
}

static void test_units(FILE *out, int *failures, int *checks)
{
    fprintf(out, "[units] GIR, numerics, launcher input, governor\n");
    H h;
    memset(&h, 0, sizeof h);
    h.out = out;

    
    {
        LibreGPUConfig def;
        libregpu_config_defaults(&def);
        CHECK(&h, def.enable_micro_pixel, "Micro-Pixel Update stays enabled by default");
        CHECK(&h, !def.enable_camera, "Camera Update stays in the code but is off by default");
        CHECK(&h, def.target_loops_per_second == 0.0, "the Passive FPS Governor stays off by default");
    }

    
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
        b.capture_failed = true;
        CHECK(&h, gir_build(&g, &b) == LIBREGPU_ERR_VALIDATION, "GIR rejects capture_failed datasets");
        b.capture_failed = false;
        b.already_forwarded = true;
        CHECK(&h, gir_build(&g, &b) == LIBREGPU_ERR_VALIDATION, "GIR rejects already-forwarded datasets");
        b.already_forwarded = false;
        b.ops[1].region.x0 = 1;                             
        CHECK(&h, gir_build(&g, &b) == LIBREGPU_OK && !g.has_surface, "surface stays unknown unless the clear is a full-target clear");
        gir_release(&g);
    }
    libregpu_dataset_release(&a); libregpu_dataset_release(&b); libregpu_dataset_release(&c);

    
    {
        SimScene w;
        libregpu_sim_scene_defaults(&w);
        w.shadow_pass = true; w.shadow_overwrite = true;
        LibreGPURawDataset r;
        GirDataset g;
        bool waw = false, raw_dep = false;
        if (libregpu_sim_build_dataset(&w, &r) == LIBREGPU_OK && gir_build(&g, &r) == LIBREGPU_OK) {
            for (size_t i = 0; i < g.dep_count; i++) {
                if (g.deps[i].kind == GIR_DEP_WAW) waw = true;
                if (g.deps[i].kind == GIR_DEP_RAW) raw_dep = true;
                if (i && g.deps[i].to < g.deps[i - 1].to) CHECK(&h, false, "dependencies must stay sorted by consumer");
            }
            CHECK(&h, waw && raw_dep, "GIR records RAW and WAW dependency edges");
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
        cfg = test_config(); cfg.target_loops_per_second = INFINITY;
        LibreGPUBackend *be = libregpu_sim_backend_create(0);
        LibreGPURuntime rt;
        CHECK(&h, be && libregpu_runtime_init(&rt, &cfg, be) == LIBREGPU_ERR_INVALID_ARG, "runtime refuses a non-finite loop rate");
        libregpu_backend_destroy(be);

        LibreGPUGovernor g;
        CHECK(&h, gov_init(&g, NAN) == LIBREGPU_ERR_INVALID_ARG && !g.enabled, "governor rejects NaN");
        CHECK(&h, gov_init(&g, -5.0) == LIBREGPU_ERR_INVALID_ARG && !g.enabled, "governor rejects negative rates");
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
        char *nan_args[]   = { "libregpu-core", "--fps", "nan" };
        char *inf_args[]   = { "libregpu-core", "--fps", "inf" };
        char *miss_args[]  = { "libregpu-core", "--game", "--demo" };
        char *nothing[]    = { "libregpu-core", "--fps" };
        char *unknown[]    = { "libregpu-core", "--bogus" };
        char *good[]       = { "libregpu-core", "--fps", "60", "--frames", "10", "--demo", "--quiet" };
        char *compat[]     = { "libregpu-core", "--compat" };
        char *doctor[]     = { "libregpu-core", "--doctor" };
        char longpath[700];
        memset(longpath, 'a', sizeof longpath - 1);
        longpath[sizeof longpath - 1] = '\0';
        char *long_args[]  = { "libregpu-core", "--game", longpath };
        CHECK(&h, ui_parse_args(&st, 3, nan_args, err, sizeof err) == LIBREGPU_ERR_INVALID_ARG, "--fps nan is rejected");
        CHECK(&h, ui_parse_args(&st, 3, inf_args, err, sizeof err) == LIBREGPU_ERR_INVALID_ARG, "--fps inf is rejected");
        CHECK(&h, ui_parse_args(&st, 3, miss_args, err, sizeof err) == LIBREGPU_ERR_INVALID_ARG && !st.demo,
              "an option is not accepted as another option's value");
        CHECK(&h, ui_parse_args(&st, 2, nothing, err, sizeof err) == LIBREGPU_ERR_INVALID_ARG, "a missing value is reported");
        CHECK(&h, ui_parse_args(&st, 2, unknown, err, sizeof err) == LIBREGPU_ERR_INVALID_ARG, "unknown options are reported");
        CHECK(&h, ui_parse_args(&st, 3, long_args, err, sizeof err) == LIBREGPU_ERR_INVALID_ARG && !st.game_selected,
              "a game path that does not fit is rejected, not truncated");
        CHECK(&h, ui_parse_args(NULL, 1, good, err, sizeof err) == LIBREGPU_ERR_INVALID_ARG, "NULL settings are rejected");
        CHECK(&h, ui_parse_args(&st, 7, good, err, sizeof err) == LIBREGPU_OK && st.demo && st.quiet &&
                  st.demo_frames == 10 && st.config.target_loops_per_second == 60.0, "a valid command line parses");
        CHECK(&h, ui_parse_args(&st, 2, compat, err, sizeof err) == LIBREGPU_OK && st.compat && !st.doctor &&
                  !st.demo && !st.selftest, "--compat selects the compatibility report");
        CHECK(&h, ui_parse_args(&st, 2, doctor, err, sizeof err) == LIBREGPU_OK && st.doctor && !st.compat &&
                  !st.demo && !st.selftest, "--doctor selects the diagnostics report");
    }
    h_fold(&h, failures, checks);
}

int libregpu_selftest(FILE *out)
{
    int failures = 0, checks = 0;
    fprintf(out, "LIBREGPU %s (%s, %s) self-test (simulated backend; GPU time is a cost model)\n\n",
            LIBREGPU_VERSION, LIBREGPU_STEP_LABEL, LIBREGPU_RELEASE);
    test_pipeline(out, &failures, &checks);
    test_camera_model(out, &failures, &checks);
    test_dependency_integrity(out, &failures, &checks);
    test_lifetime(out, &failures, &checks);
    test_backend_failures(out, &failures, &checks);
    test_capture(out, &failures, &checks);
    test_rebuilder_validation(out, &failures, &checks);
    test_memory_manager(out, &failures, &checks);
    test_session(out, &failures, &checks);
    test_defaults_and_version(out, &failures, &checks);
    test_contracts(out, &failures, &checks);
    test_m2_lifecycle(out, &failures, &checks);
    test_m2_gir(out, &failures, &checks);
    test_units(out, &failures, &checks);
    fprintf(out, "\n%d checks, %d failed\n", checks, failures);
    return failures;
}

