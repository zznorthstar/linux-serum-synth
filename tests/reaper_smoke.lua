-- REAPER host smoke test. Set ZYGZXG_HOST_SMOKE_DIR to an isolated writable
-- directory. Set ZYGZXG_HOST_SMOKE_PRESET/ASSETS for the private preset hook.
-- The script saves a native REAPER project containing MIDI and the VST3.
local root = os.getenv("ZYGZXG_HOST_SMOKE_DIR")
if not root then error("ZYGZXG_HOST_SMOKE_DIR is required") end

reaper.InsertTrackAtIndex(0, true)
local track = reaper.GetTrack(0, 0)
local fx = reaper.TrackFX_AddByName(track, "VST3: ZYG-ZXG", false, -1)
local report = assert(io.open(root .. "/host-scan.txt", "w"))
report:write("TrackFX_AddByName index: " .. tostring(fx) .. "\n")
if fx < 0 then
  report:close()
  error("REAPER did not discover ZYG-ZXG VST3")
end
local _, name = reaper.TrackFX_GetFXName(track, fx)
report:write("FX name: " .. tostring(name) .. "\n")
report:close()

local item = reaper.CreateNewMIDIItemInProj(track, 0.0, 2.0)
local take = reaper.GetMediaItemTake(item, 0)
local start = reaper.MIDI_GetPPQPosFromProjTime(take, 0.0)
local finish = reaper.MIDI_GetPPQPosFromProjTime(take, 1.5)
assert(reaper.MIDI_InsertNote(take, false, false, start, finish, 0, 48, 100, false))
reaper.MIDI_Sort(take)

reaper.GetSetProjectInfo(0, "RENDER_BOUNDSFLAG", 1, true)
reaper.GetSetProjectInfo(0, "RENDER_SETTINGS", 0, true)
reaper.GetSetProjectInfo(0, "RENDER_SRATE", 48000, true)
reaper.GetSetProjectInfo(0, "RENDER_CHANNELS", 2, true)
reaper.GetSetProjectInfo_String(0, "RENDER_FILE", root, true)
reaper.GetSetProjectInfo_String(0, "RENDER_PATTERN", "host-smoke", true)
reaper.GetSetProjectInfo_String(0, "RENDER_FORMAT", "evaw", true)
reaper.Main_SaveProjectEx(0, root .. "/host-smoke.rpp", 8)
