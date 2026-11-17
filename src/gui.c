



























#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#endif


#define SDL_MAIN_HANDLED

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#ifndef NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#  define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#endif
#ifndef NK_INCLUDE_FONT_BAKING
#  define NK_INCLUDE_FONT_BAKING
#endif
#ifndef NK_INCLUDE_DEFAULT_FONT
#  define NK_INCLUDE_DEFAULT_FONT
#endif
#ifndef NK_UINT_DRAW_INDEX
#  define NK_UINT_DRAW_INDEX
#endif
#define NK_IMPLEMENTATION

#include <SDL.h>
#include "nuklear.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <dirent.h>
#  include <fcntl.h>
#  include <spawn.h>
#  include <sys/types.h>
#  include <unistd.h>
extern char **environ;
#endif








#define LIBREGPU_UI_DECLARATIONS_ONLY
#include "ui.c"
#undef LIBREGPU_UI_DECLARATIONS_ONLY






#include <stddef.h>
#include <stdint.h>

#ifndef LIBREGPU_UI_MAX_PROFILES
#define LIBREGPU_UI_MAX_PROFILES 256
#endif

#define LIBREGPU_PROFILE_PAGE_SIZE 5
#define LIBREGPU_FPS_CAP           32
#define LIBREGPU_ERROR_CAP         160
#define LIBREGPU_PI                3.14159265358979323846f


#define LIBREGPU_REFERENCE_W      720.0f
#define LIBREGPU_REFERENCE_H      591.0f


#define LIBREGPU_PAGE_SCROLL_TIME 0.30f
#define LIBREGPU_PAGE_SCROLL_PX   64.0f
#define LIBREGPU_LOGO_SHAKE_TIME  0.16f
#define LIBREGPU_LOGO_SHAKE_PX    2.2f
#define LIBREGPU_RIPPLE_LIFE      0.78f
#define LIBREGPU_RIPPLE_DELAY     0.16f
#define LIBREGPU_RIPPLE_COUNT     3
#define LIBREGPU_RIPPLE_DOTS      25
#define LIBREGPU_RIPPLE_MAX_R     118.0f

typedef struct LibreGPUUIProfile {
    uint32_t id;                  
    char game_path[LIBREGPU_UI_PATH_MAX];
    LibreGPUConfig config;           
    struct nk_image thumbnail;   
    int has_thumbnail;
    char name[96];
} LibreGPUUIProfile;

typedef struct LibreGPUUIHooks {
    void *userdata;

    
    void (*launch_profile)(void *userdata,
                           const LibreGPULauncherSettings *settings);

    
    int (*pick_executable)(void *userdata,
                           char *out_path, size_t out_capacity);

    
    int (*path_is_file)(void *userdata, const char *path);

    
    int (*save_profile)(void *userdata,
                        const LibreGPULauncherSettings *settings,
                        LibreGPUUIProfile *out_profile,
                        char *error_text, size_t error_capacity);

    
    int (*delete_profile)(void *userdata, uint32_t profile_id);

    
    void (*quit)(void *userdata);
} LibreGPUUIHooks;

typedef struct LibreGPUUI {
    LibreGPUUIHooks hooks;
    LibreGPULauncherSettings *settings;
    LibreGPULauncherSettings fallback_settings;

    struct nk_image logo;
    int has_logo;

    LibreGPUUIProfile profiles[LIBREGPU_UI_MAX_PROFILES];
    size_t profile_count;
    size_t current_page;
    size_t target_page;

    int config_open;
    char path_text[LIBREGPU_UI_PATH_MAX];
    char fps_text[LIBREGPU_FPS_CAP];
    int path_invalid;
    int fps_invalid;
    char backend_error[LIBREGPU_ERROR_CAP];

    int context_open;
    uint32_t context_profile_id;
    float context_x;
    float context_y;

    int page_animating;
    float page_elapsed;
    int logo_shaking;
    float shake_elapsed;
    int ripple_active;
    float ripple_elapsed;
    int quit_requested;
} LibreGPUUI;


void libregpu_ui_init(LibreGPUUI *ui,
                   LibreGPULauncherSettings *settings,
                   const LibreGPUUIHooks *hooks,
                   struct nk_image logo,
                   int has_logo);
void libregpu_ui_set_logo(LibreGPUUI *ui, struct nk_image logo, int has_logo);
void libregpu_ui_set_profiles(LibreGPUUI *ui,
                           const LibreGPUUIProfile *profiles,
                           size_t profile_count);
int libregpu_ui_quit_requested(const LibreGPUUI *ui);
void libregpu_ui_draw(struct nk_context *ctx,
                   LibreGPUUI *ui,
                   float window_width,
                   float window_height,
                   float delta_seconds);





typedef struct LibreGPUNkSdl2 {
    SDL_Window *window;
    SDL_Renderer *renderer;

    struct nk_context ctx;
    struct nk_font_atlas atlas;
    struct nk_font *font;

    SDL_Texture *font_texture;
    SDL_Texture *white_texture;

    struct nk_buffer commands;
    struct nk_buffer vertices;
    struct nk_buffer elements;
    struct nk_convert_config config;

    int ready;
} LibreGPUNkSdl2;



int libregpu_nk_sdl2_init(LibreGPUNkSdl2 *nk, SDL_Window *window, SDL_Renderer *renderer);


void libregpu_nk_sdl2_shutdown(LibreGPUNkSdl2 *nk);


void libregpu_nk_sdl2_input_begin(LibreGPUNkSdl2 *nk);
void libregpu_nk_sdl2_input_end(LibreGPUNkSdl2 *nk);



void libregpu_nk_sdl2_handle_event(LibreGPUNkSdl2 *nk, const SDL_Event *event);



void libregpu_nk_sdl2_render(LibreGPUNkSdl2 *nk);





#define LIBREGPU_PATH_NAME_MAX 256

typedef struct LibreGPUDirEntry {
    char name[LIBREGPU_PATH_NAME_MAX];
    int is_directory;
} LibreGPUDirEntry;


int libregpu_path_join(char *out, size_t capacity,
                    const char *directory, const char *name);



int libregpu_path_parent(const char *path, char *out, size_t capacity);


int libregpu_path_is_file(const char *path);



int libregpu_path_is_executable_file(const char *path);

int libregpu_path_is_directory(const char *path);



int libregpu_path_replace_file(const char *temporary, const char *final_path);



int libregpu_path_list_directory(const char *directory,
                              LibreGPUDirEntry *entries, size_t capacity,
                              size_t *out_count);


const char *libregpu_path_basename(const char *path);



int libregpu_path_initial_directory(char *out, size_t capacity);







int libregpu_launch_detached(const char *path,
                          char *error_text, size_t error_capacity);





#define LIBREGPU_PROFILE_PATH_MAX 512
#define LIBREGPU_PROFILE_NAME_MAX 96
#define LIBREGPU_PROFILE_MAX      256
#define LIBREGPU_PROFILE_FPS_MAX  1000000

typedef struct LibreGPUStoredProfile {
    uint32_t id;
    char path[LIBREGPU_PROFILE_PATH_MAX];
    int fps;
    char name[LIBREGPU_PROFILE_NAME_MAX];
} LibreGPUStoredProfile;

typedef struct LibreGPUProfileStore {
    char directory[LIBREGPU_PROFILE_PATH_MAX];
    char file_path[LIBREGPU_PROFILE_PATH_MAX];
    LibreGPUStoredProfile items[LIBREGPU_PROFILE_MAX];
    size_t count;
    uint32_t next_id;
    int loaded;
} LibreGPUProfileStore;



int libregpu_profiles_init(LibreGPUProfileStore *store, const char *directory);

size_t libregpu_profiles_count(const LibreGPUProfileStore *store);
const LibreGPUStoredProfile *libregpu_profiles_at(const LibreGPUProfileStore *store,
                                            size_t index);



int libregpu_profiles_add(LibreGPUProfileStore *store,
                       const char *path, int fps,
                       LibreGPUStoredProfile *out_profile,
                       char *error_text, size_t error_capacity);


int libregpu_profiles_delete(LibreGPUProfileStore *store, uint32_t id);

const char *libregpu_profiles_file_path(const LibreGPUProfileStore *store);





#define LIBREGPU_NK_FONT_HEIGHT 15.0f





static void libregpu_nk_sdl2_copy(nk_handle userdata, const char *text, int len)
{
    char *terminated;
    (void)userdata;
    if (!text || len <= 0) return;

    
    terminated = (char *)SDL_malloc((size_t)len + 1u);
    if (!terminated) return;
    SDL_memcpy(terminated, text, (size_t)len);
    terminated[len] = '\0';
    SDL_SetClipboardText(terminated);
    SDL_free(terminated);
}

static void libregpu_nk_sdl2_paste(nk_handle userdata, struct nk_text_edit *edit)
{
    char *text;
    (void)userdata;
    if (!edit) return;
    text = SDL_GetClipboardText();
    if (!text) return;
    nk_textedit_paste(edit, text, (int)strlen(text));
    SDL_free(text);
}





