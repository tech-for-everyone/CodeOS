-- =========================================================================
-- 1. MODULE IMPORTS (Rust API Exposure)
-- =========================================================================
-- The desktop environment injects its Rust core API into the Lua state
local env = require("rust_desktop_core")
local actions = env.actions

-- =========================================================================
-- 2. ENVIRONMENT & PREFERENCES
-- =========================================================================
local config = {
    terminal      = "rtermy",            -- Fast, Rust-based GPU terminal
    runner        = "fuzzel",            -- Lightweight application launcher
    border_width  = 2,                   -- Window border in pixels
    active_color  = "#15788c",            -- Accent border / focused window
    inactive_color = "#000000",          -- Inactive window border
    gaps          = 5,                   -- Pixel spacing between tiled windows
    panel_height  = 26,                  -- Status bar height reserved on screen
    background    = "#3888a4",           -- Desktop background (Chroma compositor)
    visual_effects = true,               -- Compositor: rounded corners + real compositing
}

-- Apply the local preferences table back into the Rust core engine
env.set_options(config)

-- =========================================================================
-- 3. WORKSPACES / TAGS
-- =========================================================================
-- Defining virtual screens managed by the Rust binary
env.workspaces = { "1:🌐", "2:💻", "3:📁", "4:📱", "5:⚙️" }

-- =========================================================================
-- 4. KEYBINDINGS & INPUT MANAGEMENT
-- =========================================================================
-- Define the primary modifier key (e.g., "Super" / Windows Key)
local MOD = "Super"

env.keys = {
    -- --- Core Actions ---
    { {MOD},          "Return", actions.spawn(config.terminal) },
    { {MOD},          "d",      actions.spawn(config.runner) },
    { {MOD},          "Tab",    actions.launcher() },
    { {MOD, "Shift"}, "q",      actions.close_client() },
    { {MOD, "Shift"}, "e",      actions.exit_desktop() },

    -- --- Window Navigation (Vim Keys) ---
    { {MOD},          "m",      actions.focus_direction("down") },
    { {MOD},          "k",      actions.focus_direction("up") },
    { {MOD},          "n",      actions.focus_direction("left") },
    { {MOD},          "comma", actions.focus_direction("right") },

    -- --- Layout Manipulation ---
    { {MOD},          "Space",  actions.next_layout() },
    { {MOD, "Shift"}, "k",      actions.swap_direction("down") },
    { {MOD, "Shift"}, "m",      actions.swap_direction("up") },
}

-- Map Workspace numbers 1 through 5 seamlessly to keyboard keys
for i = 1, #env.workspaces do
    -- Switch to workspace (e.g., Super + 1)
    table.insert(env.keys, { {MOD}, tostring(i), actions.view_workspace(i) })
    -- Move focused window to workspace (e.g., Super + Shift + 1)
    table.insert(env.keys, { {MOD, "Shift"}, tostring(i), actions.move_to_workspace(i) })
end

-- =========================================================================
-- 5. WINDOW MANAGEMENT RULES
-- =========================================================================
-- Instruct the Rust layout engine how to treat specific window classes
env.rules = {
    { class = "Openweb",      workspace = 1 },
    { class = "NetBeam",      workspace = 4 },
    { class = "Ziggy",        floating = true }, -- Keep image editor decoupled from tiles
}

-- =========================================================================
-- 6. AUTOSTART DAEMONS
-- =========================================================================
-- Shell execution strings triggered when the Rust environment boots.
-- A daemon that fails to start is logged as a warning, never fatal to the DE.
env.autostart = {
    "android-container",        -- Android Container
    "w-manager",                -- Wallpaper engine
    "n-man",                    -- Notification daemon
    "dunst",
}