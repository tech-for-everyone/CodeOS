-- ═══════════════════════════════════════════════════════════════════
-- HyperDE: theme config module
-- Loaded via: local theme = require("config.theme")
-- ═══════════════════════════════════════════════════════════════════

local M = {}

M.accent = "#40D9F0"
M.background = "#1a1a2e"
M.foreground = "#e0e0e0"
M.surface = "#16213e"
M.surface_variant = "#0f3460"
M.panel_bg = "#1e1e2e"
M.panel_fg = "#cdd6f4"
M.error = "#e94560"
M.warning = "#ffc107"
M.success = "#4caf50"
M.glass_top = "#242428"
M.glass_bottom = "#17171B"
M.glass_alpha = 192

function M.get_accent()
    return M.accent
end

function M.get_panel_background()
    return M.panel_bg
end

function M.get_panel_foreground()
    return M.panel_fg
end

function M.get_glass_top()
    return M.glass_top
end

function M.get_glass_bottom()
    return M.glass_bottom
end

function M.get_glass_alpha()
    return M.glass_alpha
end

function M.get_color(name)
    local colors = {
        accent = M.accent,
        background = M.background,
        foreground = M.foreground,
        surface = M.surface,
        surface_variant = M.surface_variant,
        panel_bg = M.panel_bg,
        panel_fg = M.panel_fg,
        error = M.error,
        warning = M.warning,
        success = M.success,
        glass_top = M.glass_top,
        glass_bottom = M.glass_bottom,
    }
    return colors[name] or "#000000"
end

return M
