








#define _GNU_SOURCE


#define LIBREGPU_UI_DECLARATIONS_ONLY
#include "ui.c"

#if defined(SIM_GPU)



























#ifndef LIBREGPU_GL_DECLARED
#define LIBREGPU_GL_DECLARED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>



#define LIBREGPU_GL_TARGET_RES_ID   1u
#define LIBREGPU_GL_SHADOW_RES_ID   2u
#define LIBREGPU_GL_TEXTURE_RES_ID  3u

typedef enum {
    LIBREGPU_GL_HEADLESS = 0,     
    LIBREGPU_GL_WINDOWED = 1      
} LibreGPUGlMode;

typedef struct {
    LibreGPUGlMode mode;
    int32_t     width, height;
    bool        hidden;        
    const char *title;
} LibreGPUGlOptions;


enum { LIBREGPU_GL_DRAW_CLEAR = 1u, LIBREGPU_GL_DRAW_RECT = 2u };

typedef struct {
    uint32_t kind;
    int32_t  x, y, w, h;       
    uint32_t r, g, b, a;
} LibreGPUGlDraw;

LibreGPUBackend *libregpu_gl_backend_create(const LibreGPUGlOptions *options);
void          libregpu_gl_backend_destroy(LibreGPUBackend *be);



LibreGPUStatus   libregpu_gl_enqueue(LibreGPUBackend *be, LibreGPURawDataset *ds);


LibreGPUStatus   libregpu_gl_read_target(LibreGPUBackend *be, uint32_t *rgba, size_t pixels);


int32_t       libregpu_gl_width(const LibreGPUBackend *be);
int32_t       libregpu_gl_height(const LibreGPUBackend *be);




bool          libregpu_gl_have_gpu_timing(const LibreGPUBackend *be);
bool          libregpu_gl_have_timer_query(const LibreGPUBackend *be);
const char   *libregpu_gl_timing_name(const LibreGPUBackend *be);
uint64_t      libregpu_gl_frames_presented(const LibreGPUBackend *be);
uint64_t      libregpu_gl_violations(const LibreGPUBackend *be);


bool          libregpu_gl_should_close(LibreGPUBackend *be);


const char   *libregpu_gl_last_error(void);

#endif 

#ifndef LIBREGPU_GL_DECLARATIONS_ONLY
#ifndef LIBREGPU_GL_IMPLEMENTED
#define LIBREGPU_GL_IMPLEMENTED

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <GL/gl.h>
#include <GL/glext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <SDL.h>

typedef struct {
    uint64_t key;
    GLuint   tex, fbo;
    int32_t  w, h;
    uint64_t alloc_bytes;
    bool     alive;
} GlEntry;

#define GL_MAX_PENDING 32

typedef struct {
    uint64_t fence;
    GLuint   query;
    GLsync   sync;
    uint64_t start_ns;      
    bool     used;
} GlPending;

typedef struct {
    LibreGPUBackend base;                 
    LibreGPUGlMode  mode;
    int32_t      W, H;

    SDL_Window    *window;
    SDL_GLContext  sdl_context;
    bool           sdl_owned;

    EGLDisplay     edpy;
    EGLContext     ectx;
    EGLSurface     esurf;
    EGLConfig      ecfg;
    bool           egl_owned;

    void *(*getproc)(const char *);

    GLuint target_tex, target_fbo;
    GLuint display_tex, display_fbo;   
    GLuint shadow_tex, shadow_fbo;
    GLuint white_tex;

    GlEntry *entries;
    size_t   entry_count, entry_capacity;

    LibreGPURawDataset *queue;
    size_t           q_head, q_count, q_capacity;

    uint64_t submitted, completed, presented;
    uint64_t violations;
    bool     session_valid, attached, quit;

    bool have_fbo, have_blit, have_sync, have_timer;
    PFNGLGENFRAMEBUFFERSEXTPROC          p_GenFramebuffers;
    PFNGLBINDFRAMEBUFFEREXTPROC          p_BindFramebuffer;
    PFNGLFRAMEBUFFERTEXTURE2DEXTPROC     p_FramebufferTexture2D;
    PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC   p_CheckFramebufferStatus;
    PFNGLDELETEFRAMEBUFFERSEXTPROC       p_DeleteFramebuffers;
    PFNGLBLITFRAMEBUFFEREXTPROC          p_BlitFramebuffer;
    PFNGLFENCESYNCPROC                   p_FenceSync;
    PFNGLCLIENTWAITSYNCPROC              p_ClientWaitSync;
    PFNGLDELETESYNCPROC                  p_DeleteSync;
    PFNGLGENQUERIESPROC                  p_GenQueries;
    PFNGLDELETEQUERIESPROC               p_DeleteQueries;
    PFNGLBEGINQUERYPROC                  p_BeginQuery;
    PFNGLENDQUERYPROC                    p_EndQuery;
    PFNGLGETQUERYOBJECTUIVPROC           p_GetQueryObjectuiv;
    PFNGLGETQUERYOBJECTUI64VEXTPROC      p_GetQueryObjectui64v;

    GlPending pending[GL_MAX_PENDING];
    size_t    pending_slot;
} GlHandle;

_Static_assert(offsetof(GlHandle, base) == 0, "LibreGPUBackend must be first in GlHandle");

static char gl_error_text[256] = "not initialised";

static void gl_set_error(const char *text)
{
    (void)snprintf(gl_error_text, sizeof gl_error_text, "%s", text ? text : "unknown error");
}

const char *libregpu_gl_last_error(void) { return gl_error_text; }




static void *gl_proc(GlHandle *g, const char *name)
{
    return g->getproc ? g->getproc(name) : NULL;
}

static void gl_clear_errors(void)
{
    while (glGetError() != GL_NO_ERROR) {  }
}

#define GLLOAD(field, name)                                                      \
    do {                                                                          \
        void *p_ = gl_proc(g, (name));                                            \
        _Static_assert(sizeof p_ == sizeof g->field, "incompatible pointer size"); \
        memcpy(&g->field, &p_, sizeof p_);                                        \
    } while (0)




static GlEntry *entry_find(GlHandle *g, LibreGPUResourceId id)
{
    const uint64_t key = libregpu_res_key(id);
    for (size_t i = 0; i < g->entry_count; i++) if (g->entries[i].key == key) return &g->entries[i];
    return NULL;
}

static GlEntry *entry_add(GlHandle *g, LibreGPUResourceId id)
{
    if (g->entry_count == g->entry_capacity) {
        size_t ncap = g->entry_capacity ? g->entry_capacity * 2 : 32;
        GlEntry *n = (GlEntry *)realloc(g->entries, ncap * sizeof *n);
        if (!n) return NULL;
        g->entries = n;
        g->entry_capacity = ncap;
    }
    GlEntry *e = &g->entries[g->entry_count++];
    memset(e, 0, sizeof *e);
    e->key = libregpu_res_key(id);
    return e;
}

static void entry_remove(GlHandle *g, GlEntry *e)
{
    const size_t i = (size_t)(e - g->entries);
    if (i + 1 < g->entry_count) g->entries[i] = g->entries[g->entry_count - 1];
    g->entry_count--;
}




