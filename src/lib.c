





















#ifndef LIBREGPU_LIB_DECLARATIONS_ONLY
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L   
#  endif
#endif



































#ifndef LIBREGPU_LIB_H
#define LIBREGPU_LIB_H

#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define LIBREGPU_CODE_NAME         "LibreGPU-rc1"
#define LIBREGPU_VERSION           "rc1"
#define LIBREGPU_STEP_LABEL        "Step 2"     
#define LIBREGPU_NO_INDEX          UINT32_MAX
#define LIBREGPU_MAX_OP_RES        4            
#define LIBREGPU_MAX_REGIONS       4            
#define LIBREGPU_MAX_DATASET_OPS   (1u << 24)   
#define LIBREGPU_MAX_COORD         (1 << 30)    







#define LIBREGPU_SCRATCH_ID_BASE   0x80000000u




typedef enum {
    LIBREGPU_OK = 0,
    LIBREGPU_NO_DATA,             
    LIBREGPU_ERR_INVALID_ARG,
    LIBREGPU_ERR_NO_MEMORY,
    LIBREGPU_ERR_STATE,           
    LIBREGPU_ERR_UNSUPPORTED,
    LIBREGPU_ERR_BACKEND,         
    LIBREGPU_ERR_LIFETIME,        
    LIBREGPU_ERR_VALIDATION,      
    LIBREGPU_ERR_SESSION_INVALID  
} LibreGPUStatus;

const char *libregpu_status_str(LibreGPUStatus s);




static inline uint64_t libregpu_mix64(uint64_t x)
{
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

static inline uint64_t libregpu_hash_combine(uint64_t seed, uint64_t v)
{
    return libregpu_mix64(seed ^ (v + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2)));
}

static inline uint64_t libregpu_sat_add_u64(uint64_t a, uint64_t b)
{ return a > UINT64_MAX - b ? UINT64_MAX : a + b; }

uint64_t libregpu_hash_bytes(const void *data, size_t len);   
uint64_t libregpu_now_ns(void);                               
void     libregpu_sleep_ns(uint64_t ns);



typedef struct { int32_t x0, y0, x1, y1; } LibreGPURect;

static inline int32_t libregpu_sat32(int64_t v)
{ return v > INT32_MAX ? INT32_MAX : (v < INT32_MIN ? INT32_MIN : (int32_t)v); }

