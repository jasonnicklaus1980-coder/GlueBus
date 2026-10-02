-- MPC Arcade: takes one screenshot about 40 seconds into the first play of a game, for the library's game page.
-- Loaded with -autoboot_script only when the game has no screenshot yet. Every call is guarded: if this MAME's
-- Lua API differs, nothing happens and the game is not affected.
local taken = false
local function seconds()
    local ok, t = pcall(function() return manager.machine.time end)
    if not ok or t == nil then return nil end
    local ok2, s = pcall(function() return t:as_double() end)
    if ok2 and s then return s end
    ok2, s = pcall(function() return t.seconds end)
    if ok2 then return s end
    return nil
end
local ok = pcall(function()
    emu.register_periodic(function()
        if taken then return end
        local s = seconds()
        if s == nil then taken = true return end
        if s >= 40 then
            taken = true
            pcall(function() manager.machine.video:snapshot() end)
        end
    end)
end)