static bool gl_make_texture(GlHandle *g, GLuint *tex, GLuint *fbo, int32_t w, int32_t h)
{
    glGenTextures(1, tex);
    if (*tex == 0) return false;
    glBindTexture(GL_TEXTURE_2D, *tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (!fbo) return true;
    g->p_GenFramebuffers(1, fbo);
    if (*fbo == 0) { glDeleteTextures(1, tex); *tex = 0; return false; }
    g->p_BindFramebuffer(GL_FRAMEBUFFER_EXT, *fbo);
    g->p_FramebufferTexture2D(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                              GL_TEXTURE_2D, *tex, 0);
    const GLenum status = g->p_CheckFramebufferStatus(GL_FRAMEBUFFER_EXT);
    g->p_BindFramebuffer(GL_FRAMEBUFFER_EXT, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE_EXT) {
        g->p_DeleteFramebuffers(1, fbo);
        *fbo = 0;
        glDeleteTextures(1, tex);
        *tex = 0;
        return false;
    }
    return true;
}

static void gl_drop_texture(GlHandle *g, GLuint *tex, GLuint *fbo)
{
    if (fbo && *fbo) { g->p_DeleteFramebuffers(1, fbo); *fbo = 0; }
    if (tex && *tex) { glDeleteTextures(1, tex); *tex = 0; }
}

static void gl_bind_target(GlHandle *g)
{
    g->p_BindFramebuffer(GL_FRAMEBUFFER_EXT, g->target_fbo);
    glViewport(0, 0, (GLsizei)g->W, (GLsizei)g->H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (double)g->W, (double)g->H, 0.0, -1.0, 1.0);   
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void gl_apply_scissor(GlHandle *g, const LibreGPURect *r)
{
    if (!r) { glDisable(GL_SCISSOR_TEST); return; }
    int32_t x0 = r->x0, x1 = r->x1, y0 = r->y0, y1 = r->y1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > g->W) x1 = g->W;
    if (y1 > g->H) y1 = g->H;
    if (x1 <= x0 || y1 <= y0) { glEnable(GL_SCISSOR_TEST); glScissor(0, 0, 0, 0); return; }
    glEnable(GL_SCISSOR_TEST);
    glScissor((GLint)x0, (GLint)(g->H - y1), (GLsizei)(x1 - x0), (GLsizei)(y1 - y0));
}

static void gl_full_quad(GlHandle *g)
{
    glBegin(GL_QUADS);
    glVertex2i(0, 0);
    glVertex2i((GLint)g->W, 0);
    glVertex2i((GLint)g->W, (GLint)g->H);
    glVertex2i(0, (GLint)g->H);
    glEnd();
}

static void gl_draw_op(GlHandle *g, const LibreGPUGlDraw *d, const LibreGPURect *scissor)
{
    gl_apply_scissor(g, scissor);
    glBindTexture(GL_TEXTURE_2D, g->white_tex);
    if (d->kind == LIBREGPU_GL_DRAW_CLEAR) {
        glDisable(GL_BLEND);
        glColor4ub((GLubyte)d->r, (GLubyte)d->g, (GLubyte)d->b, (GLubyte)d->a);
        gl_full_quad(g);
        return;
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);                 
    glColor4ub((GLubyte)d->r, (GLubyte)d->g, (GLubyte)d->b, (GLubyte)d->a);
    const int32_t x0 = d->x, y0 = d->y;
    const int32_t x1 = d->x + d->w, y1 = d->y + d->h;
    glBegin(GL_QUADS);
    glVertex2i((GLint)x0, (GLint)y0);
    glVertex2i((GLint)x1, (GLint)y0);
    glVertex2i((GLint)x1, (GLint)y1);
    glVertex2i((GLint)x0, (GLint)y1);
    glEnd();
}

static void gl_present(GlHandle *g)
{
    glDisable(GL_SCISSOR_TEST);
    g->p_BindFramebuffer(GL_READ_FRAMEBUFFER_EXT, g->target_fbo);
    g->p_BindFramebuffer(GL_DRAW_FRAMEBUFFER_EXT, g->display_fbo);
    g->p_BlitFramebuffer(0, 0, (GLint)g->W, (GLint)g->H,
                         0, 0, (GLint)g->W, (GLint)g->H,
                         GL_COLOR_BUFFER_BIT, GL_NEAREST);
    g->p_BindFramebuffer(GL_FRAMEBUFFER_EXT, 0);
    g->presented++;
}






static void gl_reproject(GlHandle *g, GlEntry *src, int32_t sx, int32_t sy)
{
    if (!src || !src->fbo || src->w != g->W || src->h != g->H) return;

    const int32_t dx0 = sx > 0 ? sx : 0;
    const int32_t dx1 = sx < 0 ? g->W + sx : g->W;
    const int32_t dy0 = sy > 0 ? sy : 0;
    const int32_t dy1 = sy < 0 ? g->H + sy : g->H;
    if (dx1 > dx0 && dy1 > dy0) {
        const int32_t srx0 = dx0 - sx, srx1 = dx1 - sx;
        const int32_t sry0 = dy0 - sy, sry1 = dy1 - sy;
        g->p_BindFramebuffer(GL_READ_FRAMEBUFFER_EXT, src->fbo);
        g->p_BindFramebuffer(GL_DRAW_FRAMEBUFFER_EXT, g->target_fbo);
        g->p_BlitFramebuffer((GLint)srx0, (GLint)(g->H - sry1),
                             (GLint)srx1, (GLint)(g->H - sry0),
                             (GLint)dx0, (GLint)(g->H - dy1),
                             (GLint)dx1, (GLint)(g->H - dy0),
                             GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    g->p_BindFramebuffer(GL_FRAMEBUFFER_EXT, 0);
}




static void gl_swap_target_scratch(GlHandle *g, GlEntry *s)
{
    GLuint tex = g->target_tex, fbo = g->target_fbo;
    g->target_tex = s->tex;
    g->target_fbo = s->fbo;
    s->tex = tex;
    s->fbo = fbo;
}




static void *egl_proc(const char *name)
{
    __eglMustCastToProperFunctionPointerType fn = eglGetProcAddress(name);
    void *p = NULL;
    _Static_assert(sizeof p == sizeof fn, "incompatible pointer size");
    memcpy(&p, &fn, sizeof p);
    return p;
}

static bool gl_init_egl(GlHandle *g)
{
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform;
    {
        void *p = egl_proc("eglGetPlatformDisplayEXT");
        _Static_assert(sizeof p == sizeof get_platform, "incompatible pointer size");
        memcpy(&get_platform, &p, sizeof p);
    }
    if (!get_platform) { gl_set_error("EGL has no platform-display entry point"); return false; }
    g->edpy = get_platform(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);
    if (g->edpy == EGL_NO_DISPLAY) { gl_set_error("EGL surfaceless display unavailable"); return false; }
    EGLint major = 0, minor = 0;
    if (!eglInitialize(g->edpy, &major, &minor)) {
        gl_set_error("EGL initialisation failed");
        g->edpy = EGL_NO_DISPLAY;
        return false;
    }
    g->egl_owned = true;
    if (!eglBindAPI(EGL_OPENGL_API)) { gl_set_error("EGL cannot bind OpenGL"); return false; }

    const EGLint attrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0,
        EGL_NONE
    };
    EGLint n = 0;
    if (!eglChooseConfig(g->edpy, attrs, &g->ecfg, 1, &n) || n == 0) {
        gl_set_error("EGL has no RGBA8 OpenGL config");
        return false;
    }
    const EGLint ctxattrs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 2, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE
    };
    g->ectx = eglCreateContext(g->edpy, g->ecfg, EGL_NO_CONTEXT, ctxattrs);
    if (g->ectx == EGL_NO_CONTEXT) { gl_set_error("EGL OpenGL 2.1 context unavailable"); return false; }
    const EGLint pb[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
    g->esurf = eglCreatePbufferSurface(g->edpy, g->ecfg, pb);
    if (g->esurf == EGL_NO_SURFACE) { gl_set_error("EGL pbuffer surface unavailable"); return false; }
    if (!eglMakeCurrent(g->edpy, g->esurf, g->esurf, g->ectx)) {
        gl_set_error("EGL make-current failed");
        return false;
    }
    g->getproc = egl_proc;
    return true;
}

static bool gl_init_sdl(GlHandle *g, const LibreGPUGlOptions *o)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { gl_set_error(SDL_GetError()); return false; }
    g->sdl_owned = true;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

    Uint32 flags = SDL_WINDOW_OPENGL;
    if (o->hidden) flags |= SDL_WINDOW_HIDDEN;
    g->window = SDL_CreateWindow(o->title ? o->title : "LIBREGPU OpenGL",
                                 SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 o->width, o->height, flags);
    if (!g->window) { gl_set_error(SDL_GetError()); return false; }
    g->sdl_context = SDL_GL_CreateContext(g->window);
    if (!g->sdl_context) { gl_set_error(SDL_GetError()); return false; }
    if (SDL_GL_MakeCurrent(g->window, g->sdl_context) != 0) { gl_set_error(SDL_GetError()); return false; }
    SDL_GL_SetSwapInterval(0);                     
    g->getproc = SDL_GL_GetProcAddress;
    return true;
}

static bool gl_load_procs(GlHandle *g)
{
    GLLOAD(p_GenFramebuffers,        "glGenFramebuffersEXT");
    GLLOAD(p_BindFramebuffer,        "glBindFramebufferEXT");
    GLLOAD(p_FramebufferTexture2D,   "glFramebufferTexture2DEXT");
    GLLOAD(p_CheckFramebufferStatus, "glCheckFramebufferStatusEXT");
    GLLOAD(p_DeleteFramebuffers,     "glDeleteFramebuffersEXT");
    GLLOAD(p_BlitFramebuffer,        "glBlitFramebufferEXT");
    if (!g->p_BlitFramebuffer) GLLOAD(p_BlitFramebuffer, "glBlitFramebuffer");
    g->have_fbo = g->p_GenFramebuffers && g->p_BindFramebuffer && g->p_FramebufferTexture2D &&
                  g->p_CheckFramebufferStatus && g->p_DeleteFramebuffers;
    g->have_blit = g->p_BlitFramebuffer != NULL;

    GLLOAD(p_FenceSync,      "glFenceSync");
    GLLOAD(p_ClientWaitSync, "glClientWaitSync");
    GLLOAD(p_DeleteSync,     "glDeleteSync");
    g->have_sync = g->p_FenceSync && g->p_ClientWaitSync && g->p_DeleteSync;

    GLLOAD(p_GenQueries,          "glGenQueries");
    GLLOAD(p_DeleteQueries,       "glDeleteQueries");
    GLLOAD(p_BeginQuery,          "glBeginQuery");
    GLLOAD(p_EndQuery,            "glEndQuery");
    GLLOAD(p_GetQueryObjectuiv,   "glGetQueryObjectuiv");
    GLLOAD(p_GetQueryObjectui64v, "glGetQueryObjectui64vEXT");
    const char *ext = (const char *)glGetString(GL_EXTENSIONS);
    const bool ext_timer = ext && strstr(ext, "GL_EXT_timer_query") != NULL;
    g->have_timer = ext_timer && g->p_GenQueries && g->p_DeleteQueries && g->p_BeginQuery &&
                    g->p_EndQuery && g->p_GetQueryObjectuiv && g->p_GetQueryObjectui64v;

    return g->have_fbo && g->have_blit;
}

static bool gl_create_objects(GlHandle *g)
{
    if (!gl_make_texture(g, &g->target_tex, &g->target_fbo, g->W, g->H)) {
        gl_set_error("cannot create the retained RGBA8 target");
        return false;
    }
    if (g->mode == LIBREGPU_GL_HEADLESS) {
        if (!gl_make_texture(g, &g->display_tex, &g->display_fbo, g->W, g->H)) {
            gl_set_error("cannot create the private display target");
            return false;
        }
    } else {
        g->display_fbo = 0;
    }

    

    if (!gl_make_texture(g, &g->shadow_tex, &g->shadow_fbo, 64, 64)) {
        gl_set_error("cannot create the intermediate target");
        return false;
    }

    glGenTextures(1, &g->white_tex);
    if (!g->white_tex) { gl_set_error("cannot create the source texture"); return false; }
    const GLubyte white[4] = { 255, 255, 255, 255 };
    glBindTexture(GL_TEXTURE_2D, g->white_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glBindTexture(GL_TEXTURE_2D, 0);

    
    glDisable(GL_DITHER);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_COLOR_LOGIC_OP);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_MULTISAMPLE);
    glShadeModel(GL_FLAT);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    gl_bind_target(g);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    g->p_BindFramebuffer(GL_FRAMEBUFFER_EXT, 0);
    return true;
}