static inline LibreGPURect libregpu_rect_make(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{ LibreGPURect r = { x0, y0, x1, y1 }; return r; }
static inline bool libregpu_rect_empty(LibreGPURect r) { return r.x1 <= r.x0 || r.y1 <= r.y0; }
static inline bool libregpu_rect_intersects(LibreGPURect a, LibreGPURect b)
{
    return !libregpu_rect_empty(a) && !libregpu_rect_empty(b) &&
           a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}
static inline LibreGPURect libregpu_rect_intersect(LibreGPURect a, LibreGPURect b)
{
    LibreGPURect r = { a.x0 > b.x0 ? a.x0 : b.x0, a.y0 > b.y0 ? a.y0 : b.y0,
                    a.x1 < b.x1 ? a.x1 : b.x1, a.y1 < b.y1 ? a.y1 : b.y1 };
    if (libregpu_rect_empty(r)) { r.x0 = r.y0 = r.x1 = r.y1 = 0; }
    return r;
}
static inline LibreGPURect libregpu_rect_union(LibreGPURect a, LibreGPURect b)
{
    if (libregpu_rect_empty(a)) return b;
    if (libregpu_rect_empty(b)) return a;
    LibreGPURect r = { a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0,
                    a.x1 > b.x1 ? a.x1 : b.x1, a.y1 > b.y1 ? a.y1 : b.y1 };
    return r;
}
static inline int64_t libregpu_rect_area(LibreGPURect r)
{
    if (libregpu_rect_empty(r)) return 0;
    const int64_t w = (int64_t)r.x1 - (int64_t)r.x0;          
    const int64_t h = (int64_t)r.y1 - (int64_t)r.y0;
    return (w > INT64_MAX / h) ? INT64_MAX : w * h;           
}
static inline LibreGPURect libregpu_rect_inflate(LibreGPURect r, int32_t m)
{
    LibreGPURect o = { libregpu_sat32((int64_t)r.x0 - m), libregpu_sat32((int64_t)r.y0 - m),
                    libregpu_sat32((int64_t)r.x1 + m), libregpu_sat32((int64_t)r.y1 + m) };
    return o;
}
static inline LibreGPURect libregpu_rect_translate(LibreGPURect r, int32_t dx, int32_t dy)
{
    LibreGPURect o = { libregpu_sat32((int64_t)r.x0 + dx), libregpu_sat32((int64_t)r.y0 + dy),
                    libregpu_sat32((int64_t)r.x1 + dx), libregpu_sat32((int64_t)r.y1 + dy) };
    return o;
}






typedef struct {
    double    target_loops_per_second;       
    bool      enable_micro_pixel;
    bool      enable_camera;                 
    float     camera_max_reuse_angle;        
    float     camera_position_epsilon;       
    float     camera_roll_epsilon;
    float     camera_shift_tolerance_px;     

    int32_t   exposed_margin_px;             
    float     micro_pixel_max_area_fraction; 
    uint32_t  baseline_sample_interval;      

    uint32_t  full_refresh_interval;         
} LibreGPUConfig;

void        libregpu_config_defaults(LibreGPUConfig *cfg);
LibreGPUStatus libregpu_config_validate(const LibreGPUConfig *cfg);   




typedef struct { uint32_t id; uint32_t generation; } LibreGPUResourceId;

static inline bool libregpu_res_equal(LibreGPUResourceId a, LibreGPUResourceId b)
{ return a.id == b.id && a.generation == b.generation; }
static inline uint64_t libregpu_res_key(LibreGPUResourceId r)
{ return ((uint64_t)r.id << 32) | r.generation; }

typedef enum {
    LIBREGPU_OP_DRAW = 0,
    LIBREGPU_OP_DISPATCH,
    LIBREGPU_OP_RES_CREATE,
    LIBREGPU_OP_RES_UPDATE,
    LIBREGPU_OP_RES_COPY,
    LIBREGPU_OP_RES_DESTROY,
    LIBREGPU_OP_STATE_CHANGE,
    LIBREGPU_OP_SET_CAMERA,
    LIBREGPU_OP_SYNC,         
    LIBREGPU_OP_PRESENT,
    LIBREGPU_OP_OPAQUE        
} LibreGPUOpKind;

const char *libregpu_op_kind_str(LibreGPUOpKind k);






static inline bool libregpu_op_is_mandatory(LibreGPUOpKind k)
{
    return k == LIBREGPU_OP_RES_CREATE || k == LIBREGPU_OP_STATE_CHANGE ||
           k == LIBREGPU_OP_SET_CAMERA || k == LIBREGPU_OP_SYNC || k == LIBREGPU_OP_PRESENT;
}




#define LIBREGPU_OPF_CLEARS_TARGET (1u << 0)





typedef enum {
    LIBREGPU_PROJ_UNKNOWN = 0,   
    LIBREGPU_PROJ_PAN_2D,        
    LIBREGPU_PROJ_PERSPECTIVE
} LibreGPUProjection;




typedef struct {
    float           position[3];
    float           yaw, pitch, roll;   

    float           fov_x, fov_y;       
    int32_t         surface_w, surface_h;
    LibreGPUProjection projection;
} LibreGPUCameraPose;
















































typedef struct {
    LibreGPUOpKind     kind;
    uint32_t        flags;
    LibreGPUResourceId reads[LIBREGPU_MAX_OP_RES];   uint8_t read_count;
    LibreGPUResourceId writes[LIBREGPU_MAX_OP_RES];  uint8_t write_count;
    



    uint64_t        payload_hash;
    LibreGPURect       region;                    
    bool            region_valid;
    LibreGPUCameraPose camera;                    
    bool            camera_valid;
    uint64_t        size_bytes;                
    uint8_t        *blob;                      
    size_t          blob_size;
} LibreGPURawOp;

typedef struct {
    uint64_t    sequence;                      
    uint64_t    timestamp_ns;                  
    LibreGPURawOp *ops;
    size_t      op_count, op_capacity;
    bool        complete;                      
    bool        forwarded;                     

    bool        corrupt;                       


} LibreGPURawDataset;

void        libregpu_dataset_init(LibreGPURawDataset *ds);
void        libregpu_dataset_release(LibreGPURawDataset *ds);
LibreGPUStatus libregpu_dataset_append(LibreGPURawDataset *ds, const LibreGPURawOp *op); 

















typedef LibreGPUStatus (*LibreGPUForwardFn)(void *ctx, const LibreGPURawOp *op);

typedef struct {
    LibreGPURawDataset current;
    uint64_t        next_sequence;
    LibreGPUForwardFn  forward;
    void           *forward_ctx;
    bool            passing_through;           
} LibreGPUCapture;



void        libregpu_capture_init(LibreGPUCapture *c);
void        libregpu_capture_init_forward(LibreGPUCapture *c, LibreGPUForwardFn forward, void *forward_ctx);
void        libregpu_capture_shutdown(LibreGPUCapture *c);
LibreGPUStatus libregpu_capture_feed(LibreGPUCapture *c, const LibreGPURawOp *op,
                               LibreGPURawDataset *out , bool *ready);
LibreGPUStatus libregpu_capture_report_uncapturable(LibreGPUCapture *c);




typedef struct {
    uint64_t        sig;              

    LibreGPUOpKind     kind;
    uint32_t        flags;
    uint32_t        source_index;     
    LibreGPUResourceId reads[LIBREGPU_MAX_OP_RES];   uint8_t read_count;
    LibreGPUResourceId writes[LIBREGPU_MAX_OP_RES];  uint8_t write_count;
    LibreGPURect       region;           bool region_valid;
    LibreGPUCameraPose camera;           bool camera_valid;
    bool            opaque;           
} GirRecord;



typedef enum { GIR_DEP_RAW, GIR_DEP_WAW } GirDepKind;

typedef struct {                       
    uint32_t        from, to;
    GirDepKind      kind;
    LibreGPUResourceId res;
} GirDep;

typedef struct {                       
    LibreGPUResourceId id;
    uint32_t first_writer, last_writer;   
} GirResource;

typedef struct {
    uint64_t     dataset_sequence;
    GirRecord   *records;   size_t record_count;
    GirDep      *deps;      size_t dep_count;
    GirResource *resources; size_t resource_count;
    uint32_t    *res_table; size_t res_table_mask;   
    size_t       opaque_count;
    size_t       camera_count;                       
    bool         has_present_target;  LibreGPUResourceId present_target;
    bool         has_surface;         LibreGPURect surface;
    bool         has_camera;          uint32_t camera_record;   
    bool         valid;
} GirDataset;

typedef enum { GIR_CHG_ADDED, GIR_CHG_REMOVED, GIR_CHG_MODIFIED } GirChangeKind;

typedef struct {
    GirChangeKind kind;
    uint32_t      x_index;            
    uint32_t      y_index;            
} GirChange;

typedef struct {
    bool       identical;
    GirChange *changes;  size_t change_count;
    size_t     prefix_len, suffix_len;
    bool       structure_changed;     
    bool       camera_changed;        
    bool       non_camera_change;     
} GirDelta;

LibreGPUStatus       gir_build(GirDataset *out, const LibreGPURawDataset *raw);
void              gir_release(GirDataset *g);
const GirResource *gir_find_resource(const GirDataset *g, LibreGPUResourceId id);
LibreGPUStatus       gir_compare(const GirDataset *x, const GirDataset *y, GirDelta *out);
void              gir_delta_release(GirDelta *d);


















typedef enum {
    LIBREGPU_Z_ORDINARY,       
    LIBREGPU_Z_NO_IMPACT,      
    LIBREGPU_Z_INCREMENTAL     
} LibreGPUZMode;

typedef enum {
    LIBREGPU_STREAM_BOTH,                
    LIBREGPU_STREAM_MICRO_PIXEL,         
    LIBREGPU_STREAM_CAMERA,              
    LIBREGPU_STREAM_FALLBACK_NO_IMPACT,  
    LIBREGPU_STREAM_FALLBACK_ORDINARY    
} LibreGPUStreamAction;

const char *libregpu_stream_action_str(LibreGPUStreamAction a);

typedef struct { uint32_t op_index; bool scissored; } LibreGPUReplayOp;

typedef struct {
    float   delta_yaw, delta_pitch;
    float   fov_x, fov_y;
    int32_t surface_w, surface_h;
    int32_t shift_x, shift_y;          
} LibreGPUReprojection;

typedef struct {                       
    LibreGPUZMode        mode;
    LibreGPUReplayOp    *replay;  size_t replay_count;  
    LibreGPURect         regions[LIBREGPU_MAX_REGIONS];    
    uint8_t           region_count;
    bool              reproject;
    LibreGPUReprojection reprojection;
} LibreGPUZPlan;

typedef struct {
    bool              micro_pixel;     
    bool              camera;          
    const char       *micro_reason;
    const char       *camera_reason;
    const char       *fallback_reason;
    LibreGPUStreamAction action;
    double            record_changes_per_second;   

    uint64_t          dirty_pixels;                

    LibreGPUZPlan        plan;
} LibreGPUStreamlineResult;

typedef struct {                       
    


    bool supports_retained_results;
    

    bool supports_scissor_replay;
    

    bool supports_reproject;
    
    bool supports_gpu_timing;
} LibreGPUBackendCaps;

LibreGPUStatus streamline_decide(const LibreGPUConfig *cfg, const LibreGPUBackendCaps *caps,
                              const GirDataset *x, const GirDataset *y, const GirDelta *d,
                              double dt_seconds, LibreGPUStreamlineResult *out);
void        streamline_force_ordinary(LibreGPUStreamlineResult *r, const char *reason);
void        streamline_result_release(LibreGPUStreamlineResult *r);



































































typedef struct {
    const LibreGPUReplayOp *ops;      size_t op_count;
    LibreGPURect            regions[LIBREGPU_MAX_REGIONS];
    uint8_t              region_count;
    bool                 reproject;
    LibreGPUReprojection    reprojection;
    bool                 has_scratch;
    LibreGPUResourceId      scratch;      
} LibreGPUUpdateWork;

typedef struct LibreGPUBackend LibreGPUBackend;
struct LibreGPUBackend {
    const char       *name;
    LibreGPUBackendCaps  caps;
    void             *ctx;
    LibreGPUStatus (*attach)(void *ctx);
    void        (*detach)(void *ctx);
    bool        (*session_valid)(void *ctx);
    LibreGPUStatus (*next_dataset)(void *ctx, LibreGPURawDataset *out);   
    LibreGPUStatus (*submit)(void *ctx, const LibreGPURawDataset *ds,
                          const LibreGPUUpdateWork *work ,
                          uint64_t *fence_out);
    uint64_t    (*completed_fence)(void *ctx);
    void        (*wait_idle)(void *ctx);
    LibreGPUStatus (*create_scratch)(void *ctx, LibreGPUResourceId id, uint64_t bytes);
    LibreGPUStatus (*destroy_resource)(void *ctx, LibreGPUResourceId id);
    uint64_t    (*gpu_time_ns)(void *ctx, uint64_t fence);          
    uint64_t    (*presented_count)(void *ctx);
    uint64_t    (*error_count)(void *ctx);
    void        (*destroy)(LibreGPUBackend *self);   
};

LibreGPUStatus libregpu_backend_validate(const LibreGPUBackend *be);   
void        libregpu_backend_destroy(LibreGPUBackend *be);          



















typedef enum { LIBREGPU_OWNER_X, LIBREGPU_OWNER_Y, LIBREGPU_OWNER_Z, LIBREGPU_OWNER_COUNT } LibreGPURefOwner;

typedef struct LibreGPUMemRes LibreGPUMemRes;

typedef struct {
    uint64_t bytes_tracked, bytes_tracked_peak;   
    size_t   resources_tracked;
    uint64_t reclaimed_count, reclaimed_bytes;
    uint64_t destroy_failures;                    
} LibreGPUMemStats;

typedef struct {
    const LibreGPUBackend *backend;
    LibreGPUMemRes       **buckets;  size_t bucket_count, live_count;
    uint32_t            next_scratch_id;
    LibreGPUMemStats       stats;
} LibreGPUMemoryManager;

LibreGPUStatus mem_init(LibreGPUMemoryManager *m, const LibreGPUBackend *be);
void        mem_shutdown(LibreGPUMemoryManager *m);
LibreGPUStatus mem_track(LibreGPUMemoryManager *m, LibreGPUResourceId id, uint64_t size, bool scratch);
LibreGPUStatus mem_track_raw(LibreGPUMemoryManager *m, const LibreGPURawDataset *raw);


bool        mem_creates_tracked_identity(const LibreGPUMemoryManager *m, const LibreGPURawDataset *raw);

void        mem_reset_fences(LibreGPUMemoryManager *m);

LibreGPUStatus mem_sync_owner_refs(LibreGPUMemoryManager *m, LibreGPURefOwner owner, const GirDataset *g);
void        mem_note_raw_submission(LibreGPUMemoryManager *m, const LibreGPURawDataset *raw, uint64_t fence);
void        mem_note_use(LibreGPUMemoryManager *m, LibreGPUResourceId id, uint64_t fence);
LibreGPUStatus mem_request_release(LibreGPUMemoryManager *m, LibreGPUResourceId id);
LibreGPUStatus mem_acquire_scratch(LibreGPUMemoryManager *m, uint64_t bytes, LibreGPUResourceId *out);
size_t      mem_collect(LibreGPUMemoryManager *m);
void        mem_get_stats(const LibreGPUMemoryManager *m, LibreGPUMemStats *out);





typedef struct {
    size_t   ops_in_dataset;
    size_t   ops_submitted;
    bool     incremental;
    uint64_t fence;
} LibreGPURebuildResult;

LibreGPUStatus dataset_rebuilder_execute(const LibreGPUZPlan *plan, const LibreGPURawDataset *y,
                                      const LibreGPUBackend *be, const LibreGPUResourceId *scratch,
                                      LibreGPURebuildResult *out);




#define LIBREGPU_GOV_MAX_HZ 1000000.0
#define LIBREGPU_GOV_MIN_HZ 1.0

typedef struct {
    bool     enabled;
    double   target_hz;
    uint64_t interval_ns;                 
    uint64_t deadline_ns, start_ns, last_ns, loops;
    bool     present_seen;
    uint64_t present_first, present_last, present_first_ns, present_last_ns;
} LibreGPUGovernor;



LibreGPUStatus gov_init(LibreGPUGovernor *g, double target_loops_per_second);
void        gov_wait(LibreGPUGovernor *g);                       
void        gov_note_presented(LibreGPUGovernor *g, uint64_t presented_total);
double      gov_loop_rate(const LibreGPUGovernor *g);            
double      gov_present_rate(const LibreGPUGovernor *g);         




typedef struct {
    uint64_t cycles, baseline_cycles, incremental_cycles, no_impact_cycles;
    uint64_t fallback_cycles;       
    uint64_t failed_rebuilds;
    uint64_t forwarded_datasets,    
             refused_datasets,      
             corrupt_datasets,      
             bookkeeping_errors, baseline_losses;
    uint64_t ops_total, ops_submitted, dirty_pixels_total;
    uint64_t cpu_ns_total, cpu_ns_max;
    uint64_t gpu_ns_incremental, gpu_samples_incremental;
    uint64_t gpu_ns_ordinary,    gpu_samples_ordinary;
    uint64_t gpu_ns_baseline,    gpu_samples_baseline;   
    uint64_t vram_sum, vram_samples, vram_peak;
    uint64_t lifetime_errors;
    uint64_t interval_count;  double interval_mean_ns, interval_m2;   
    uint64_t last_cycle_ns;
} LibreGPUMetrics;

void libregpu_metrics_init(LibreGPUMetrics *m);
void libregpu_metrics_note_cycle_time(LibreGPUMetrics *m, uint64_t now_ns);
void libregpu_metrics_report(const LibreGPUMetrics *m, const LibreGPUGovernor *gov, FILE *out);
int  libregpu_selftest(FILE *out);       




typedef enum { LIBREGPU_RT_IDLE, LIBREGPU_RT_RUNNING, LIBREGPU_RT_STOPPED, LIBREGPU_RT_FAILED } LibreGPURuntimeState;

typedef struct {
    LibreGPUConfig         config;
    LibreGPUBackend       *backend;
    LibreGPURuntimeState   state;
    GirDataset          gir_x;                
    bool                have_baseline;
    LibreGPUMemoryManager  mem;
    LibreGPUGovernor       gov;
    LibreGPUMetrics        metrics;
    uint64_t            cycle, last_dataset_ts_ns, last_fence;
    uint64_t            last_sequence;        
    bool                last_cycle_optimized;
    bool                last_cycle_z_built;   
    bool                processing_y;
} LibreGPURuntime;

typedef struct {                               
    LibreGPURuntimeState state;
    bool graphics_path_active;
    bool baseline_exists;
    bool y_in_progress;
    bool z_constructed;
    bool gpu_in_flight;
    bool optimizing;                           
    bool session_valid;
} LibreGPURuntimeStatus;

typedef struct {
    bool              had_dataset, was_baseline, session_invalid;
    bool              submitted;               
    bool              forwarded;               
    LibreGPUStreamAction decision;                
    LibreGPUZMode        submitted_mode;          
    bool              fell_back_after_failure;
    bool              baseline_lost;           
    const char       *reason;
    const char       *micro_reason, *camera_reason;   
    size_t            ops_total, ops_submitted, changes;
    double            record_changes_per_second;
    uint64_t          dirty_pixels;
    uint64_t          gpu_ns, cpu_ns;
} LibreGPUCycleInfo;

typedef void (*LibreGPUCycleCallback)(const LibreGPUCycleInfo *info, void *user);

LibreGPUStatus libregpu_runtime_init(LibreGPURuntime *rt, const LibreGPUConfig *cfg, LibreGPUBackend *backend);
LibreGPUStatus libregpu_runtime_start(LibreGPURuntime *rt);
LibreGPUStatus libregpu_runtime_step(LibreGPURuntime *rt, LibreGPUCycleInfo *info);
LibreGPUStatus libregpu_runtime_run(LibreGPURuntime *rt, volatile sig_atomic_t *stop,
                              bool stop_when_idle, LibreGPUCycleCallback cb, void *user);
void        libregpu_runtime_stop(LibreGPURuntime *rt);          
void        libregpu_runtime_status(const LibreGPURuntime *rt, LibreGPURuntimeStatus *out);
const char *libregpu_runtime_state_str(LibreGPURuntimeState s);

#endif 




#ifndef LIBREGPU_LIB_DECLARATIONS_ONLY
#ifndef LIBREGPU_LIB_IMPLEMENTED
#define LIBREGPU_LIB_IMPLEMENTED

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <time.h>
#endif


















struct LibreGPUMemRes {
    LibreGPUResourceId id;
    uint64_t        size;
    uint8_t         refmask;            
    bool            scratch;            
    bool            release_requested;
    uint64_t        last_use_fence;
    LibreGPUMemRes    *next;               
};

static bool mm_ready(const LibreGPUMemoryManager *m)
{
    return m && m->buckets && m->backend;
}

static size_t bucket_index(const LibreGPUMemoryManager *m, LibreGPUResourceId id)
{
    return (size_t)libregpu_mix64(libregpu_res_key(id)) & (m->bucket_count - 1);
}

static LibreGPUMemRes *find_node(const LibreGPUMemoryManager *m, LibreGPUResourceId id)
{
    for (LibreGPUMemRes *n = m->buckets[bucket_index(m, id)]; n; n = n->next)
        if (libregpu_res_equal(n->id, id)) return n;
    return NULL;
}

static LibreGPUStatus grow_table(LibreGPUMemoryManager *m)
{
    const size_t nb = m->bucket_count * 2;
    LibreGPUMemRes **nt = (LibreGPUMemRes **)calloc(nb, sizeof *nt);
    if (!nt) return LIBREGPU_ERR_NO_MEMORY;
    LibreGPUMemRes **old = m->buckets;
    const size_t ob = m->bucket_count;
    m->buckets = nt;
    m->bucket_count = nb;
    for (size_t b = 0; b < ob; b++) {
        for (LibreGPUMemRes *n = old[b], *nx; n; n = nx) {
            nx = n->next;
            const size_t i = bucket_index(m, n->id);
            n->next = m->buckets[i];
            m->buckets[i] = n;
        }
    }
    free(old);
    return LIBREGPU_OK;
}

static void link_node(LibreGPUMemoryManager *m, LibreGPUMemRes *n)
{
    const size_t i = bucket_index(m, n->id);
    n->next = m->buckets[i];
    m->buckets[i] = n;
    m->live_count++;
}

static void account_add(LibreGPUMemoryManager *m, uint64_t bytes)
{
    LibreGPUMemStats *s = &m->stats;
    s->bytes_tracked = libregpu_sat_add_u64(s->bytes_tracked, bytes);
    if (s->bytes_tracked > s->bytes_tracked_peak) s->bytes_tracked_peak = s->bytes_tracked;
}

static void account_sub(LibreGPUMemoryManager *m, uint64_t bytes)
{
    m->stats.bytes_tracked = bytes > m->stats.bytes_tracked ? 0 : m->stats.bytes_tracked - bytes;
}

LibreGPUStatus mem_init(LibreGPUMemoryManager *m, const LibreGPUBackend *be)
{
    if (!m) return LIBREGPU_ERR_INVALID_ARG;
    memset(m, 0, sizeof *m);
    LibreGPUStatus st = libregpu_backend_validate(be);        
    if (st != LIBREGPU_OK) return st;
    m->backend = be;
    m->next_scratch_id = LIBREGPU_SCRATCH_ID_BASE;
    m->bucket_count = 256;
    m->buckets = (LibreGPUMemRes **)calloc(m->bucket_count, sizeof *m->buckets);
    return m->buckets ? LIBREGPU_OK : LIBREGPU_ERR_NO_MEMORY;
}

LibreGPUStatus mem_track(LibreGPUMemoryManager *m, LibreGPUResourceId id, uint64_t size, bool scratch)
{
    if (!mm_ready(m)) return LIBREGPU_ERR_INVALID_ARG;
    if (scratch != (id.id >= LIBREGPU_SCRATCH_ID_BASE)) return LIBREGPU_ERR_INVALID_ARG;   

    LibreGPUMemRes *n = find_node(m, id);
    if (n) {                                          
        if (n->scratch != scratch) return LIBREGPU_ERR_INVALID_ARG;
        if (size > n->size) { account_add(m, size - n->size); n->size = size; }
        return LIBREGPU_OK;
    }
    if (m->live_count * 4 > m->bucket_count * 3) {
        LibreGPUStatus st = grow_table(m);
        if (st != LIBREGPU_OK) return st;
    }
    n = (LibreGPUMemRes *)calloc(1, sizeof *n);
    if (!n) return LIBREGPU_ERR_NO_MEMORY;
    n->id = id;
    n->size = size;
    n->scratch = scratch;
    link_node(m, n);
    account_add(m, size);
    return LIBREGPU_OK;
}

bool mem_creates_tracked_identity(const LibreGPUMemoryManager *m, const LibreGPURawDataset *raw)
{
    if (!mm_ready(m) || !raw) return false;
    for (size_t i = 0; i < raw->op_count; i++) {
        const LibreGPURawOp *op = &raw->ops[i];
        if (op->kind != LIBREGPU_OP_RES_CREATE) continue;
        for (uint8_t k = 0; k < op->write_count; k++)
            if (find_node(m, op->writes[k])) return true;
    }
    return false;
}

void mem_reset_fences(LibreGPUMemoryManager *m)
{
    if (!mm_ready(m)) return;
    for (size_t b = 0; b < m->bucket_count; b++)
        for (LibreGPUMemRes *n = m->buckets[b]; n; n = n->next) n->last_use_fence = 0;
}



LibreGPUStatus mem_track_raw(LibreGPUMemoryManager *m, const LibreGPURawDataset *raw)
{
    if (!mm_ready(m) || !raw) return LIBREGPU_ERR_INVALID_ARG;
    LibreGPUStatus first = LIBREGPU_OK;
    for (size_t i = 0; i < raw->op_count; i++) {
        const LibreGPURawOp *op = &raw->ops[i];
        for (uint8_t k = 0; k < op->read_count; k++) {
            LibreGPUStatus st = mem_track(m, op->reads[k], 0, false);
            if (st != LIBREGPU_OK && first == LIBREGPU_OK) first = st;
        }
        for (uint8_t k = 0; k < op->write_count; k++) {
            const uint64_t sz = op->kind == LIBREGPU_OP_RES_CREATE ? op->size_bytes : 0;
            LibreGPUStatus st = mem_track(m, op->writes[k], sz, false);
            if (st != LIBREGPU_OK && first == LIBREGPU_OK) first = st;
        }
    }
    return first;
}



LibreGPUStatus mem_sync_owner_refs(LibreGPUMemoryManager *m, LibreGPURefOwner owner, const GirDataset *g)
{
    if (!mm_ready(m) || (unsigned)owner >= (unsigned)LIBREGPU_OWNER_COUNT) return LIBREGPU_ERR_INVALID_ARG;
    const uint8_t bit = (uint8_t)(1u << owner);

    if (g) {                                            
        for (size_t i = 0; i < g->resource_count; i++) {
            if (find_node(m, g->resources[i].id)) continue;
            LibreGPUStatus st = mem_track(m, g->resources[i].id, 0, false);
            if (st != LIBREGPU_OK) return st;              
        }
    }
    for (size_t b = 0; b < m->bucket_count; b++)        
        for (LibreGPUMemRes *n = m->buckets[b]; n; n = n->next) n->refmask &= (uint8_t)~bit;
    if (g) {
        for (size_t i = 0; i < g->resource_count; i++) {
            LibreGPUMemRes *n = find_node(m, g->resources[i].id);
            if (n) n->refmask |= bit;
        }
    }
    return LIBREGPU_OK;
}

void mem_note_use(LibreGPUMemoryManager *m, LibreGPUResourceId id, uint64_t fence)
{
    if (!mm_ready(m)) return;
    LibreGPUMemRes *n = find_node(m, id);
    if (n && fence > n->last_use_fence) n->last_use_fence = fence;
}



void mem_note_raw_submission(LibreGPUMemoryManager *m, const LibreGPURawDataset *raw, uint64_t fence)
{
    if (!mm_ready(m) || !raw) return;
    for (size_t i = 0; i < raw->op_count; i++) {
        const LibreGPURawOp *op = &raw->ops[i];
        for (uint8_t k = 0; k < op->read_count; k++)  mem_note_use(m, op->reads[k], fence);
        for (uint8_t k = 0; k < op->write_count; k++) mem_note_use(m, op->writes[k], fence);
    }
}

LibreGPUStatus mem_request_release(LibreGPUMemoryManager *m, LibreGPUResourceId id)
{
    if (!mm_ready(m)) return LIBREGPU_ERR_INVALID_ARG;
    LibreGPUMemRes *n = find_node(m, id);
    if (!n) {
        if (id.id >= LIBREGPU_SCRATCH_ID_BASE) return LIBREGPU_ERR_INVALID_ARG;    
        LibreGPUStatus st = mem_track(m, id, 0, false);
        if (st != LIBREGPU_OK) return st;                  
        n = find_node(m, id);
    }
    n->release_requested = true;
    return LIBREGPU_OK;
}

size_t mem_collect(LibreGPUMemoryManager *m)
{
    if (!mm_ready(m)) return 0;
    const uint64_t done = m->backend->completed_fence(m->backend->ctx);
    size_t released = 0;

    for (size_t b = 0; b < m->bucket_count; b++) {
        LibreGPUMemRes **pp = &m->buckets[b];
        while (*pp) {
            LibreGPUMemRes *n = *pp;
            if (!n->release_requested || n->refmask != 0 || n->last_use_fence > done) {
                pp = &n->next;
                continue;
            }
            if (m->backend->destroy_resource(m->backend->ctx, n->id) != LIBREGPU_OK) {
                m->stats.destroy_failures++;            
                pp = &n->next;
                continue;
            }
            *pp = n->next;
            m->live_count--;
            account_sub(m, n->size);
            m->stats.reclaimed_count++;
            m->stats.reclaimed_bytes = libregpu_sat_add_u64(m->stats.reclaimed_bytes, n->size);
            free(n);
            released++;
        }
    }
    return released;
}


LibreGPUStatus mem_acquire_scratch(LibreGPUMemoryManager *m, uint64_t bytes, LibreGPUResourceId *out)
{
    if (!mm_ready(m) || !out || bytes == 0) return LIBREGPU_ERR_INVALID_ARG;
    if (!m->backend->create_scratch) return LIBREGPU_ERR_UNSUPPORTED;
    if (m->next_scratch_id == UINT32_MAX) return LIBREGPU_ERR_NO_MEMORY;       
    LibreGPUResourceId id = { m->next_scratch_id++, 1 };
    LibreGPUStatus st = m->backend->create_scratch(m->backend->ctx, id, bytes);
    if (st != LIBREGPU_OK) return st;
    st = mem_track(m, id, bytes, true);
    if (st != LIBREGPU_OK) {
        if (m->backend->destroy_resource(m->backend->ctx, id) != LIBREGPU_OK) m->stats.destroy_failures++;
        return st;
    }
    *out = id;
    return LIBREGPU_OK;
}

void mem_get_stats(const LibreGPUMemoryManager *m, LibreGPUMemStats *out)
{
    if (!m || !out) return;
    *out = m->stats;
    out->resources_tracked = m->live_count;
}

void mem_shutdown(LibreGPUMemoryManager *m)
{
    if (!m || !m->buckets) return;
    if (m->backend && m->backend->wait_idle) m->backend->wait_idle(m->backend->ctx);
    for (size_t b = 0; b < m->bucket_count; b++) {
        LibreGPUMemRes *n = m->buckets[b];
        while (n) {
            LibreGPUMemRes *nx = n->next;
            if (n->scratch || n->release_requested)     
                if (m->backend->destroy_resource(m->backend->ctx, n->id) != LIBREGPU_OK) m->stats.destroy_failures++;
            free(n);                                    
            n = nx;
        }
        m->buckets[b] = NULL;
    }
    free(m->buckets);
    m->buckets = NULL;
    m->live_count = 0;
}











































typedef struct {
    bool       ok;
    const char *reason;
    uint8_t   *in_set;          
    uint8_t   *scissored;       
    LibreGPURect  regions[LIBREGPU_MAX_REGIONS];
    uint8_t    region_count;
    bool       reproject;
    LibreGPUReprojection rp;
} Partial;

typedef struct { bool target, other; } WriteClass;

const char *libregpu_stream_action_str(LibreGPUStreamAction a)
{
    switch (a) {
    case LIBREGPU_STREAM_BOTH:                return "micro-pixel+camera";
    case LIBREGPU_STREAM_MICRO_PIXEL:         return "micro-pixel";
    case LIBREGPU_STREAM_CAMERA:              return "camera";
    case LIBREGPU_STREAM_FALLBACK_NO_IMPACT:  return "fallback:no-impact";
    case LIBREGPU_STREAM_FALLBACK_ORDINARY:   return "fallback:ordinary";
    }
    return "?";
}




static bool partial_init(Partial *p, size_t n)
{
    memset(p, 0, sizeof *p);
    p->in_set    = (uint8_t *)calloc(n ? n : 1, 1);
    p->scissored = (uint8_t *)calloc(n ? n : 1, 1);
    return p->in_set && p->scissored;
}

static void partial_free(Partial *p)
{
    free(p->in_set);
    free(p->scissored);
    memset(p, 0, sizeof *p);
}

static bool is_target(const GirDataset *g, LibreGPUResourceId id)
{
    return g->has_present_target && libregpu_res_equal(id, g->present_target);
}

static bool is_mandatory_kind(LibreGPUOpKind k) { return libregpu_op_is_mandatory(k); }

static WriteClass classify_writes(const GirDataset *g, const GirRecord *r)
{
    WriteClass w = { false, false };
    for (uint8_t i = 0; i < r->write_count; i++) {
        if (is_target(g, r->writes[i])) w.target = true; else w.other = true;
    }
    return w;
}

static bool reads_target(const GirDataset *g, const GirRecord *r)
{
    for (uint8_t i = 0; i < r->read_count; i++)
        if (is_target(g, r->reads[i])) return true;
    return false;
}




static const char *producers_intact(const GirDataset *y, const uint8_t *in_set)
{
    for (size_t k = 0; k < y->dep_count; k++) {
        const GirDep *e = &y->deps[k];
        if (e->kind != GIR_DEP_RAW) continue;
        const bool consumer_runs = in_set[e->to] || is_mandatory_kind(y->records[e->to].kind);
        const bool producer_runs = in_set[e->from] || is_mandatory_kind(y->records[e->from].kind);
        if (!consumer_runs || producer_runs) continue;
        if (is_target(y, e->res)) continue;              
        const GirResource *res = gir_find_resource(y, e->res);
        if (!res || res->last_writer != e->from) return "producer output is not retained";
    }
    return NULL;
}



static bool target_cleared_first(const GirDataset *y)
{
    const GirResource *t = gir_find_resource(y, y->present_target);
    if (!t || t->first_writer == LIBREGPU_NO_INDEX) return false;
    return (y->records[t->first_writer].flags & LIBREGPU_OPF_CLEARS_TARGET) != 0;
}




static void eval_camera(const LibreGPUConfig *cfg, const LibreGPUBackendCaps *caps,
                        const GirDataset *x, const GirDataset *y, const GirDelta *d, Partial *p)
{
#define CAM_FAIL(msg) do { p->reason = (msg); return; } while (0)
    p->ok = false;
    if (!cfg->enable_camera) CAM_FAIL("camera unit disabled");
    if (!caps->supports_reproject || !caps->supports_scissor_replay)
        CAM_FAIL("backend lacks reprojection or scissored replay");
    if (x->camera_count > 1 || y->camera_count > 1) CAM_FAIL("multiple camera records: no authoritative pose");
    if (!x->has_camera || !y->has_camera) CAM_FAIL("camera pose not captured");
    const GirRecord *cx = &x->records[x->camera_record];
    const GirRecord *cy = &y->records[y->camera_record];
    if (!cx->camera_valid || !cy->camera_valid) CAM_FAIL("camera pose not decodable");
    if (!d->camera_changed) CAM_FAIL("camera unchanged");
    if (d->structure_changed) CAM_FAIL("operation structure changed");

    const LibreGPUCameraPose *a = &cx->camera, *b = &cy->camera;
    if (a->projection == LIBREGPU_PROJ_PERSPECTIVE || b->projection == LIBREGPU_PROJ_PERSPECTIVE)
        CAM_FAIL("perspective reprojection needs resampling and is not exact");
    if (a->projection != LIBREGPU_PROJ_PAN_2D || b->projection != LIBREGPU_PROJ_PAN_2D)
        CAM_FAIL("camera projection model not declared");
    if (fabsf(a->fov_x - b->fov_x) > 1e-6f || fabsf(a->fov_y - b->fov_y) > 1e-6f ||
        a->surface_w != b->surface_w || a->surface_h != b->surface_h)
        CAM_FAIL("projection or surface changed");

    const float dx = b->position[0] - a->position[0];
    const float dy = b->position[1] - a->position[1];
    const float dz = b->position[2] - a->position[2];
    if (sqrtf(dx * dx + dy * dy + dz * dz) > cfg->camera_position_epsilon)
        CAM_FAIL("camera translated: parallax invalidates reuse");
    if (fabsf(b->roll - a->roll) > cfg->camera_roll_epsilon) CAM_FAIL("camera rolled");

    const float dyaw = b->yaw - a->yaw, dpitch = b->pitch - a->pitch;
    if (fabsf(dyaw) > cfg->camera_max_reuse_angle || fabsf(dpitch) > cfg->camera_max_reuse_angle)
        CAM_FAIL("rotation exceeds reuse limit");

    if (!y->has_present_target || !y->has_surface) CAM_FAIL("present target or surface unknown");
    const int32_t W = b->surface_w, H = b->surface_h;
    if (y->surface.x0 != 0 || y->surface.y0 != 0 || y->surface.x1 != W || y->surface.y1 != H)
        CAM_FAIL("camera surface does not match target surface");

    


    size_t writers = 0;
    for (size_t i = 0; i < y->record_count; i++) {
        const GirRecord *r = &y->records[i];
        if (r->kind != LIBREGPU_OP_PRESENT && reads_target(y, r)) {
            const WriteClass w = classify_writes(y, r);
            if (!(w.target && !w.other)) CAM_FAIL("present target consumed by another pass");
        }
        if (r->kind == LIBREGPU_OP_DRAW || r->kind == LIBREGPU_OP_DISPATCH || r->kind == LIBREGPU_OP_RES_COPY) {
            const WriteClass w = classify_writes(y, r);
            if (!w.target || w.other) CAM_FAIL("frame has intermediate passes");
            p->in_set[i] = 1;
            p->scissored[i] = 1;
            writers++;
        }
    }
    if (writers == 0) CAM_FAIL("no operations write the present target");
    if (!target_cleared_first(y)) CAM_FAIL("target is not fully cleared at the start of the frame");
    { const char *why = producers_intact(y, p->in_set); if (why) CAM_FAIL(why); }

    

    const double fx = -((double)dyaw   / (double)b->fov_x * (double)W);
    const double fy =  ((double)dpitch / (double)b->fov_y * (double)H);
    const double limit = (double)INT32_MAX - 1.0;
    if (!isfinite(fx) || !isfinite(fy) ||
        fx < -limit || fx > limit || fy < -limit || fy > limit)
        CAM_FAIL("reprojection shift is out of range");
    const int32_t sx = (int32_t)lround(fx), sy = (int32_t)lround(fy);
    const double tol = (double)cfg->camera_shift_tolerance_px;
    if (fabs(fx - (double)sx) > tol || fabs(fy - (double)sy) > tol)
        CAM_FAIL("rotation is not a whole-pixel shift: reprojection would not be exact");

    const int32_t m = cfg->exposed_margin_px;
    p->rp.delta_yaw = dyaw;   p->rp.delta_pitch = dpitch;
    p->rp.fov_x = b->fov_x;   p->rp.fov_y = b->fov_y;
    p->rp.surface_w = W;      p->rp.surface_h = H;
    p->rp.shift_x = sx;       p->rp.shift_y = sy;

    if (sx == 0 && sy == 0) {     
        memset(p->in_set, 0, y->record_count);
        memset(p->scissored, 0, y->record_count);
        p->region_count = 0;
        p->reproject = false;
        p->ok = true;
        return;
    }
    const int64_t ax = sx < 0 ? -(int64_t)sx : (int64_t)sx;
    const int64_t ay = sy < 0 ? -(int64_t)sy : (int64_t)sy;
    if ((sx != 0 && ax + m >= W) || (sy != 0 && ay + m >= H))
        CAM_FAIL("exposed strip covers the surface");

    

    int32_t hx0 = 0, hx1 = W;
    if (sx < 0) { p->regions[p->region_count++] = libregpu_rect_make(W + sx - m, 0, W, H); hx1 = W + sx - m; }
    if (sx > 0) { p->regions[p->region_count++] = libregpu_rect_make(0, 0, sx + m, H);     hx0 = sx + m; }
    if (sy > 0) p->regions[p->region_count++] = libregpu_rect_make(hx0, 0, hx1, sy + m);
    if (sy < 0) p->regions[p->region_count++] = libregpu_rect_make(hx0, H + sy - m, hx1, H);
    p->reproject = true;
    p->ok = true;
#undef CAM_FAIL
}




static void eval_micro(const LibreGPUConfig *cfg, const GirDataset *x, const GirDataset *y,
                       const GirDelta *d, bool camera_changed, bool camera_ok,
                       int32_t shift_x, int32_t shift_y, Partial *p)
{
    const size_t ny = y->record_count;
    uint8_t *affected = (uint8_t *)calloc(ny ? ny : 1, 1);
    uint8_t *resflag  = (uint8_t *)calloc(y->resource_count ? y->resource_count : 1, 1);
    LibreGPURect R = { 0, 0, 0, 0 };
    const int32_t margin = (shift_x != 0 || shift_y != 0) ? cfg->exposed_margin_px : 0;
    const GirResource *tres = NULL;
    const char *why = NULL;

#define MP_FAIL(msg) do { p->reason = (msg); goto done; } while (0)
    p->ok = false;
    if (!affected || !resflag) MP_FAIL("out of memory");
    if (!cfg->enable_micro_pixel) MP_FAIL("micro-pixel unit disabled");
    if (!d->non_camera_change) MP_FAIL("no non-camera changes");
    if (d->structure_changed) MP_FAIL("operation structure changed");
    if (camera_changed && !camera_ok) MP_FAIL("camera moved without a valid camera update");
    if (!y->has_present_target || !y->has_surface) MP_FAIL("present target or surface unknown");
    if (!x->has_present_target || !libregpu_res_equal(x->present_target, y->present_target))
        MP_FAIL("present target changed");
    tres = gir_find_resource(y, y->present_target);
    if (!tres) MP_FAIL("present target not referenced");

    for (size_t i = 0; i < ny; i++) {                      
        const GirRecord *r = &y->records[i];
        if (r->kind == LIBREGPU_OP_PRESENT || !reads_target(y, r)) continue;
        const WriteClass w = classify_writes(y, r);
        if (!(w.target && !w.other)) MP_FAIL("present target consumed by another pass");
    }

    


    for (size_t c = 0; c < d->change_count; c++) {
        const GirChange *ch = &d->changes[c];
        const GirRecord *ry = &y->records[ch->y_index];
        const GirRecord *rx = &x->records[ch->x_index];
        if (ry->kind == LIBREGPU_OP_SET_CAMERA && rx->kind == LIBREGPU_OP_SET_CAMERA) continue;
        if (ry->kind != rx->kind) MP_FAIL("operation kind changed");
        if (!(ry->kind == LIBREGPU_OP_DRAW || ry->kind == LIBREGPU_OP_DISPATCH ||
              ry->kind == LIBREGPU_OP_RES_UPDATE || ry->kind == LIBREGPU_OP_RES_COPY))
            MP_FAIL("changed operation kind is not eligible");
        affected[ch->y_index] = 1;
        if (classify_writes(x, rx).target) {               
            if (!rx->region_valid) MP_FAIL("previous footprint unknown");
            R = libregpu_rect_union(R, libregpu_rect_inflate(
                    libregpu_rect_translate(rx->region, shift_x, shift_y), margin));
        }
    }

    

    for (size_t k = 0; k < y->dep_count; k++) {
        const GirDep *e = &y->deps[k];
        if (affected[e->from] && !affected[e->to] && !is_target(y, e->res)) affected[e->to] = 1;
    }

    
    for (size_t i = 0; i < ny; i++) {
        if (!affected[i]) continue;
        const GirRecord *r = &y->records[i];
        const WriteClass w = classify_writes(y, r);
        if (w.target && w.other) MP_FAIL("operation writes the target and an intermediate");
        p->in_set[i] = 1;
        if (w.target) {
            if (!r->region_valid) MP_FAIL("affected operation has no reliable footprint");
            R = libregpu_rect_union(R, r->region);
            p->scissored[i] = 1;
        } else {
            for (uint8_t k = 0; k < r->write_count; k++) {
                const GirResource *res = gir_find_resource(y, r->writes[k]);
                if (res) resflag[res - y->resources] = 1;
            }
        }
    }
    R = libregpu_rect_intersect(R, y->surface);
    if (libregpu_rect_empty(R)) {                              
        for (size_t i = 0; i < ny; i++) if (p->scissored[i]) { p->scissored[i] = 0; p->in_set[i] = 0; }
    }

    

    for (size_t ri = 0; ri < y->resource_count; ri++) {
        if (!resflag[ri]) continue;
        const GirResource *res = &y->resources[ri];
        if (res->first_writer == LIBREGPU_NO_INDEX ||
            !(y->records[res->first_writer].flags & LIBREGPU_OPF_CLEARS_TARGET))
            MP_FAIL("intermediate resource is not fully rewritten by its first writer");
        for (size_t j = 0; j < ny; j++) {
            const GirRecord *r = &y->records[j];
            for (uint8_t k = 0; k < r->write_count; k++) {
                if (!libregpu_res_equal(r->writes[k], res->id)) continue;
                if (classify_writes(y, r).target) MP_FAIL("intermediate writer also writes the target");
                p->in_set[j] = 1;
                break;
            }
        }
    }

    
    if (!libregpu_rect_empty(R)) {
        if (!target_cleared_first(y)) MP_FAIL("target is not fully cleared at the start of the frame");
        for (size_t j = 0; j < ny; j++) {
            const GirRecord *r = &y->records[j];
            if (!classify_writes(y, r).target || p->in_set[j]) continue;
            if (r->flags & LIBREGPU_OPF_CLEARS_TARGET) { p->in_set[j] = 1; p->scissored[j] = 1; continue; }
            if (!r->region_valid) MP_FAIL("target writer without a reliable footprint");
            if (libregpu_rect_intersects(r->region, R)) { p->in_set[j] = 1; p->scissored[j] = 1; }
        }
    }

    
    why = producers_intact(y, p->in_set);
    if (why) MP_FAIL(why);
    if (!libregpu_rect_empty(R) &&
        (double)libregpu_rect_area(R) > (double)cfg->micro_pixel_max_area_fraction *
                                     (double)libregpu_rect_area(y->surface))
        MP_FAIL("affected region too large to benefit");

    if (!libregpu_rect_empty(R)) { p->regions[0] = R; p->region_count = 1; }
    p->ok = true;
done:
    free(affected);
    free(resflag);
#undef MP_FAIL
}




static LibreGPUStatus build_plan(const GirDataset *y, const uint8_t *in_set, const uint8_t *scissored,
                              const LibreGPURect *regions, uint8_t region_count,
                              bool reproject, const LibreGPUReprojection *rp, LibreGPUZMode mode,
                              LibreGPUZPlan *plan)
{
    size_t n = 0;
    for (size_t i = 0; i < y->record_count; i++) {
        const LibreGPUOpKind k = y->records[i].kind;
        if (k == LIBREGPU_OP_RES_DESTROY) continue;             
        if (is_mandatory_kind(k) || (in_set && in_set[i])) n++;
    }
    LibreGPUReplayOp *ops = (LibreGPUReplayOp *)calloc(n ? n : 1, sizeof *ops);
    if (!ops) return LIBREGPU_ERR_NO_MEMORY;
    size_t w = 0;
    for (size_t i = 0; i < y->record_count; i++) {
        const LibreGPUOpKind k = y->records[i].kind;
        if (k == LIBREGPU_OP_RES_DESTROY) continue;
        const bool mandatory = is_mandatory_kind(k);
        if (!mandatory && !(in_set && in_set[i])) continue;
        ops[w].op_index = y->records[i].source_index;
        ops[w].scissored = !mandatory && scissored && scissored[i];
        w++;
    }
    memset(plan, 0, sizeof *plan);
    plan->mode = mode;
    plan->replay = ops;
    plan->replay_count = w;
    for (uint8_t i = 0; i < region_count && i < LIBREGPU_MAX_REGIONS; i++) plan->regions[i] = regions[i];
    plan->region_count = region_count;
    plan->reproject = reproject;
    if (reproject && rp) plan->reprojection = *rp;
    return LIBREGPU_OK;
}

static void set_ordinary(LibreGPUStreamlineResult *out, const char *reason)
{
    free(out->plan.replay);
    memset(&out->plan, 0, sizeof out->plan);
    out->plan.mode = LIBREGPU_Z_ORDINARY;
    out->action = LIBREGPU_STREAM_FALLBACK_ORDINARY;
    out->fallback_reason = reason;
    out->dirty_pixels = 0;
}

void streamline_force_ordinary(LibreGPUStreamlineResult *r, const char *reason)
{
    if (r) set_ordinary(r, reason);
}

void streamline_result_release(LibreGPUStreamlineResult *r)
{
    if (!r) return;
    free(r->plan.replay);
    memset(r, 0, sizeof *r);
}

static uint64_t regions_area(const LibreGPURect *r, uint8_t n)
{
    uint64_t a = 0;
    for (uint8_t i = 0; i < n; i++) a = libregpu_sat_add_u64(a, (uint64_t)libregpu_rect_area(r[i]));
    return a;
}

LibreGPUStatus streamline_decide(const LibreGPUConfig *cfg, const LibreGPUBackendCaps *caps,
                              const GirDataset *x, const GirDataset *y, const GirDelta *d,
                              double dt_seconds, LibreGPUStreamlineResult *out)
{
    if (!cfg || !caps || !out) return LIBREGPU_ERR_INVALID_ARG;
    memset(out, 0, sizeof *out);
    out->micro_reason = out->camera_reason = "not evaluated";
    out->plan.mode = LIBREGPU_Z_ORDINARY;
    out->action = LIBREGPU_STREAM_FALLBACK_ORDINARY;

    if (!x || !y || !d || !x->valid || !y->valid) { out->fallback_reason = "representation invalid"; return LIBREGPU_OK; }
    out->record_changes_per_second = dt_seconds > 0.0 ? (double)d->change_count / dt_seconds : 0.0;

    if (x->opaque_count || y->opaque_count) { out->fallback_reason = "opaque operations present"; return LIBREGPU_OK; }
    if (!caps->supports_retained_results)  { out->fallback_reason = "backend cannot retain results"; return LIBREGPU_OK; }
    if (!x->has_present_target || !y->has_present_target ||
        !libregpu_res_equal(x->present_target, y->present_target)) {
        out->fallback_reason = "no stable present target";
        return LIBREGPU_OK;
    }

    LibreGPUStatus st;
    if (d->identical) {                                       
        out->micro_reason = out->camera_reason = "no changes";
        st = build_plan(y, NULL, NULL, NULL, 0, false, NULL, LIBREGPU_Z_NO_IMPACT, &out->plan);
        if (st != LIBREGPU_OK) { set_ordinary(out, "out of memory"); return st; }
        out->action = LIBREGPU_STREAM_FALLBACK_NO_IMPACT;
        out->fallback_reason = "Y equals X";
        return LIBREGPU_OK;
    }

    Partial m = { 0 }, c = { 0 };            
    if (!partial_init(&m, y->record_count) || !partial_init(&c, y->record_count)) {
        partial_free(&m);
        partial_free(&c);
        set_ordinary(out, "out of memory");
        return LIBREGPU_ERR_NO_MEMORY;
    }

    eval_camera(cfg, caps, x, y, d, &c);
    eval_micro(cfg, x, y, d, d->camera_changed, c.ok,
               c.ok ? c.rp.shift_x : 0, c.ok ? c.rp.shift_y : 0, &m);
    out->micro_pixel = m.ok;
    out->camera = c.ok;
    out->micro_reason  = m.ok ? "ok" : m.reason;
    out->camera_reason = c.ok ? "ok" : c.reason;

    st = LIBREGPU_OK;
    if (m.ok && c.ok) {
        

        bool compatible = true;
        uint8_t *in_set = (uint8_t *)calloc(y->record_count ? y->record_count : 1, 1);
        uint8_t *scis   = (uint8_t *)calloc(y->record_count ? y->record_count : 1, 1);
        if (!in_set || !scis) { free(in_set); free(scis); partial_free(&m); partial_free(&c);
                                set_ordinary(out, "out of memory"); return LIBREGPU_ERR_NO_MEMORY; }
        for (size_t i = 0; i < y->record_count; i++) {
            if (m.in_set[i] && !m.scissored[i]) compatible = false;   
            in_set[i] = (uint8_t)(m.in_set[i] || c.in_set[i]);
            scis[i]   = (uint8_t)(m.scissored[i] || c.scissored[i]);
        }
        LibreGPURect regs[LIBREGPU_MAX_REGIONS];
        uint8_t nreg = 0;
        for (uint8_t i = 0; i < c.region_count; i++) regs[nreg++] = c.regions[i];
        if (m.region_count) {
            LibreGPURect r = m.regions[0];
            bool hit = false;
            for (uint8_t i = 0; i < nreg; i++) if (libregpu_rect_intersects(regs[i], r)) hit = true;
            if (hit) {                                  
                for (uint8_t i = 0; i < nreg; i++) r = libregpu_rect_union(r, regs[i]);
                regs[0] = r; nreg = 1;
            } else if (nreg < LIBREGPU_MAX_REGIONS) {
                regs[nreg++] = r;
            } else {
                compatible = false;
            }
        }
        if (compatible) {
            st = build_plan(y, in_set, scis, regs, nreg, c.reproject, &c.rp, LIBREGPU_Z_INCREMENTAL, &out->plan);
            out->action = LIBREGPU_STREAM_BOTH;
            out->dirty_pixels = regions_area(regs, nreg);
        } else {
            set_ordinary(out, "micro-pixel and camera updates are incompatible");
        }
        free(in_set); free(scis);
    } else if (m.ok) {
        st = build_plan(y, m.in_set, m.scissored, m.regions, m.region_count, false, NULL,
                        LIBREGPU_Z_INCREMENTAL, &out->plan);
        out->action = LIBREGPU_STREAM_MICRO_PIXEL;
        out->dirty_pixels = regions_area(m.regions, m.region_count);
    } else if (c.ok && !d->non_camera_change) {
        st = build_plan(y, c.in_set, c.scissored, c.regions, c.region_count, c.reproject, &c.rp,
                        LIBREGPU_Z_INCREMENTAL, &out->plan);
        out->action = LIBREGPU_STREAM_CAMERA;
        out->dirty_pixels = regions_area(c.regions, c.region_count);
    } else if (c.ok) {
        set_ordinary(out, "camera plan cannot cover non-camera changes");
    } else {
        set_ordinary(out, "neither unit can apply the update safely");
    }

    if (st != LIBREGPU_OK) set_ordinary(out, "plan construction failed");
    partial_free(&m);
    partial_free(&c);
    return st;
}




























static LibreGPUStatus validate_dataset(const LibreGPURawDataset *y)
{
    if (!y->complete || y->forwarded || y->corrupt) return LIBREGPU_ERR_VALIDATION;
    if (y->op_count > 0 && y->ops == NULL) return LIBREGPU_ERR_VALIDATION;
    if (y->op_count > LIBREGPU_MAX_DATASET_OPS) return LIBREGPU_ERR_VALIDATION;
    return LIBREGPU_OK;
}

static LibreGPUStatus validate_plan(const LibreGPUZPlan *plan, const LibreGPURawDataset *y,
                                 const LibreGPUBackendCaps *caps, const LibreGPUResourceId *scratch)
{
    if ((unsigned)plan->mode > (unsigned)LIBREGPU_Z_INCREMENTAL) return LIBREGPU_ERR_VALIDATION;
    if (plan->mode == LIBREGPU_Z_ORDINARY) return LIBREGPU_OK;

    if (!caps->supports_retained_results) return LIBREGPU_ERR_UNSUPPORTED;
    if (plan->replay_count > 0 && plan->replay == NULL) return LIBREGPU_ERR_VALIDATION;
    if (plan->replay_count > y->op_count) return LIBREGPU_ERR_VALIDATION;
    if (plan->region_count > LIBREGPU_MAX_REGIONS) return LIBREGPU_ERR_VALIDATION;

    
    for (size_t i = 0; i < y->op_count; i++)
        if (y->ops[i].kind == LIBREGPU_OP_OPAQUE) return LIBREGPU_ERR_VALIDATION;

    

    for (uint8_t i = 0; i < plan->region_count; i++) {
        if (libregpu_rect_empty(plan->regions[i])) return LIBREGPU_ERR_VALIDATION;
        for (uint8_t j = (uint8_t)(i + 1); j < plan->region_count; j++)
            if (libregpu_rect_intersects(plan->regions[i], plan->regions[j])) return LIBREGPU_ERR_VALIDATION;
    }
    if (plan->reproject) {
        if (!caps->supports_reproject) return LIBREGPU_ERR_UNSUPPORTED;
        if (!scratch) return LIBREGPU_ERR_VALIDATION;                 
    }

    const bool no_impact = (plan->mode == LIBREGPU_Z_NO_IMPACT);
    if (no_impact && (plan->region_count != 0 || plan->reproject)) return LIBREGPU_ERR_VALIDATION;

    bool presents = false, any_scissored = false;
    for (size_t i = 0; i < plan->replay_count; i++) {
        const LibreGPUReplayOp *r = &plan->replay[i];
        if (r->op_index >= y->op_count) return LIBREGPU_ERR_VALIDATION;
        if (i > 0 && r->op_index <= plan->replay[i - 1].op_index)
            return LIBREGPU_ERR_VALIDATION;                           
        const LibreGPUOpKind k = y->ops[r->op_index].kind;
        if (k == LIBREGPU_OP_RES_DESTROY || k == LIBREGPU_OP_OPAQUE)
            return LIBREGPU_ERR_VALIDATION;                           
        if (no_impact && (!libregpu_op_is_mandatory(k) || r->scissored))
            return LIBREGPU_ERR_VALIDATION;                           
        if (k == LIBREGPU_OP_PRESENT) presents = true;
        if (r->scissored) any_scissored = true;
    }
    if (!presents) return LIBREGPU_ERR_VALIDATION;                    
    if (any_scissored && (plan->region_count == 0 || !caps->supports_scissor_replay))
        return LIBREGPU_ERR_VALIDATION;

    
    size_t j = 0;
    for (size_t i = 0; i < y->op_count; i++) {
        while (j < plan->replay_count && plan->replay[j].op_index < i) j++;
        const bool present = j < plan->replay_count && plan->replay[j].op_index == i;
        if (libregpu_op_is_mandatory(y->ops[i].kind) && !present) return LIBREGPU_ERR_VALIDATION;
    }
    return LIBREGPU_OK;
}

LibreGPUStatus dataset_rebuilder_execute(const LibreGPUZPlan *plan, const LibreGPURawDataset *y,
                                      const LibreGPUBackend *be, const LibreGPUResourceId *scratch,
                                      LibreGPURebuildResult *out)
{
    if (!plan || !y || !be || !be->submit || !out) return LIBREGPU_ERR_INVALID_ARG;
    memset(out, 0, sizeof *out);
    out->ops_in_dataset = y->op_count;

    LibreGPUStatus st = validate_dataset(y);
    if (st != LIBREGPU_OK) return st;
    st = validate_plan(plan, y, &be->caps, scratch);
    if (st != LIBREGPU_OK) return st;

    if (plan->mode == LIBREGPU_Z_ORDINARY) {                          
        st = be->submit(be->ctx, y, NULL, &out->fence);
        if (st == LIBREGPU_OK) out->ops_submitted = y->op_count;
        return st;
    }

    LibreGPUUpdateWork work;
    memset(&work, 0, sizeof work);
    work.ops = plan->replay;
    work.op_count = plan->replay_count;
    work.region_count = plan->region_count;
    for (uint8_t i = 0; i < plan->region_count; i++) work.regions[i] = plan->regions[i];
    work.reproject = plan->reproject;
    work.reprojection = plan->reprojection;
    if (scratch) { work.has_scratch = true; work.scratch = *scratch; }

    st = be->submit(be->ctx, y, &work, &out->fence);
    if (st == LIBREGPU_OK) {
        out->ops_submitted = plan->replay_count;
        out->incremental = true;
    }
    return st;
}




















const char *libregpu_status_str(LibreGPUStatus s)
{
    switch (s) {
    case LIBREGPU_OK:                  return "ok";
    case LIBREGPU_NO_DATA:             return "no data";
    case LIBREGPU_ERR_INVALID_ARG:     return "invalid argument";
    case LIBREGPU_ERR_NO_MEMORY:       return "out of memory";
    case LIBREGPU_ERR_STATE:           return "invalid state";
    case LIBREGPU_ERR_UNSUPPORTED:     return "unsupported";
    case LIBREGPU_ERR_BACKEND:         return "graphics backend error";
    case LIBREGPU_ERR_LIFETIME:        return "resource lifetime violation";
    case LIBREGPU_ERR_VALIDATION:      return "validation failed";
    case LIBREGPU_ERR_SESSION_INVALID: return "graphics session invalid";
    }
    return "unknown status";
}

const char *libregpu_op_kind_str(LibreGPUOpKind k)
{
    static const char *names[] = {
        "draw", "dispatch", "res_create", "res_update", "res_copy",
        "res_destroy", "state_change", "set_camera", "sync", "present", "opaque"
    };
    return (unsigned)k <= (unsigned)LIBREGPU_OP_OPAQUE ? names[k] : "?";
}

uint64_t libregpu_hash_bytes(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h = 0xcbf29ce484222325ULL;               
    for (size_t i = 0; i < len; i++) { h ^= p[i]; h *= 0x100000001b3ULL; }
    return h;
}

void libregpu_dataset_init(LibreGPURawDataset *ds)
{
    if (ds) memset(ds, 0, sizeof *ds);
}

void libregpu_dataset_release(LibreGPURawDataset *ds)
{
    if (!ds) return;
    for (size_t i = 0; i < ds->op_count; i++) free(ds->ops[i].blob);
    free(ds->ops);
    memset(ds, 0, sizeof *ds);
}



static bool op_acceptable(const LibreGPURawOp *op)
{
    if ((unsigned)op->kind > (unsigned)LIBREGPU_OP_OPAQUE) return false;
    if (op->read_count > LIBREGPU_MAX_OP_RES || op->write_count > LIBREGPU_MAX_OP_RES) return false;
    if (op->blob_size > 0 && op->blob == NULL) return false;     
    for (uint8_t i = 0; i < op->read_count; i++)
        if (op->reads[i].id >= LIBREGPU_SCRATCH_ID_BASE) return false;   
    for (uint8_t i = 0; i < op->write_count; i++)
        if (op->writes[i].id >= LIBREGPU_SCRATCH_ID_BASE) return false;
    return true;
}

LibreGPUStatus libregpu_dataset_append(LibreGPURawDataset *ds, const LibreGPURawOp *op)
{
    if (!ds || !op) return LIBREGPU_ERR_INVALID_ARG;
    if (ds->complete) return LIBREGPU_ERR_STATE;          
    if (!op_acceptable(op)) return LIBREGPU_ERR_INVALID_ARG;
    if (ds->op_count >= LIBREGPU_MAX_DATASET_OPS) return LIBREGPU_ERR_NO_MEMORY;

    if (ds->op_count == ds->op_capacity) {
        size_t ncap = ds->op_capacity ? ds->op_capacity * 2 : 64;
        if (ncap > LIBREGPU_MAX_DATASET_OPS) ncap = LIBREGPU_MAX_DATASET_OPS;
        LibreGPURawOp *n = (LibreGPURawOp *)realloc(ds->ops, ncap * sizeof *n);
        if (!n) return LIBREGPU_ERR_NO_MEMORY;
        ds->ops = n;
        ds->op_capacity = ncap;
    }

    LibreGPURawOp *dst = &ds->ops[ds->op_count];
    *dst = *op;
    dst->blob = NULL;
    dst->blob_size = 0;
    if (op->blob && op->blob_size) {
        uint8_t *copy = (uint8_t *)malloc(op->blob_size);
        if (!copy) return LIBREGPU_ERR_NO_MEMORY;         
        memcpy(copy, op->blob, op->blob_size);
        dst->blob = copy;
        dst->blob_size = op->blob_size;
        if (dst->payload_hash == 0)                    
            dst->payload_hash = libregpu_hash_bytes(copy, dst->blob_size);
    }
    ds->op_count++;
    return LIBREGPU_OK;
}

void libregpu_capture_init_forward(LibreGPUCapture *c, LibreGPUForwardFn forward, void *forward_ctx)
{
    if (!c) return;
    memset(c, 0, sizeof *c);
    c->next_sequence = 1;                      
    c->forward = forward;
    c->forward_ctx = forward_ctx;
}



void libregpu_capture_init(LibreGPUCapture *c)
{
    libregpu_capture_init_forward(c, NULL, NULL);
}

void libregpu_capture_shutdown(LibreGPUCapture *c)
{
    if (c) libregpu_dataset_release(&c->current);
}

static LibreGPUStatus forward_op(LibreGPUCapture *c, const LibreGPURawOp *op)
{
    return c->forward ? c->forward(c->forward_ctx, op) : LIBREGPU_ERR_UNSUPPORTED;   
}



static LibreGPUStatus enter_pass_through(LibreGPUCapture *c)
{
    LibreGPUStatus first = LIBREGPU_OK;
    if (c->passing_through) return first;
    for (size_t i = 0; i < c->current.op_count; i++) {
        LibreGPUStatus st = forward_op(c, &c->current.ops[i]);
        if (st != LIBREGPU_OK && first == LIBREGPU_OK) first = st;
    }
    libregpu_dataset_release(&c->current);
    c->passing_through = true;
    return first;
}



static void emit_forwarded(LibreGPUCapture *c, LibreGPURawDataset *out)
{
    libregpu_dataset_init(out);
    out->sequence     = c->next_sequence++;       
    out->timestamp_ns = libregpu_now_ns();
    out->complete     = false;
    out->forwarded    = true;
    out->corrupt      = true;                     
    libregpu_dataset_init(&c->current);
    c->passing_through = false;
}

LibreGPUStatus libregpu_capture_feed(LibreGPUCapture *c, const LibreGPURawOp *op,
                               LibreGPURawDataset *out, bool *ready)
{
    if (!c || !op || !out || !ready) return LIBREGPU_ERR_INVALID_ARG;
    *ready = false;
    if (out->ops || out->op_count || out->op_capacity)   
        return LIBREGPU_ERR_INVALID_ARG;

    if (c->passing_through) {                            
        const LibreGPUStatus st = forward_op(c, op);
        if (op->kind == LIBREGPU_OP_PRESENT) { emit_forwarded(c, out); *ready = true; }
        return st;
    }

    const LibreGPUStatus st = libregpu_dataset_append(&c->current, op);
    if (st == LIBREGPU_OK) {
        if (op->kind == LIBREGPU_OP_PRESENT) {              
            c->current.complete     = true;
            c->current.sequence     = c->next_sequence++;
            c->current.timestamp_ns = libregpu_now_ns();    
            *out = c->current;                           
            libregpu_dataset_init(&c->current);
            *ready = true;
        }
        return LIBREGPU_OK;
    }

    
    const LibreGPUStatus flushed   = enter_pass_through(c);
    const LibreGPUStatus forwarded = forward_op(c, op);
    if (op->kind == LIBREGPU_OP_PRESENT) { emit_forwarded(c, out); *ready = true; }
    if (flushed != LIBREGPU_OK) return flushed;             
    if (forwarded != LIBREGPU_OK) return forwarded;
    return st;                                           
}

LibreGPUStatus libregpu_capture_report_uncapturable(LibreGPUCapture *c)
{
    if (!c) return LIBREGPU_ERR_INVALID_ARG;
    return enter_pass_through(c);
}





























static uint64_t float_bits(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof u);
    return u;
}

