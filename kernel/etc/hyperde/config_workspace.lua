-- ═══════════════════════════════════════════════════════════════════
-- HyperDE: workspace config module
-- Loaded via: local wm = require("config.workspace_manager")
-- ═══════════════════════════════════════════════════════════════════

local M = {}

function M.get_workspaces()
    return { "1", "2", "3", "4", "5", "6", "7", "8", "9" }
end

function M.get_default_workspace()
    return 1
end

function M.get_workspace_name(idx)
    local names = { "1", "2", "3", "4", "5", "6", "7", "8", "9" }
    return names[idx] or tostring(idx)
end

function M.get_workspace_count()
    return 9
end

function M.is_floating(class_name)
    local floating_classes = {
        "dmenu", "dunst", "file_progress", "notification",
        "splash", "confirm", "dialog", "verification",
    }
    for _, cls in ipairs(floating_classes) do
        if cls == class_name then return true end
    end
    return false
end

return M
