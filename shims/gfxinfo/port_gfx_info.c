/*
 * libPortGfxInfo.so — runtime helper for the Disco Elysium native Linux port.
 *
 * Why this exists: Unity 2020.3's GLCore backend
 * computes SystemInfo.graphicsMemorySize through a fallback chain of three GL
 * queries, where each executed query overwrites the previous value:
 *
 *   1. GL_NVX_gpu_memory_info   -> GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX (total, KB)
 *   2. GL_ATI_meminfo           -> GL_RENDERBUFFER_FREE_MEMORY_ATI        (FREE, KB)
 *   3. GLX_MESA_query_renderer  -> GLX_RENDERER_VIDEO_MEMORY_MESA         (total, MB)
 *
 * Step 3 is gated behind the engine's extension registry, which is built from
 * the GL extension strings only; GLX extension names never get registered, so
 * step 3 never runs on Mesa.  The value the game sees is therefore the ATI
 * *free renderbuffer* reading (a moving quantity, e.g. ~200 MB on a Renoir
 * iGPU) instead of the advertised 1024 MB, and the game's low-VRAM heuristics
 * misfire on UMA systems.
 *
 * This library performs the query the engine fails to make.  It needs no GL
 * context (glXQueryRendererIntegerMESA takes display/screen/renderer
 * explicitly).  Results are cached after the first call.  On any failure the
 * functions return 0 / -1 so the caller can fall back to the engine value.
 *
 * Build/install: scripts/build-shims.sh (installs shims/libPortGfxInfo.so;
 * run.sh puts shims/ on LD_LIBRARY_PATH).
 */
#include <string.h>
#include <X11/Xlib.h>
#include <GL/glx.h>

typedef Bool (*PFNGLXQUERYRENDERERINTEGERMESA)(Display *dpy, int screen, int renderer,
                                               int attribute, unsigned int *value);

static int g_queried;      /* 0 = not yet, 1 = done */
static int g_video_mb;     /* 0 = unknown */
static int g_unified;      /* 1 = yes, 0 = no, -1 = unknown */

static void port_gfx_query(void)
{
    Display *dpy;
    int screen;
    const char *exts;
    PFNGLXQUERYRENDERERINTEGERMESA query;
    int renderer;
    unsigned int best_mb = 0;
    unsigned int best_unified = 0;

    g_video_mb = 0;
    g_unified = -1;

    dpy = XOpenDisplay(NULL);
    if (!dpy)
        return;
    screen = DefaultScreen(dpy);

    exts = glXQueryExtensionsString(dpy, screen);
    if (!exts || !strstr(exts, "GLX_MESA_query_renderer"))
    {
        XCloseDisplay(dpy);
        return;
    }

    query = (PFNGLXQUERYRENDERERINTEGERMESA)glXGetProcAddress(
        (const GLubyte *)"glXQueryRendererIntegerMESA");
    if (!query)
    {
        XCloseDisplay(dpy);
        return;
    }

    /* Renderer 0 is the screen's default renderer; if it does not answer,
     * scan the remaining indices.  (On a hybrid-GPU X screen this picks the
     * default renderer, which is what an incidentally created GLX context
     * would use too.) */
    for (renderer = 0; renderer < 16; renderer++)
    {
        unsigned int mb = 0;
        unsigned int unified = 0;

        if (!query(dpy, screen, renderer, GLX_RENDERER_VIDEO_MEMORY_MESA, &mb))
        {
            if (renderer == 0)
                continue;   /* index 0 not usable; keep scanning */
            break;          /* indices are dense: end of list */
        }
        query(dpy, screen, renderer, GLX_RENDERER_UNIFIED_MEMORY_ARCHITECTURE_MESA, &unified);
        if (mb > 0)
        {
            best_mb = mb;
            best_unified = unified;
            break;          /* first usable renderer wins */
        }
    }

    XCloseDisplay(dpy);

    if (best_mb > 0)
    {
        g_video_mb = (int)best_mb;
        g_unified = (int)best_unified;
    }
}

/* Advertised video memory in MB, 0 when unknown. */
int port_gfx_video_memory_mb(void)
{
    if (!g_queried)
    {
        port_gfx_query();
        g_queried = 1;
    }
    return g_video_mb;
}

/* 1 when the renderer uses unified (shared) memory, 0 when dedicated,
 * -1 when unknown. */
int port_gfx_unified_memory(void)
{
    if (!g_queried)
    {
        port_gfx_query();
        g_queried = 1;
    }
    return g_unified;
}