static bool pose_is_sane(const LibreGPUCameraPose *c)
{
    const float v[] = { c->position[0], c->position[1], c->position[2],
                        c->yaw, c->pitch, c->roll, c->fov_x, c->fov_y };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++)
        if (!isfinite(v[i])) return false;
    if ((unsigned)c->projection > (unsigned)LIBREGPU_PROJ_PERSPECTIVE) return false;
    return c->fov_x > 0.0f && c->fov_y > 0.0f && c->surface_w > 0 && c->surface_h > 0;
}

static bool coord_ok(int32_t v) { return v >= -LIBREGPU_MAX_COORD && v <= LIBREGPU_MAX_COORD; }

static bool op_is_valid(const LibreGPURawOp *op)
{
    if ((unsigned)op->kind > (unsigned)LIBREGPU_OP_OPAQUE) return false;
    if (op->read_count > LIBREGPU_MAX_OP_RES || op->write_count > LIBREGPU_MAX_OP_RES) return false;
    for (uint8_t i = 0; i < op->read_count; i++)
        if (op->reads[i].id >= LIBREGPU_SCRATCH_ID_BASE) return false;     
    for (uint8_t i = 0; i < op->write_count; i++)
        if (op->writes[i].id >= LIBREGPU_SCRATCH_ID_BASE) return false;
    if (op->region_valid) {
        const LibreGPURect *r = &op->region;
        if (r->x1 < r->x0 || r->y1 < r->y0) return false;
        if (!coord_ok(r->x0) || !coord_ok(r->y0) || !coord_ok(r->x1) || !coord_ok(r->y1)) return false;
    }
    return true;
}





