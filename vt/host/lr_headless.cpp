// Headless libretro frontend: EGL (surfaceless, Mesa llvmpipe) + offscreen FBO.
// Usage: lr_headless <core.so> <game.zip> <sysdir> <frames> <outdir> [shot_every] [input_script]
// input_script lines: "<frame> <port> <button_id> <0|1>"
#include <dlfcn.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <cstdarg>
#include "libretro.h"

static retro_hw_render_callback hw{};
static GLuint fbo = 0, color = 0, depth = 0;
static int fbw = 2560, fbh = 1440;
static std::string sysdir, outdir;
static int cur_frame = 0, shot_every = 60;
static unsigned last_w = 640, last_h = 480;
static bool have_frame = false;
static std::map<int, std::vector<std::tuple<int,int,int>>> script; // frame -> (port,id,val)
static int pad[4][32];
static std::map<std::string, std::string> opts = {
    {"reicast_internal_resolution", "640x480"},
    {"reicast_threaded_rendering", "disabled"},
    {"reicast_hle_bios", "disabled"},
    {"reicast_enable_dsp", "disabled"},
};
static void load_opts_env() {
    // LR_OPTS="key=value,key=value"
    const char* e = getenv("LR_OPTS");
    if (!e) return;
    std::string s = e;
    size_t p = 0;
    while (p < s.size()) {
        size_t c = s.find(',', p); if (c == std::string::npos) c = s.size();
        std::string kv = s.substr(p, c - p);
        size_t eq = kv.find('=');
        if (eq != std::string::npos) opts[kv.substr(0, eq)] = kv.substr(eq + 1);
        p = c + 1;
    }
}

#define GLF(ret, name, args) typedef ret (*PFN_##name) args; static PFN_##name p_##name;
GLF(void, glGenFramebuffers, (GLsizei, GLuint*))
GLF(void, glBindFramebuffer, (GLenum, GLuint))
GLF(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))
GLF(void, glFramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint))
GLF(void, glGenRenderbuffers, (GLsizei, GLuint*))
GLF(void, glBindRenderbuffer, (GLenum, GLuint))
GLF(void, glRenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei))
GLF(void, glGenTextures, (GLsizei, GLuint*))
GLF(void, glBindTexture, (GLenum, GLuint))
GLF(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*))
GLF(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*))
GLF(GLenum, glCheckFramebufferStatus, (GLenum))
#define LOADGL(n) p_##n = (PFN_##n)eglGetProcAddress(#n); if (!p_##n) { fprintf(stderr, "missing " #n "\n"); exit(1); }

static void write_ppm(const char* path, const std::vector<uint8_t>& rgba, int w, int h) {
    FILE* f = fopen(path, "wb"); if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; y--)
        for (int x = 0; x < w; x++) fwrite(&rgba[(y * w + x) * 4], 1, 3, f);
    fclose(f);
}

static void log_cb(enum retro_log_level level, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "[core%d] ", (int)level); vfprintf(stderr, fmt, ap);
    va_end(ap);
}
static uintptr_t get_fb() { return fbo; }
static retro_proc_address_t get_proc(const char* s) { return (retro_proc_address_t)eglGetProcAddress(s); }

static bool env_cb(unsigned cmd, void* data) {
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((retro_log_callback*)data)->log = log_cb; return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char**)data = sysdir.c_str(); return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return true;
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER: *(unsigned*)data = RETRO_HW_CONTEXT_OPENGL_CORE; return true;
    case RETRO_ENVIRONMENT_SET_HW_RENDER: {
        auto* h = (retro_hw_render_callback*)data;
        if (h->context_type != RETRO_HW_CONTEXT_OPENGL_CORE && h->context_type != RETRO_HW_CONTEXT_OPENGL) return false;
        hw = *h;
        h->get_current_framebuffer = get_fb;
        h->get_proc_address = get_proc;
        hw.get_current_framebuffer = get_fb;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        auto* v = (retro_variable*)data;
        auto it = opts.find(v->key);
        v->value = it == opts.end() ? nullptr : it->second.c_str();
        if (getenv("LR_LOGVARS")) fprintf(stderr, "VAR %s = %s\n", v->key, v->value ? v->value : "(null)");
        return it != opts.end();
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool*)data = false; return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS: return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: *(unsigned*)data = 0; return true;
    default: return false;
    }
}

static void video_cb(const void* data, unsigned w, unsigned h, size_t) {
    last_w = w; last_h = h; have_frame = data != nullptr;
    if (!data || shot_every <= 0 || (cur_frame % shot_every) != 0) return;
    std::vector<uint8_t> px(w * h * 4);
    if (data == RETRO_HW_FRAME_BUFFER_VALID) {
        p_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        p_glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    } else return;
    char path[512]; snprintf(path, sizeof path, "%s/frame_%05d.ppm", outdir.c_str(), cur_frame);
    write_ppm(path, px, w, h);
}
static void audio_cb(int16_t, int16_t) {}
static size_t audio_batch_cb(const int16_t*, size_t f) { return f; }
static void input_poll_cb() {
    auto it = script.find(cur_frame);
    if (it != script.end()) for (auto& [p, id, v] : it->second) pad[p][id] = v;
}
static int16_t input_state_cb(unsigned port, unsigned device, unsigned, unsigned id) {
    if (port >= 4 || device != RETRO_DEVICE_JOYPAD) return 0;
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) { int16_t m = 0; for (int i = 0; i < 16; i++) if (pad[port][i]) m |= 1 << i; return m; }
    return id < 32 ? pad[port][id] : 0;
}

