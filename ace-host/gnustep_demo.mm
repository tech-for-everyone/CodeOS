// A GNUstep (AppKit) window that hosts the ArkUI layout engine: `ArkUIView` is an
// NSView whose content is laid out and painted by ace_engine, and whose resize /
// mouse / key events drive fresh engine work.
//
// WHY THIS EXISTS
// ---------------
// Stage 3a put the engine's layout in a raw X11 window. Stage 3b is the shell
// integration: GNUstep owns the application, window and event loop (NSApplication,
// NSWindow), while ArkUI owns the content. `ArkUIView` is the seam between them --
// it is the `ArkUIView : NSView` box in the README's target architecture.
//
// The engine work is the same code the X11 demo runs: `BuildVisualScene` /
// `RenderVisualScene` from ace_visual.hpp, i.e. the real `LinearLayoutUtils` for
// layout and a real `DividerModifier`/`DividerPainter` for paint. Nothing about
// the engine is reimplemented here; this file only adapts GNUstep (NSView bounds,
// NSBitmapImageRep, NSResponder events) to that scene.
//
// The surface carries 0x00RRGGBB pixels; `drawRect:` repacks them into an
// NSBitmapImageRep and draws it. `writePpmFromSurface:` writes the same surface,
// so a headless run can check exactly the pixels the view rendered.
//
// Usage:
//   ace_gnustep_demo [--size WxH] [--screenshot FILE]
//     --screenshot FILE  render one frame through the view and write it as PPM,
//                        then exit (headless / Xvfb testing)
//   with no --screenshot the window stays up: drag/resize relayouts, click or
//   space toggles the engine-painted divider's width, q/Escape quits.
//
// Build/link: this file is Objective-C++ and links gnustep-gui, gnustep-base and
// objc. It is only built when `gnustep-config` and the GUI kit are found (see
// CMakeLists.txt), so the rest of the build is unaffected on a host without them.

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ace_visual.hpp"

// The three widths the divider toggles between on click / space. The first is
// the default from ace_visual.hpp; changing it exercises the full loop
// "GNUstep event -> engine relayout -> engine paint -> NSView redraw".
static const float kDividerFractions[] = { 0.8f, 0.4f, 0.2f };
static const int kDividerFractionCount = 3;

@interface ArkUIView : NSView {
    acehost::VisualScene* _scene;
    acehost::Surface* _surface;
    int _w;
    int _h;
    int _renders; // renderScene calls (engine layouts)
    int _draws;   // drawRect calls (AppKit draw path)
    int _fractionIndex;
}
- (void)renderScene;
- (void)toggleDividerWidth;
- (BOOL)writePpmFromSurface:(const char*)path;
- (int)renderCount;
- (int)drawCount;
@end

@implementation ArkUIView

- (instancetype)initWithFrame:(NSRect)frameRect
{
    self = [super initWithFrame:frameRect];
    if (self != nil) {
        _scene = new acehost::VisualScene(acehost::BuildVisualScene());
        _surface = nullptr;
        _w = (int)frameRect.size.width;
        _h = (int)frameRect.size.height;
        _renders = 0;
        _draws = 0;
        _fractionIndex = 0;
    }
    return self;
}

- (void)dealloc
{
    delete _surface;
    delete _scene;
    [super dealloc];
}

// Match the engine's coordinate system (origin top-left) so the surface and the
// NSView agree on where y = 0 is.
- (BOOL)isFlipped
{
    return YES;
}

- (BOOL)acceptsFirstResponder
{
    return YES;
}

// Run the engine at the view's current size and turn the result into pixels.
- (void)renderScene
{
    const NSSize size = [self bounds].size;
    _w = (int)size.width;
    _h = (int)size.height;
    if (_w <= 0 || _h <= 0) {
        return;
    }
    delete _surface;
    _surface = new acehost::Surface(_w, _h, acehost::kVisualBackground);
    acehost::RenderVisualScene(*_scene, _w, _h, *_surface);
    _renders++;
}

// AppKit's draw entry point. It calls the engine, then blits the surface by
// repacking our 0x00RRGGBB pixels into an NSBitmapImageRep.
- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    [self renderScene];
    _draws++;

    if (_surface == nullptr || _w <= 0 || _h <= 0) {
        return;
    }
    NSBitmapImageRep* rep = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:NULL
                     pixelsWide:_w
                     pixelsHigh:_h
                  bitsPerSample:8
                samplesPerPixel:3
                       hasAlpha:NO
                       isPlanar:NO
                 colorSpaceName:NSDeviceRGBColorSpace
                    bytesPerRow:_w * 3
                   bitsPerPixel:24];
    if (rep == nil) {
        return;
    }
    unsigned char* dst = [rep bitmapData];
    const uint32_t* src = _surface->Data();
    if (dst != NULL) {
        for (int i = 0; i < _w * _h; ++i) {
            const uint32_t px = src[i];
            dst[i * 3 + 0] = (unsigned char)((px >> 16) & 0xffu);
            dst[i * 3 + 1] = (unsigned char)((px >> 8) & 0xffu);
            dst[i * 3 + 2] = (unsigned char)(px & 0xffu);
        }
        [rep drawInRect:[self bounds]];
    }
    [rep release];

    const auto rootSize = _scene->root->GetGeometryNode()->GetFrameSize();
    const auto pctSize = _scene->pct->GetGeometryNode()->GetFrameSize();
    const auto divSize = _scene->div->GetGeometryNode()->GetFrameSize();
    std::printf("  draw %dx%d (render #%d): root=%.0fx%.0f pct=%.0fx%.0f div=%.0fx%.0f\n", _w, _h, _renders,
        rootSize.Width(), rootSize.Height(), pctSize.Width(), pctSize.Height(), divSize.Width(), divSize.Height());
}