static uint64_t record_signature(const GirRecord *r, uint64_t payload_hash, uint64_t size_bytes)
{
    uint64_t h = libregpu_mix64((uint64_t)r->kind + 1u);
    h = libregpu_hash_combine(h, r->flags);
    for (uint8_t i = 0; i < r->read_count; i++)
        h = libregpu_hash_combine(h, libregpu_res_key(r->reads[i]));
    h = libregpu_hash_combine(h, 0x5eedULL);                        
    for (uint8_t i = 0; i < r->write_count; i++)
        h = libregpu_hash_combine(h, libregpu_res_key(r->writes[i]));
    h = libregpu_hash_combine(h, payload_hash);
    h = libregpu_hash_combine(h, size_bytes);
    if (r->kind == LIBREGPU_OP_SET_CAMERA) {
        h = libregpu_hash_combine(h, r->camera_valid ? 1u : 0u);
        if (r->camera_valid) {
            const LibreGPUCameraPose *c = &r->camera;
            h = libregpu_hash_combine(h, float_bits(c->position[0]));
            h = libregpu_hash_combine(h, float_bits(c->position[1]));
            h = libregpu_hash_combine(h, float_bits(c->position[2]));
            h = libregpu_hash_combine(h, float_bits(c->yaw));
            h = libregpu_hash_combine(h, float_bits(c->pitch));
            h = libregpu_hash_combine(h, float_bits(c->roll));
            h = libregpu_hash_combine(h, float_bits(c->fov_x));
            h = libregpu_hash_combine(h, float_bits(c->fov_y));
            h = libregpu_hash_combine(h, (uint64_t)(uint32_t)c->surface_w);
            h = libregpu_hash_combine(h, (uint64_t)(uint32_t)c->surface_h);
            h = libregpu_hash_combine(h, (uint64_t)c->projection);
        }
    }
    return h;
}