static void libregpu_nk_sdl2_theme(struct nk_context *ctx)
{
    struct nk_color table[NK_COLOR_COUNT];

    memset(table, 0, sizeof(table));
    table[NK_COLOR_TEXT]                    = nk_rgba(226, 229, 236, 255);
    table[NK_COLOR_WINDOW]                  = nk_rgba(29, 32, 38, 255);
    table[NK_COLOR_HEADER]                  = nk_rgba(37, 40, 47, 255);
    table[NK_COLOR_BORDER]                  = nk_rgba(70, 75, 86, 255);
    table[NK_COLOR_BUTTON]                  = nk_rgba(52, 56, 66, 255);
    table[NK_COLOR_BUTTON_HOVER]            = nk_rgba(67, 73, 86, 255);
    table[NK_COLOR_BUTTON_ACTIVE]           = nk_rgba(82, 90, 106, 255);
    table[NK_COLOR_TOGGLE]                  = nk_rgba(45, 49, 58, 255);
    table[NK_COLOR_TOGGLE_HOVER]            = nk_rgba(58, 63, 75, 255);
    table[NK_COLOR_TOGGLE_CURSOR]           = nk_rgba(96, 132, 214, 255);
    table[NK_COLOR_SELECT]                  = nk_rgba(52, 58, 72, 255);
    table[NK_COLOR_SELECT_ACTIVE]           = nk_rgba(96, 132, 214, 255);
    table[NK_COLOR_SLIDER]                  = nk_rgba(40, 44, 52, 255);
    table[NK_COLOR_SLIDER_CURSOR]           = nk_rgba(96, 132, 214, 255);
    table[NK_COLOR_SLIDER_CURSOR_HOVER]     = nk_rgba(116, 150, 226, 255);
    table[NK_COLOR_SLIDER_CURSOR_ACTIVE]    = nk_rgba(136, 166, 236, 255);
    table[NK_COLOR_PROPERTY]                = nk_rgba(40, 44, 52, 255);
    table[NK_COLOR_EDIT]                    = nk_rgba(20, 22, 27, 255);
    table[NK_COLOR_EDIT_CURSOR]             = nk_rgba(226, 229, 236, 255);
    table[NK_COLOR_COMBO]                   = nk_rgba(40, 44, 52, 255);
    table[NK_COLOR_CHART]                   = nk_rgba(40, 44, 52, 255);
    table[NK_COLOR_CHART_COLOR]             = nk_rgba(96, 132, 214, 255);
    table[NK_COLOR_CHART_COLOR_HIGHLIGHT]   = nk_rgba(210, 84, 84, 255);
    table[NK_COLOR_SCROLLBAR]               = nk_rgba(40, 44, 52, 255);
    table[NK_COLOR_SCROLLBAR_CURSOR]        = nk_rgba(67, 73, 86, 255);
    table[NK_COLOR_SCROLLBAR_CURSOR_HOVER]  = nk_rgba(82, 90, 106, 255);
    table[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = nk_rgba(96, 132, 214, 255);
    table[NK_COLOR_TAB_HEADER]              = nk_rgba(37, 40, 47, 255);

    nk_style_from_table(ctx, table);
}





static SDL_Texture *libregpu_nk_sdl2_make_texture(SDL_Renderer *renderer,
                                               const void *pixels,
                                               int width, int height)
{
    SDL_Texture *texture;

    texture = SDL_CreateTexture(renderer,
                                SDL_PIXELFORMAT_RGBA32,
                                SDL_TEXTUREACCESS_STATIC,
                                width, height);
    if (!texture) return NULL;

    if (SDL_UpdateTexture(texture, NULL, pixels,
                          width * (int)sizeof(Uint32)) != 0) {
        SDL_DestroyTexture(texture);
        return NULL;
    }

    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
    return texture;
}






static int libregpu_nk_sdl2_find_font(char *out, size_t capacity)
{
    static const char *const absolute_candidates[] = {
        
        "/System/Library/Fonts/Supplemental/Times New Roman.ttf",
        "/System/Library/Fonts/Times.ttc",
        "/Library/Fonts/Times New Roman.ttf",
        
        "C:\\Windows\\Fonts\\times.ttf",
        "C:\\Windows\\Fonts\\Times New Roman.ttf",
        
        "/usr/share/fonts/truetype/msttcorefonts/Times_New_Roman.ttf",
        "/usr/share/fonts/truetype/msttcorefonts/times.ttf",
        "/usr/share/fonts/TTF/Times_New_Roman.ttf",
        "/usr/local/share/fonts/Times_New_Roman.ttf",
        "/usr/local/share/fonts/times.ttf",
        
        "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf",
        "/usr/share/fonts/liberation/LiberationSerif-Regular.ttf",
        "/usr/share/fonts/liberation-serif/LiberationSerif-Regular.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSerif-Regular.ttf",
        "/usr/share/fonts/truetype/tinos/Tinos-Regular.ttf",
        "/usr/share/fonts/tinos/Tinos-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSerif-Regular.ttf",
        "/usr/share/fonts/noto/NotoSerif-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSerif.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf"
    };
    static const char *const bundled[] = {
        "times.ttf",
        "Times New Roman.ttf",
        "timesnewroman.ttf",
        "fonts/times.ttf",
        "fonts/Times New Roman.ttf"
    };
    static const char *const user_paths[] = {
        ".fonts/times.ttf",
        ".fonts/Times New Roman.ttf",
        ".local/share/fonts/times.ttf",
        ".local/share/fonts/Times New Roman.ttf",
        "Library/Fonts/Times New Roman.ttf"
    };
    const char *override = getenv("LIBREGPU_UI_FONT");
    const char *home = getenv("HOME");
    char *base;
    size_t i;

    if (!out || capacity == 0) return 0;
    out[0] = '\0';

    if (override && override[0] != '\0' && libregpu_path_is_file(override)) {
        (void)snprintf(out, capacity, "%s", override);
        return 1;
    }

    base = SDL_GetBasePath();
    if (base) {
        for (i = 0; i < sizeof(bundled) / sizeof(bundled[0]); ++i) {
            if (libregpu_path_join(out, capacity, base, bundled[i]) &&
                libregpu_path_is_file(out)) {
                SDL_free(base);
                return 1;
            }
        }
        SDL_free(base);
    }

    if (home && home[0] != '\0') {
        for (i = 0; i < sizeof(user_paths) / sizeof(user_paths[0]); ++i) {
            if (libregpu_path_join(out, capacity, home, user_paths[i]) &&
                libregpu_path_is_file(out)) {
                return 1;
            }
        }
    }

    for (i = 0;
         i < sizeof(absolute_candidates) / sizeof(absolute_candidates[0]);
         ++i) {
        if (libregpu_path_is_file(absolute_candidates[i])) {
            (void)snprintf(out, capacity, "%s", absolute_candidates[i]);
            return 1;
        }
    }

    out[0] = '\0';
    return 0;
}

int libregpu_nk_sdl2_init(LibreGPUNkSdl2 *nk, SDL_Window *window, SDL_Renderer *renderer)
{
    static const struct nk_draw_vertex_layout_element layout[] = {
        {NK_VERTEX_POSITION, NK_FORMAT_FLOAT, NK_OFFSETOF(SDL_Vertex, position)},
        {NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8, NK_OFFSETOF(SDL_Vertex, color)},
        {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, NK_OFFSETOF(SDL_Vertex, tex_coord)},
        {NK_VERTEX_LAYOUT_END}
    };
    const void *pixels;
    Uint32 white = 0xFFFFFFFFu;
    char font_path[1024];
    int width = 0;
    int height = 0;

    if (!nk || !window || !renderer) return 0;

    memset(nk, 0, sizeof(*nk));
    nk->window = window;
    nk->renderer = renderer;

    nk_font_atlas_init_default(&nk->atlas);
    nk_font_atlas_begin(&nk->atlas);
    font_path[0] = '\0';
    if (libregpu_nk_sdl2_find_font(font_path, sizeof(font_path))) {
        nk->font = nk_font_atlas_add_from_file(&nk->atlas, font_path,
                                               LIBREGPU_NK_FONT_HEIGHT, NULL);
    }
    if (!nk->font) {
        nk->font = nk_font_atlas_add_default(&nk->atlas,
                                             LIBREGPU_NK_FONT_HEIGHT, NULL);
    }
    if (!nk->font) goto fail;

    pixels = nk_font_atlas_bake(&nk->atlas, &width, &height,
                                NK_FONT_ATLAS_RGBA32);
    if (!pixels || width <= 0 || height <= 0) goto fail;

    nk->font_texture = libregpu_nk_sdl2_make_texture(renderer, pixels,
                                                  width, height);
    if (!nk->font_texture) goto fail;

    nk_font_atlas_end(&nk->atlas, nk_handle_ptr(nk->font_texture), NULL);

    nk->white_texture = libregpu_nk_sdl2_make_texture(renderer, &white, 1, 1);
    if (!nk->white_texture) goto fail;

    if (!nk_init_default(&nk->ctx, &nk->font->handle)) goto fail;

    nk_buffer_init_default(&nk->commands);
    nk_buffer_init_default(&nk->vertices);
    nk_buffer_init_default(&nk->elements);

    libregpu_nk_sdl2_theme(&nk->ctx);
    nk->ctx.style.font = &nk->font->handle;

    nk->ctx.clip.copy = libregpu_nk_sdl2_copy;
    nk->ctx.clip.paste = libregpu_nk_sdl2_paste;
    nk->ctx.clip.userdata = nk_handle_ptr(nk);

    nk->config.global_alpha = 1.0f;
    nk->config.shape_AA = NK_ANTI_ALIASING_ON;
    nk->config.line_AA = NK_ANTI_ALIASING_ON;
    nk->config.circle_segment_count = 22u;
    nk->config.arc_segment_count = 22u;
    nk->config.curve_segment_count = 22u;
    nk->config.tex_null.texture = nk_handle_ptr(nk->white_texture);
    nk->config.tex_null.uv = nk_vec2(0.5f, 0.5f);
    nk->config.vertex_layout = layout;
    nk->config.vertex_size = sizeof(SDL_Vertex);

    nk->ready = 1;
    return 1;

fail:
    libregpu_nk_sdl2_shutdown(nk);
    return 0;
}

void libregpu_nk_sdl2_shutdown(LibreGPUNkSdl2 *nk)
{
    if (!nk) return;

    nk_buffer_free(&nk->elements);
    nk_buffer_free(&nk->vertices);
    nk_buffer_free(&nk->commands);

    if (nk->ready) {
        nk_free(&nk->ctx);
        nk->ready = 0;
    }

    if (nk->white_texture) {
        SDL_DestroyTexture(nk->white_texture);
        nk->white_texture = NULL;
    }
    if (nk->font_texture) {
        SDL_DestroyTexture(nk->font_texture);
        nk->font_texture = NULL;
    }

    nk_font_atlas_clear(&nk->atlas);
    memset(nk, 0, sizeof(*nk));
}





void libregpu_nk_sdl2_input_begin(LibreGPUNkSdl2 *nk)
{
    if (!nk || !nk->ready) return;
    nk_input_begin(&nk->ctx);
}

void libregpu_nk_sdl2_input_end(LibreGPUNkSdl2 *nk)
{
    if (!nk || !nk->ready) return;
    nk_input_end(&nk->ctx);
}

static void libregpu_nk_sdl2_mouse(LibreGPUNkSdl2 *nk, int window_x, int window_y,
                                float *out_x, float *out_y)
{
    int window_w = 1;
    int window_h = 1;
    int output_w = 1;
    int output_h = 1;

    SDL_GetWindowSize(nk->window, &window_w, &window_h);
    SDL_GetRendererOutputSize(nk->renderer, &output_w, &output_h);

    if (window_w <= 0) window_w = 1;
    if (window_h <= 0) window_h = 1;
    if (output_w <= 0) output_w = 1;
    if (output_h <= 0) output_h = 1;

    *out_x = (float)window_x * (float)output_w / (float)window_w;
    *out_y = (float)window_y * (float)output_h / (float)window_h;
}

static enum nk_keys libregpu_nk_sdl2_key(SDL_Keycode key, SDL_Keymod mod,
                                      int *translated)
{
    const int ctrl = (mod & KMOD_CTRL) != 0;

    *translated = 1;
    if (ctrl) {
        switch (key) {
        case SDLK_c: return NK_KEY_COPY;
        case SDLK_x: return NK_KEY_CUT;
        case SDLK_v: return NK_KEY_PASTE;
        case SDLK_a: return NK_KEY_TEXT_SELECT_ALL;
        case SDLK_z: return (mod & KMOD_SHIFT) ? NK_KEY_TEXT_REDO
                                               : NK_KEY_TEXT_UNDO;
        case SDLK_y: return NK_KEY_TEXT_REDO;
        case SDLK_HOME: return NK_KEY_TEXT_START;
        case SDLK_END: return NK_KEY_TEXT_END;
        case SDLK_LEFT: return NK_KEY_TEXT_WORD_LEFT;
        case SDLK_RIGHT: return NK_KEY_TEXT_WORD_RIGHT;
        default: break;
        }
    }

    switch (key) {
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:    return NK_KEY_SHIFT;
    case SDLK_LCTRL:
    case SDLK_RCTRL:     return NK_KEY_CTRL;
    case SDLK_BACKSPACE: return NK_KEY_BACKSPACE;
    case SDLK_DELETE:    return NK_KEY_DEL;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:  return NK_KEY_ENTER;
    case SDLK_TAB:       return NK_KEY_TAB;
    case SDLK_UP:        return NK_KEY_UP;
    case SDLK_DOWN:      return NK_KEY_DOWN;
    case SDLK_LEFT:      return NK_KEY_LEFT;
    case SDLK_RIGHT:     return NK_KEY_RIGHT;
    case SDLK_HOME:      return NK_KEY_TEXT_LINE_START;
    case SDLK_END:       return NK_KEY_TEXT_LINE_END;
    case SDLK_PAGEUP:    return NK_KEY_SCROLL_UP;
    case SDLK_PAGEDOWN:  return NK_KEY_SCROLL_DOWN;
    default:
        *translated = 0;
        return NK_KEY_NONE;
    }
}

static void libregpu_nk_sdl2_text(LibreGPUNkSdl2 *nk, const char *text)
{
    const unsigned char *p = (const unsigned char *)text;

    if (!text) return;
    while (*p) {
        nk_rune rune = 0;
        int length = 1;
        int i;

        if (*p < 0x80u) {
            rune = (nk_rune)*p;
            length = 1;
        } else if ((*p & 0xE0u) == 0xC0u) {
            rune = (nk_rune)(*p & 0x1Fu);
            length = 2;
        } else if ((*p & 0xF0u) == 0xE0u) {
            rune = (nk_rune)(*p & 0x0Fu);
            length = 3;
        } else if ((*p & 0xF8u) == 0xF0u) {
            rune = (nk_rune)(*p & 0x07u);
            length = 4;
        } else {
            ++p; 
            continue;
        }

        for (i = 1; i < length; ++i) {
            if ((p[i] & 0xC0u) != 0x80u) {
                length = 0;
                break;
            }
            rune = (rune << 6) | (nk_rune)(p[i] & 0x3Fu);
        }
        if (length == 0) {
            ++p;
            continue;
        }

        nk_input_unicode(&nk->ctx, rune);
        p += length;
    }
}

void libregpu_nk_sdl2_handle_event(LibreGPUNkSdl2 *nk, const SDL_Event *event)
{
    float x;
    float y;

    if (!nk || !nk->ready || !event) return;

    switch (event->type) {
    case SDL_MOUSEMOTION:
        libregpu_nk_sdl2_mouse(nk, event->motion.x, event->motion.y, &x, &y);
        nk_input_motion(&nk->ctx, (int)x, (int)y);
        break;

    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
        enum nk_buttons button;
        int down = (event->type == SDL_MOUSEBUTTONDOWN);
        switch (event->button.button) {
        case SDL_BUTTON_LEFT:   button = NK_BUTTON_LEFT; break;
        case SDL_BUTTON_MIDDLE: button = NK_BUTTON_MIDDLE; break;
        case SDL_BUTTON_RIGHT:  button = NK_BUTTON_RIGHT; break;
        default: return;
        }
        libregpu_nk_sdl2_mouse(nk, event->button.x, event->button.y, &x, &y);
        nk_input_button(&nk->ctx, button, (int)x, (int)y, down);
        break;
    }

    case SDL_MOUSEWHEEL:
        nk_input_scroll(&nk->ctx,
                        nk_vec2((float)event->wheel.x,
                                (float)event->wheel.y));
        break;

    case SDL_TEXTINPUT:
        libregpu_nk_sdl2_text(nk, event->text.text);
        break;

    case SDL_KEYDOWN:
    case SDL_KEYUP: {
        enum nk_keys key;
        int translated = 0;
        int down = (event->type == SDL_KEYDOWN);

        key = libregpu_nk_sdl2_key(event->key.keysym.sym,
                                SDL_GetModState(), &translated);
        if (!translated) return;

        if (down && event->key.repeat) {
            

            nk_input_key(&nk->ctx, key, 0);
            nk_input_key(&nk->ctx, key, 1);
        } else {
            nk_input_key(&nk->ctx, key, down);
        }
        break;
    }

    default:
        break;
    }
}





static SDL_Texture *libregpu_nk_sdl2_texture(LibreGPUNkSdl2 *nk, nk_handle handle)
{
    if (handle.ptr) return (SDL_Texture *)handle.ptr;
    return nk->white_texture;
}

void libregpu_nk_sdl2_render(LibreGPUNkSdl2 *nk)
{
    const struct nk_draw_command *command;
    const SDL_Vertex *vertices;
    const int *indices;
    int vertex_count;
    nk_size index_offset = 0;

    if (!nk || !nk->ready) return;

    if (nk_convert(&nk->ctx, &nk->commands, &nk->vertices, &nk->elements,
                   &nk->config) != NK_CONVERT_SUCCESS) {
        nk_clear(&nk->ctx);
        return;
    }

    vertices = (const SDL_Vertex *)nk_buffer_memory_const(&nk->vertices);
    indices = (const int *)nk_buffer_memory_const(&nk->elements);
    vertex_count = (int)(nk->vertices.allocated / sizeof(SDL_Vertex));

    nk_draw_foreach(command, &nk->ctx, &nk->commands) {
        SDL_Rect clip;
        SDL_Texture *texture;

        if (!command->elem_count) continue;

        clip.x = (int)floorf(command->clip_rect.x);
        clip.y = (int)floorf(command->clip_rect.y);
        clip.w = (int)ceilf(command->clip_rect.x + command->clip_rect.w) - clip.x;
        clip.h = (int)ceilf(command->clip_rect.y + command->clip_rect.h) - clip.y;
        if (clip.w <= 0 || clip.h <= 0) continue;

        SDL_RenderSetClipRect(nk->renderer, &clip);
        texture = libregpu_nk_sdl2_texture(nk, command->texture);
        SDL_RenderGeometry(nk->renderer, texture, vertices, vertex_count,
                           indices + index_offset,
                           (int)command->elem_count);
        index_offset += (nk_size)command->elem_count;
    }
    SDL_RenderSetClipRect(nk->renderer, NULL);

    nk_buffer_clear(&nk->commands);
    nk_buffer_clear(&nk->vertices);
    nk_buffer_clear(&nk->elements);
    nk_clear(&nk->ctx);
}





#if defined(_WIN32)
#  define LIBREGPU_PATH_SEP '\\'
#else
#  define LIBREGPU_PATH_SEP '/'
#endif