static bool gl_timer_calibrate(GlHandle *g)
{
    if (!g->have_timer) return false;
    for (int trial = 0; trial < 3; trial++) {
        GLuint q = 0;
        g->p_GenQueries(1, &q);
        if (!q) { g->have_timer = false; return false; }
        g->p_BeginQuery(GL_TIME_ELAPSED_EXT, q);
        gl_bind_target(g);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        for (int i = 0; i < 512; i++) {
            glClearColor((float)(i & 1), 0.25f, 0.5f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        glFinish();
        g->p_EndQuery(GL_TIME_ELAPSED_EXT);
        glFinish();
        GLuint64 ns = 0;
        g->p_GetQueryObjectui64v(q, GL_QUERY_RESULT, &ns);
        g->p_DeleteQueries(1, &q);
        

        if (ns == 0 || ns > 1000000000ull) {
            g->have_timer = false;
            break;
        }
    }
    gl_bind_target(g);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    g->p_BindFramebuffer(GL_FRAMEBUFFER_EXT, 0);
    return g->have_timer;
}




static LibreGPUStatus gl_queue_init(GlHandle *g)
{
    g->q_capacity = 256;
    g->queue = (LibreGPURawDataset *)calloc(g->q_capacity, sizeof *g->queue);
    return g->queue ? LIBREGPU_OK : LIBREGPU_ERR_NO_MEMORY;
}

static void gl_queue_release(GlHandle *g)
{
    for (size_t i = 0; i < g->q_capacity; i++) libregpu_dataset_release(&g->queue[i]);
    free(g->queue);
    g->queue = NULL;
    g->q_capacity = g->q_head = g->q_count = 0;
}

static LibreGPUStatus gl_next_dataset(void *ctx, LibreGPURawDataset *out)
{
    GlHandle *g = (GlHandle *)ctx;
    if (g->q_count == 0) return LIBREGPU_NO_DATA;
    *out = g->queue[g->q_head];
    memset(&g->queue[g->q_head], 0, sizeof g->queue[0]);
    g->q_head = (g->q_head + 1) % g->q_capacity;
    g->q_count--;
    return LIBREGPU_OK;
}

LibreGPUStatus libregpu_gl_enqueue(LibreGPUBackend *be, LibreGPURawDataset *ds)
{
    if (!be || !ds) return LIBREGPU_ERR_INVALID_ARG;
    GlHandle *g = (GlHandle *)be;
    if (g->q_count == g->q_capacity) return LIBREGPU_ERR_NO_MEMORY;
    g->queue[(g->q_head + g->q_count) % g->q_capacity] = *ds;      
    g->q_count++;
    memset(ds, 0, sizeof *ds);
    return LIBREGPU_OK;
}




static LibreGPUStatus gl_attach(void *ctx)
{
    GlHandle *g = (GlHandle *)ctx;
    if (g->attached) return LIBREGPU_OK;
    if (g->mode == LIBREGPU_GL_HEADLESS) {
        if (!eglMakeCurrent(g->edpy, g->esurf, g->esurf, g->ectx)) return LIBREGPU_ERR_BACKEND;
    } else {
        if (SDL_GL_MakeCurrent(g->window, g->sdl_context) != 0) return LIBREGPU_ERR_BACKEND;
    }
    g->attached = true;
    return LIBREGPU_OK;
}

static void gl_detach(void *ctx)
{
    GlHandle *g = (GlHandle *)ctx;
    if (g->mode == LIBREGPU_GL_HEADLESS) {
        if (g->edpy != EGL_NO_DISPLAY) eglMakeCurrent(g->edpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    g->attached = false;
}

static bool gl_session_valid(void *ctx) { return ((GlHandle *)ctx)->session_valid; }

static uint64_t gl_completed_fence(void *ctx)
{
    GlHandle *g = (GlHandle *)ctx;
    if (!g->have_sync) return g->completed;
    for (size_t i = 0; i < GL_MAX_PENDING; i++) {
        GlPending *p = &g->pending[i];
        if (!p->used || !p->sync) continue;
        const GLenum r = g->p_ClientWaitSync(p->sync, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
        if (r == GL_ALREADY_SIGNALED || r == GL_CONDITION_SATISFIED) {
            if (p->fence > g->completed) g->completed = p->fence;
            g->p_DeleteSync(p->sync);
            p->sync = NULL;
        }
    }
    return g->completed;
}

static void gl_wait_idle(void *ctx)
{
    GlHandle *g = (GlHandle *)ctx;
    if (!g->have_sync) { glFinish(); g->completed = g->submitted; return; }
    for (size_t i = 0; i < GL_MAX_PENDING; i++) {
        GlPending *p = &g->pending[i];
        if (!p->used || !p->sync) continue;
        (void)g->p_ClientWaitSync(p->sync, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull);
        g->p_DeleteSync(p->sync);
        p->sync = NULL;
        if (p->fence > g->completed) g->completed = p->fence;
    }
    glFinish();
}





static uint64_t gl_gpu_time(void *ctx, uint64_t fence)
{
    GlHandle *g = (GlHandle *)ctx;
    for (size_t i = 0; i < GL_MAX_PENDING; i++) {
        GlPending *p = &g->pending[i];
        if (!p->used || p->fence != fence) continue;
        if (p->sync) {
            (void)g->p_ClientWaitSync(p->sync, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull);
            g->p_DeleteSync(p->sync);
            p->sync = NULL;
            if (p->fence > g->completed) g->completed = p->fence;
        }
        const uint64_t fence_ns = libregpu_now_ns() - p->start_ns;
        uint64_t result = fence_ns;
        if (g->have_timer && p->query) {
            GLuint64 ns = 0;
            g->p_GetQueryObjectui64v(p->query, GL_QUERY_RESULT, &ns);
            

            if (ns > 0 && ns <= 1000000000ull) result = (uint64_t)ns;
            else g->have_timer = false;
        }
        if (p->query) {
            g->p_DeleteQueries(1, &p->query);
            p->query = 0;
        }
        p->used = false;
        return result;
    }
    return 0;
}

static LibreGPUStatus gl_create_scratch(void *ctx, LibreGPUResourceId id, uint64_t bytes)
{
    GlHandle *g = (GlHandle *)ctx;
    if (entry_find(g, id)) return LIBREGPU_ERR_BACKEND;      
    GlEntry *e = entry_add(g, id);
    if (!e) return LIBREGPU_ERR_NO_MEMORY;
    e->alloc_bytes = bytes;
    e->alive = true;
    return LIBREGPU_OK;
}

static LibreGPUStatus gl_destroy_resource(void *ctx, LibreGPUResourceId id)
{
    GlHandle *g = (GlHandle *)ctx;
    GlEntry *e = entry_find(g, id);
    if (!e) { g->violations++; return LIBREGPU_ERR_BACKEND; }
    if (!e->alive) { g->violations++; return LIBREGPU_ERR_BACKEND; }
    gl_drop_texture(g, &e->tex, &e->fbo);
    entry_remove(g, e);
    return LIBREGPU_OK;
}

static uint64_t gl_presented_fn(void *ctx) { return ((GlHandle *)ctx)->presented; }
static uint64_t gl_errors_fn(void *ctx)    { return ((GlHandle *)ctx)->violations; }




static bool gl_draw_valid(const LibreGPURawOp *op)
{
    return op->blob != NULL && op->blob_size >= sizeof(LibreGPUGlDraw);
}

static bool gl_op_executable(const LibreGPURawOp *op)
{
    switch (op->kind) {
    case LIBREGPU_OP_DRAW:
    case LIBREGPU_OP_DISPATCH:
    case LIBREGPU_OP_RES_UPDATE:
    case LIBREGPU_OP_RES_COPY: {
        if (!gl_draw_valid(op)) return false;
        LibreGPUGlDraw d;
        memcpy(&d, op->blob, sizeof d);
        return d.kind == LIBREGPU_GL_DRAW_CLEAR || d.kind == LIBREGPU_GL_DRAW_RECT;
    }
    case LIBREGPU_OP_RES_CREATE:
    case LIBREGPU_OP_RES_DESTROY:
    case LIBREGPU_OP_STATE_CHANGE:
    case LIBREGPU_OP_SET_CAMERA:
    case LIBREGPU_OP_SYNC:
    case LIBREGPU_OP_PRESENT:
        return true;
    case LIBREGPU_OP_OPAQUE:
        return false;                       
    }
    return false;
}

static LibreGPUStatus gl_prevalidate(const LibreGPURawDataset *ds, const LibreGPUUpdateWork *w)
{
    if (!libregpu_dataset_is_submittable(ds)) return LIBREGPU_ERR_VALIDATION;
    if (w) {
        if (w->region_count > LIBREGPU_MAX_REGIONS) return LIBREGPU_ERR_VALIDATION;
        if (w->op_count > 0 && !w->ops) return LIBREGPU_ERR_VALIDATION;
        for (size_t i = 0; i < w->op_count; i++) {
            if (w->ops[i].op_index >= ds->op_count) return LIBREGPU_ERR_VALIDATION;
            if (w->ops[i].scissored && w->region_count == 0) return LIBREGPU_ERR_VALIDATION;
            if (!gl_op_executable(&ds->ops[w->ops[i].op_index])) return LIBREGPU_ERR_UNSUPPORTED;
        }
        if (w->reproject) {
            if (!w->has_scratch) return LIBREGPU_ERR_VALIDATION;
            if (w->reprojection.surface_w <= 0 || w->reprojection.surface_h <= 0) return LIBREGPU_ERR_VALIDATION;
        }
        return LIBREGPU_OK;
    }
    for (size_t i = 0; i < ds->op_count; i++) {
        if (ds->ops[i].kind == LIBREGPU_OP_RES_DESTROY) continue;
        if (!gl_op_executable(&ds->ops[i])) return LIBREGPU_ERR_UNSUPPORTED;
    }
    return LIBREGPU_OK;
}

static bool op_writes_target(const LibreGPURawOp *op)
{
    for (uint8_t i = 0; i < op->write_count; i++)
        if (op->writes[i].id == LIBREGPU_GL_TARGET_RES_ID) return true;
    return false;
}

static void gl_exec_op(GlHandle *g, const LibreGPURawOp *op, const LibreGPURect *scissor)
{
    switch (op->kind) {
    case LIBREGPU_OP_DRAW:
    case LIBREGPU_OP_DISPATCH:
    case LIBREGPU_OP_RES_UPDATE:
    case LIBREGPU_OP_RES_COPY: {
        if (!op_writes_target(op)) return;             
        LibreGPUGlDraw d;
        memcpy(&d, op->blob, sizeof d);
        gl_draw_op(g, &d, scissor);
        return;
    }
    case LIBREGPU_OP_RES_CREATE:
        for (uint8_t i = 0; i < op->write_count; i++) {
            GlEntry *e = entry_find(g, op->writes[i]);
            if (e && e->alive) { g->violations++; continue; }          
            if (!e) e = entry_add(g, op->writes[i]);
            if (!e) { g->violations++; continue; }
            e->alive = true;
            e->alloc_bytes = op->size_bytes;
        }
        return;
    case LIBREGPU_OP_RES_DESTROY:
        return;                                    
    case LIBREGPU_OP_PRESENT:
        gl_present(g);
        return;
    case LIBREGPU_OP_SET_CAMERA:
    case LIBREGPU_OP_STATE_CHANGE:
    case LIBREGPU_OP_SYNC:
    default:
        return;
    }
}

static LibreGPUStatus gl_execute(GlHandle *g, const LibreGPURawDataset *ds,
                              const LibreGPUUpdateWork *w)
{
    gl_bind_target(g);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D, g->white_tex);
    glEnable(GL_TEXTURE_2D);

    if (w && w->reproject) {
        GlEntry *scratch = entry_find(g, w->scratch);
        if (!scratch) return LIBREGPU_ERR_BACKEND;
        if (!scratch->fbo || scratch->w != g->W || scratch->h != g->H) {
            gl_drop_texture(g, &scratch->tex, &scratch->fbo);
            if (!gl_make_texture(g, &scratch->tex, &scratch->fbo, g->W, g->H))
                return LIBREGPU_ERR_BACKEND;
            scratch->w = g->W;
            scratch->h = g->H;
        }
        

        gl_swap_target_scratch(g, scratch);
        gl_reproject(g, scratch, w->reprojection.shift_x, w->reprojection.shift_y);
        gl_bind_target(g);
    }

    if (!w) {
        for (size_t i = 0; i < ds->op_count; i++)
            gl_exec_op(g, &ds->ops[i], NULL);
    } else {
        for (size_t i = 0; i < w->op_count; i++) {
            const LibreGPUReplayOp *r = &w->ops[i];
            const LibreGPURawOp *op = &ds->ops[r->op_index];
            if (r->scissored) {
                for (uint8_t k = 0; k < w->region_count; k++)
                    gl_exec_op(g, op, &w->regions[k]);
            } else {
                gl_exec_op(g, op, NULL);
            }
        }
    }

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    g->p_BindFramebuffer(GL_FRAMEBUFFER_EXT, 0);
    if (g->mode == LIBREGPU_GL_WINDOWED) SDL_GL_SwapWindow(g->window);
    return LIBREGPU_OK;
}

static LibreGPUStatus gl_submit(void *ctx, const LibreGPURawDataset *ds,
                             const LibreGPUUpdateWork *w, uint64_t *fence_out)
{
    GlHandle *g = (GlHandle *)ctx;
    if (!g->session_valid) return LIBREGPU_ERR_SESSION_INVALID;
    const LibreGPUStatus pre = gl_prevalidate(ds, w);
    if (pre != LIBREGPU_OK) return pre;
    if (!g->attached) return LIBREGPU_ERR_STATE;
    gl_clear_errors();                              

    const uint64_t fence = ++g->submitted;
    GLuint query = 0;
    if (g->have_timer) {
        g->p_GenQueries(1, &query);
        if (query) g->p_BeginQuery(GL_TIME_ELAPSED_EXT, query);
    }

    const uint64_t start_ns = libregpu_now_ns();
    const LibreGPUStatus st = gl_execute(g, ds, w);

    if (g->have_timer && query) g->p_EndQuery(GL_TIME_ELAPSED_EXT);
    GLsync sync = NULL;
    if (g->have_sync) sync = g->p_FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();

    GlPending *p = &g->pending[g->pending_slot];
    g->pending_slot = (g->pending_slot + 1) % GL_MAX_PENDING;
    if (p->used) {                                 
        if (p->sync) { g->p_DeleteSync(p->sync); p->sync = NULL; }
        if (p->query) { g->p_DeleteQueries(1, &p->query); p->query = 0; }
    }
    p->used = true;
    p->fence = fence;
    p->query = query;
    p->sync = sync;
    p->start_ns = start_ns;

    if (st != LIBREGPU_OK) return st;
    if (glGetError() != GL_NO_ERROR) {
        g->violations++;
        return LIBREGPU_ERR_BACKEND;                  
    }
    if (!g->have_sync) g->completed = fence;       
    if (fence_out) *fence_out = fence;
    return LIBREGPU_OK;
}




static void gl_destroy_handle(GlHandle *g)
{
    if (!g) return;
    if (g->getproc) {                                  
        if (g->have_fbo) {
            for (size_t i = 0; i < g->entry_count; i++)
                gl_drop_texture(g, &g->entries[i].tex, &g->entries[i].fbo);
            gl_drop_texture(g, &g->white_tex, NULL);
            gl_drop_texture(g, &g->shadow_tex, &g->shadow_fbo);
            if (g->mode == LIBREGPU_GL_HEADLESS)
                gl_drop_texture(g, &g->display_tex, &g->display_fbo);
            gl_drop_texture(g, &g->target_tex, &g->target_fbo);
        } else {
            if (g->white_tex) glDeleteTextures(1, &g->white_tex);
            if (g->shadow_tex) glDeleteTextures(1, &g->shadow_tex);
            if (g->target_tex) glDeleteTextures(1, &g->target_tex);
            if (g->display_tex) glDeleteTextures(1, &g->display_tex);
        }
        for (size_t i = 0; i < GL_MAX_PENDING; i++) {
            GlPending *p = &g->pending[i];
            if (!p->used) continue;
            if (p->sync && g->p_DeleteSync) g->p_DeleteSync(p->sync);
            if (p->query && g->p_DeleteQueries) g->p_DeleteQueries(1, &p->query);
        }
        glFinish();
    }
    free(g->entries);
    gl_queue_release(g);

    if (g->mode == LIBREGPU_GL_WINDOWED) {
        if (g->sdl_context) SDL_GL_DeleteContext(g->sdl_context);
        if (g->window) SDL_DestroyWindow(g->window);
        if (g->sdl_owned) SDL_Quit();
    } else if (g->egl_owned) {
        eglMakeCurrent(g->edpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g->esurf != EGL_NO_SURFACE) eglDestroySurface(g->edpy, g->esurf);
        if (g->ectx != EGL_NO_CONTEXT) eglDestroyContext(g->edpy, g->ectx);
        eglTerminate(g->edpy);
    }
    free(g);
}

void libregpu_gl_backend_destroy(LibreGPUBackend *be)
{
    if (be) gl_destroy_handle((GlHandle *)be);
}

LibreGPUBackend *libregpu_gl_backend_create(const LibreGPUGlOptions *options)
{
    if (!options || options->width <= 0 || options->height <= 0) {
        gl_set_error("invalid OpenGL surface size");
        return NULL;
    }
    GlHandle *g = (GlHandle *)calloc(1, sizeof *g);
    if (!g) { gl_set_error("out of memory"); return NULL; }
    g->mode = options->mode;
    g->W = options->width;
    g->H = options->height;
    g->session_valid = true;
    g->attached = false;
    gl_set_error("cannot initialise the OpenGL backend");

    bool ok = (g->mode == LIBREGPU_GL_HEADLESS) ? gl_init_egl(g) : gl_init_sdl(g, options);
    if (ok) ok = gl_load_procs(g);
    if (ok) ok = gl_create_objects(g);
    if (ok) gl_timer_calibrate(g);                 
    if (ok) ok = (gl_queue_init(g) == LIBREGPU_OK);
    if (!ok) {
        gl_destroy_handle(g);
        return NULL;
    }

    LibreGPUBackend *b = &g->base;
    b->name = "opengl";
    b->ctx = g;
    const bool timing = g->have_timer || g->have_sync;
    b->caps.supports_retained_results = true;
    b->caps.supports_scissor_replay   = true;
    b->caps.supports_reproject        = g->have_blit;
    b->caps.supports_gpu_timing       = timing;
    b->attach = gl_attach;
    b->detach = gl_detach;
    b->session_valid = gl_session_valid;
    b->next_dataset = gl_next_dataset;
    b->submit = gl_submit;
    b->completed_fence = gl_completed_fence;
    b->wait_idle = gl_wait_idle;
    b->create_scratch = gl_create_scratch;
    b->destroy_resource = gl_destroy_resource;
    b->gpu_time_ns = timing ? gl_gpu_time : NULL;
    b->presented_count = gl_presented_fn;
    b->error_count = gl_errors_fn;
    b->destroy = libregpu_gl_backend_destroy;
    return b;
}




int32_t libregpu_gl_width(const LibreGPUBackend *be)  { return be ? ((const GlHandle *)be)->W : 0; }
int32_t libregpu_gl_height(const LibreGPUBackend *be) { return be ? ((const GlHandle *)be)->H : 0; }
bool libregpu_gl_have_gpu_timing(const LibreGPUBackend *be)
{
    if (!be) return false;
    const GlHandle *g = (const GlHandle *)be;
    return g->have_timer || g->have_sync;
}
bool libregpu_gl_have_timer_query(const LibreGPUBackend *be)
{ return be ? ((const GlHandle *)be)->have_timer : false; }
const char *libregpu_gl_timing_name(const LibreGPUBackend *be)
{
    if (!be) return "none";
    const GlHandle *g = (const GlHandle *)be;
    if (g->have_timer) return "GL_EXT_timer_query";
    if (g->have_sync)  return "GL_ARB_sync fence completion";
    return "none";
}
uint64_t libregpu_gl_frames_presented(const LibreGPUBackend *be)
{ return be ? ((const GlHandle *)be)->presented : 0; }
uint64_t libregpu_gl_violations(const LibreGPUBackend *be)
{ return be ? ((const GlHandle *)be)->violations : 0; }

LibreGPUStatus libregpu_gl_read_target(LibreGPUBackend *be, uint32_t *rgba, size_t pixels)
{
    if (!be || !rgba) return LIBREGPU_ERR_INVALID_ARG;
    GlHandle *g = (GlHandle *)be;
    if (pixels < (size_t)g->W * (size_t)g->H) return LIBREGPU_ERR_INVALID_ARG;
    if (!g->attached) return LIBREGPU_ERR_STATE;

    uint32_t *tmp = (uint32_t *)malloc((size_t)g->W * (size_t)g->H * sizeof *tmp);
    if (!tmp) return LIBREGPU_ERR_NO_MEMORY;
    g->p_BindFramebuffer(GL_FRAMEBUFFER_EXT, g->target_fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glReadPixels(0, 0, (GLsizei)g->W, (GLsizei)g->H, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
    g->p_BindFramebuffer(GL_FRAMEBUFFER_EXT, 0);

    for (int32_t y = 0; y < g->H; y++) {
        const uint32_t *src = &tmp[(size_t)(g->H - 1 - y) * (size_t)g->W];
        memcpy(&rgba[(size_t)y * (size_t)g->W], src, (size_t)g->W * sizeof *rgba);
    }
    free(tmp);
    return glGetError() == GL_NO_ERROR ? LIBREGPU_OK : LIBREGPU_ERR_BACKEND;
}

bool libregpu_gl_should_close(LibreGPUBackend *be)
{
    if (!be) return true;
    GlHandle *g = (GlHandle *)be;
    if (g->mode != LIBREGPU_GL_WINDOWED) return g->quit;
    if (!g->sdl_owned) return g->quit;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) g->quit = true;
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) g->quit = true;
    }
    return g->quit;
}

#endif 
#endif 



























#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>




#define GD_W 960
#define GD_H 540
#define GD_MAX_OBJECTS 8

typedef struct {
    int32_t  x, y, w, h;
    uint32_t r, g, b;
} GdObject;

typedef struct {
    int32_t  pan_x, pan_y;
    size_t   count;
    GdObject obj[GD_MAX_OBJECTS];
    uint32_t bg_r, bg_g, bg_b;
} GdScene;





static const uint32_t GD_BG[3] = { 12, 14, 20 };

static const GdObject GD_A0 = {   0,  40, 620, 380, 14,  8, 10 };
static const GdObject GD_A1 = { 180, 120, 620, 380,  8, 14, 10 };
static const GdObject GD_A2 = { 340, 180, 620, 380, 10,  8, 16 };
static const GdObject GD_A3 = {  80, 300, 520, 380, 12, 12,  8 };
static const GdObject GD_A4 = { 260,   0, 520, 480,  6, 10, 14 };
static const GdObject GD_A5 = {   0, 180, 420, 460, 10,  6, 12 };
static const GdObject GD_A6 = { 520,  80, 420, 380, 14, 10,  6 };
static const GdObject GD_A7 = { 220, 140, 620, 380,  8,  8,  8 };


static const GdObject GD_B0 = {   0,  40, 620, 380, 44,  8, 10 };
static const GdObject GD_D3 = {  80, 300, 520, 380, 36, 12,  8 };

static const GdObject GD_SET_A[8] = { GD_A0, GD_A1, GD_A2, GD_A3, GD_A4, GD_A5, GD_A6, GD_A7 };
static const GdObject GD_SET_B[8] = { GD_B0, GD_A1, GD_A2, GD_A3, GD_A4, GD_A5, GD_A6, GD_A7 };
static const GdObject GD_SET_D[8] = { GD_B0, GD_A1, GD_A2, GD_D3, GD_A4, GD_A5, GD_A6, GD_A7 };

typedef struct { int32_t pan_x, pan_y; size_t count; const GdObject *obj; } GdLook;







static const GdLook GD_SCRIPT[16] = {
    {  0, 0, 8, GD_SET_A }, {  0, 0, 8, GD_SET_A },   
    {  0, 0, 8, GD_SET_B }, {  0, 0, 8, GD_SET_B },   
    {  8, 4, 8, GD_SET_B }, {  8, 4, 8, GD_SET_B },   
    {  8, 4, 8, GD_SET_D }, {  8, 4, 8, GD_SET_D },   
    { 20, 7, 8, GD_SET_D }, { 20, 7, 8, GD_SET_D },   
    { 20, 7, 8, GD_SET_D }, { 20, 7, 8, GD_SET_D },   
    { 20, 7, 7, GD_SET_D }, { 20, 7, 7, GD_SET_D },   
    {  4, 1, 8, GD_SET_A }, {  4, 1, 8, GD_SET_A },   
};

static void gd_scene_for(uint32_t frame, GdScene *s)
{
    const GdLook *l = &GD_SCRIPT[frame % 16u];
    memset(s, 0, sizeof *s);
    s->pan_x = l->pan_x;
    s->pan_y = l->pan_y;
    s->count = l->count;
    for (size_t i = 0; i < l->count && i < GD_MAX_OBJECTS; i++) s->obj[i] = l->obj[i];
    s->bg_r = GD_BG[0]; s->bg_g = GD_BG[1]; s->bg_b = GD_BG[2];
}

static bool gd_scene_equal(const GdScene *a, const GdScene *b)
{
    if (a->pan_x != b->pan_x || a->pan_y != b->pan_y || a->count != b->count) return false;
    return memcmp(a->obj, b->obj, a->count * sizeof a->obj[0]) == 0;
}




static LibreGPURawOp gd_op(LibreGPUOpKind kind)
{
    LibreGPURawOp op;
    memset(&op, 0, sizeof op);
    op.kind = kind;
    return op;
}

static LibreGPUResourceId gd_rid(uint32_t id)
{
    LibreGPUResourceId r;
    r.id = id;
    r.generation = 1;
    return r;
}

static uint64_t gd_object_hash(const GdObject *o)
{
    uint64_t h = libregpu_hash_combine(0x0B7EC7u, (uint64_t)(uint32_t)o->x);
    h = libregpu_hash_combine(h, (uint64_t)(uint32_t)o->y);
    h = libregpu_hash_combine(h, (uint64_t)(uint32_t)o->w);
    h = libregpu_hash_combine(h, (uint64_t)(uint32_t)o->h);
    h = libregpu_hash_combine(h, ((uint64_t)o->r << 16) | ((uint64_t)o->g << 8) | o->b);
    return h;
}

static LibreGPUStatus gd_build(const GdScene *s, LibreGPURawDataset *out)
{
    if (!s || !out) return LIBREGPU_ERR_INVALID_ARG;
    libregpu_dataset_init(out);
    if (s->count > GD_MAX_OBJECTS) return LIBREGPU_ERR_INVALID_ARG;

    LibreGPUCapture cap;
    libregpu_capture_init(&cap);
    LibreGPUStatus st = LIBREGPU_OK;
    bool ready = false;

#define GD_FEED(opv)                                                             \
    do {                                                                         \
        st = libregpu_capture_feed(&cap, &(opv), out, &ready);                      \
        if (st != LIBREGPU_OK) goto fail;                                           \
    } while (0)

    {
        LibreGPURawOp op = gd_op(LIBREGPU_OP_SET_CAMERA);
        op.camera_valid = true;
        op.camera.projection = LIBREGPU_PROJ_PAN_2D;
        op.camera.fov_x = 1.0f;
        op.camera.fov_y = (float)GD_H / (float)GD_W;
        op.camera.surface_w = GD_W;
        op.camera.surface_h = GD_H;
        op.camera.yaw   = (float)s->pan_x * op.camera.fov_x / (float)GD_W;
        op.camera.pitch = (float)s->pan_y * op.camera.fov_y / (float)GD_H;
        GD_FEED(op);
    }
    {
        LibreGPUGlDraw d;
        memset(&d, 0, sizeof d);
        d.kind = LIBREGPU_GL_DRAW_CLEAR;
        d.x = 0; d.y = 0; d.w = GD_W; d.h = GD_H;
        d.r = s->bg_r; d.g = s->bg_g; d.b = s->bg_b; d.a = 255;

        LibreGPURawOp op = gd_op(LIBREGPU_OP_DRAW);
        op.flags = LIBREGPU_OPF_CLEARS_TARGET;
        op.writes[0] = gd_rid(LIBREGPU_GL_TARGET_RES_ID);
        op.write_count = 1;
        op.payload_hash = libregpu_hash_combine(0xC1EAu, ((uint64_t)s->bg_r << 16) |
                                                          ((uint64_t)s->bg_g << 8) | s->bg_b);
        op.region = libregpu_rect_make(0, 0, GD_W, GD_H);
        op.region_valid = true;
        op.blob = (uint8_t *)&d;
        op.blob_size = sizeof d;
        GD_FEED(op);
    }
    for (size_t i = 0; i < s->count; i++) {
        const GdObject *o = &s->obj[i];
        LibreGPUGlDraw d;
        memset(&d, 0, sizeof d);
        d.kind = LIBREGPU_GL_DRAW_RECT;
        d.x = o->x - s->pan_x;
        d.y = o->y + s->pan_y;
        d.w = o->w;
        d.h = o->h;
        d.r = o->r; d.g = o->g; d.b = o->b; d.a = 0;   

        LibreGPURawOp op = gd_op(LIBREGPU_OP_DRAW);
        op.reads[0] = gd_rid(LIBREGPU_GL_TEXTURE_RES_ID);
        op.read_count = 1;
        op.writes[0] = gd_rid(LIBREGPU_GL_TARGET_RES_ID);
        op.write_count = 1;
        op.payload_hash = gd_object_hash(o);            
        op.region = libregpu_rect_make(d.x, d.y, d.x + d.w, d.y + d.h);
        op.region_valid = true;
        op.blob = (uint8_t *)&d;
        op.blob_size = sizeof d;
        GD_FEED(op);
    }
    {
        LibreGPURawOp op = gd_op(LIBREGPU_OP_PRESENT);
        op.reads[0] = gd_rid(LIBREGPU_GL_TARGET_RES_ID);
        op.read_count = 1;
        GD_FEED(op);
    }
#undef GD_FEED
    libregpu_capture_shutdown(&cap);
    return ready ? LIBREGPU_OK : LIBREGPU_ERR_VALIDATION;

fail:
    libregpu_capture_shutdown(&cap);
    libregpu_dataset_release(out);
    return st;
}




static uint32_t gd_pack(uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
    return (a << 24) | (b << 16) | (g << 8) | r;   
}

static void gd_reference(const LibreGPURawDataset *ds, uint32_t *out)
{
    for (size_t i = 0; i < (size_t)GD_W * (size_t)GD_H; i++) out[i] = 0u;
    for (size_t i = 0; i < ds->op_count; i++) {
        const LibreGPURawOp *op = &ds->ops[i];
        if (!(op->kind == LIBREGPU_OP_DRAW || op->kind == LIBREGPU_OP_DISPATCH ||
              op->kind == LIBREGPU_OP_RES_UPDATE || op->kind == LIBREGPU_OP_RES_COPY)) continue;
        bool writes_target = false;
        for (uint8_t k = 0; k < op->write_count; k++)
            if (op->writes[k].id == LIBREGPU_GL_TARGET_RES_ID) writes_target = true;
        if (!writes_target || !op->blob || op->blob_size < sizeof(LibreGPUGlDraw)) continue;
        LibreGPUGlDraw d;
        memcpy(&d, op->blob, sizeof d);

        int32_t x0, y0, x1, y1;
        if (d.kind == LIBREGPU_GL_DRAW_CLEAR) {
            x0 = 0; y0 = 0; x1 = GD_W; y1 = GD_H;
        } else {
            x0 = d.x; y0 = d.y; x1 = d.x + d.w; y1 = d.y + d.h;
        }
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > GD_W) x1 = GD_W;
        if (y1 > GD_H) y1 = GD_H;
        if (x1 <= x0 || y1 <= y0) continue;

        for (int32_t y = y0; y < y1; y++) {
            for (int32_t x = x0; x < x1; x++) {
                uint32_t *p = &out[(size_t)y * (size_t)GD_W + (size_t)x];
                if (d.kind == LIBREGPU_GL_DRAW_CLEAR) {
                    *p = gd_pack(d.r, d.g, d.b, d.a);
                } else {
                    uint32_t r = (*p & 0xFFu) + d.r;
                    uint32_t g = ((*p >> 8) & 0xFFu) + d.g;
                    uint32_t b = ((*p >> 16) & 0xFFu) + d.b;
                    uint32_t a = ((*p >> 24) & 0xFFu) + d.a;
                    if (r > 255u) r = 255u;
                    if (g > 255u) g = 255u;
                    if (b > 255u) b = 255u;
                    if (a > 255u) a = 255u;
                    *p = gd_pack(r, g, b, a);
                }
            }
        }
    }
}




typedef struct {
    bool     windowed, hidden, quiet;
    bool     use_camera, use_micro;
    bool     require_timer;
    uint32_t frames;
    uint32_t baseline_sample;
    int32_t  width, height;
    bool     help, version;
} GdOptions;

static void gd_defaults(GdOptions *o)
{
    memset(o, 0, sizeof *o);
    o->use_camera = true;          
    o->use_micro = true;
    o->require_timer = true;
    o->frames = 96;
    o->baseline_sample = 2;        
    o->width = GD_W;
    o->height = GD_H;
}

static void gd_usage(FILE *out, const char *prog)
{
    fprintf(out,
        "usage: %s [options]\n"
        "  --windowed             run in an SDL2 window (default: headless EGL)\n"
        "  --hidden               create the window without mapping it\n"
        "  --frames <n>           frames to run, 1..100000 (default 96)\n"
        "  --baseline-sample <n>  force an ordinary frame every n cycles, 0 disables (default 2)\n"
        "  --no-camera            disable the Camera Update Unit\n"
        "  --no-micro             disable the Micro-Pixel Update Unit\n"
        "  --width <n>            surface width (default %d)\n"
        "  --height <n>           surface height (default %d)\n"
        "  --require-timer        fail when GL_EXT_timer_query is unavailable (default)\n"
        "  --allow-no-timer       accept a run without hardware GPU timing\n"
        "  --quiet                print only the final result\n"
        "  --version              print the build identity\n"
        "  --help                 this text\n",
        prog ? prog : "libregpu-gpu", GD_W, GD_H);
}

static bool gd_parse_u32(const char *s, uint32_t *out)
{
    char *end = NULL;
    if (!s || *s == '-' || *s == '+' || *s == '\0') return false;
    const unsigned long v = strtoul(s, &end, 10);
    if (!end || *end != '\0' || v > 100000ul) return false;
    *out = (uint32_t)v;
    return true;
}

static LibreGPUStatus gd_parse(GdOptions *o, int argc, char **argv, char *err, size_t errlen)
{
    gd_defaults(o);
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { o->help = true; continue; }
        if (!strcmp(a, "--version")) { o->version = true; continue; }
        if (!strcmp(a, "--windowed")) { o->windowed = true; continue; }
        if (!strcmp(a, "--hidden")) { o->hidden = true; continue; }
        if (!strcmp(a, "--quiet")) { o->quiet = true; continue; }
        if (!strcmp(a, "--no-camera")) { o->use_camera = false; continue; }
        if (!strcmp(a, "--no-micro")) { o->use_micro = false; continue; }
        if (!strcmp(a, "--require-timer")) { o->require_timer = true; continue; }
        if (!strcmp(a, "--allow-no-timer")) { o->require_timer = false; continue; }

        const bool value_opt = !strcmp(a, "--frames") || !strcmp(a, "--baseline-sample") ||
                               !strcmp(a, "--width") || !strcmp(a, "--height");
        if (!value_opt || !v || (v[0] == '-' && v[1] == '-')) {
            (void)snprintf(err, errlen, "unknown or incomplete option '%s'", a);
            return LIBREGPU_ERR_INVALID_ARG;
        }
        uint32_t n = 0;
        if (!gd_parse_u32(v, &n)) {
            (void)snprintf(err, errlen, "%s needs a non-negative integer", a);
            return LIBREGPU_ERR_INVALID_ARG;
        }
        if (!strcmp(a, "--frames")) {
            if (n == 0) { (void)snprintf(err, errlen, "--frames must be at least 1"); return LIBREGPU_ERR_INVALID_ARG; }
            o->frames = n;
        } else if (!strcmp(a, "--baseline-sample")) {
            o->baseline_sample = n;
        } else if (!strcmp(a, "--width")) {
            if (n < 16 || n > 4096) { (void)snprintf(err, errlen, "--width must be 16..4096"); return LIBREGPU_ERR_INVALID_ARG; }
            o->width = (int32_t)n;
        } else {
            if (n < 16 || n > 4096) { (void)snprintf(err, errlen, "--height must be 16..4096"); return LIBREGPU_ERR_INVALID_ARG; }
            o->height = (int32_t)n;
        }
        i++;
    }
    return LIBREGPU_OK;
}




int main(int argc, char **argv)
{
    GdOptions o;
    char err[200] = "";
    if (gd_parse(&o, argc, argv, err, sizeof err) != LIBREGPU_OK) {
        fprintf(stderr, "error: %s\n", err);
        gd_usage(stderr, argc > 0 ? argv[0] : "libregpu-gpu");
        return 2;
    }
    if (o.help) { gd_usage(stdout, argc > 0 ? argv[0] : "libregpu-gpu"); return 0; }
    if (o.version) {
        printf("libregpu-gpu %s (%s, %s)\n", LIBREGPU_VERSION, LIBREGPU_STEP_LABEL, LIBREGPU_RELEASE);
        return 0;
    }
    if (o.width != GD_W || o.height != GD_H) {
        fprintf(stderr, "error: this controlled scene is authored for %dx%d\n", GD_W, GD_H);
        return 2;
    }

    LibreGPUGlOptions go;
    memset(&go, 0, sizeof go);
    go.mode = o.windowed ? LIBREGPU_GL_WINDOWED : LIBREGPU_GL_HEADLESS;
    go.width = o.width;
    go.height = o.height;
    go.hidden = o.hidden;
    go.title = "LIBREGPU M3 controlled OpenGL scene";

    LibreGPUBackend *be = libregpu_gl_backend_create(&go);
    if (!be) {
        fprintf(stderr, "error: OpenGL backend: %s\n", libregpu_gl_last_error());
        return 3;
    }
    if (!libregpu_gl_have_gpu_timing(be) && o.require_timer) {
        fprintf(stderr, "error: no working hardware GPU timing source is available\n"
                        "       (use --allow-no-timer to run without hardware GPU timing)\n");
        libregpu_backend_destroy(be);
        return 3;
    }

    LibreGPUConfig cfg;
    libregpu_config_defaults(&cfg);
    cfg.enable_micro_pixel = o.use_micro;
    cfg.enable_camera = o.use_camera;          
    cfg.baseline_sample_interval = o.baseline_sample;

    LibreGPURuntime rt;
    LibreGPUStatus st = libregpu_runtime_init(&rt, &cfg, be);
    if (st != LIBREGPU_OK) {
        fprintf(stderr, "error: runtime init: %s\n", libregpu_status_str(st));
        libregpu_backend_destroy(be);
        return 4;
    }
    st = libregpu_runtime_start(&rt);
    if (st != LIBREGPU_OK) {
        fprintf(stderr, "error: runtime start: %s\n", libregpu_status_str(st));
        libregpu_backend_destroy(be);
        return 4;
    }

    const char *renderer = "?";
    {
        

        renderer = o.windowed ? "SDL2 OpenGL window" : "EGL surfaceless";
    }

    if (!o.quiet) {
        printf("LIBREGPU %s (%s, %s)\n", LIBREGPU_VERSION, LIBREGPU_STEP_LABEL, LIBREGPU_RELEASE);
        printf("controlled OpenGL scene: %dx%d, %s, camera=%s, micro-pixel=%s\n",
               o.width, o.height, renderer,
               o.use_camera ? "on" : "off", o.use_micro ? "on" : "off");
        printf("GPU timing: %s, baseline sample interval: %u\n\n",
               libregpu_gl_timing_name(be), o.baseline_sample);
    }

    size_t pixels = (size_t)o.width * (size_t)o.height;
    uint32_t *reference = (uint32_t *)malloc(pixels * sizeof *reference);
    uint32_t *actual = (uint32_t *)malloc(pixels * sizeof *actual);
    if (!reference || !actual) {
        fprintf(stderr, "error: out of memory\n");
        free(reference); free(actual);
        libregpu_runtime_stop(&rt);
        libregpu_backend_destroy(be);
        return 4;
    }

    uint64_t verified_frames = 0, mismatched_frames = 0, mismatched_pixels = 0;
    uint64_t optimized_cycles = 0, camera_cycles = 0, micro_cycles = 0, no_impact = 0;
    uint64_t ordinary_cycles = 0, first_bad_frame = 0;
    bool failed = false;
    int rc = 0;

    


    GdScene prev_scene;
    LibreGPUCycleInfo prev_info;
    bool have_prev = false;
    memset(&prev_scene, 0, sizeof prev_scene);
    memset(&prev_info, 0, sizeof prev_info);
    uint64_t pair_all = 0, pair_all_opt_ns = 0, pair_all_ord_ns = 0;
    uint64_t pair_cam = 0, pair_cam_opt_ns = 0, pair_cam_ord_ns = 0;
    uint64_t pair_mic = 0, pair_mic_opt_ns = 0, pair_mic_ord_ns = 0;
    uint64_t pair_ni  = 0, pair_ni_opt_ns  = 0, pair_ni_ord_ns  = 0;

    for (uint32_t f = 0; f < o.frames; f++) {
        if (libregpu_gl_should_close(be)) break;

        GdScene scene;
        gd_scene_for(f, &scene);
        LibreGPURawDataset ds;
        st = gd_build(&scene, &ds);
        if (st != LIBREGPU_OK) {
            fprintf(stderr, "error: capture failed on frame %u: %s\n", f, libregpu_status_str(st));
            rc = 5; failed = true;
            break;
        }
        gd_reference(&ds, reference);                 
        st = libregpu_gl_enqueue(be, &ds);
        libregpu_dataset_release(&ds);                   
        if (st != LIBREGPU_OK) {
            fprintf(stderr, "error: enqueue failed on frame %u: %s\n", f, libregpu_status_str(st));
            rc = 5; failed = true;
            break;
        }

        LibreGPUCycleInfo info;
        memset(&info, 0, sizeof info);
        st = libregpu_runtime_step(&rt, &info);
        if (st != LIBREGPU_OK) {
            fprintf(stderr, "error: runtime step failed on frame %u: %s\n", f, libregpu_status_str(st));
            rc = 5; failed = true;
            break;
        }

        if (info.submitted_mode == LIBREGPU_Z_INCREMENTAL) {
            optimized_cycles++;
            if (info.camera) camera_cycles++;
            if (info.micro_pixel) micro_cycles++;
        } else if (info.submitted_mode == LIBREGPU_Z_NO_IMPACT) {
            optimized_cycles++;
            no_impact++;
        } else {
            ordinary_cycles++;
        }

        if (info.forced_baseline && have_prev && prev_info.gpu_ns && info.gpu_ns &&
            prev_info.submitted_mode != LIBREGPU_Z_ORDINARY &&
            gd_scene_equal(&prev_scene, &scene)) {
            const uint64_t opt = prev_info.gpu_ns, ord = info.gpu_ns;
            pair_all++;
            pair_all_opt_ns += opt;
            pair_all_ord_ns += ord;
            if (prev_info.submitted_mode == LIBREGPU_Z_NO_IMPACT) {
                pair_ni++;
                pair_ni_opt_ns += opt;
                pair_ni_ord_ns += ord;
            } else if (prev_info.camera) {
                pair_cam++;
                pair_cam_opt_ns += opt;
                pair_cam_ord_ns += ord;
            } else {
                pair_mic++;
                pair_mic_opt_ns += opt;
                pair_mic_ord_ns += ord;
            }
        }

        st = libregpu_gl_read_target(be, actual, pixels);
        if (st != LIBREGPU_OK) {
            fprintf(stderr, "error: read-back failed on frame %u: %s\n", f, libregpu_status_str(st));
            rc = 5; failed = true;
            break;
        }
        uint64_t bad = 0;
        for (size_t i = 0; i < pixels; i++) if (actual[i] != reference[i]) bad++;
        verified_frames++;
        if (bad) {
            mismatched_frames++;
            mismatched_pixels += bad;
            if (first_bad_frame == 0) first_bad_frame = (uint64_t)f + 1;
            if (!o.quiet) {
                fprintf(stderr, "A/B MISMATCH frame %u: %llu/%zu pixels differ\n",
                        f, (unsigned long long)bad, pixels);
            }
            failed = true;
        }
        if (!o.quiet) {
            ui_show_cycle(stdout, rt.cycle, &info);
        }
        prev_info = info;
        prev_scene = scene;
        have_prev = true;
    }

    if (!o.quiet) printf("\n");
    libregpu_runtime_stop(&rt);
    if (libregpu_gl_violations(be)) failed = true;

    libregpu_metrics_report(&rt.metrics, &rt.gov, stdout);

    printf("\nM3 OpenGL validation\n");
    printf("  A/B frames            : %llu verified, %llu mismatched (%llu pixels)\n",
           (unsigned long long)verified_frames,
           (unsigned long long)mismatched_frames,
           (unsigned long long)mismatched_pixels);
    if (mismatched_frames) printf("  first mismatch        : frame %llu\n",
                                  (unsigned long long)first_bad_frame);
    printf("  submitted path        : %llu optimized (%llu incremental, %llu no-impact), %llu ordinary\n",
           (unsigned long long)optimized_cycles,
           (unsigned long long)(optimized_cycles - no_impact),
           (unsigned long long)no_impact,
           (unsigned long long)ordinary_cycles);
    printf("  unit verdicts         : %llu camera-update cycles, %llu micro-pixel cycles\n",
           (unsigned long long)camera_cycles, (unsigned long long)micro_cycles);
    printf("  resource-lifetime err : %llu (backend), %llu (core)\n",
           (unsigned long long)libregpu_gl_violations(be),
           (unsigned long long)rt.metrics.lifetime_errors);
    printf("  GPU timing source     : %s\n", libregpu_gl_timing_name(be));
    if (pair_all) {
        const double ao = (double)pair_all_opt_ns / (double)pair_all / 1e6;
        const double ar = (double)pair_all_ord_ns / (double)pair_all / 1e6;
        const double as = ar > 0.0 ? 100.0 * (ar - ao) / ar : 0.0;
        printf("  paired A/B frames     : %llu identical-scene pairs (optimised vs forced ordinary)\n",
               (unsigned long long)pair_all);
        printf("  paired GPU time       : optimized %.3f ms vs ordinary %.3f ms (%.1f%% less)\n",
               ao, ar, as);
        if (pair_mic)
            printf("    micro-pixel         : %llu pairs, %.3f ms vs %.3f ms (%.1f%% less)\n",
                   (unsigned long long)pair_mic,
                   (double)pair_mic_opt_ns / (double)pair_mic / 1e6,
                   (double)pair_mic_ord_ns / (double)pair_mic / 1e6,
                   100.0 * ((double)pair_mic_ord_ns - (double)pair_mic_opt_ns) / (double)pair_mic_ord_ns);
        if (pair_cam)
            printf("    camera update       : %llu pairs, %.3f ms vs %.3f ms (%.1f%% less)\n",
                   (unsigned long long)pair_cam,
                   (double)pair_cam_opt_ns / (double)pair_cam / 1e6,
                   (double)pair_cam_ord_ns / (double)pair_cam / 1e6,
                   100.0 * ((double)pair_cam_ord_ns - (double)pair_cam_opt_ns) / (double)pair_cam_ord_ns);
        if (pair_ni)
            printf("    no-impact           : %llu pairs, %.3f ms vs %.3f ms (%.1f%% less)\n",
                   (unsigned long long)pair_ni,
                   (double)pair_ni_opt_ns / (double)pair_ni / 1e6,
                   (double)pair_ni_ord_ns / (double)pair_ni / 1e6,
                   100.0 * ((double)pair_ni_ord_ns - (double)pair_ni_opt_ns) / (double)pair_ni_ord_ns);
        if (pair_all_ord_ns > pair_all_opt_ns)
            printf("  hardware result       : %.1f%% less GPU time on identical frames\n", as);
        else
            printf("  hardware result       : no reduction measured on this driver\n");
    } else if (libregpu_gl_have_gpu_timing(be) && rt.metrics.gpu_samples_baseline >= 4 &&
               rt.metrics.gpu_samples_incremental > 0) {
        const double base = (double)rt.metrics.gpu_ns_baseline / (double)rt.metrics.gpu_samples_baseline;
        const double inc  = (double)rt.metrics.gpu_ns_incremental / (double)rt.metrics.gpu_samples_incremental;
        const double saving = base > 0.0 ? 100.0 * (base - inc) / base : 0.0;
        printf("  hardware result       : %.1f%% less GPU time on optimized frames (unpaired)\n", saving);
    } else {
        printf("  hardware result       : not demonstrated (no comparable GPU samples)\n");
    }

    if (verified_frames == 0) {
        printf("RESULT: FAIL (no frame was verified)\n");
        failed = true;
    } else if (optimized_cycles == 0) {
        printf("RESULT: FAIL (no optimized frame was submitted)\n");
        failed = true;
    } else if (mismatched_frames || libregpu_gl_violations(be) ||
               rt.metrics.lifetime_errors || rt.metrics.bookkeeping_errors) {
        printf("RESULT: FAIL (incorrect output or lifetime violation)\n");
        failed = true;
    } else {
        printf("RESULT: PASS (every verified frame matched the independent reference)\n");
    }

    free(reference);
    free(actual);
    libregpu_backend_destroy(be);
    return (failed || rc != 0) ? 1 : 0;
}

#endif 

#if defined(SIM_INTERPOSE)



























#include <dlfcn.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <GL/glx.h>
#include <EGL/egl.h>

typedef void (*GlxSwapProc)(Display *, GLXDrawable);
typedef EGLBoolean (*EglSwapProc)(EGLDisplay, EGLSurface);
typedef const unsigned char *(*GlGetStringProc)(unsigned int);
typedef __GLXextFuncPtr (*GlxGetProcProc)(const GLubyte *);
struct SDL_Window;
typedef void (*SdlSwapProc)(struct SDL_Window *);

static GlxSwapProc     g_real_glx_swap;
static EglSwapProc     g_real_egl_swap;
static GlGetStringProc g_gl_get_string;
static GlxGetProcProc  g_real_glx_get_proc;
static SdlSwapProc     g_real_sdl_swap;

static atomic_ullong g_glx_swaps;
static atomic_ullong g_egl_swaps;
static atomic_ullong g_sdl_swaps;
static atomic_ullong g_forward_failures;
static atomic_int  g_named;

static char g_renderer[256];
static char g_version[128];
static char g_vendor[256];
static atomic_flag g_writing = ATOMIC_FLAG_INIT;

static GlGetStringProc load_gl_get_string(const char *name);
static GlGetStringProc load_gl_get_string_from(void *handle, const char *name);

static void copy_string(char *dst, size_t cap, const unsigned char *src)
{
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen((const char *)src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}



static void name_once(void)
{
    if (atomic_exchange(&g_named, 1) != 0) return;
    



    if (!g_gl_get_string) g_gl_get_string = load_gl_get_string("glGetString");
    if (!g_gl_get_string) {
        static const char *const libs[] = { "libGL.so.1", "libGL.so", "libGLESv2.so.2" };
        for (size_t i = 0; i < sizeof libs / sizeof libs[0] && !g_gl_get_string; i++) {
            void *h = dlopen(libs[i], RTLD_LAZY);
            if (h) g_gl_get_string = load_gl_get_string_from(h, "glGetString");
        }
    }
    if (!g_gl_get_string) return;
    copy_string(g_vendor,   sizeof g_vendor,   g_gl_get_string(GL_VENDOR));
    copy_string(g_renderer, sizeof g_renderer, g_gl_get_string(GL_RENDERER));
    copy_string(g_version,  sizeof g_version,  g_gl_get_string(GL_VERSION));
    if (getenv("LIBREGPU_INTERPOSE_VERBOSE")) {
        fprintf(stderr, "libregpu-interpose: %s | %s | %s\n",
                g_vendor[0] ? g_vendor : "unknown vendor",
                g_renderer[0] ? g_renderer : "unknown renderer",
                g_version[0] ? g_version : "unknown version");
    }
}

static void write_stats(void)
{
    const char *path = getenv("LIBREGPU_INTERPOSE_STATS");
    if (!path || !path[0]) return;

    const unsigned long long glx = atomic_load(&g_glx_swaps);
    const unsigned long long egl = atomic_load(&g_egl_swaps);
    const unsigned long long sdl = atomic_load(&g_sdl_swaps);
    const unsigned long long fail = atomic_load(&g_forward_failures);

    


    if (glx + egl + sdl == 0) return;
    if (atomic_flag_test_and_set(&g_writing)) return;

    char text[1024];
    const int n = snprintf(text, sizeof text,
        "libregpu-interpose 1\n"
        "mode=presentation-forwarding\n"
        "draw_calls_captured=0\n"
        "frames=%llu\n"
        "glx_swaps=%llu\n"
        "egl_swaps=%llu\n"
        "sdl_swaps=%llu\n"
        "forward_failures=%llu\n"
        "forwarded_unchanged=%d\n"
        "vendor=%s\n"
        "renderer=%s\n"
        "version=%s\n",
        glx + egl + sdl, glx, egl, sdl, fail, fail == 0 ? 1 : 0,
        g_vendor, g_renderer, g_version);
    if (n <= 0) { atomic_flag_clear(&g_writing); return; }

    const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        size_t off = 0;
        const size_t len = (size_t)n < sizeof text ? (size_t)n : sizeof text - 1;
        while (off < len) {
            const ssize_t w = write(fd, text + off, len - off);
            if (w <= 0) break;
            off += (size_t)w;
        }
        (void)close(fd);
    }
    atomic_flag_clear(&g_writing);
}




static unsigned long long total_frames(void)
{
    return atomic_load(&g_glx_swaps) + atomic_load(&g_egl_swaps) + atomic_load(&g_sdl_swaps);
}

static void maybe_flush(unsigned long long frames)
{
    if (frames == 1 || (frames % 120) == 0) write_stats();
}




static GlxSwapProc load_glx_swap(const char *name)
{
    union { void *obj; GlxSwapProc fn; } u;
    u.obj = dlsym(RTLD_NEXT, name);
    return u.fn;
}

static EglSwapProc load_egl_swap(const char *name)
{
    union { void *obj; EglSwapProc fn; } u;
    u.obj = dlsym(RTLD_NEXT, name);
    return u.fn;
}

static GlGetStringProc load_gl_get_string_from(void *handle, const char *name)
{
    union { void *obj; GlGetStringProc fn; } u;
    u.obj = dlsym(handle, name);
    return u.fn;
}

static GlGetStringProc load_gl_get_string(const char *name)
{
    return load_gl_get_string_from(RTLD_DEFAULT, name);
}

static GlxGetProcProc load_glx_get_proc(const char *name)
{
    union { void *obj; GlxGetProcProc fn; } u;
    u.obj = dlsym(RTLD_NEXT, name);
    return u.fn;
}

static SdlSwapProc load_sdl_swap(const char *name)
{
    union { void *obj; SdlSwapProc fn; } u;
    u.obj = dlsym(RTLD_NEXT, name);
    return u.fn;
}

__attribute__((constructor))
static void libregpu_interpose_init(void)
{
    g_real_glx_swap = load_glx_swap("glXSwapBuffers");
    g_real_egl_swap = load_egl_swap("eglSwapBuffers");
    g_gl_get_string = load_gl_get_string("glGetString");
    g_real_glx_get_proc = load_glx_get_proc("glXGetProcAddressARB");
    g_real_sdl_swap = load_sdl_swap("SDL_GL_SwapWindow");
    (void)atexit(write_stats);
}

void glXSwapBuffers(Display *dpy, GLXDrawable drawable)
{
    name_once();
    atomic_fetch_add(&g_glx_swaps, 1);
    maybe_flush(total_frames());
    if (!g_real_glx_swap) {
        atomic_fetch_add(&g_forward_failures, 1);
        return;
    }
    g_real_glx_swap(dpy, drawable);          
}




__GLXextFuncPtr glXGetProcAddressARB(const GLubyte *name)
{
    if (name && strcmp((const char *)name, "glXSwapBuffers") == 0)
        return (__GLXextFuncPtr)glXSwapBuffers;
    if (!g_real_glx_get_proc) return NULL;
    return g_real_glx_get_proc(name);
}

__GLXextFuncPtr glXGetProcAddress(const GLubyte *name)
{
    return glXGetProcAddressARB(name);
}





void SDL_GL_SwapWindow(struct SDL_Window *window)
{
    name_once();
    atomic_fetch_add(&g_sdl_swaps, 1);
    maybe_flush(total_frames());
    if (!g_real_sdl_swap) {
        atomic_fetch_add(&g_forward_failures, 1);
        return;
    }
    g_real_sdl_swap(window);                 
}

EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface)
{
    name_once();
    atomic_fetch_add(&g_egl_swaps, 1);
    maybe_flush(total_frames());
    if (!g_real_egl_swap) {
        atomic_fetch_add(&g_forward_failures, 1);
        return EGL_FALSE;
    }
    return g_real_egl_swap(dpy, surface);    
}

#endif 