static GirResource *res_find_or_add(GirDataset *g, LibreGPUResourceId id)
{
    const uint64_t key = libregpu_res_key(id);
    size_t slot = (size_t)libregpu_mix64(key) & g->res_table_mask;
    for (;;) {
        uint32_t idx = g->res_table[slot];
        if (idx == LIBREGPU_NO_INDEX) {
            idx = (uint32_t)g->resource_count++;
            GirResource *r = &g->resources[idx];
            memset(r, 0, sizeof *r);
            r->id = id;
            r->first_writer = r->last_writer = LIBREGPU_NO_INDEX;
            g->res_table[slot] = idx;
            return r;
        }
        if (libregpu_res_key(g->resources[idx].id) == key) return &g->resources[idx];
        slot = (slot + 1) & g->res_table_mask;
    }
}

const GirResource *gir_find_resource(const GirDataset *g, LibreGPUResourceId id)
{
    if (!g || !g->res_table) return NULL;
    const uint64_t key = libregpu_res_key(id);
    size_t slot = (size_t)libregpu_mix64(key) & g->res_table_mask;
    for (;;) {
        uint32_t idx = g->res_table[slot];
        if (idx == LIBREGPU_NO_INDEX) return NULL;
        if (libregpu_res_key(g->resources[idx].id) == key) return &g->resources[idx];
        slot = (slot + 1) & g->res_table_mask;
    }
}

