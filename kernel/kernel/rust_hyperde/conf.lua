-- ══════════════════════════════════════════════════════════════════════
-- HyperDE conf.lua (like Hyprland's hyprland.conf)
-- Modular configuration loaded via mlua require()
-- ══════════════════════════════════════════════════════════════════════

return {
    -- ── General ──
    general = {
        title = "HyperDE",
        version = "0.2.0",
        font = "Inter 11",
        icon_theme = "Papirus",
        cursor_theme = "Bibata-Modern-Classic",
        cursor_size = 24,
        double_click_time = 300,
        focus_on_map = true,
    },

    -- ── Compositor ──
    compositor = {
        panel_height = 32,
        panel_position = "top",
        panel_opacity = 0.95,
        panel_blur = true,
        panel_radius = 0,
        desktop_blur = true,
        desktop_blur_radius = 12,
        vblank_mode = "adaptive",
        vsync = true,
        adaptive_sync = true,
        render_delay = 16,  /* ms */
    },

    -- ── Window Manager ──
    window_manager = {
        border_width = 2,
        focused_border = "#40D9F0",
        normal_border = "#3c3836",
        floating_border = "#3c3836",
        focus_follow_mouse = true,
        focus_on_enters = true,
        focus_on_click = true,
        raise_on_click = true,
        center_on_map = true,
        animation_duration = 200,
        workspace_names = { "1", "2", "3", "4", "5", "6", "7", "8", "9" },
        default_workspace = 1,
        mouse_warp = false,
        snap_threshold = 10,
        snap_radius = 5,
        floating_classes = {
            "dmenu", "dunst", "file_progress", "notification",
            "splash", "confirm", "dialog", "verification",
        },
    },

    -- ── Layout (Penrose tiling) ──
    layout = {
        algorithm = "penrose",
        penrose_order = 3,
        monitor_policy = "focus",
        single_monitor_mode = "tiled",
        gaps_in = 4,
        gaps_out = 8,
        smart_gaps = true,
        border_squeeze = true,
        col_align = true,
        row_align = true,
        single_monitors = false,
        focus_new = true,
        resize_to_border = true,
        center_on_float = true,
    },

    -- ── Decorations ──
    decorations = {
        blur = true,
        blur_radius = 8,
        blur_passes = 3,
        shadow = true,
        shadow_radius = 4,
        shadow_x_offset = 0,
        shadow_y_offset = 2,
        shadow_color = "#000000",
        shadow_opacity = 0.25,
        round = true,
        round_radius = 6,
        glass_effect = true,
        glass_top = "#242428",
        glass_bottom = "#17171B",
        glass_alpha = 192,
    },

    -- ── Animations ──
    animations = {
        enabled = true,
        duration = 200,
        easing = "easeOut",  /* linear, easeIn, easeOut, easeInOut, spring, bounce */
        window = {
            appear = "easeOut",
            disappear = "easeIn",
            move = "linear",
            resize = "easeOut",
            fade = "easeOut",
            slide = "easeOut",
        },
        bar = {
            appear = "easeOut",
            disappear = "easeIn",
            move = "linear",
            fade = "easeOut",
        },
        general = {
            appear = "easeOut",
            disappear = "easeIn",
            move = "linear",
            fade = "easeOut",
        },
    },

    -- ── Input ──
    input = {
        kb_layout = "us",
        kb_variant = "",
        kb_options = "caps:super",
        repeat_delay = 250,
        repeat_rate = 30,
        mouse_refocus = true,
        follow_mouse = true,
        focus_on_enter = true,
        grab_after_enter = true,
        reset_bindings_on_grab = true,
        no_warps_on_grab = false,
        xkb_options = "",
        natural_scroll = false,
        left_handed = false,
        numlock = false,
    },

    -- ── Theme ──
    theme = {
        accent = "#40D9F0",
        background = "#1a1a2e",
        foreground = "#e0e0e0",
        surface = "#16213e",
        surface_variant = "#0f3460",
        panel_background = "#1e1e2e",
        panel_foreground = "#cdd6f4",
        error = "#e94560",
        warning = "#ffc107",
        success = "#4caf50",
    },

    -- ── Window rules ──
    windowrule = {
        float = { "dmenu", "file_progress", "splash" },
        fullscreen = { "mpv", "firefox" },
        maximized = { "codeos-files", "openweb" },
        pinned = { "dunst" },
        ignore_focus = { "splash" },
        center = { "confirm", "dialog" },
        no_animate = { "splash", "splash" },
    },

    -- ── Autostart ──
    autostart = {
        { command = "codeos-files", delay = 0 },
        { command = "dunst", delay = 100 },
        { command = "openweb", delay = 200 },
        { command = "codeos-calculator", delay = 500 },
        { command = "codeos-settings", delay = 300 },
    },

    -- ── Keybinds ──
    keybinds = {
        { key = "M-Return", command = "codeos-terminal" },
        { key = "M-b", command = "codeos-browser" },
        { key = "M-d", command = "codeos-files" },
        { key = "M-Shift-c", command = "kill_client" },
        { key = "M-f", command = "toggle_fullscreen" },
        { key = "M-space", command = "toggle_floating" },
        { key = "M-t", command = "toggle_terminal" },
        { key = "M-q", command = "close_window" },
        { key = "M-v", command = "screenshot" },
        { key = "Print", command = "screenshot" },
        { key = "M-Shift-q", command = "exit" },
    },
}