static int libregpu_path_is_sep(char c)
{
    return c == '/' || c == '\\';
}

const char *libregpu_path_basename(const char *path)
{
    const char *base;
    const char *p;

    if (!path) return "";
    base = path;
    for (p = path; *p; ++p) {
        if (libregpu_path_is_sep(*p) && p[1] != '\0')
            base = p + 1;
    }
    return base;
}

int libregpu_path_join(char *out, size_t capacity,
                    const char *directory, const char *name)
{
    size_t length;
    int need_separator;

    if (!out || capacity == 0) return 0;
    out[0] = '\0';
    if (!directory) directory = "";
    if (!name) name = "";

    length = strlen(directory);
    need_separator = (length > 0 && !libregpu_path_is_sep(directory[length - 1]));

    if ((size_t)snprintf(out, capacity, "%s%s%s",
                         directory,
                         need_separator ? (char[2]){LIBREGPU_PATH_SEP, '\0'} : "",
                         name) >= capacity) {
        out[0] = '\0';
        return 0;
    }
    return 1;
}

int libregpu_path_parent(const char *path, char *out, size_t capacity)
{
    size_t length;
    size_t cut;

    if (!path || !out || capacity == 0) return 0;
    out[0] = '\0';

    length = strlen(path);
    while (length > 1 && libregpu_path_is_sep(path[length - 1]))
        --length;

    cut = length;
    while (cut > 0 && !libregpu_path_is_sep(path[cut - 1]))
        --cut;

    if (cut == 0) return 0;
    while (cut > 1 && libregpu_path_is_sep(path[cut - 1]))
        --cut;

    if (cut >= capacity) return 0;
    memcpy(out, path, cut);
    out[cut] = '\0';
    return 1;
}

int libregpu_path_is_file(const char *path)
{
#if defined(_WIN32)
    DWORD attributes;
    if (!path || path[0] == '\0') return 0;
    attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) return 0;
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    struct stat info;
    if (!path || path[0] == '\0') return 0;
    if (stat(path, &info) != 0) return 0;
    return S_ISREG(info.st_mode) ? 1 : 0;
#endif
}

int libregpu_path_is_executable_file(const char *path)
{
    if (!libregpu_path_is_file(path)) return 0;
#if defined(_WIN32)
    return 1;
#else
    return access(path, X_OK) == 0 ? 1 : 0;
#endif
}

int libregpu_path_is_directory(const char *path)
{
#if defined(_WIN32)
    DWORD attributes;
    if (!path || path[0] == '\0') return 0;
    attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) return 0;
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat info;
    if (!path || path[0] == '\0') return 0;
    if (stat(path, &info) != 0) return 0;
    return S_ISDIR(info.st_mode) ? 1 : 0;
#endif
}