static void add_dep(GirDataset *g, GirDepKind kind, uint32_t from, uint32_t to, LibreGPUResourceId res)
{
    GirDep *d = &g->deps[g->dep_count++];     
    d->kind = kind; d->from = from; d->to = to; d->res = res;
}

void gir_release(GirDataset *g)
{
    if (!g) return;
    free(g->records);
    free(g->deps);
    free(g->resources);
    free(g->res_table);
    memset(g, 0, sizeof *g);
}

LibreGPUStatus gir_build(GirDataset *g, const LibreGPURawDataset *raw)
{
    if (!g || !raw) return LIBREGPU_ERR_INVALID_ARG;
    memset(g, 0, sizeof *g);
    if (!raw->complete || raw->forwarded || raw->corrupt) return LIBREGPU_ERR_VALIDATION;

    const size_t n = raw->op_count;
    if (n > LIBREGPU_MAX_DATASET_OPS || (n > 0 && raw->ops == NULL)) return LIBREGPU_ERR_VALIDATION;

    size_t refs = 0;
    for (size_t i = 0; i < n; i++) {                      
        const LibreGPURawOp *op = &raw->ops[i];
        if (!op_is_valid(op)) return LIBREGPU_ERR_VALIDATION;
        refs += (size_t)op->read_count + op->write_count;   
    }

    size_t tcap = 16;
    while (tcap < refs * 2 + 2) tcap <<= 1;               

    g->records   = (GirRecord *)calloc(n ? n : 1, sizeof *g->records);
    g->deps      = (GirDep *)calloc(refs ? refs : 1, sizeof *g->deps);    
    g->resources = (GirResource *)calloc(refs ? refs : 1, sizeof *g->resources);
    g->res_table = (uint32_t *)malloc(tcap * sizeof *g->res_table);
    if (!g->records || !g->deps || !g->resources || !g->res_table) {
        gir_release(g);
        return LIBREGPU_ERR_NO_MEMORY;
    }
    memset(g->res_table, 0xFF, tcap * sizeof *g->res_table);   
    g->res_table_mask = tcap - 1;
    g->record_count   = n;
    g->dataset_sequence = raw->sequence;
    g->camera_record  = LIBREGPU_NO_INDEX;

    for (size_t i = 0; i < n; i++) {
        const LibreGPURawOp *op = &raw->ops[i];
        GirRecord *r = &g->records[i];
        const uint32_t seq = (uint32_t)i;

        r->kind = op->kind;
        r->flags = op->flags;
        r->source_index = seq;
        r->read_count = op->read_count;
        r->write_count = op->write_count;
        memcpy(r->reads, op->reads, sizeof r->reads);
        memcpy(r->writes, op->writes, sizeof r->writes);
        r->region = op->region;
        r->region_valid = op->region_valid;
        r->opaque = (op->kind == LIBREGPU_OP_OPAQUE);
        if (r->opaque) g->opaque_count++;

        if (op->kind == LIBREGPU_OP_SET_CAMERA) {
            r->camera = op->camera;
            r->camera_valid = op->camera_valid && pose_is_sane(&op->camera);
            if (g->camera_count++ == 0) g->camera_record = seq;
        }
        r->sig = record_signature(r, op->payload_hash, op->size_bytes);

        for (uint8_t k = 0; k < r->read_count; k++) {
            GirResource *res = res_find_or_add(g, r->reads[k]);
            if (res->last_writer != LIBREGPU_NO_INDEX)
                add_dep(g, GIR_DEP_RAW, res->last_writer, seq, r->reads[k]);
        }
        for (uint8_t k = 0; k < r->write_count; k++) {
            GirResource *res = res_find_or_add(g, r->writes[k]);
            if (res->last_writer != LIBREGPU_NO_INDEX)
                add_dep(g, GIR_DEP_WAW, res->last_writer, seq, r->writes[k]);
            if (res->first_writer == LIBREGPU_NO_INDEX) res->first_writer = seq;
            res->last_writer = seq;
        }
    }

    
    g->has_camera = (g->camera_count == 1);

    
    for (size_t i = 0; i < n && !g->has_present_target; i++) {
        const GirRecord *r = &g->records[i];
        if (r->kind == LIBREGPU_OP_PRESENT && r->read_count > 0) {
            g->has_present_target = true;
            g->present_target = r->reads[0];
        }
    }
    if (g->has_present_target) {
        const GirResource *t = gir_find_resource(g, g->present_target);
        if (t) {
            


            if (t->first_writer != LIBREGPU_NO_INDEX) {
                const GirRecord *fw = &g->records[t->first_writer];
                bool ok = (fw->flags & LIBREGPU_OPF_CLEARS_TARGET) && fw->region_valid &&
                          !libregpu_rect_empty(fw->region) && fw->region.x0 == 0 && fw->region.y0 == 0;
                if (ok && g->has_camera && g->records[g->camera_record].camera_valid) {
                    const LibreGPUCameraPose *c = &g->records[g->camera_record].camera;
                    ok = fw->region.x1 == c->surface_w && fw->region.y1 == c->surface_h;
                }
                if (ok) { g->has_surface = true; g->surface = fw->region; }
            }
        }
    }
    g->valid = true;
    return LIBREGPU_OK;
}