// A resize is a new constraint for the engine, so mark the view dirty and let
// drawRect: run the layout again at the new size.
- (void)setFrameSize:(NSSize)newSize
{
    [super setFrameSize:newSize];
    [self setNeedsDisplay:YES];
}

// Cycle the engine-painted divider's width. The next drawRect: asks the engine to
// lay the tree out again, and the divider follows the engine's new rectangle.
- (void)toggleDividerWidth
{
    _fractionIndex = (_fractionIndex + 1) % kDividerFractionCount;
    _scene->div->SetPercentWidth(kDividerFractions[_fractionIndex]);
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    (void)event;
    [self toggleDividerWidth];
}

- (void)keyDown:(NSEvent*)event
{
    NSString* chars = [event characters];
    if ([chars length] == 0) {
        return;
    }
    const unichar c = [chars characterAtIndex:0];
    if (c == ' ' || c == '\r') {
        [self toggleDividerWidth];
    } else if (c == 'q' || c == 'Q' || c == 0x1b) {
        [NSApp terminate:nil];
    }
}

- (BOOL)writePpmFromSurface:(const char*)path
{
    if (_surface == nullptr) {
        return NO;
    }
    return _surface->WritePpm(path) ? YES : NO;
}

- (int)renderCount
{
    return _renders;
}

- (int)drawCount
{
    return _draws;
}

@end

static int ParseSize(const char* spec, int* w, int* h)
{
    return std::sscanf(spec, "%dx%d", w, h) == 2 && *w > 0 && *h > 0;
}

int main(int argc, const char** argv)
{
    int w = 360;
    int h = 640;
    const char* screenshot = nullptr;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            if (!ParseSize(argv[++i], &w, &h)) {
                std::fprintf(stderr, "bad --size, want WxH\n");
                return 2;
            }
        } else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshot = argv[++i];
        } else if (std::strcmp(argv[i], "--help") == 0) {
            std::printf("usage: ace_gnustep_demo [--size WxH] [--screenshot FILE]\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 2;
        }
    }

    @autoreleasepool {
        @try {
            [NSApplication sharedApplication];

            const NSRect frame = NSMakeRect(0, 0, w, h);
            const unsigned int style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable;
            NSWindow* win = [[NSWindow alloc] initWithContentRect:frame
                                                        styleMask:style
                                                          backing:NSBackingStoreBuffered
                                                            defer:NO];
            if (win == nil) {
                std::fprintf(stderr, "could not create NSWindow (is a display available? try xvfb-run)\n");
                return 3;
            }
            [win setTitle:@"ArkUI layout (ace-host / GNUstep)"];

            ArkUIView* view = [[ArkUIView alloc] initWithFrame:frame];
            [win setContentView:view];
            [win makeFirstResponder:view];
            [win makeKeyAndOrderFront:nil];
            [NSApp activateIgnoringOtherApps:YES];

            std::printf("ace gnustep demo: ArkUI layout in a GNUstep window\n");

            if (screenshot != nullptr) {
                // Drive the real AppKit draw path, then dump the surface it
                // produced. If the display machinery did not call drawRect: (e.g.
                // a headless backend), fall back to rendering directly so the dump
                // still happens, and report both counts so a caller can tell which
                // path ran.
                [view display];
                if ([view renderCount] == 0) {
                    [view renderScene];
                }
                const BOOL wrote = [view writePpmFromSurface:screenshot];
                std::printf("  wrote %s (%dx%d): renders=%d draws=%d\n", screenshot, w, h, [view renderCount],
                    [view drawCount]);
                [view release];
                [win release];
                if (!wrote) {
                    std::fprintf(stderr, "could not write %s\n", screenshot);
                    return 4;
                }
                return 0;
            }

            [NSApp run];
            [view release];
            [win release];
        } @catch (NSException* e) {
            // The GUI needs its drawing backend (the gnustep-back bundle); without
            // it NSApplication cannot initialise. Say so plainly and use a distinct
            // exit code, rather than relying on the runtime's uncaught-exception
            // output.
            std::fprintf(stderr, "GNUstep could not start (is gnustep-back installed?): %s: %s\n",
                [[e name] UTF8String], [[e reason] UTF8String]);
            return 5;
        }
    }
    return 0;
}
