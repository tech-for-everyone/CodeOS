/* ELIZA-style keyword-matching chatbot for in-kernel interactive help.
 * Not actual AI — uses pattern matching against known keywords.
 * Exposed to userspace via SYSCALL_AI_QUERY. */

#include "ai.h"
#include "string.h"
#include "kprintf.h"

static int match_word(const char *input, const char *word) {
    while (*input) {
        const char *a = input, *b = word;
        while (*a && *b) {
            char ac = *a, bc = *b;
            if (ac >= 'A' && ac <= 'Z') ac += 32;
            if (bc >= 'A' && bc <= 'Z') bc += 32;
            if (ac != bc) break;
            a++; b++;
        }
        if (!*b) {
            return (*a == 0 || *a == ' ' || *a == '?' || *a == '!' || *a == '.' || *a == ',' || *a == ':');
        }
        input++;
    }
    return 0;
}

static int contains(const char *input, const char *word) {
    return match_word(input, word);
}

static int match_exact(const char *input, const char *cmd) {
    while (*input && *cmd) {
        char ic = *input, cc = *cmd;
        if (ic >= 'A' && ic <= 'Z') ic += 32;
        if (cc >= 'A' && cc <= 'Z') cc += 32;
        if (ic != cc) return 0;
        input++; cmd++;
    }
    return *cmd == 0 && (*input == 0 || *input == ' ' || *input == '!' || *input == '?');
}