int libregpu_path_replace_file(const char *temporary, const char *final_path)
{
    if (!temporary || !final_path) return 0;
#if defined(_WIN32)
    if (MoveFileExA(temporary, final_path,
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return 1;
    if (DeleteFileA(final_path) || GetLastError() == ERROR_FILE_NOT_FOUND)
        return MoveFileA(temporary, final_path) ? 1 : 0;
    return 0;
#else
    return rename(temporary, final_path) == 0 ? 1 : 0;
#endif
}

static int libregpu_path_entry_compare(const void *left, const void *right)
{
    const LibreGPUDirEntry *a = (const LibreGPUDirEntry *)left;
    const LibreGPUDirEntry *b = (const LibreGPUDirEntry *)right;
    const unsigned char *pa;
    const unsigned char *pb;

    if (a->is_directory != b->is_directory)
        return a->is_directory ? -1 : 1;

    pa = (const unsigned char *)a->name;
    pb = (const unsigned char *)b->name;
    while (*pa && *pb) {
        int ca = tolower(*pa);
        int cb = tolower(*pb);
        if (ca != cb) return ca < cb ? -1 : 1;
        ++pa;
        ++pb;
    }
    if (*pa == *pb) return 0;
    return *pa ? 1 : -1;
}

static int libregpu_path_push_entry(LibreGPUDirEntry *entries, size_t capacity,
                                 size_t *count, const char *name,
                                 int is_directory)
{
    if (!entries || !count || *count >= capacity) return 0;
    if (!name || name[0] == '\0') return 0;
    if (strlen(name) >= sizeof(entries[*count].name)) return 0;

    (void)snprintf(entries[*count].name, sizeof(entries[*count].name),
                   "%s", name);
    entries[*count].is_directory = is_directory;
    ++*count;
    return 1;
}

int libregpu_path_list_directory(const char *directory,
                              LibreGPUDirEntry *entries, size_t capacity,
                              size_t *out_count)
{
    size_t count = 0;

    if (out_count) *out_count = 0;
    if (!directory || !entries || capacity == 0) return 0;
#if defined(_WIN32)
    {
        char pattern[LIBREGPU_PATH_NAME_MAX + 4];
        WIN32_FIND_DATAA data;
        HANDLE handle;

        if (!libregpu_path_join(pattern, sizeof(pattern), directory, "*"))
            return 0;
        handle = FindFirstFileA(pattern, &data);
        if (handle == INVALID_HANDLE_VALUE) return 0;

        do {
            const int is_directory =
                (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (strcmp(data.cFileName, ".") == 0 ||
                strcmp(data.cFileName, "..") == 0)
                continue;
            (void)libregpu_path_push_entry(entries, capacity, &count,
                                        data.cFileName, is_directory);
        } while (FindNextFileA(handle, &data));

        FindClose(handle);
    }
#else
    {
        DIR *dir;
        struct dirent *entry;

        dir = opendir(directory);
        if (!dir) return 0;

        while ((entry = readdir(dir)) != NULL) {
            char full[LIBREGPU_PATH_NAME_MAX * 2];
            struct stat info;
            int is_directory = 0;

            if (strcmp(entry->d_name, ".") == 0 ||
                strcmp(entry->d_name, "..") == 0)
                continue;

            if (libregpu_path_join(full, sizeof(full), directory,
                                entry->d_name)) {
                if (stat(full, &info) == 0)
                    is_directory = S_ISDIR(info.st_mode) ? 1 : 0;
            }
            (void)libregpu_path_push_entry(entries, capacity, &count,
                                        entry->d_name, is_directory);
        }
        closedir(dir);
    }
#endif

    if (count > 1)
        qsort(entries, count, sizeof(*entries), libregpu_path_entry_compare);
    if (out_count) *out_count = count;
    return 1;
}

int libregpu_path_initial_directory(char *out, size_t capacity)
{
    const char *home = getenv("HOME");

    if (!out || capacity == 0) return 0;
    out[0] = '\0';

#if defined(_WIN32)
    if (!home) home = getenv("USERPROFILE");
#else
    if (!home) home = getenv("HOME");
#endif

    if (home && home[0] != '\0' && libregpu_path_is_directory(home)) {
        if (strlen(home) >= capacity) return 0;
        (void)snprintf(out, capacity, "%s", home);
        return 1;
    }

#if defined(_WIN32)
    {
        DWORD length = GetCurrentDirectoryA((DWORD)capacity, out);
        if (length == 0 || length >= (DWORD)capacity) {
            out[0] = '\0';
            return 0;
        }
        return 1;
    }
#else
    return getcwd(out, capacity) != NULL ? 1 : 0;
#endif
}





#define LIBREGPU_LAUNCH_PATH_MAX 4096

int libregpu_launch_detached(const char *path,
                          char *error_text, size_t error_capacity)
{
    if (error_text && error_capacity > 0) error_text[0] = '\0';
    if (!path || path[0] == '\0') {
        if (error_text && error_capacity > 0)
            (void)snprintf(error_text, error_capacity,
                           "%s", "No executable path was selected");
        return 0;
    }

#if defined(_WIN32)
    {
        char command_line[LIBREGPU_LAUNCH_PATH_MAX + 3];
        STARTUPINFOA startup;
        PROCESS_INFORMATION process;
        BOOL started;

        if ((size_t)snprintf(command_line, sizeof(command_line), "\"%s\"",
                             path) >= sizeof(command_line)) {
            if (error_text && error_capacity > 0)
                (void)snprintf(error_text, error_capacity,
                               "%s", "The path is too long to launch");
            return 0;
        }

        memset(&startup, 0, sizeof(startup));
        memset(&process, 0, sizeof(process));
        startup.cb = sizeof(startup);

        started = CreateProcessA(path, command_line,
                                 NULL, NULL, FALSE,
                                 DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
                                 NULL, NULL, &startup, &process);
        if (!started) {
            if (error_text && error_capacity > 0)
                (void)snprintf(error_text, error_capacity,
                               "Could not start the game (Windows error %lu)",
                               (unsigned long)GetLastError());
            return 0;
        }

        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 1;
    }
#else
    {
        posix_spawn_file_actions_t actions;
        posix_spawnattr_t attributes;
        char *argv[2];
        int result = 0;
        int have_actions = 0;
        int have_attributes = 0;

        argv[0] = (char *)path;
        argv[1] = NULL;

        result = posix_spawn_file_actions_init(&actions);
        if (result == 0) have_actions = 1;

        if (result == 0) {
            result = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO,
                                                      "/dev/null",
                                                      O_RDONLY, 0);
        }

        if (result == 0) {
            result = posix_spawnattr_init(&attributes);
            if (result == 0) have_attributes = 1;
        }

        if (result == 0) {
            short flags = 0;
#ifdef POSIX_SPAWN_SETSID
            flags |= POSIX_SPAWN_SETSID;
#endif
#ifdef POSIX_SPAWN_SETPGROUP
            if ((flags & POSIX_SPAWN_SETSID) == 0)
                flags |= POSIX_SPAWN_SETPGROUP;
#endif
            if (flags != 0)
                result = posix_spawnattr_setflags(&attributes, flags);
        }

        if (result == 0) {
            pid_t pid = 0;
            result = posix_spawn(&pid, path, &actions, &attributes,
                                 argv, environ);
        }

        if (have_attributes) (void)posix_spawnattr_destroy(&attributes);
        if (have_actions) (void)posix_spawn_file_actions_destroy(&actions);

        if (result != 0) {
            if (error_text && error_capacity > 0)
                (void)snprintf(error_text, error_capacity,
                               "Could not start the game: %s",
                               strerror(result));
            return 0;
        }
        return 1;
    }
#endif
}





#define LIBREGPU_PROFILE_HEADER "libregpu-profiles"
#define LIBREGPU_PROFILE_VERSION "1"
#define LIBREGPU_PROFILE_LINE_MAX 4096

static void libregpu_profiles_error(char *text, size_t capacity, const char *message)
{
    if (!text || capacity == 0) return;
    (void)snprintf(text, capacity, "%s", message ? message : "");
}

static int libregpu_profiles_escape(char *out, size_t capacity, const char *input)
{
    size_t used = 0;

    if (!out || capacity == 0) return 0;
    if (!input) input = "";

    while (*input) {
        const char *replacement = NULL;
        char literal[2];
        const char *piece;
        size_t length;

        switch (*input) {
        case '\\': replacement = "\\\\"; break;
        case '\t': replacement = "\\t"; break;
        case '\n': replacement = "\\n"; break;
        case '\r': replacement = "\\r"; break;
        default:
            literal[0] = *input;
            literal[1] = '\0';
            replacement = literal;
            break;
        }

        piece = replacement;
        length = strlen(piece);
        if (used + length >= capacity) return 0;
        memcpy(out + used, piece, length);
        used += length;
        ++input;
    }

    out[used] = '\0';
    return 1;
}

static int libregpu_profiles_unescape(char *out, size_t capacity, const char *input)
{
    size_t used = 0;

    if (!out || capacity == 0) return 0;
    if (!input) input = "";

    while (*input) {
        char value = *input;

        if (value == '\\' && input[1] != '\0') {
            ++input;
            switch (*input) {
            case 't': value = '\t'; break;
            case 'n': value = '\n'; break;
            case 'r': value = '\r'; break;
            case '\\': value = '\\'; break;
            default: value = *input; break;
            }
        }

        if (used + 1 >= capacity) return 0;
        out[used++] = value;
        ++input;
    }

    out[used] = '\0';
    return 1;
}

static int libregpu_profiles_split(char *line, char *fields[], int max_fields)
{
    int count = 0;
    char *cursor = line;

    if (max_fields <= 0) return 0;
    fields[count++] = cursor;

    while (*cursor && count < max_fields) {
        if (*cursor == '\t') {
            *cursor = '\0';
            fields[count++] = cursor + 1;
        }
        ++cursor;
    }
    return count;
}

static void libregpu_profiles_trim_line(char *line)
{
    size_t length;

    if (!line) return;
    length = strlen(line);
    while (length > 0 &&
           (line[length - 1] == '\n' || line[length - 1] == '\r')) {
        line[--length] = '\0';
    }
}

static int libregpu_profiles_parse_id(const char *text, uint32_t *out)
{
    char *end = NULL;
    unsigned long value;

    if (!text || !out || text[0] == '\0' || text[0] == '-') return 0;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0)
        return 0;
    if (value > 0xFFFFFFFFul) return 0;
    *out = (uint32_t)value;
    return 1;
}

static int libregpu_profiles_parse_fps(const char *text, int *out)
{
    char *end = NULL;
    long value;

    if (!text || !out || text[0] == '\0') return 0;
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0')
        return 0;
    if (value < 0 || value > LIBREGPU_PROFILE_FPS_MAX) return 0;
    *out = (int)value;
    return 1;
}

static int libregpu_profiles_load(LibreGPUProfileStore *store)
{
    FILE *file;
    char line[LIBREGPU_PROFILE_LINE_MAX];

    store->count = 0;
    store->next_id = 1;

    file = fopen(store->file_path, "rb");
    if (!file) {
        
        return (errno == ENOENT) ? 1 : 0;
    }

    if (!fgets(line, sizeof(line), file)) {
        fclose(file);
        return 1;
    }
    libregpu_profiles_trim_line(line);
    {
        char *fields[3];
        const int count = libregpu_profiles_split(line, fields, 3);
        if (count != 2 || strcmp(fields[0], LIBREGPU_PROFILE_HEADER) != 0 ||
            strcmp(fields[1], LIBREGPU_PROFILE_VERSION) != 0) {
            fclose(file);
            return 0;
        }
    }

    while (fgets(line, sizeof(line), file)) {
        char *fields[6];
        int count;
        LibreGPUStoredProfile profile;
        int fps = 0;
        uint32_t id = 0;
        size_t i;
        int duplicate = 0;

        libregpu_profiles_trim_line(line);
        if (line[0] == '\0') continue;

        count = libregpu_profiles_split(line, fields, 6);
        if (count != 5 || strcmp(fields[0], "profile") != 0) continue;
        if (!libregpu_profiles_parse_id(fields[1], &id)) continue;
        if (!libregpu_profiles_parse_fps(fields[2], &fps)) continue;
        if (store->count >= LIBREGPU_PROFILE_MAX) break;

        memset(&profile, 0, sizeof(profile));
        profile.id = id;
        profile.fps = fps;
        if (!libregpu_profiles_unescape(profile.name, sizeof(profile.name),
                                     fields[3]))
            continue;
        if (!libregpu_profiles_unescape(profile.path, sizeof(profile.path),
                                     fields[4]))
            continue;
        if (profile.path[0] == '\0') continue;

        for (i = 0; i < store->count; ++i) {
            if (store->items[i].id == id) {
                duplicate = 1;
                break;
            }
        }
        if (duplicate) continue;

        store->items[store->count++] = profile;
        if (id >= store->next_id) {
            store->next_id = id + 1;
            if (store->next_id == 0) store->next_id = 1;
        }
    }

    for (;;) {
        size_t k;
        int taken = 0;
        for (k = 0; k < store->count; ++k) {
            if (store->items[k].id == store->next_id) {
                taken = 1;
                break;
            }
        }
        if (!taken) break;
        ++store->next_id;
        if (store->next_id == 0) store->next_id = 1;
    }

    fclose(file);
    return 1;
}

static int libregpu_profiles_save(const LibreGPUProfileStore *store)
{
    char temporary[LIBREGPU_PROFILE_PATH_MAX + 8];
    FILE *file;
    size_t i;

    if ((size_t)snprintf(temporary, sizeof(temporary), "%s.tmp",
                         store->file_path) >= sizeof(temporary))
        return 0;

    file = fopen(temporary, "wb");
    if (!file) return 0;

    if (fprintf(file, "%s\t%s\n", LIBREGPU_PROFILE_HEADER,
                LIBREGPU_PROFILE_VERSION) < 0) {
        fclose(file);
        (void)remove(temporary);
        return 0;
    }

    for (i = 0; i < store->count; ++i) {
        char name[LIBREGPU_PROFILE_NAME_MAX * 2 + 2];
        char path[LIBREGPU_PROFILE_PATH_MAX * 2 + 2];
        const LibreGPUStoredProfile *profile = &store->items[i];

        if (!libregpu_profiles_escape(name, sizeof(name), profile->name) ||
            !libregpu_profiles_escape(path, sizeof(path), profile->path)) {
            fclose(file);
            (void)remove(temporary);
            return 0;
        }

        if (fprintf(file, "profile\t%lu\t%d\t%s\t%s\n",
                    (unsigned long)profile->id, profile->fps,
                    name, path) < 0) {
            fclose(file);
            (void)remove(temporary);
            return 0;
        }
    }

    if (fflush(file) != 0) {
        fclose(file);
        (void)remove(temporary);
        return 0;
    }
    if (fclose(file) != 0) {
        (void)remove(temporary);
        return 0;
    }

    if (!libregpu_path_replace_file(temporary, store->file_path)) {
        (void)remove(temporary);
        return 0;
    }
    return 1;
}

int libregpu_profiles_init(LibreGPUProfileStore *store, const char *directory)
{
    if (!store || !directory || directory[0] == '\0') return 0;
    if (strlen(directory) >= sizeof(store->directory)) return 0;

    memset(store, 0, sizeof(*store));
    (void)snprintf(store->directory, sizeof(store->directory),
                   "%s", directory);

    if (!libregpu_path_join(store->file_path, sizeof(store->file_path),
                         directory, "profiles.txt"))
        return 0;

    if (!libregpu_profiles_load(store)) return 0;
    store->loaded = 1;
    return 1;
}

size_t libregpu_profiles_count(const LibreGPUProfileStore *store)
{
    return store ? store->count : 0;
}

const LibreGPUStoredProfile *libregpu_profiles_at(const LibreGPUProfileStore *store,
                                            size_t index)
{
    if (!store || index >= store->count) return NULL;
    return &store->items[index];
}

int libregpu_profiles_add(LibreGPUProfileStore *store,
                       const char *path, int fps,
                       LibreGPUStoredProfile *out_profile,
                       char *error_text, size_t error_capacity)
{
    LibreGPUStoredProfile profile;
    const char *base;

    if (error_text && error_capacity > 0) error_text[0] = '\0';
    if (!store || !store->loaded || !path || path[0] == '\0') {
        libregpu_profiles_error(error_text, error_capacity, "Invalid profile path");
        return 0;
    }
    if (strlen(path) >= sizeof(profile.path)) {
        libregpu_profiles_error(error_text, error_capacity,
                             "Path is too long to store");
        return 0;
    }
    if (fps < 0 || fps > LIBREGPU_PROFILE_FPS_MAX) {
        libregpu_profiles_error(error_text, error_capacity, "Invalid FPS value");
        return 0;
    }
    if (store->count >= LIBREGPU_PROFILE_MAX) {
        libregpu_profiles_error(error_text, error_capacity,
                             "Profile capacity reached");
        return 0;
    }

    memset(&profile, 0, sizeof(profile));
    profile.id = store->next_id;
    profile.fps = fps;
    (void)snprintf(profile.path, sizeof(profile.path), "%s", path);

    base = libregpu_path_basename(path);
    if (!base || base[0] == '\0') base = "Game";
    (void)snprintf(profile.name, sizeof(profile.name), "%s", base);

    store->items[store->count++] = profile;
    if (!libregpu_profiles_save(store)) {
        --store->count;
        libregpu_profiles_error(error_text, error_capacity,
                             "Could not write the profile file");
        return 0;
    }

    ++store->next_id;
    if (store->next_id == 0) store->next_id = 1;

    if (out_profile) *out_profile = profile;
    return 1;
}

int libregpu_profiles_delete(LibreGPUProfileStore *store, uint32_t id)
{
    size_t index;
    size_t i;
    LibreGPUStoredProfile removed;

    if (!store || !store->loaded || id == 0) return 0;

    for (index = 0; index < store->count; ++index) {
        if (store->items[index].id == id) break;
    }
    if (index >= store->count) return 0;

    removed = store->items[index];
    for (i = index; i + 1 < store->count; ++i)
        store->items[i] = store->items[i + 1];
    --store->count;

    if (!libregpu_profiles_save(store)) {
        for (i = store->count; i > index; --i)
            store->items[i] = store->items[i - 1];
        store->items[index] = removed;
        ++store->count;
        return 0;
    }
    return 1;
}

const char *libregpu_profiles_file_path(const LibreGPUProfileStore *store)
{
    return store ? store->file_path : "";
}







typedef struct LibreGPUPoint {
    float x;
    float y;
} LibreGPUPoint;

static const LibreGPUPoint libregpu_orbit_offsets[7] = {
    {123.0f, -163.0f},
    {164.0f, -120.0f},
    {197.0f,  -69.0f},
    {215.0f,   -9.0f},
    {205.0f,   52.0f},
    {168.0f,  114.0f},
    {113.0f,  155.0f}
};





static float libregpu_clampf(float value, float low, float high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static float libregpu_smoothstep(float t)
{
    t = libregpu_clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static size_t libregpu_page_count(const LibreGPUUI *ui)
{
    if (!ui || ui->profile_count == 0) return 0;
    return (ui->profile_count + (LIBREGPU_PROFILE_PAGE_SIZE - 1)) /
           LIBREGPU_PROFILE_PAGE_SIZE;
}

static struct nk_rect libregpu_circle_rect(float cx, float cy, float radius)
{
    return nk_rect(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
}

static int libregpu_point_in_circle(float px, float py,
                                 float cx, float cy, float radius)
{
    const float dx = px - cx;
    const float dy = py - cy;
    return (dx * dx + dy * dy) <= (radius * radius);
}



static int libregpu_mouse_clicked_circle(const struct nk_context *ctx,
                                      enum nk_buttons button,
                                      float cx, float cy, float radius)
{
    const struct nk_rect broad = libregpu_circle_rect(cx, cy, radius);
    const struct nk_vec2 p = ctx->input.mouse.buttons[button].clicked_pos;

    if (!nk_input_is_mouse_click_in_rect(&ctx->input, button, broad))
        return 0;

    return libregpu_point_in_circle(p.x, p.y, cx, cy, radius);
}

static int libregpu_mouse_clicked_rect(const struct nk_context *ctx,
                                    enum nk_buttons button,
                                    struct nk_rect rect)
{
    return nk_input_is_mouse_click_in_rect(&ctx->input, button, rect) ? 1 : 0;
}

static int libregpu_profile_index_by_id(const LibreGPUUI *ui, uint32_t id)
{
    size_t i;
    for (i = 0; i < ui->profile_count; ++i) {
        if (ui->profiles[i].id == id) return (int)i;
    }
    return -1;
}

static void libregpu_remove_profile_at(LibreGPUUI *ui, size_t index)
{
    size_t i;
    if (!ui || index >= ui->profile_count) return;

    for (i = index; i + 1 < ui->profile_count; ++i)
        ui->profiles[i] = ui->profiles[i + 1];

    --ui->profile_count;

    if (ui->profile_count == 0) {
        ui->current_page = 0;
        ui->target_page = 0;
    } else {
        const size_t pages = libregpu_page_count(ui);
        if (ui->current_page >= pages) ui->current_page = pages - 1;
        if (ui->target_page >= pages) ui->target_page = pages - 1;
    }
}

static int libregpu_parse_integer(const char *text, int *out_value)
{
    char *end = NULL;
    long value;
    const char *start = text;

    if (!text || !out_value) return 0;
    while (*start && isspace((unsigned char)*start)) ++start;
    if (*start == '\0') return 0;

    errno = 0;
    value = strtol(start, &end, 10);
    if (end == start || errno == ERANGE) return 0;

    while (*end && isspace((unsigned char)*end)) ++end;
    if (*end != '\0') return 0;
    if (value < INT_MIN || value > INT_MAX) return 0;

    *out_value = (int)value;
    return 1;
}

static int libregpu_default_path_is_file(const char *path)
{
    struct stat info;
    FILE *file;

    if (!path || path[0] == '\0') return 0;
    if (stat(path, &info) != 0) return 0;

    
#if defined(S_ISREG)
    if (!S_ISREG(info.st_mode)) return 0;
#elif defined(_S_IFMT) && defined(_S_IFREG)
    if ((info.st_mode & _S_IFMT) != _S_IFREG) return 0;
#endif

    
    file = fopen(path, "rb");
    if (!file) return 0;
    fclose(file);
    return 1;
}

static int libregpu_ui_path_is_file(LibreGPUUI *ui, const char *path)
{
    if (!path || path[0] == '\0') return 0;
    if (ui->hooks.path_is_file)
        return ui->hooks.path_is_file(ui->hooks.userdata, path) != 0;
    return libregpu_default_path_is_file(path);
}

static void libregpu_clear_config_feedback(LibreGPUUI *ui)
{
    ui->path_invalid = 0;
    ui->fps_invalid = 0;
    ui->backend_error[0] = '\0';
}

static void libregpu_open_config(LibreGPUUI *ui)
{
    ui->config_open = 1;
    ui->path_text[0] = '\0';
    ui->fps_text[0] = '\0';
    libregpu_clear_config_feedback(ui);
}

static void libregpu_close_config(LibreGPUUI *ui)
{
    ui->config_open = 0;
    ui->path_text[0] = '\0';
    ui->fps_text[0] = '\0';
    libregpu_clear_config_feedback(ui);
}





void libregpu_ui_init(LibreGPUUI *ui,
                   LibreGPULauncherSettings *settings,
                   const LibreGPUUIHooks *hooks,
                   struct nk_image logo,
                   int has_logo)
{
    if (!ui) return;
    memset(ui, 0, sizeof(*ui));
    if (hooks) ui->hooks = *hooks;
    if (settings) {
        ui->settings = settings;
    } else {
        libregpu_config_defaults(&ui->fallback_settings.config);
        ui->settings = &ui->fallback_settings;
    }
    ui->logo = logo;
    ui->has_logo = has_logo ? 1 : 0;
}

void libregpu_ui_set_logo(LibreGPUUI *ui, struct nk_image logo, int has_logo)
{
    if (!ui) return;
    ui->logo = logo;
    ui->has_logo = has_logo ? 1 : 0;
}

void libregpu_ui_set_profiles(LibreGPUUI *ui,
                           const LibreGPUUIProfile *profiles,
                           size_t profile_count)
{
    size_t count;
    if (!ui) return;

    count = profile_count;
    if (count > LIBREGPU_UI_MAX_PROFILES) count = LIBREGPU_UI_MAX_PROFILES;

    if (profiles && count > 0) {
        size_t i;
        size_t accepted = 0;
        for (i = 0; i < count; ++i) {
            size_t j;
            int duplicate_id = 0;
            LibreGPUUIProfile candidate = profiles[i];

            candidate.game_path[LIBREGPU_UI_PATH_MAX - 1] = '\0';
            candidate.name[sizeof candidate.name - 1] = '\0';

            
            for (j = 0; j < accepted; ++j) {
                if (ui->profiles[j].id == candidate.id) {
                    duplicate_id = 1;
                    break;
                }
            }
            if (duplicate_id) continue;
            ui->profiles[accepted++] = candidate;
        }
        ui->profile_count = accepted;
    } else {
        ui->profile_count = 0;
    }

    ui->page_animating = 0;
    ui->page_elapsed = 0.0f;
    ui->context_open = 0;

    if (ui->profile_count == 0) {
        ui->current_page = 0;
        ui->target_page = 0;
    } else {
        const size_t pages = libregpu_page_count(ui);
        if (ui->current_page >= pages) ui->current_page = pages - 1;
        ui->target_page = ui->current_page;
    }
}

int libregpu_ui_quit_requested(const LibreGPUUI *ui)
{
    return ui ? ui->quit_requested : 0;
}





static void libregpu_start_logo_response(LibreGPUUI *ui)
{
    const size_t pages = libregpu_page_count(ui);

    ui->logo_shaking = 1;
    ui->shake_elapsed = 0.0f;

    ui->ripple_active = 1;
    ui->ripple_elapsed = 0.0f;

    if (!ui->page_animating && pages > 1) {
        ui->target_page = (ui->current_page + 1) % pages;
        ui->page_animating = 1;
        ui->page_elapsed = 0.0f;
    }
}

static void libregpu_update_animations(LibreGPUUI *ui, float dt)
{
    if (!isfinite(dt) || dt < 0.0f) dt = 0.0f;
    if (dt > 0.10f) dt = 0.10f; 

    if (ui->page_animating) {
        ui->page_elapsed += dt;
        if (ui->page_elapsed >= LIBREGPU_PAGE_SCROLL_TIME) {
            ui->current_page = ui->target_page;
            ui->page_animating = 0;
            ui->page_elapsed = 0.0f;
        }
    }

    if (ui->logo_shaking) {
        ui->shake_elapsed += dt;
        if (ui->shake_elapsed >= LIBREGPU_LOGO_SHAKE_TIME) {
            ui->logo_shaking = 0;
            ui->shake_elapsed = 0.0f;
        }
    }

    if (ui->ripple_active) {
        ui->ripple_elapsed += dt;
        if (ui->ripple_elapsed >=
            (LIBREGPU_RIPPLE_LIFE + LIBREGPU_RIPPLE_DELAY * (LIBREGPU_RIPPLE_COUNT - 1))) {
            ui->ripple_active = 0;
            ui->ripple_elapsed = 0.0f;
        }
    }
}

static float libregpu_logo_shake_offset_x(const LibreGPUUI *ui, float scale)
{
    float t;
    float envelope;
    if (!ui->logo_shaking) return 0.0f;

    t = libregpu_clampf(ui->shake_elapsed / LIBREGPU_LOGO_SHAKE_TIME, 0.0f, 1.0f);
    envelope = 1.0f - t;
    return sinf(t * 9.0f * LIBREGPU_PI) *
           (LIBREGPU_LOGO_SHAKE_PX * scale * envelope);
}

static float libregpu_logo_shake_offset_y(const LibreGPUUI *ui, float scale)
{
    float t;
    float envelope;
    if (!ui->logo_shaking) return 0.0f;

    t = libregpu_clampf(ui->shake_elapsed / LIBREGPU_LOGO_SHAKE_TIME, 0.0f, 1.0f);
    envelope = 1.0f - t;
    return cosf(t * 7.0f * LIBREGPU_PI) *
           (0.65f * LIBREGPU_LOGO_SHAKE_PX * scale * envelope);
}





static void libregpu_draw_image(struct nk_command_buffer *canvas,
                             const struct nk_image *image,
                             int has_image,
                             float cx, float cy, float radius,
                             struct nk_color tint)
{
    if (!canvas || !image || !has_image) return;
    nk_draw_image(canvas, libregpu_circle_rect(cx, cy, radius), image, tint);
}



static void libregpu_stroke_circle_minimal(struct nk_command_buffer *canvas,
                                        float cx, float cy, float radius,
                                        float scale, int hovered)
{
    nk_stroke_circle(canvas, libregpu_circle_rect(cx, cy, radius),
                     (hovered ? 1.6f : 1.0f) * scale,
                     nk_rgba(235, 238, 245, hovered ? 170 : 80));
}

static void libregpu_draw_plus(struct nk_command_buffer *canvas,
                            float cx, float cy, float radius,
                            float scale, int hovered)
{
    const float arm = 7.0f * scale;
    const float thick = (hovered ? 2.8f : 2.2f) * scale;
    const struct nk_color ink = nk_rgba(22, 24, 28, 245);

    libregpu_stroke_circle_minimal(canvas, cx, cy, radius, scale, hovered);
    nk_stroke_line(canvas, cx - arm, cy, cx + arm, cy, thick, ink);
    nk_stroke_line(canvas, cx, cy - arm, cx, cy + arm, thick, ink);
}

static void libregpu_draw_close(struct nk_command_buffer *canvas,
                             float cx, float cy, float radius,
                             float scale, int hovered)
{
    const float arm = 7.0f * scale;
    const float thick = (hovered ? 2.8f : 2.2f) * scale;
    const struct nk_color ink = nk_rgba(22, 24, 28, 245);

    libregpu_stroke_circle_minimal(canvas, cx, cy, radius, scale, hovered);
    nk_stroke_line(canvas, cx - arm, cy - arm, cx + arm, cy + arm, thick, ink);
    nk_stroke_line(canvas, cx - arm, cy + arm, cx + arm, cy - arm, thick, ink);
}


static void libregpu_draw_ripple(struct nk_command_buffer *canvas,
                              float cx, float cy, float scale,
                              float elapsed)
{
    int ring;
    for (ring = 0; ring < LIBREGPU_RIPPLE_COUNT; ++ring) {
        const float age = elapsed - ((float)ring * LIBREGPU_RIPPLE_DELAY);
        float t;
        float radius;
        float alpha_f;
        struct nk_color dot_color;
        int dot;

        if (age < 0.0f || age > LIBREGPU_RIPPLE_LIFE) continue;
        t = age / LIBREGPU_RIPPLE_LIFE;
        radius = (25.0f + LIBREGPU_RIPPLE_MAX_R * t) * scale;
        alpha_f = (1.0f - t) * 175.0f;
        dot_color = nk_rgba(35, 38, 43, (int)libregpu_clampf(alpha_f, 0.0f, 175.0f));

        for (dot = 0; dot < LIBREGPU_RIPPLE_DOTS; ++dot) {
            const float u = (float)dot / (float)(LIBREGPU_RIPPLE_DOTS - 1);
            const float angle = (0.5f * LIBREGPU_PI) + (LIBREGPU_PI * u);
            const float x = cx + cosf(angle) * radius;
            const float y = cy + sinf(angle) * radius;
            const float dot_radius = 1.35f * scale;
            nk_fill_circle(canvas,
                           libregpu_circle_rect(x, y, dot_radius),
                           dot_color);
        }
    }
}

static const struct nk_color libregpu_profile_palette[8] = {
    { 58,  92, 140, 255 },
    { 120, 74,  96, 255 },
    { 74, 112,  88, 255 },
    { 126, 102, 58, 255 },
    { 86,  76, 132, 255 },
    { 56, 108, 120, 255 },
    { 128, 72,  64, 255 },
    { 92,  98, 112, 255 }
};

static char libregpu_profile_initial(const LibreGPUUIProfile *profile)
{
    const char *text;
    const char *base;
    const char *cursor;

    if (!profile) return '?';

    text = profile->name;
    if (text[0] == '\0') {
        base = profile->game_path;
        for (cursor = profile->game_path; *cursor; ++cursor) {
            if (*cursor == '/' || *cursor == '\\') base = cursor + 1;
        }
        text = base;
    }

    for (cursor = text; *cursor; ++cursor) {
        if (isalnum((unsigned char)*cursor))
            return (char)toupper((unsigned char)*cursor);
    }
    return '?';
}

static void libregpu_draw_profile_slot(struct nk_context *ctx,
                                    struct nk_command_buffer *canvas,
                                    const LibreGPUUIProfile *profile,
                                    float cx, float cy,
                                    float radius,
                                    int hovered)
{
    struct nk_rect rect;
    struct nk_color fill;
    float scale;

    if (!profile) return;
    if (hovered) radius *= 1.075f;
    scale = radius / 22.0f;

    if (profile->has_thumbnail) {
        libregpu_draw_image(canvas, &profile->thumbnail, 1,
                         cx, cy, radius, nk_rgba(255, 255, 255, 255));
        libregpu_stroke_circle_minimal(canvas, cx, cy, radius, scale, hovered);
        return;
    }

    

    rect = libregpu_circle_rect(cx, cy, radius);
    fill = libregpu_profile_palette[profile->id % 8u];

    nk_fill_circle(canvas, rect, fill);
    libregpu_stroke_circle_minimal(canvas, cx, cy, radius, scale, hovered);

    if (ctx->style.font) {
        char mark = libregpu_profile_initial(profile);
        const struct nk_user_font *font = ctx->style.font;
        const float height = font->height;
        const float width = font->width(font->userdata, height, &mark, 1);
        nk_draw_text(canvas,
                     nk_rect(cx - width * 0.5f, cy - height * 0.5f,
                             width, height),
                     &mark, 1, font,
                     nk_rgba(0, 0, 0, 0),
                     nk_rgba(245, 246, 250, 255));
    }
}

static void libregpu_draw_logo_placeholder(struct nk_context *ctx,
                                        struct nk_command_buffer *canvas,
                                        float cx, float cy, float radius)
{
    const struct nk_rect rect = libregpu_circle_rect(cx, cy, radius);
    const float scale = radius / 58.0f;

    nk_fill_circle(canvas, rect, nk_rgba(34, 38, 46, 255));
    nk_stroke_circle(canvas, rect, 2.0f * scale,
                     nk_rgba(150, 160, 180, 200));
    nk_stroke_circle(canvas, libregpu_circle_rect(cx, cy, radius * 0.58f),
                     1.2f * scale, nk_rgba(120, 130, 150, 160));

    if (ctx->style.font) {
        char mark = 'G';
        const struct nk_user_font *font = ctx->style.font;
        const float height = font->height;
        const float width = font->width(font->userdata, height, &mark, 1);
        nk_draw_text(canvas,
                     nk_rect(cx - width * 0.5f, cy - height * 0.5f,
                             width, height),
                     &mark, 1, font,
                     nk_rgba(0, 0, 0, 0),
                     nk_rgba(226, 229, 236, 255));
    }
}

static void libregpu_draw_context_menu(struct nk_context *ctx,
                                   struct nk_command_buffer *canvas,
                                   LibreGPUUI *ui,
                                   float window_w,
                                   float window_h)
{
    const float menu_w = 116.0f;
    const float menu_h = 36.0f;
    float x = ui->context_x;
    float y = ui->context_y;
    struct nk_rect menu_rect;
    struct nk_rect delete_rect;
    const struct nk_vec2 mouse = ctx->input.mouse.pos;
    int hovering;

    if (!ui->context_open) return;

    if (x + menu_w > window_w) x = window_w - menu_w - 4.0f;
    if (y + menu_h > window_h) y = window_h - menu_h - 4.0f;
    if (x < 4.0f) x = 4.0f;
    if (y < 4.0f) y = 4.0f;

    menu_rect = nk_rect(x, y, menu_w, menu_h);
    delete_rect = nk_rect(x + 3.0f, y + 3.0f, menu_w - 6.0f, menu_h - 6.0f);
    hovering = (mouse.x >= menu_rect.x && mouse.x <= menu_rect.x + menu_rect.w &&
                mouse.y >= menu_rect.y && mouse.y <= menu_rect.y + menu_rect.h);

    nk_fill_rect(canvas, menu_rect, 5.0f,
                 nk_rgba(26, 28, 33, 238));
    nk_fill_rect(canvas, delete_rect, 4.0f,
                 hovering ? nk_rgba(86, 42, 45, 250)
                          : nk_rgba(42, 44, 49, 245));

    if (ctx->style.font) {
        nk_draw_text(canvas,
                     nk_rect(x + 13.0f, y + 7.0f, menu_w - 26.0f, 22.0f),
                     "Delete", 6, ctx->style.font,
                     nk_rgba(250, 250, 250, 255),
                     nk_rgba(0, 0, 0, 0));
    }

    if (libregpu_mouse_clicked_rect(ctx, NK_BUTTON_LEFT, delete_rect)) {
        const int index = libregpu_profile_index_by_id(ui, ui->context_profile_id);
        if (index >= 0 && ui->hooks.delete_profile &&
            ui->hooks.delete_profile(ui->hooks.userdata,
                                     ui->context_profile_id)) {
            libregpu_remove_profile_at(ui, (size_t)index);
            ui->context_open = 0;
        }
        

    } else if (nk_input_is_mouse_click_in_rect(&ctx->input, NK_BUTTON_LEFT,
                                                nk_rect(0, 0, window_w, window_h)) ||
               nk_input_is_mouse_click_in_rect(&ctx->input, NK_BUTTON_RIGHT,
                                                nk_rect(0, 0, window_w, window_h))) {
        const struct nk_vec2 click =
            ctx->input.mouse.buttons[NK_BUTTON_LEFT].clicked
                ? ctx->input.mouse.buttons[NK_BUTTON_LEFT].clicked_pos
                : ctx->input.mouse.buttons[NK_BUTTON_RIGHT].clicked_pos;
        if (click.x < menu_rect.x || click.x > menu_rect.x + menu_rect.w ||
            click.y < menu_rect.y || click.y > menu_rect.y + menu_rect.h) {
            ui->context_open = 0;
        }
    }
}





static void libregpu_process_orbit_input(struct nk_context *ctx,
                                      LibreGPUUI *ui,
                                      float logo_x,
                                      float logo_y,
                                      float scale,
                                      float window_w,
                                      float window_h)
{
    float point_x[7];
    float point_y[7];
    float logo_radius = 58.0f * scale;
    float control_radius = 22.0f * scale;
    int i;

    for (i = 0; i < 7; ++i) {
        point_x[i] = logo_x + libregpu_orbit_offsets[i].x * scale;
        point_y[i] = logo_y + libregpu_orbit_offsets[i].y * scale;
    }

    if (ui->config_open || ui->quit_requested) return;

    if (ui->context_open) {
        
        return;
    }

    if (libregpu_mouse_clicked_circle(ctx, NK_BUTTON_LEFT,
                                   point_x[0], point_y[0], control_radius)) {
        ui->quit_requested = 1;
        if (ui->hooks.quit) ui->hooks.quit(ui->hooks.userdata);
        return;
    }

    if (libregpu_mouse_clicked_circle(ctx, NK_BUTTON_LEFT,
                                   logo_x, logo_y, logo_radius)) {
        libregpu_start_logo_response(ui);
        return;
    }

    if (libregpu_mouse_clicked_circle(ctx, NK_BUTTON_LEFT,
                                   point_x[6], point_y[6], control_radius)) {
        libregpu_open_config(ui);
        return;
    }

    if (ui->page_animating) return;

    for (i = 0; i < LIBREGPU_PROFILE_PAGE_SIZE; ++i) {
        const size_t profile_index =
            ui->current_page * LIBREGPU_PROFILE_PAGE_SIZE + (size_t)i;
        const float x = point_x[i + 1];
        const float y = point_y[i + 1];
        const float r = 22.0f * scale;

        if (profile_index >= ui->profile_count) continue;

        if (libregpu_mouse_clicked_circle(ctx, NK_BUTTON_RIGHT, x, y, r)) {
            const struct nk_vec2 click =
                ctx->input.mouse.buttons[NK_BUTTON_RIGHT].clicked_pos;
            ui->context_open = 1;
            ui->context_profile_id = ui->profiles[profile_index].id;
            ui->context_x = click.x;
            ui->context_y = click.y;
            return;
        }

        if (libregpu_mouse_clicked_circle(ctx, NK_BUTTON_LEFT, x, y, r)) {
            if (ui->hooks.launch_profile) {
                LibreGPULauncherSettings launch_settings = *ui->settings;
                (void)snprintf(launch_settings.game_path, sizeof launch_settings.game_path,
                               "%s", ui->profiles[profile_index].game_path);
                launch_settings.game_selected = 1;
                launch_settings.config = ui->profiles[profile_index].config;
                ui->hooks.launch_profile(ui->hooks.userdata, &launch_settings);
            }
            return;
        }
    }

    (void)window_w;
    (void)window_h;
}





static void libregpu_draw_orbit(struct nk_context *ctx,
                             struct nk_command_buffer *canvas,
                             LibreGPUUI *ui,
                             float window_w,
                             float window_h)
{
    float scale;
    float logo_x;
    float logo_y;
    float logo_draw_x;
    float logo_draw_y;
    float logo_radius;
    float control_radius;
    float point_x[7];
    float point_y[7];
    int i;
    const struct nk_vec2 mouse = ctx->input.mouse.pos;

    scale = fminf(window_w / LIBREGPU_REFERENCE_W,
                  window_h / LIBREGPU_REFERENCE_H);
    if (scale > 1.75f) scale = 1.75f;
    if (scale < 0.35f) scale = 0.35f;

    logo_x = window_w * 0.32f;
    logo_y = window_h * 0.50f;
    logo_radius = 58.0f * scale;
    control_radius = 22.0f * scale;
    logo_draw_x = logo_x + libregpu_logo_shake_offset_x(ui, scale);
    logo_draw_y = logo_y + libregpu_logo_shake_offset_y(ui, scale);

    for (i = 0; i < 7; ++i) {
        point_x[i] = logo_x + libregpu_orbit_offsets[i].x * scale;
        point_y[i] = logo_y + libregpu_orbit_offsets[i].y * scale;
    }

    
    if (ui->ripple_active)
        libregpu_draw_ripple(canvas, logo_x, logo_y, scale, ui->ripple_elapsed);

    

    if (ui->has_logo) {
        libregpu_draw_image(canvas, &ui->logo, 1,
                         logo_draw_x, logo_draw_y, logo_radius,
                         nk_rgba(255, 255, 255, 255));
        libregpu_stroke_circle_minimal(canvas, logo_draw_x, logo_draw_y,
                                    logo_radius, scale, 0);
    } else {
        libregpu_draw_logo_placeholder(ctx, canvas,
                                    logo_draw_x, logo_draw_y, logo_radius);
    }

    

    if (ui->page_animating) {
        const float t = libregpu_smoothstep(ui->page_elapsed /
                                         LIBREGPU_PAGE_SCROLL_TIME);
        const float shift = LIBREGPU_PAGE_SCROLL_PX * scale;
        for (i = 0; i < LIBREGPU_PROFILE_PAGE_SIZE; ++i) {
            const size_t old_index =
                ui->current_page * LIBREGPU_PROFILE_PAGE_SIZE + (size_t)i;
            const size_t new_index =
                ui->target_page * LIBREGPU_PROFILE_PAGE_SIZE + (size_t)i;
            const float x = point_x[i + 1];
            const float base_y = point_y[i + 1];

            if (old_index < ui->profile_count) {
                libregpu_draw_profile_slot(ctx, canvas, &ui->profiles[old_index],
                                        x, base_y - shift * t,
                                        22.0f * scale, 0);
            }
            if (new_index < ui->profile_count) {
                libregpu_draw_profile_slot(ctx, canvas, &ui->profiles[new_index],
                                        x, base_y + shift * (1.0f - t),
                                        22.0f * scale, 0);
            }
        }
    } else {
        for (i = 0; i < LIBREGPU_PROFILE_PAGE_SIZE; ++i) {
            const size_t profile_index =
                ui->current_page * LIBREGPU_PROFILE_PAGE_SIZE + (size_t)i;
            const float x = point_x[i + 1];
            const float y = point_y[i + 1];
            const float r = 22.0f * scale;
            const int hovered =
                profile_index < ui->profile_count &&
                libregpu_point_in_circle(mouse.x, mouse.y, x, y, r);

            if (profile_index < ui->profile_count) {
                libregpu_draw_profile_slot(ctx, canvas, &ui->profiles[profile_index],
                                        x, y, r, hovered);
            }
        }
    }

    
    libregpu_draw_close(canvas, point_x[0], point_y[0], control_radius,
                     scale,
                     libregpu_point_in_circle(mouse.x, mouse.y,
                                           point_x[0], point_y[0], control_radius));
    libregpu_draw_plus(canvas, point_x[6], point_y[6], control_radius,
                    scale,
                    libregpu_point_in_circle(mouse.x, mouse.y,
                                          point_x[6], point_y[6], control_radius));

    libregpu_process_orbit_input(ctx, ui, logo_x, logo_y, scale,
                              window_w, window_h);

    if (ui->context_open)
        libregpu_draw_context_menu(ctx, canvas, ui, window_w, window_h);
}





static void libregpu_save_profile_from_config(LibreGPUUI *ui)
{
    int fps = 0;
    LibreGPUUIProfile created;
    LibreGPULauncherSettings candidate;
    char error[LIBREGPU_ERROR_CAP];
    size_t path_len;

    path_len = strlen(ui->path_text);
    ui->path_invalid = (path_len == 0 || path_len >= LIBREGPU_UI_PATH_MAX ||
                        !libregpu_ui_path_is_file(ui, ui->path_text));

    ui->fps_invalid = !libregpu_parse_integer(ui->fps_text, &fps) ||
                      fps < 0 || (double)fps > (double)LIBREGPU_GOV_MAX_HZ;
    ui->backend_error[0] = '\0';

    if (ui->path_invalid || ui->fps_invalid) return;

    if (!ui->hooks.save_profile) {
        (void)snprintf(ui->backend_error, sizeof(ui->backend_error),
                       "%s", "Profile save is not connected");
        return;
    }

    if (ui->profile_count >= LIBREGPU_UI_MAX_PROFILES) {
        (void)snprintf(ui->backend_error, sizeof(ui->backend_error),
                       "%s", "Profile capacity reached");
        return;
    }

    candidate = *ui->settings;
    (void)snprintf(candidate.game_path, sizeof candidate.game_path,
                   "%s", ui->path_text);
    candidate.game_selected = true;
    candidate.config.target_loops_per_second = (double)fps;

    memset(&created, 0, sizeof(created));
    memset(error, 0, sizeof(error));

    if (!ui->hooks.save_profile(ui->hooks.userdata,
                                &candidate,
                                &created,
                                error, sizeof(error))) {
        if (error[0] != '\0') {
            (void)snprintf(ui->backend_error, sizeof(ui->backend_error),
                           "%s", error);
        } else {
            (void)snprintf(ui->backend_error, sizeof(ui->backend_error),
                           "%s", "Could not save profile");
        }
        return;
    }

    if (libregpu_profile_index_by_id(ui, created.id) >= 0) {
        (void)snprintf(ui->backend_error, sizeof(ui->backend_error),
                       "%s", "Profile ID already exists");
        return;
    }

    

    (void)snprintf(created.game_path, sizeof created.game_path,
                   "%s", candidate.game_path);
    created.config = candidate.config;
    created.name[sizeof created.name - 1] = '\0';
    ui->profiles[ui->profile_count++] = created;

    
    *ui->settings = candidate;

    
    ui->current_page = (ui->profile_count - 1) / LIBREGPU_PROFILE_PAGE_SIZE;
    ui->target_page = ui->current_page;
    ui->page_animating = 0;

    libregpu_close_config(ui);
}

static void libregpu_draw_config_window(struct nk_context *ctx,
                                     LibreGPUUI *ui,
                                     float window_w,
                                     float window_h)
{
    const float dialog_w = fminf(460.0f, fmaxf(1.0f, window_w - 24.0f));
    const float dialog_h = fminf(318.0f, fmaxf(1.0f, window_h - 24.0f));
    const float dialog_x = fmaxf(0.0f, (window_w - dialog_w) * 0.5f);
    const float dialog_y = fmaxf(0.0f, (window_h - dialog_h) * 0.5f);
    const struct nk_rect bounds = nk_rect(dialog_x, dialog_y, dialog_w, dialog_h);
    const struct nk_style_window old_window_style = ctx->style.window;
    char old_path[LIBREGPU_UI_PATH_MAX];
    char old_fps[LIBREGPU_FPS_CAP];
    int save_clicked = 0;
    int cancel_clicked = 0;

    

    ctx->style.window.background = nk_rgba(29, 32, 38, 222);
    ctx->style.window.fixed_background =
        nk_style_item_color(nk_rgba(29, 32, 38, 222));
    ctx->style.window.header.normal =
        nk_style_item_color(nk_rgba(29, 32, 38, 222));
    ctx->style.window.header.hover =
        nk_style_item_color(nk_rgba(37, 40, 47, 230));
    ctx->style.window.header.active =
        nk_style_item_color(nk_rgba(37, 40, 47, 238));
    ctx->style.window.border_color = nk_rgba(230, 233, 240, 105);
    ctx->style.window.border = 1.0f;
    ctx->style.window.rounding = 8.0f;
    ctx->style.window.padding = nk_vec2(15.0f, 12.0f);
    ctx->style.window.spacing = nk_vec2(8.0f, 6.0f);

    if (nk_begin(ctx, "LibreGPU", bounds,
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR)) {
        (void)snprintf(old_path, sizeof(old_path), "%s", ui->path_text);
        (void)snprintf(old_fps, sizeof(old_fps), "%s", ui->fps_text);

        nk_layout_row_dynamic(ctx, 19.0f, 1);
        nk_label(ctx, "Game File Path", NK_TEXT_LEFT);

        nk_layout_row_begin(ctx, NK_DYNAMIC, 32.0f, 2);
        nk_layout_row_push(ctx, 0.78f);
        (void)nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD,
                                             ui->path_text,
                                             (int)sizeof(ui->path_text),
                                             NULL);
        nk_layout_row_push(ctx, 0.22f);
        if (nk_button_label(ctx, "Browse")) {
            if (ui->hooks.pick_executable) {
                char selected[LIBREGPU_UI_PATH_MAX];
                memset(selected, 0, sizeof(selected));
                if (ui->hooks.pick_executable(ui->hooks.userdata,
                                              selected, sizeof(selected))) {
                    selected[sizeof(selected) - 1] = '\0';
                    (void)snprintf(ui->path_text, sizeof(ui->path_text),
                                   "%s", selected);
                }
            }
        }
        nk_layout_row_end(ctx);

        if (strcmp(old_path, ui->path_text) != 0) {
            ui->path_invalid = 0;
            ui->backend_error[0] = '\0';
        }
        if (ui->path_invalid) {
            nk_layout_row_dynamic(ctx, 18.0f, 1);
            nk_label_colored(ctx, "File not found", NK_TEXT_LEFT,
                              nk_rgba(255, 105, 105, 255));
        }

        nk_layout_row_dynamic(ctx, 19.0f, 1);
        nk_label(ctx, "Limit FPS Count", NK_TEXT_LEFT);

        nk_layout_row_dynamic(ctx, 32.0f, 1);
        (void)nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD,
                                             ui->fps_text,
                                             (int)sizeof(ui->fps_text),
                                             NULL);
        if (strcmp(old_fps, ui->fps_text) != 0) {
            ui->fps_invalid = 0;
            ui->backend_error[0] = '\0';
        }
        if (ui->fps_invalid) {
            nk_layout_row_dynamic(ctx, 18.0f, 1);
            nk_label_colored(ctx, "Invalid FPS", NK_TEXT_LEFT,
                              nk_rgba(255, 105, 105, 255));
        }

        if (ui->backend_error[0] != '\0') {
            nk_layout_row_dynamic(ctx, 18.0f, 1);
            nk_label_colored(ctx, ui->backend_error, NK_TEXT_LEFT,
                              nk_rgba(255, 190, 105, 255));
        }

        nk_layout_row_dynamic(ctx, 32.0f, 2);
        cancel_clicked = nk_button_label(ctx, "Cancel");
        save_clicked = nk_button_label(ctx, "Save");
    }
    nk_end(ctx);

    ctx->style.window = old_window_style;

    if (cancel_clicked) {
        libregpu_close_config(ui);
    } else if (save_clicked) {
        libregpu_save_profile_from_config(ui);
    }
}





void libregpu_ui_draw(struct nk_context *ctx,
                   LibreGPUUI *ui,
                   float window_width,
                   float window_height,
                   float delta_seconds)
{
    const struct nk_style_window old_window_style = ctx ? ctx->style.window
                                                        : (struct nk_style_window){0};
    struct nk_command_buffer *canvas;

    if (!ctx || !ui || window_width <= 0.0f || window_height <= 0.0f ||
        ui->quit_requested) {
        return;
    }

    libregpu_update_animations(ui, delta_seconds);

    

    ctx->style.window.background = nk_rgba(0, 0, 0, 0);
    ctx->style.window.fixed_background = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    ctx->style.window.border = 0.0f;
    ctx->style.window.padding = nk_vec2(0.0f, 0.0f);
    ctx->style.window.spacing = nk_vec2(0.0f, 0.0f);

    if (nk_begin(ctx, "libregpu_orbit_overlay",
                 nk_rect(0.0f, 0.0f, window_width, window_height),
                 NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_INPUT)) {
        canvas = nk_window_get_canvas(ctx);
        if (canvas) {
            libregpu_draw_orbit(ctx, canvas, ui, window_width, window_height);
        }
    }
    nk_end(ctx);
    ctx->style.window = old_window_style;

    
    if (ui->config_open)
        libregpu_draw_config_window(ctx, ui, window_width, window_height);
}



























































#define APP_DEFAULT_WIDTH   1080
#define APP_DEFAULT_HEIGHT  720
#define APP_MIN_WIDTH       720
#define APP_MIN_HEIGHT      480
#define APP_BORDER          6
#define APP_DRAG_STRIP      18
#define APP_PICKER_ENTRIES  1024
#define APP_PICKER_PAGE     11
#define APP_TOAST_SECONDS   4.0
#define APP_TARGET_FPS      60.0





#define APP_ORBIT_OPACITY   0.55f

#define APP_RESIZE_LEFT     1
#define APP_RESIZE_RIGHT    2
#define APP_RESIZE_TOP      4
#define APP_RESIZE_BOTTOM   8

typedef struct LibreGPUApp {
    SDL_Window *window;
    SDL_Renderer *renderer;
    LibreGPUNkSdl2 nk;

    LibreGPULauncherSettings settings;
    LibreGPUUI ui;
    LibreGPUProfileStore profiles;

    int running;

    int dragging;
    int resizing;
    int resize_edges;
    int suppress_mouse;
    int drag_mouse_x;
    int drag_mouse_y;
    int drag_window_x;
    int drag_window_y;
    int drag_window_w;
    int drag_window_h;

    float window_opacity;

    char toast[384];
    double toast_time;

    int picker_requested;
    int picker_open;
    char picker_dir[LIBREGPU_PROFILE_PATH_MAX];
    LibreGPUDirEntry picker_entries[APP_PICKER_ENTRIES];
    size_t picker_count;
    size_t picker_page;
    char picker_error[160];
} LibreGPUApp;





static double app_now(void)
{
    const Uint64 frequency = SDL_GetPerformanceFrequency();
    if (frequency == 0) return 0.0;
    return (double)SDL_GetPerformanceCounter() / (double)frequency;
}

static void app_toast(LibreGPUApp *app, const char *text)
{
    if (!app) return;
    (void)snprintf(app->toast, sizeof(app->toast), "%s",
                   text ? text : "");
    app->toast_time = APP_TOAST_SECONDS;
}





static void app_launch_profile(void *userdata,
                               const LibreGPULauncherSettings *settings)
{
    LibreGPUApp *app = (LibreGPUApp *)userdata;
    char error[200];
    char message[320];

    if (!app || !settings) return;

    if (!libregpu_path_is_executable_file(settings->game_path)) {
        app_toast(app, "File not found or not executable");
        return;
    }

    if (!libregpu_launch_detached(settings->game_path, error, sizeof(error))) {
        app_toast(app, error);
        return;
    }

    

    (void)snprintf(message, sizeof(message), "Launched %.310s",
                   libregpu_path_basename(settings->game_path));
    app_toast(app, message);
}

static int app_pick_executable(void *userdata,
                               char *out_path, size_t out_capacity)
{
    LibreGPUApp *app = (LibreGPUApp *)userdata;
    (void)out_path;
    (void)out_capacity;
    if (!app) return 0;
    

    app->picker_requested = 1;
    return 0;
}

static int app_path_is_file(void *userdata, const char *path)
{
    (void)userdata;
    return libregpu_path_is_executable_file(path);
}

static int app_save_profile(void *userdata,
                            const LibreGPULauncherSettings *settings,
                            LibreGPUUIProfile *out_profile,
                            char *error_text, size_t error_capacity)
{
    LibreGPUApp *app = (LibreGPUApp *)userdata;
    LibreGPUStoredProfile stored;
    int fps;

    if (!app || !settings || !out_profile) return 0;

    fps = (int)(settings->config.target_loops_per_second + 0.5);
    memset(&stored, 0, sizeof(stored));
    if (!libregpu_profiles_add(&app->profiles, settings->game_path, fps,
                            &stored, error_text, error_capacity))
        return 0;

    memset(out_profile, 0, sizeof(*out_profile));
    out_profile->id = stored.id;
    out_profile->config = settings->config;
    out_profile->config.target_loops_per_second = (double)stored.fps;
    (void)snprintf(out_profile->game_path, sizeof(out_profile->game_path),
                   "%s", stored.path);
    (void)snprintf(out_profile->name, sizeof(out_profile->name),
                   "%s", stored.name);
    out_profile->has_thumbnail = 0;
    return 1;
}

static int app_delete_profile(void *userdata, uint32_t profile_id)
{
    LibreGPUApp *app = (LibreGPUApp *)userdata;
    if (!app) return 0;
    return libregpu_profiles_delete(&app->profiles, profile_id);
}

static void app_quit(void *userdata)
{
    LibreGPUApp *app = (LibreGPUApp *)userdata;
    if (app) app->running = 0;
}





static void app_load_profiles(LibreGPUApp *app)
{
    LibreGPUUIProfile *loaded;
    size_t count;
    size_t i;

    count = libregpu_profiles_count(&app->profiles);
    if (count == 0) {
        libregpu_ui_set_profiles(&app->ui, NULL, 0);
        return;
    }

    loaded = (LibreGPUUIProfile *)calloc(count, sizeof(*loaded));
    if (!loaded) {
        libregpu_ui_set_profiles(&app->ui, NULL, 0);
        return;
    }

    for (i = 0; i < count; ++i) {
        const LibreGPUStoredProfile *stored = libregpu_profiles_at(&app->profiles, i);
        if (!stored) continue;
        loaded[i].id = stored->id;
        loaded[i].config = app->settings.config;
        loaded[i].config.target_loops_per_second = (double)stored->fps;
        (void)snprintf(loaded[i].game_path, sizeof(loaded[i].game_path),
                       "%s", stored->path);
        (void)snprintf(loaded[i].name, sizeof(loaded[i].name),
                       "%s", stored->name);
        loaded[i].has_thumbnail = 0;
    }

    libregpu_ui_set_profiles(&app->ui, loaded, count);
    free(loaded);
}





static void app_picker_reload(LibreGPUApp *app)
{
    size_t count = 0;

    app->picker_page = 0;
    app->picker_count = 0;
    app->picker_error[0] = '\0';

    if (!libregpu_path_list_directory(app->picker_dir,
                                   app->picker_entries,
                                   APP_PICKER_ENTRIES, &count)) {
        (void)snprintf(app->picker_error, sizeof(app->picker_error),
                       "%s", "Could not read this directory");
        return;
    }
    app->picker_count = count;
}

static void app_picker_start_dir(LibreGPUApp *app)
{
    char parent[LIBREGPU_PROFILE_PATH_MAX];
    char initial[LIBREGPU_PROFILE_PATH_MAX];
    const char *selected = app->ui.path_text;

    if (selected && selected[0] != '\0') {
        if (libregpu_path_is_directory(selected)) {
            (void)snprintf(app->picker_dir, sizeof(app->picker_dir),
                           "%s", selected);
            return;
        }
        if (libregpu_path_parent(selected, parent, sizeof(parent)) &&
            libregpu_path_is_directory(parent)) {
            (void)snprintf(app->picker_dir, sizeof(app->picker_dir),
                           "%s", parent);
            return;
        }
    }

    if (libregpu_path_initial_directory(initial, sizeof(initial)))
        (void)snprintf(app->picker_dir, sizeof(app->picker_dir),
                       "%s", initial);
    else
        (void)snprintf(app->picker_dir, sizeof(app->picker_dir), "%s", ".");
}

static void app_picker_open(LibreGPUApp *app)
{
    app_picker_start_dir(app);
    app_picker_reload(app);
    app->picker_open = 1;
}

static void app_picker_navigate(LibreGPUApp *app, const char *directory)
{
    if (!directory || directory[0] == '\0') return;
    (void)snprintf(app->picker_dir, sizeof(app->picker_dir), "%s", directory);
    app_picker_reload(app);
}

static void app_picker_up(LibreGPUApp *app)
{
    char parent[LIBREGPU_PROFILE_PATH_MAX];
    if (libregpu_path_parent(app->picker_dir, parent, sizeof(parent)))
        app_picker_navigate(app, parent);
}





static void app_draw_picker(LibreGPUApp *app, int window_w, int window_h)
{
    struct nk_context *ctx = &app->nk.ctx;
    const struct nk_style_window saved_style = ctx->style.window;
    const float picker_w = fminf(640.0f, fmaxf(1.0f, (float)window_w - 40.0f));
    const float picker_h = fminf(480.0f, fmaxf(1.0f, (float)window_h - 40.0f));
    const struct nk_rect bounds = nk_rect((float)(window_w) * 0.5f - picker_w * 0.5f,
                                          (float)(window_h) * 0.5f - picker_h * 0.5f,
                                          picker_w, picker_h);
    int go_up = 0;
    int refresh = 0;
    int page_delta = 0;
    int cancel = 0;
    int navigate = 0;
    int choose = 0;
    char next_dir[LIBREGPU_PROFILE_PATH_MAX];
    char chosen_path[LIBREGPU_PROFILE_PATH_MAX];
    size_t first;
    size_t last;
    size_t i;

    next_dir[0] = '\0';
    chosen_path[0] = '\0';

    ctx->style.window.background = nk_rgba(29, 32, 38, 248);
    ctx->style.window.fixed_background =
        nk_style_item_color(nk_rgba(29, 32, 38, 248));
    ctx->style.window.border_color = nk_rgba(230, 233, 240, 110);
    ctx->style.window.border = 1.0f;
    ctx->style.window.rounding = 8.0f;
    ctx->style.window.padding = nk_vec2(14.0f, 12.0f);
    ctx->style.window.spacing = nk_vec2(7.0f, 5.0f);

    if (nk_begin(ctx, "Open Game Executable", bounds,
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_dynamic(ctx, 20.0f, 1);
        nk_label(ctx, app->picker_dir, NK_TEXT_LEFT);

        if (app->picker_error[0] != '\0') {
            nk_layout_row_dynamic(ctx, 18.0f, 1);
            nk_label_colored(ctx, app->picker_error, NK_TEXT_LEFT,
                             nk_rgba(255, 120, 120, 255));
        }

        nk_layout_row_dynamic(ctx, 28.0f, 4);
        if (nk_button_label(ctx, "Up")) go_up = 1;
        if (nk_button_label(ctx, "Refresh")) refresh = 1;
        if (nk_button_label(ctx, "< Prev")) page_delta = -1;
        if (nk_button_label(ctx, "Next >")) page_delta = 1;

        first = app->picker_page * APP_PICKER_PAGE;
        last = first + APP_PICKER_PAGE;
        if (last > app->picker_count) last = app->picker_count;

        if (app->picker_count == 0) {
            nk_layout_row_dynamic(ctx, 24.0f, 1);
            nk_label(ctx, "No entries", NK_TEXT_LEFT);
        }
        for (i = first; i < last; ++i) {
            const LibreGPUDirEntry *entry = &app->picker_entries[i];
            char label[LIBREGPU_PATH_NAME_MAX + 8];
            (void)snprintf(label, sizeof(label), "%s%s",
                           entry->is_directory ? "[DIR] " : "      ",
                           entry->name);
            nk_layout_row_dynamic(ctx, 24.0f, 1);
            if (nk_button_label(ctx, label)) {
                if (entry->is_directory) {
                    if (libregpu_path_join(next_dir, sizeof(next_dir),
                                        app->picker_dir, entry->name))
                        navigate = 1;
                } else {
                    if (libregpu_path_join(chosen_path, sizeof(chosen_path),
                                        app->picker_dir, entry->name))
                        choose = 1;
                }
            }
        }

        nk_layout_row_dynamic(ctx, 30.0f, 2);
        if (nk_button_label(ctx, "Cancel")) cancel = 1;
        {
            char page_text[64];
            const size_t pages =
                (app->picker_count + APP_PICKER_PAGE - 1) / APP_PICKER_PAGE;
            (void)snprintf(page_text, sizeof(page_text),
                           "Page %lu / %lu",
                           (unsigned long)(app->picker_page + 1),
                           (unsigned long)(pages ? pages : 1));
            nk_label(ctx, page_text, NK_TEXT_RIGHT);
        }
    }
    nk_end(ctx);

    ctx->style.window = saved_style;

    if (navigate) {
        app_picker_navigate(app, next_dir);
    } else if (go_up) {
        app_picker_up(app);
    } else if (refresh) {
        app_picker_reload(app);
    } else if (page_delta != 0) {
        const size_t pages =
            (app->picker_count + APP_PICKER_PAGE - 1) / APP_PICKER_PAGE;
        if (page_delta < 0 && app->picker_page > 0)
            --app->picker_page;
        else if (page_delta > 0 && app->picker_page + 1 < pages)
            ++app->picker_page;
    }

    if (choose) {
        if (libregpu_path_is_executable_file(chosen_path)) {
            (void)snprintf(app->ui.path_text, sizeof(app->ui.path_text),
                           "%s", chosen_path);
            app->ui.path_invalid = 0;
            app->ui.backend_error[0] = '\0';
            app->picker_open = 0;
        } else {
            (void)snprintf(app->picker_error, sizeof(app->picker_error),
                           "%s", "That file is not executable");
        }
    }

    if (cancel) app->picker_open = 0;
}





static void app_draw_toast(LibreGPUApp *app, int window_w, int window_h)
{
    struct nk_context *ctx = &app->nk.ctx;
    const struct nk_style_window saved_style = ctx->style.window;
    const float toast_w = fminf(540.0f, fmaxf(1.0f, (float)window_w - 24.0f));
    const float toast_h = 38.0f;
    const struct nk_rect bounds = nk_rect(12.0f, (float)window_h - toast_h - 12.0f,
                                          toast_w, toast_h);

    ctx->style.window.background = nk_rgba(24, 27, 32, 236);
    ctx->style.window.fixed_background =
        nk_style_item_color(nk_rgba(24, 27, 32, 236));
    ctx->style.window.border = 0.0f;
    ctx->style.window.rounding = 6.0f;
    ctx->style.window.padding = nk_vec2(12.0f, 8.0f);
    ctx->style.window.spacing = nk_vec2(0.0f, 0.0f);

    if (nk_begin(ctx, "##libregpu_toast", bounds,
                 NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_INPUT)) {
        nk_layout_row_dynamic(ctx, 22.0f, 1);
        nk_label_colored(ctx, app->toast, NK_TEXT_LEFT,
                         nk_rgba(235, 238, 245, 255));
    }
    nk_end(ctx);

    ctx->style.window = saved_style;
}





static void app_capture_start(LibreGPUApp *app)
{
    int mouse_x = 0;
    int mouse_y = 0;

    SDL_GetGlobalMouseState(&mouse_x, &mouse_y);
    SDL_GetWindowPosition(app->window,
                          &app->drag_window_x, &app->drag_window_y);
    SDL_GetWindowSize(app->window,
                      &app->drag_window_w, &app->drag_window_h);
    app->drag_mouse_x = mouse_x;
    app->drag_mouse_y = mouse_y;
    SDL_CaptureMouse(SDL_TRUE);
}

static int app_begin_drag_or_resize(LibreGPUApp *app, const SDL_Event *event)
{
    int window_w = 0;
    int window_h = 0;
    int x = event->button.x;
    int y = event->button.y;
    int edges = 0;

    if (event->button.button != SDL_BUTTON_LEFT) return 0;
    if (app->picker_open) return 0;

    SDL_GetWindowSize(app->window, &window_w, &window_h);

    if (x < APP_BORDER) edges |= APP_RESIZE_LEFT;
    if (x >= window_w - APP_BORDER) edges |= APP_RESIZE_RIGHT;
    if (y < APP_BORDER) edges |= APP_RESIZE_TOP;
    if (y >= window_h - APP_BORDER) edges |= APP_RESIZE_BOTTOM;

    if (edges != 0) {
        app->resizing = 1;
        app->resize_edges = edges;
        app_capture_start(app);
        return 1;
    }

    if (y >= APP_BORDER && y < APP_BORDER + APP_DRAG_STRIP) {
        app->dragging = 1;
        app_capture_start(app);
        return 1;
    }

    if ((SDL_GetModState() & KMOD_ALT) != 0) {
        app->dragging = 1;
        app_capture_start(app);
        return 1;
    }

    return 0;
}

static void app_update_drag_or_resize(LibreGPUApp *app)
{
    int mouse_x = 0;
    int mouse_y = 0;
    int dx;
    int dy;

    SDL_GetGlobalMouseState(&mouse_x, &mouse_y);
    dx = mouse_x - app->drag_mouse_x;
    dy = mouse_y - app->drag_mouse_y;

    if (app->dragging) {
        SDL_SetWindowPosition(app->window,
                              app->drag_window_x + dx,
                              app->drag_window_y + dy);
        return;
    }

    if (app->resizing) {
        int new_x = app->drag_window_x;
        int new_y = app->drag_window_y;
        int new_w = app->drag_window_w;
        int new_h = app->drag_window_h;

        if ((app->resize_edges & APP_RESIZE_LEFT) != 0) {
            new_x = app->drag_window_x + dx;
            new_w = app->drag_window_w - dx;
            if (new_w < APP_MIN_WIDTH) {
                new_w = APP_MIN_WIDTH;
                new_x = app->drag_window_x + app->drag_window_w - APP_MIN_WIDTH;
            }
        }
        if ((app->resize_edges & APP_RESIZE_RIGHT) != 0) {
            new_w = app->drag_window_w + dx;
            if (new_w < APP_MIN_WIDTH) new_w = APP_MIN_WIDTH;
        }
        if ((app->resize_edges & APP_RESIZE_TOP) != 0) {
            new_y = app->drag_window_y + dy;
            new_h = app->drag_window_h - dy;
            if (new_h < APP_MIN_HEIGHT) {
                new_h = APP_MIN_HEIGHT;
                new_y = app->drag_window_y + app->drag_window_h - APP_MIN_HEIGHT;
            }
        }
        if ((app->resize_edges & APP_RESIZE_BOTTOM) != 0) {
            new_h = app->drag_window_h + dy;
            if (new_h < APP_MIN_HEIGHT) new_h = APP_MIN_HEIGHT;
        }

        SDL_SetWindowPosition(app->window, new_x, new_y);
        SDL_SetWindowSize(app->window, new_w, new_h);
    }
}

static void app_end_drag_or_resize(LibreGPUApp *app)
{
    app->dragging = 0;
    app->resizing = 0;
    app->resize_edges = 0;
    SDL_CaptureMouse(SDL_FALSE);
}





static void app_handle_event(LibreGPUApp *app, const SDL_Event *event)
{
    switch (event->type) {
    case SDL_QUIT:
        app->running = 0;
        return;

    case SDL_KEYDOWN:
        if (event->key.keysym.sym == SDLK_ESCAPE && !event->key.repeat) {
            if (app->picker_open) {
                app->picker_open = 0;
                return;
            }
            if (app->ui.config_open) {
                


            }
            app->running = 0;
            return;
        }
        break;

    case SDL_DROPFILE:
        if (event->drop.file) {
            if (!app->picker_open && app->ui.config_open) {
                (void)snprintf(app->ui.path_text,
                               sizeof(app->ui.path_text),
                               "%s", event->drop.file);
                app->ui.path_invalid = 0;
                app->ui.backend_error[0] = '\0';
            }
            SDL_free(event->drop.file);
        }
        return;

    case SDL_MOUSEBUTTONDOWN:
        if (app_begin_drag_or_resize(app, event)) {
            app->suppress_mouse = 1;
            return;
        }
        break;

    case SDL_MOUSEMOTION:
        if (app->suppress_mouse) {
            app_update_drag_or_resize(app);
            return;
        }
        break;

    case SDL_MOUSEBUTTONUP:
        if (app->suppress_mouse) {
            app_end_drag_or_resize(app);
            app->suppress_mouse = 0;
            return;
        }
        break;

    default:
        break;
    }

    libregpu_nk_sdl2_handle_event(&app->nk, event);
}





static int app_create_window(LibreGPUApp *app)
{
    app->window = SDL_CreateWindow("LibreGPU",
                                   SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED,
                                   APP_DEFAULT_WIDTH, APP_DEFAULT_HEIGHT,
                                   SDL_WINDOW_BORDERLESS |
                                   SDL_WINDOW_RESIZABLE |
                                   SDL_WINDOW_ALLOW_HIGHDPI |
                                   SDL_WINDOW_INPUT_FOCUS);
    if (!app->window) {
        fprintf(stderr, "libregpu: could not create the window: %s\n",
                SDL_GetError());
        return 0;
    }

    SDL_SetWindowMinimumSize(app->window, APP_MIN_WIDTH, APP_MIN_HEIGHT);

    app->window_opacity = APP_ORBIT_OPACITY;
    (void)SDL_SetWindowOpacity(app->window, app->window_opacity);

    app->renderer = SDL_CreateRenderer(app->window, -1,
                                       SDL_RENDERER_ACCELERATED |
                                       SDL_RENDERER_PRESENTVSYNC);
    if (!app->renderer) {
        app->renderer = SDL_CreateRenderer(app->window, -1,
                                           SDL_RENDERER_SOFTWARE);
    }
    if (!app->renderer) {
        fprintf(stderr, "libregpu: could not create a renderer: %s\n",
                SDL_GetError());
        return 0;
    }

    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    return 1;
}

static int app_setup_profiles(LibreGPUApp *app)
{
    char *preference_path;
    char fallback[LIBREGPU_PROFILE_PATH_MAX];
    const char *directory;

    preference_path = SDL_GetPrefPath("LibreGPU", "LibreGPU");
    if (preference_path) {
        directory = preference_path;
    } else {
        if (!libregpu_path_initial_directory(fallback, sizeof(fallback)))
            (void)snprintf(fallback, sizeof(fallback), "%s", ".");
        directory = fallback;
    }

    if (!libregpu_profiles_init(&app->profiles, directory)) {
        fprintf(stderr,
                "libregpu: profiles could not be loaded from %s; "
                "changes will not persist this session.\n",
                libregpu_profiles_file_path(&app->profiles));
        if (preference_path) SDL_free(preference_path);
        return 0;
    }

    if (preference_path) SDL_free(preference_path);
    return 1;
}

static void app_setup_hooks(LibreGPUApp *app, LibreGPUUIHooks *hooks)
{
    memset(hooks, 0, sizeof(*hooks));
    hooks->userdata = app;
    hooks->launch_profile = app_launch_profile;
    hooks->pick_executable = app_pick_executable;
    hooks->path_is_file = app_path_is_file;
    hooks->save_profile = app_save_profile;
    hooks->delete_profile = app_delete_profile;
    hooks->quit = app_quit;
}

static void app_cleanup(LibreGPUApp *app)
{
    if (!app) return;

    if (app->nk.ready) SDL_StopTextInput();
    libregpu_nk_sdl2_shutdown(&app->nk);

    if (app->renderer) {
        SDL_DestroyRenderer(app->renderer);
        app->renderer = NULL;
    }
    if (app->window) {
        SDL_DestroyWindow(app->window);
        app->window = NULL;
    }
    SDL_Quit();
}

int libregpu_ui_host_run(void)
{
    LibreGPUApp *app;
    LibreGPUUIHooks hooks;
    const double frame_target = 1.0 / APP_TARGET_FPS;
    double previous_time;
    int exit_code = 0;

    app = (LibreGPUApp *)calloc(1, sizeof(*app));
    if (!app) {
        fprintf(stderr, "libregpu: out of memory\n");
        return 1;
    }

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "libregpu: SDL_Init failed: %s\n", SDL_GetError());
        free(app);
        return 1;
    }

    if (!app_create_window(app)) {
        app_cleanup(app);
        free(app);
        return 1;
    }

    if (!libregpu_nk_sdl2_init(&app->nk, app->window, app->renderer)) {
        fprintf(stderr, "libregpu: could not initialize the UI renderer: %s\n",
                SDL_GetError());
        app_cleanup(app);
        free(app);
        return 1;
    }

    SDL_StartTextInput();

    memset(&app->settings, 0, sizeof(app->settings));
    libregpu_config_defaults(&app->settings.config);
    app->settings.demo_frames = 120;
    app->settings.quiet = true;

    app->running = 1;
    (void)app_setup_profiles(app);

    app_setup_hooks(app, &hooks);
    libregpu_ui_init(&app->ui, &app->settings, &hooks,
                  nk_image_id(0), 0);
    app_load_profiles(app);

    previous_time = app_now();

    while (app->running) {
        SDL_Event event;
        int output_w = 0;
        int output_h = 0;
        double frame_start;
        double delta;

        libregpu_nk_sdl2_input_begin(&app->nk);
        while (SDL_PollEvent(&event)) {
            app_handle_event(app, &event);
        }
        libregpu_nk_sdl2_input_end(&app->nk);

        if (!app->running) break;

        SDL_GetRendererOutputSize(app->renderer, &output_w, &output_h);
        if (output_w <= 0 || output_h <= 0) {
            SDL_Delay(10);
            continue;
        }

        frame_start = app_now();
        delta = frame_start - previous_time;
        previous_time = frame_start;
        if (!isfinite(delta) || delta < 0.0) delta = 0.0;
        if (delta > 0.1) delta = 0.1;

        if (app->toast_time > 0.0) {
            app->toast_time -= delta;
            if (app->toast_time <= 0.0) app->toast[0] = '\0';
        }

        {
            const float target_opacity =
                (app->picker_open || app->ui.config_open)
                    ? 1.0f : APP_ORBIT_OPACITY;
            if (target_opacity != app->window_opacity) {
                (void)SDL_SetWindowOpacity(app->window, target_opacity);
                app->window_opacity = target_opacity;
            }
        }

        SDL_SetRenderDrawColor(app->renderer, 29, 32, 38, 255);
        SDL_RenderClear(app->renderer);

        if (app->picker_open) {
            SDL_Rect full;
            full.x = 0;
            full.y = 0;
            full.w = output_w;
            full.h = output_h;
            SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 130);
            SDL_RenderFillRect(app->renderer, &full);
            app_draw_picker(app, output_w, output_h);
        } else {
            libregpu_ui_draw(&app->nk.ctx, &app->ui,
                          (float)output_w, (float)output_h,
                          (float)delta);
            if (app->toast_time > 0.0 && app->toast[0] != '\0')
                app_draw_toast(app, output_w, output_h);
        }

        libregpu_nk_sdl2_render(&app->nk);
        SDL_RenderPresent(app->renderer);

        if (app->picker_requested) {
            app->picker_requested = 0;
            app_picker_open(app);
        }

        if (libregpu_ui_quit_requested(&app->ui)) app->running = 0;

        frame_start = app_now() - frame_start;
        if (frame_start < frame_target && app->nk.ready) {
            const double remaining = frame_target - frame_start;
            const Uint32 delay_ms = (Uint32)(remaining * 1000.0);
            if (delay_ms > 0) SDL_Delay(delay_ms);
        }
    }

    app_cleanup(app);
    free(app);
    return exit_code;
}





