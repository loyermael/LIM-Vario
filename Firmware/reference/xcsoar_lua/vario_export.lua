-- vario_export.lua
-- A placer dans : /sdcard/XCSoarData/lua/vario_export.lua
-- XCSoar charge automatiquement les scripts Lua au demarrage
-- Ecrit les donnees de vol dans un fichier lu par le script Termux

local FILE = "/sdcard/XCSoarData/vario_live.txt"

local function safe(val)
  if val == nil then return 0 end
  return val
end

-- Mise a jour toutes les secondes (intervalle en secondes)
xcsoar.timer.new(1, function(t)
  local v = xcsoar.blackboard.values

  local f = io.open(FILE, "w")
  if f then
    f:write(string.format("ALT_BARO=%.1f\n",      safe(v.altitude_baro)))
    f:write(string.format("GS=%.3f\n",             safe(v.ground_speed)))   -- m/s
    f:write(string.format("VARIO=%.2f\n",          safe(v.vario)))
    f:write(string.format("AVG_VARIO=%.2f\n",      safe(v.average_vario)))
    f:write(string.format("WIND_SPEED=%.2f\n",     safe(v.wind_speed)))     -- m/s
    f:write(string.format("WIND_BEARING=%.1f\n",   safe(v.wind_bearing)))
    f:close()
  end
end)