void gir_delta_release(GirDelta *d)
{
    if (!d) return;
    free(d->changes);
    memset(d, 0, sizeof *d);
}

static void delta_add(GirDelta *d, GirChangeKind kind, uint32_t xi, uint32_t yi)
{
    GirChange *c = &d->changes[d->change_count++];    
    c->kind = kind; c->x_index = xi; c->y_index = yi;
}

LibreGPUStatus gir_compare(const GirDataset *x, const GirDataset *y, GirDelta *d)
{
    if (!d) return LIBREGPU_ERR_INVALID_ARG;
    memset(d, 0, sizeof *d);
    if (!x || !y || !x->valid || !y->valid) return LIBREGPU_ERR_INVALID_ARG;

    const size_t nx = x->record_count, ny = y->record_count;
    const size_t minn = nx < ny ? nx : ny;

    size_t p = 0;
    while (p < minn && x->records[p].sig == y->records[p].sig) p++;
    size_t s = 0;
    while (s < minn - p && x->records[nx - 1 - s].sig == y->records[ny - 1 - s].sig) s++;

    d->prefix_len = p;
    d->suffix_len = s;
    d->structure_changed = (nx != ny);
    d->identical = (nx == ny && p == nx);
    if (d->identical) return LIBREGPU_OK;

    const size_t cap = nx > ny ? nx : ny;
    d->changes = (GirChange *)calloc(cap ? cap : 1, sizeof *d->changes);
    if (!d->changes) return LIBREGPU_ERR_NO_MEMORY;

    const size_t mx = nx - p - s, my = ny - p - s;
    const size_t common = mx < my ? mx : my;
    for (size_t i = 0; i < common; i++) {
        const size_t xi = p + i, yi = p + i;
        if (x->records[xi].sig != y->records[yi].sig)
            delta_add(d, GIR_CHG_MODIFIED, (uint32_t)xi, (uint32_t)yi);
    }
    for (size_t i = common; i < my; i++) delta_add(d, GIR_CHG_ADDED,   LIBREGPU_NO_INDEX, (uint32_t)(p + i));
    for (size_t i = common; i < mx; i++) delta_add(d, GIR_CHG_REMOVED, (uint32_t)(p + i), LIBREGPU_NO_INDEX);

    for (size_t i = 0; i < d->change_count; i++) {
        const GirChange *c = &d->changes[i];
        const GirRecord *rx = c->x_index != LIBREGPU_NO_INDEX ? &x->records[c->x_index] : NULL;
        const GirRecord *ry = c->y_index != LIBREGPU_NO_INDEX ? &y->records[c->y_index] : NULL;
        const bool cam = (rx && rx->kind == LIBREGPU_OP_SET_CAMERA) ||
                         (ry && ry->kind == LIBREGPU_OP_SET_CAMERA);
        const bool other = (rx && rx->kind != LIBREGPU_OP_SET_CAMERA) ||
                           (ry && ry->kind != LIBREGPU_OP_SET_CAMERA);
        if (cam)   d->camera_changed = true;
        if (other) d->non_camera_change = true;
    }
    return LIBREGPU_OK;
}






