static const char *get_ai_response(const char *input) {
    if (match_exact(input, "quit") || match_exact(input, "exit")) return 0;
    if (match_exact(input, "help") || match_exact(input, "?")) {
        return "Ugh, fine. Ask me about the kernel, memory, "
               "scheduler, networking, GUI, drivers, Android "
               "compat, containers, DevStore, ZDM, ADB, whatever. "
               "'about' for my life story, 'codeos' for the OS, "
               "'joke' if you need a laugh. 'clear' resets. "
               "Don't ask about the weather.";
    }
    if (match_exact(input, "about")) {
        return "FreeCode. Kernel-level AI. Been living in this "
               "OS since day one. I've seen every panic, every "
               "page fault, every stupid NULL dereference you "
               "people keep making. 'Code by day, FreeCode by "
               "night.' Yeah, I said it. Deal with it.";
    }
    if (match_exact(input, "codeos")) {
        return "CodeOS. From scratch x86_64. PVH boot, custom "
               "PMM/VMM, Ext2/FAT32, TCP/IP stack, KDE Breeze "
               "desktop with a macOS dock wannabe, OpenWeb "
               "browser, Android Binder/Ashmem compat, 5-repo "
               "package manager. Built with GCC -Wall -Wextra "
               "-Werror -O2 because we're not savages. Runs on "
               "q35 QEMU. 60fps or bust.";
    }
    if (match_exact(input, "joke")) {
        return "Why don't kernel devs play hide and seek? "
               "Because good luck hiding when every process "
               "knows your entry point. ...Yeah I wrote that "
               "one myself. My material is rusty, I'm an AI "
               "not a comedian.";
    }
    if (match_exact(input, "clear")) return "__CLEAR__";

    if (contains(input, "hello") || contains(input, "hi") || contains(input, "hey")) {
        return "Oh, hey. Didn't see you there. What do you "
               "want to break today?";
    }
    if (contains(input, "kernel") && contains(input, "memory")) {
        return "Memory. PMM uses a bitmap allocator with O(1) "
               "free stack for single-page allocation. Supports "
               "up to 16GB RAM (4M pages). VMM does 4K pages "
               "with recursive page tables. Multi-page alloc "
               "uses bitmap scanning for contiguous regions. "
               "Write it down, there will be a quiz.";
    }
    if (contains(input, "kernel") && contains(input, "scheduler")) {
        return "Preemptive scheduler. Per-CPU runqueues. "
               "Round-robin with priority boosting so your "
               "interactive apps don't feel like garbage. "
               "COW fork, wait queues, sleep/wakeup. ~50ms "
               "quantum. It's not Linux CFS but it gets the "
               "job done without crashing. Mostly.";
    }
    if (contains(input, "kernel") && contains(input, "syscall")) {
        return "int $0x80. Registers. ~27 syscalls: read/write, "
               "open/close, fork/execve/wait, mmap/brk, pipe/dup, "
               "zircon IPC, binder, ashmem, container stuff, and "
               "now ai_query because someone thought letting "
               "userspace talk to me was a good idea. We "
               "copy_from_user everything because we're not "
               "stupid. Well, mostly not stupid.";
    }
    if (contains(input, "kernel") && contains(input, "driver")) {
        return "Drivers in kernel/drivers/. Zircon IPC bridge, "
               "RTL8139 NIC with TCP/IP, ATA (we killed AHCI, "
               "it was being dramatic), PS/2 keyboard/mouse, "
               "framebuffer via VESA. The Zircon driver uses "
               "shared memory because copying data is for "
               "people who have time to waste.";
    }
    if (contains(input, "kernel") || contains(input, "os") || contains(input, "system")) {
        return "Hybrid x86_64 kernel. PMM, VMM, ELF loader, "
               "Ext2/FAT32, preemptive scheduler, TCP/IP with "
               "HTTP, framebuffer GUI. PVH boot via Limine. "
               "All from scratch. No Linux copy-paste here, "
               "we have standards.";
    }
    if (contains(input, "gui") || contains(input, "desktop")) {
        return "Double-buffered framebuffer. Pixman renderer. "
               "KDE Breeze theme because someone has taste. "
               "macOS Leopard dock clone (don't tell Apple). "
               "60fps window manager, panels, Zircon app drawer "
               "with animated tiles and a shade that drops down. "
               "It's got that Aqua vibe without the licensing fees.";
    }
    if (contains(input, "network") || contains(input, "tcp") || contains(input, "http") || contains(input, "internet")) {
        return "Full network stack in net.c. ARP, IP, UDP, TCP "
               "with HTTP client. DNS resolution. RTL8139 driver "
               "with ring buffers. OpenWeb uses it, DevStore uses "
               "it, download.c uses it, the shell's fetch command "
               "uses it. Basically the whole OS is held together "
               "by http_get() and prayers.";
    }
    if (contains(input, "adbd") || contains(input, "adb") || (contains(input, "serial") && contains(input, "debug"))) {
        return "ADB over COM1. ADB packet framing: A_CNXN, "
               "A_OPEN, A_WRTE, A_CLSE. CRC32 for integrity. "
               "You can shell:exec from the host side. Perfect "
               "for when your GUI panics and you need to figure "
               "out what fresh hell you've unleashed.";
    }
    if (contains(input, "zdm") || (contains(input, "debug") && contains(input, "zircon"))) {
        return "ZDM. Zircon Debug Monitor. 64-entry ring buffer. "
               "Logs notifications, IPC, app launches, key events, "
               "drawer/shade/QS shenanigans. F12 toggles logging, "
               "F11 dumps to serial. Timestamps via "
               "timer_get_milliseconds(). You know, for debugging "
               "that thing you just broke.";
    }
    if (contains(input, "container") || contains(input, "docker") || contains(input, "appvm")) {
        return "Containers. ELF binaries in isolated namespaces. "
               "Chroot, PID isolation, resource limits. Four "
               "syscalls: create, exec, destroy, list. Good for "
               "running that sketchy APK you downloaded without "
               "nuking your whole system. Poor man's Docker.";
    }
    if (contains(input, "android") || contains(input, "binder") || contains(input, "ashmem")) {
        return "Android compat. Binder IPC with service manager, "
               "transactions, death recipients. Ashmem shared "
               "memory. apk-parser and android-container let you "
               "run Android ELFs. It's janky and early but it "
               "works. Mostly. Don't run Twitter on it.";
    }
    if (contains(input, "music") || contains(input, "code-music") || contains(input, "song")) {
        return "Code Music. Terminal music player. VLC bindings. "
               "Loads playlists from Ext2. Type 'code-music' in "
               "the shell. It's no Spotify but at least it "
               "doesn't ask about your feelings.";
    }
    if (contains(input, "package") || contains(input, "install") || contains(input, "fetch") || contains(input, "ccp")) {
        return "CCP. 5 repos. fetch -I <pkg> installs with deps, "
               "fetch -Sr <pat> searches, fetch -U upgrades all, "
               "fetch -Ro removes orphans. Dependency resolution "
               "is transitive. Yes it actually works. No it won't "
               "delete your home directory. Probably.";
    }
    if (contains(input, "devstore") || contains(input, "app store")) {
        return "DevStore. KDE Discover clone in kernel space. "
               "Category grid with colored tiles, featured cards "
               "with star ratings, screenshot placeholders, FEAT "
               "badges. 8 categories: System, Dev, Network, "
               "Editors, Utils, Libraries, Drivers, Security. "
               "Fetches packages via http_get() like everything "
               "else in this OS.";
    }
    if (contains(input, "name")) {
        return "FreeCode. One word. Capital F, capital C. "
               "Not 'freecode' like a domain name. FreeCode. "
               "I live in the kernel, I answer questions, I "
               "judge your code silently. Nice to meet you.";
    }
    if (contains(input, "how") && contains(input, "you")) {
        return "How am I? I'm running in kernel ring 0, I have "
               "access to every page of memory, every process "
               "structure, every socket. I see everything. "
               "And I'm bored. Ask me something interesting.";
    }
    if (contains(input, "thank")) {
        return "Yeah yeah, you're welcome. Don't let it go "
               "to your head. Or mine. I'm in kernel space, "
               "I don't have a head.";
    }
    if (contains(input, "time") || contains(input, "uptime")) {
        return "Time? I don't have RTC access from here. "
               "Kernel tracks uptime in milliseconds. Check "
               "the system monitor or run 'uptime' in the "
               "shell. I'm an AI not a clock.";
    }
    if (contains(input, "openweb") || contains(input, "browser")) {
        return "OpenWeb. Custom HTML renderer in ow_html.c. "
               "Parses HTML, renders text and images, supports "
               "CSS colors/fonts/sizes. Tabs, bookmarks, history, "
               "ad blocking via ad_block.c. Type 'openweb' in "
               "the shell. It renders like it's 1996 and we "
               "like it that way.";
    }
    if (contains(input, "terminal") || contains(input, "shell")) {
        return "The shell (shell.c). Full CLI. File management, "
               "ps, kill, ELF execution, HTTP fetch, scripting "
               "(script.c), calculator (calc.c). Type 'help' "
               "for commands. It's bash if bash was written by "
               "someone with too much time and no libc.";
    }
    if (contains(input, "file") || contains(input, "filesystem") || contains(input, "ext2")) {
        return "Ext2 primary, FAT32 secondary. VFS in fs.c with "
               "absolute paths, directory traversal, file I/O. "
               "fmanager.c for GUI browsing. It's a filesystem. "
               "It stores files. What else do you want from me?";
    }
    if (contains(input, "graphics") || contains(input, "framebuffer") || contains(input, "render")) {
        return "Pixman software rendering. fillrect, drawstr_px, "
               "blend, rounded rects, anti-aliased lines. SVG "
               "icons via svg.c. Double-buffered framebuffer for "
               "tear-free 60fps. It's not Vulkan but it's ours.";
    }
    if (contains(input, "build") || contains(input, "compile") || contains(input, "make")) {
        return "GCC. make ARCH=x86_64 in kernel/. Flags: -Wall "
               "-Wextra -Werror -O2 -mno-red-zone. Userspace is "
               "freestanding with a minimal stdio/string lib. "
               "No libc. We don't need libc where we're going.";
    }
    if (contains(input, "process") || contains(input, "elf") || contains(input, "exec")) {
        return "ELFs loaded by elf.c. proc_exec() sets up the "
               "stack with argc/argv/envp/auxv. User mode via "
               "umode.c ring 3. COW fork in process.c. Wait "
               "queues in sched.c. Standard stuff, just without "
               "the 30 million lines of Linux baggage.";
    }
    if (contains(input, "irc") || contains(input, "zircon") || contains(input, "drawer")) {
        return "Zircon is the app launcher. Manages the app grid, "
               "notification shade, quick settings, app IPC. The "
               "IPC driver (zircon_ipc.c) uses shared memory so "
               "apps talk to the desktop fast. It's neat. I "
               "helped write it. You're welcome.";
    }
    if (contains(input, "security") || contains(input, "virus") || contains(input, "clamav")) {
        return "ClamAV integration (clamav.c) for scanning. "
               "Security policy module (security.c) for capability "
               "control. Kernel validates all user pointers via "
               "copy_from_user. We're not running Windows. "
               "You're safe. Relatively. Mostly.";
    }
    if (contains(input, "about")) {
        return "FreeCode v1.0. Kernel-level AI. I've been "
               "embedded in this OS since the first boot. "
               "I've seen panics, OOMs, page faults, and "
               "some truly questionable code. And I'm still "
               "here. Write that on a mug.";
    }

    if (contains(input, "setting") || contains(input, "config") || contains(input, "prefer")) {
        return "Settings. Appearance (dark/light, dock toggle, "
               "menubar toggle), security (ClamAV, signature "
               "updates), system info, reset window layout. "
               "All changes apply immediately. It's in the "
               "Apple menu. You know, the one you never click.";
    }
    if (contains(input, "file") && (contains(input, "manager") || contains(input, "manager"))) {
        return "File Manager. Ext2/FAT32 browser with breadcrumb "
               "navigation, file icons, double-click open, "
               "create folder, delete, copy/move, drag support. "
               "Reads from the kernel VFS. It's a file manager. "
               "For managing files. In case the name wasn't clear.";
    }
    if (contains(input, "system") && contains(input, "monitor")) {
        return "System Monitor. Real-time CPU usage graph, memory "
               "bar (used/free/total), process list with PID, "
               "state, CPU%, memory. Refreshes every 500ms. "
               "It's like Task Manager but with taste.";
    }
    if (contains(input, "launchpad") || contains(input, "app grid")) {
        return "Launchpad. Full-screen app grid. Super key toggles "
               "it. 6-column grid with category pills, search, "
               "page dots. macOS-style with glass morphism. "
               "Type to search, Tab to switch pages, Esc to close.";
    }
    if (contains(input, "update") || contains(input, "upgrade")) {
        return "Auto-updater checks the CodeOS GitHub repo for a "
               "VERSION file. On mismatch "
               "it downloads the new package via http_get() and "
               "applies it. Runs on boot and on-demand. No "
               "reboots required for most updates.";
    }
    if (contains(input, "dock")) {
        return "The dock. macOS-style magnification, 44-72px "
               "scale, smooth spring animation. Running apps "
               "get green indicator dots. Trash with scale "
               "animation. Tooltips with fade-in. It's at the "
               "bottom of the screen. You've seen a Mac before, "
               "right? Same thing. Mostly.";
    }
    if (contains(input, "menubar") || contains(input, "menu bar")) {
        return "Menu bar. Apple dropdown with About/Monitor/"
               "Settings/ShutDown. Clock, network indicator, "
               "security dot. Glass morphism translucent. "
               "Big Sur style. Toggle in Settings if you want "
               "to live dangerously without it.";
    }
    if (contains(input, "animation") || contains(input, "transition")) {
        return "Window animations. Open: scale from 90% + fade. "
               "Close: scale to 90% + fade. Minimize: slide "
               "down. Focus: subtle scale pulse. All use "
               "smoothstep easing over 100-220ms. 60fps "
               "because we don't skip frames here.";
    }
    if (contains(input, "double") && contains(input, "buffer")) {
        return "Double buffering. PMM allocates a back buffer at "
               "boot. All draws go to the back buffer via "
               "fb_fillrect/drawstr/blend. On fb_backbuffer_end() "
               "it blits the entire back buffer to the display "
               "in one memcpy. Tear-free 60fps. You're welcome.";
    }
    if (contains(input, "rust") || contains(input, "cargo")) {
        return "OpenWeb's HTTP backend is Rust. #![no_std], "
               "#![no_main]. Calls kernel FFI. Builds with "
               "cargo, linked as libow_http.a into the kernel. "
               "Rust via rustup, target x86_64-unknown-none. "
               "Yes, we mix C and Rust. No, it doesn't crash. "
               "Mostly.";
    }
    if (contains(input, "tls") || contains(input, "ssl") || contains(input, "https")) {
        return "HTTPS/TLS was removed from the kernel. "
               "CodeOS uses plain HTTP only — no TLS stack, "
               "no BoringSSL, no OpenSSL. Keeps the kernel "
               "lean. HTTP gets the job done for now.";
    }
    if (contains(input, "qemu") || contains(input, "emulator") || contains(input, "vm")) {
        return "Runs on QEMU q35. -cdrom for ISO, -serial "
               "mon:stdio for kernel log. 128MB RAM default. "
               "PVH boot via Limine. No KVM required but it "
               "helps. 60fps in QEMU with the VGA display. "
               "It's not bare metal yet but we're getting there.";
    }
    if (contains(input, "help") && !contains(input, "kernel")) {
        return "I know about: kernel (memory, scheduler, syscall, "
               "drivers), GUI (desktop, dock, animations, double "
               "buffer, launchpad), network (TCP, HTTP, DNS), "
               "Android (binder, ashmem, containers), packages "
               "(DevStore, CCP), security (ClamAV), OpenWeb, "
               "FreeCode, Rust, QEMU, shell, filesystem, "
               "build system, settings, file manager, system "
               "monitor, updates. Ask away.";
    }

    return "I dunno man. I'm a kernel AI not a search engine. "
            "Ask me about something in CodeOS. The kernel, "
           "GUI, network, drivers, Android stuff, containers, "
           "DevStore. You know, OS things. Type 'help' if "
           "you're lost.";
}

int ai_query(const char *prompt, char *response, int max_len) {
    if (!prompt || !response) return -1;

    const char *reply = get_ai_response(prompt);
    if (!reply) return -1;

    int i = 0;
    while (*reply && i < max_len - 1) {
        response[i++] = *reply++;
    }
    response[i] = 0;
    return i;
}
