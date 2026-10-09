-- ═══════════════════════════════════════════════════════════════════
-- HyperDE: animations config module
-- Loaded via: local anims = require("config.animations")
-- ═══════════════════════════════════════════════════════════════════

local M = {}

M.enabled = true
M.duration = 200
M.easing = "easeOut"

M.window = {
    appear = "easeOut",
    disappear = "easeIn",
    move = "linear",
    resize = "easeOut",
    fade = "easeOut",
    slide = "easeOut",
}

M.bar = {
    appear = "easeOut",
    disappear = "easeIn",
    move = "linear",
    fade = "easeOut",
}

M.general = {
    appear = "easeOut",
    disappear = "easeIn",
    move = "linear",
    fade = "easeOut",
}

function M.get_window_easing(name)
    return M.window[name] or M.easing
end

function M.get_bar_easing(name)
    return M.bar[name] or M.easing
end

function M.get_duration()
    return M.duration
end

function M.get_easing()
    return M.easing
end

function M.is_enabled()
    return M.enabled
end

function M.is_animating(obj_type, anim_name)
    if not M.enabled then return false end
    local cfg = M[obj_type]
    if cfg then
        return cfg[anim_name] or nil
    end
    return false
end

return M