#define SPIN_NS 200000ULL      

uint64_t libregpu_now_ns(void)
{
#ifdef _WIN32
    static LARGE_INTEGER freq;
    static int use_qpc = -1;
    if (use_qpc < 0) use_qpc = QueryPerformanceFrequency(&freq) && freq.QuadPart > 0;
    if (use_qpc) {
        LARGE_INTEGER c;
        if (QueryPerformanceCounter(&c))
            return (uint64_t)((double)c.QuadPart * 1e9 / (double)freq.QuadPart);
    }
    return (uint64_t)GetTickCount64() * 1000000ULL;      
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
}

void libregpu_sleep_ns(uint64_t ns)
{
#ifdef _WIN32
    Sleep((DWORD)(ns / 1000000ULL));
#else
    struct timespec req, rem;
    req.tv_sec = (time_t)(ns / 1000000000ULL);
    req.tv_nsec = (long)(ns % 1000000000ULL);
    while (nanosleep(&req, &rem) != 0) {
        if (errno != EINTR) return;                    
        req = rem;
    }
#endif
}

LibreGPUStatus gov_init(LibreGPUGovernor *g, double target_loops_per_second)
{
    if (!g) return LIBREGPU_ERR_INVALID_ARG;
    memset(g, 0, sizeof *g);
    g->start_ns = g->deadline_ns = g->last_ns = libregpu_now_ns();
    if (target_loops_per_second == 0.0) return LIBREGPU_OK;                   
    if (!isfinite(target_loops_per_second) || target_loops_per_second < LIBREGPU_GOV_MIN_HZ ||
        target_loops_per_second > LIBREGPU_GOV_MAX_HZ)
        return LIBREGPU_ERR_INVALID_ARG;                                      
    const double interval = 1e9 / target_loops_per_second;
    if (!isfinite(interval) || interval > 1e18) return LIBREGPU_ERR_INVALID_ARG;
    g->enabled = true;
    g->target_hz = target_loops_per_second;
    g->interval_ns = (uint64_t)interval;
    if (g->interval_ns == 0) g->interval_ns = 1;
    return LIBREGPU_OK;
}

void gov_wait(LibreGPUGovernor *g)
{
    if (g->enabled) {
        const uint64_t now = libregpu_now_ns();
        g->deadline_ns += g->interval_ns;
        if (now < g->deadline_ns) {
            const uint64_t remain = g->deadline_ns - now;
            if (remain > SPIN_NS) libregpu_sleep_ns(remain - SPIN_NS);
            while (libregpu_now_ns() < g->deadline_ns) {  }
        } else if (now - g->deadline_ns > g->interval_ns) {
            g->deadline_ns = now;                      
        }
    }
    g->loops++;
    g->last_ns = libregpu_now_ns();
}

void gov_note_presented(LibreGPUGovernor *g, uint64_t presented_total)
{
    const uint64_t now = libregpu_now_ns();
    if (!g->present_seen) {
        g->present_seen = true;
        g->present_first = presented_total;
        g->present_first_ns = now;
    }
    g->present_last = presented_total;
    g->present_last_ns = now;
}

double gov_loop_rate(const LibreGPUGovernor *g)
{
    const uint64_t span = g->last_ns - g->start_ns;
    return (g->loops && span) ? (double)g->loops * 1e9 / (double)span : 0.0;
}

double gov_present_rate(const LibreGPUGovernor *g)
{
    if (!g->present_seen || g->present_last_ns <= g->present_first_ns) return 0.0;
    const double dt = (double)(g->present_last_ns - g->present_first_ns) * 1e-9;
    return (double)(g->present_last - g->present_first) / dt;
}

#endif 
#endif 