int main(int argc, char** argv) {
    if (argc < 6) { fprintf(stderr, "usage: %s core.so game sysdir frames outdir [shot_every] [script]\n", argv[0]); return 1; }
    sysdir = argv[3]; outdir = argv[5];
    load_opts_env();
    int frames = atoi(argv[4]);
    if (argc > 6) shot_every = atoi(argv[6]);
    if (argc > 7) { FILE* f = fopen(argv[7], "r"); int a, b, c, d; while (f && fscanf(f, "%d %d %d %d", &a, &b, &c, &d) == 4) script[a].push_back({b, c, d}); if (f) fclose(f); }

    // EGL surfaceless
    auto getPlat = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy = getPlat ? getPlat(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr) : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint maj, min;
    if (!eglInitialize(dpy, &maj, &min)) { fprintf(stderr, "eglInitialize failed\n"); return 1; }
    eglBindAPI(EGL_OPENGL_API);
    EGLint cfgattr[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE};
    EGLConfig cfg; EGLint n = 0;
    eglChooseConfig(dpy, cfgattr, &cfg, 1, &n);
    EGLint ctxattr[] = {EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 3,
                        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, n ? cfg : EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ctxattr);
    if (ctx == EGL_NO_CONTEXT) { fprintf(stderr, "eglCreateContext failed 0x%x\n", eglGetError()); return 1; }
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
    LOADGL(glGenFramebuffers) LOADGL(glBindFramebuffer) LOADGL(glFramebufferTexture2D) LOADGL(glFramebufferRenderbuffer)
    LOADGL(glGenRenderbuffers) LOADGL(glBindRenderbuffer) LOADGL(glRenderbufferStorage) LOADGL(glGenTextures)
    LOADGL(glBindTexture) LOADGL(glTexImage2D) LOADGL(glReadPixels) LOADGL(glCheckFramebufferStatus)
    p_glGenTextures(1, &color); p_glBindTexture(GL_TEXTURE_2D, color);
    p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fbw, fbh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    p_glGenRenderbuffers(1, &depth); p_glBindRenderbuffer(GL_RENDERBUFFER, depth);
    p_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, fbw, fbh);
    p_glGenFramebuffers(1, &fbo); p_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);
    fprintf(stderr, "FBO status 0x%x\n", p_glCheckFramebufferStatus(GL_FRAMEBUFFER));

    void* so = dlopen(argv[1], RTLD_NOW);
    if (!so) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
#define SYM(n) auto n = (decltype(&::n))dlsym(so, #n)
    SYM(retro_set_environment); SYM(retro_set_video_refresh); SYM(retro_set_audio_sample);
    SYM(retro_set_audio_sample_batch); SYM(retro_set_input_poll); SYM(retro_set_input_state);
    SYM(retro_init); SYM(retro_load_game); SYM(retro_run); SYM(retro_deinit); SYM(retro_unload_game);
    SYM(retro_get_memory_data); SYM(retro_get_memory_size);
    retro_set_environment(env_cb);
    retro_set_video_refresh(video_cb); retro_set_audio_sample(audio_cb);
    retro_set_audio_sample_batch(audio_batch_cb); retro_set_input_poll(input_poll_cb);
    retro_set_input_state(input_state_cb);
    retro_init();
    retro_game_info gi{argv[2], nullptr, 0, nullptr};
    if (!retro_load_game(&gi)) { fprintf(stderr, "load_game failed\n"); return 1; }
    if (hw.context_reset) hw.context_reset();
    FILE* hashlog = getenv("LR_RAMHASH") ? fopen(getenv("LR_RAMHASH"), "w") : nullptr;
    for (cur_frame = 0; cur_frame < frames; cur_frame++) {
        retro_run();
        if (getenv("LR_RAMDUMP_EVERY") && cur_frame % atoi(getenv("LR_RAMDUMP_EVERY")) == 0) {
            const uint8_t* ram = (const uint8_t*)retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
            char fn[512]; snprintf(fn, sizeof(fn), "%s/ram_%05d.bin", outdir.c_str(), cur_frame);
            FILE* df = fopen(fn, "wb");
            if (df && ram) { fwrite(ram, 1, retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM), df); fclose(df); }
        }
        if (getenv("LR_RAMDUMP_FRAME") && atoi(getenv("LR_RAMDUMP_FRAME")) == cur_frame) {
            const uint8_t* ram = (const uint8_t*)retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
            FILE* df = fopen(getenv("LR_RAMDUMP_FILE"), "wb");
            if (df && ram) { fwrite(ram, 1, retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM), df); fclose(df); }
        }
        if (hashlog) {
            const uint8_t* ram = (const uint8_t*)retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
            size_t sz = retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
            uint64_t h = 1469598103934665603ull;
            for (size_t i = 0; ram && i < sz; i += 8) { uint64_t v; memcpy(&v, ram + i, 8); h = (h ^ v) * 1099511628211ull; }
            fprintf(hashlog, "%d %016llx %zu\n", cur_frame, (unsigned long long)h, sz);
        }
    }
    if (hashlog) fclose(hashlog);
    fprintf(stderr, "done %d frames, last %ux%u\n", frames, last_w, last_h);
    retro_unload_game();
    retro_deinit();
    return 0;
}
