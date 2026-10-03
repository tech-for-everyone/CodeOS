// A live X11 window showing ArkUI's *real* layout, re-run on every resize.
//
// WHY THIS EXISTS
// ---------------
// tree_probe asserts the engine's geometry and writes a PPM. This demo turns the
// same layout into a running window: the engine lays the tree out at the window's
// current size, the geometry is rasterised to a 32-bit surface, and the surface is
// blitted with XPutImage. On ConfigureNotify the window size changes and the
// engine is asked to lay the tree out again -- a full layout -> pixels -> event ->
// layout loop, with the layout done by ArkUI.
//
// WHAT IS REAL AND WHAT IS A TEST DOUBLE
// --------------------------------------
// Same split as tree_probe, and the same header (ace_scene.hpp): the algorithm is
// the engine's LinearLayoutUtils, the LayoutWrapper is a test double (no
// FrameNode, so no service core), and the rasteriser is ours because the engine's
// paint path is the absent Skia/Rosen backend. This window proves the *layout* is
// live; it does not claim the engine painted the pixels.
//
// Usage:
//   ace_window_demo [--size WxH] [--frames N] [--screenshot FILE]
//     --frames N        present N times and exit (headless / Xvfb testing)
//     --screenshot FILE write the last presented surface as a PPM
// With no --frames the window stays up until closed (Escape/q or WM close).

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "ace_visual.hpp"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

using namespace OHOS::Ace;
using namespace OHOS::Ace::NG;

namespace {

// The scene (the tree, the palette, the engine-painted divider) is shared with
// the GNUstep view and lives in ace_visual.hpp, so the two shells cannot drift
// into showing different content and the pixel controls measure one definition.
using Scene = acehost::VisualScene;

int ParseSize(const char* spec, int* w, int* h)
{
    return std::sscanf(spec, "%dx%d", w, h) == 2 && *w > 0 && *h > 0;
}

void HandleEvent(XEvent& e, Atom wmDelete, bool* done, bool* dirty, int* w, int* h)
{
    switch (e.type) {
        case ConfigureNotify:
            if (e.xconfigure.width != *w || e.xconfigure.height != *h) {
                *w = e.xconfigure.width;
                *h = e.xconfigure.height;
                *dirty = true;
            }
            break;
        case Expose:
            if (e.xexpose.count == 0) {
                *dirty = true;
            }
            break;
        case KeyPress: {
            const KeySym ks = XLookupKeysym(&e.xkey, 0);
            if (ks == XK_Escape || ks == XK_q || ks == XK_Q) {
                *done = true;
            }
            break;
        }
        case ClientMessage:
            if (static_cast<Atom>(e.xclient.data.l[0]) == wmDelete) {
                *done = true;
            }
            break;
        default:
            break;
    }
}

// Lay the engine's tree out at w x h, rasterise it, and blit it to the window.
void Present(Display* dpy, ::Window win, GC gc, Visual* visual, int depth, Scene& scene, int w, int h,
    acehost::Surface& surface)
{
    // The engine lays the tree out and its paint code draws the divider; the
    // raster is ours. Shared with the GNUstep view via ace_visual.hpp.
    acehost::RenderVisualScene(scene, w, h, surface);

    XImage* img = XCreateImage(dpy, visual, static_cast<unsigned int>(depth), ZPixmap, 0,
        reinterpret_cast<char*>(surface.Data()), static_cast<unsigned int>(w), static_cast<unsigned int>(h), 32, 0);
    if (img == nullptr) {
        std::fprintf(stderr, "XCreateImage failed\n");
        return;
    }
    XPutImage(dpy, win, gc, img, 0, 0, 0, 0, static_cast<unsigned int>(w), static_cast<unsigned int>(h));
    XFlush(dpy);
    // The XImage borrowed the surface's buffer; make sure destroying it does not
    // free memory the surface still owns.
    img->data = nullptr;
    XDestroyImage(img);

    const auto rootSize = scene.root->GetGeometryNode()->GetFrameSize();
    const auto pctSize = scene.pct->GetGeometryNode()->GetFrameSize();
    const auto divSize = scene.div->GetGeometryNode()->GetFrameSize();
    std::printf("  present %dx%d: root=%.0fx%.0f pct=%.0fx%.0f div=%.0fx%.0f\n", w, h, rootSize.Width(),
        rootSize.Height(), pctSize.Width(), pctSize.Height(), divSize.Width(), divSize.Height());
}

} // namespace

int main(int argc, char* argv[])
{
    int w = 360;
    int h = 640;
    int frames = 0;
    const char* screenshot = nullptr;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            if (!ParseSize(argv[++i], &w, &h)) {
                std::fprintf(stderr, "bad --size, want WxH\n");
                return 2;
            }
        } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshot = argv[++i];
        } else if (std::strcmp(argv[i], "--help") == 0) {
            std::printf("usage: ace_window_demo [--size WxH] [--frames N] [--screenshot FILE]\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 2;
        }
    }

    std::printf("ace window demo: ArkUI layout in an X11 window\n");
    Scene scene = acehost::BuildVisualScene();

    Display* dpy = XOpenDisplay(nullptr);
    if (dpy == nullptr) {
        std::fprintf(stderr, "cannot open X display (is DISPLAY set? try: xvfb-run -a %s)\n", argv[0]);
        return 3;
    }
    const int screen = DefaultScreen(dpy);
    Visual* visual = DefaultVisual(dpy, screen);
    const int depth = DefaultDepth(dpy, screen);
    ::Window win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 0, 0, static_cast<unsigned int>(w),
        static_cast<unsigned int>(h), 0, BlackPixel(dpy, screen), WhitePixel(dpy, screen));
    XStoreName(dpy, win, "ArkUI layout (ace-host)");
    XSelectInput(dpy, win, ExposureMask | StructureNotifyMask | KeyPressMask);
    Atom wmDelete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wmDelete, 1);
    XMapWindow(dpy, win);
    GC gc = XCreateGC(dpy, win, 0, nullptr);

    acehost::Surface surface(w, h, acehost::kVisualBackground);
    bool done = false;
    bool dirty = true;
    int presents = 0;

    if (frames > 0) {
        // Non-interactive: present --frames times, draining any events that
        // arrived, so the same binary can be checked under Xvfb.
        for (int i = 0; i < frames && !done; ++i) {
            while (XPending(dpy) > 0) {
                XEvent e;
                XNextEvent(dpy, &e);
                HandleEvent(e, wmDelete, &done, &dirty, &w, &h);
            }
            Present(dpy, win, gc, visual, depth, scene, w, h, surface);
            presents++;
        }
    } else {
        Present(dpy, win, gc, visual, depth, scene, w, h, surface);
        presents++;
        while (!done) {
            XEvent e;
            XNextEvent(dpy, &e);
            HandleEvent(e, wmDelete, &done, &dirty, &w, &h);
            if (dirty && !done) {
                Present(dpy, win, gc, visual, depth, scene, w, h, surface);
                presents++;
                dirty = false;
            }
        }
    }

    if (screenshot != nullptr && surface.WritePpm(screenshot)) {
        std::printf("  wrote %s (%dx%d)\n", screenshot, w, h);
    } else if (screenshot != nullptr) {
        std::fprintf(stderr, "could not write %s\n", screenshot);
    }

    std::printf("  %d frame(s) presented, exiting\n", presents);

    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
