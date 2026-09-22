-- ═══════════════════════════════════════════════════════════════════
-- HyperDE: autostart config module
-- Loaded via: local auto = require("config.autostart")
-- ═══════════════════════════════════════════════════════════════════

local M = {}

M.items = {
    { command = "codeos-files", delay = 0 },
    { command = "dunst", delay = 100 },
    { command = "openweb", delay = 200 },
    { command = "codeos-calculator", delay = 500 },
    { command = "codeos-settings", delay = 300 },
}

function M.get_items()
    return M.items
end

function M.run(delay_ms)
    local items = {}
    for _, item in ipairs(M.items) do
        if item.delay <= delay_ms then
            table.insert(items, item.command)
        end
    end
    return items
end

function M.count()
    return #M.items
end

function M.add(command, delay)
    table.insert(M.items, { command = command, delay = delay or 0 })
end

return M