static void print_usage(FILE *out, const char *program)
{
    fprintf(out,
            "usage: %s [--help | --version]\n"
            "\n"
            "%s %s (%s) graphical launcher\n"
            "\n"
            "Controls\n"
            "  logo click          cycle profile pages\n"
            "  +                   add a profile\n"
            "  left-click profile  launch the game\n"
            "  right-click profile delete the profile\n"
            "  X                   quit\n"
            "  Esc                 quit\n"
            "  top strip / Alt+drag move the window\n"
            "  window edges        resize the window\n"
            "  drop a file         fill the config path field\n",
            program ? program : "libregpu", LIBREGPU_CODE_NAME, LIBREGPU_VERSION, LIBREGPU_STEP_LABEL);
}

int libregpu_gui_main(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (!arg) continue;

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage(stdout, argc > 0 ? argv[0] : "libregpu");
            return 0;
        }
        if (strcmp(arg, "--version") == 0) {
            printf("%s %s (%s)\n", LIBREGPU_CODE_NAME, LIBREGPU_VERSION, LIBREGPU_STEP_LABEL);
            return 0;
        }

        fprintf(stderr, "libregpu: unknown option '%s'\n", arg);
        print_usage(stderr, argc > 0 ? argv[0] : "libregpu");
        return 2;
    }

    return libregpu_ui_host_run();
}
