-- Disposable visual host check. Run in a separate REAPER instance only.
local started = reaper.time_precise()
local function show()
  if reaper.time_precise() - started < 5 then reaper.defer(show); return end
  reaper.Main_OnCommand(40859, 0) -- new disposable tab
  reaper.InsertTrackAtIndex(0, true)
  local track = reaper.GetTrack(0, 0)
  local fx = reaper.TrackFX_AddByName(track, "VST3: ZYG-ZXG", false, -1)
  if fx < 0 then error("ZYG-ZXG unavailable") end
  reaper.TrackFX_Show(track, fx, 3)
  local f = assert(io.open("/tmp/zygzxg-gui-ready.txt", "w"))
  f:write("fx=" .. fx .. "\n")
  f:close()
end
reaper.defer(show)
