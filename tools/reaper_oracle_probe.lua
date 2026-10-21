-- Launch in a disposable instance: reaper -newinst tools/reaper_oracle_probe.lua
-- Outputs stay outside the repository. Never run on a populated project.
local _, script = reaper.get_action_context()
local root = assert(script:match("^(.*[\\/])tools[\\/]"), "Cannot locate repository")
local out = root .. "../oracle-private/"
reaper.RecursiveCreateDirectory(out, 0)
local function write(name, value)
  local f = assert(io.open(out .. name, "wb")); f:write(value); f:close()
end
local function main()
  assert(reaper.CountTracks(0) == 0, "Probe requires an empty disposable project")
  local installed = {}
  for i = 0, 10000 do
    local ok, name, ident = reaper.EnumInstalledFX(i)
    if not ok then break end
    installed[#installed + 1] = name .. "\t" .. ident
  end
  write("installed-fx.tsv", table.concat(installed, "\n"))
  reaper.InsertTrackAtIndex(0, false)
  local track = reaper.GetTrack(0, 0)
  local fx = reaper.TrackFX_AddByName(track, "VST3i: Serum 2 (Xfer Records)", false, -1)
  assert(fx >= 0, "Serum 2 not found")
  local rows = {"index\tname\tnormalized\tformatted"}
  for p = 0, reaper.TrackFX_GetNumParams(track, fx) - 1 do
    local _, name = reaper.TrackFX_GetParamName(track, fx, p, "")
    local _, value = reaper.TrackFX_GetFormattedParamValue(track, fx, p, "")
    rows[#rows + 1] = string.format("%d\t%s\t%.12g\t%s", p, name,
      reaper.TrackFX_GetParamNormalized(track, fx, p), value)
  end
  write("serum-init-parameters.tsv", table.concat(rows, "\n"))
  local ok, chunk = reaper.TrackFX_GetNamedConfigParm(track, fx, "vst_chunk")
  if ok then write("serum-init-chunk.base64", chunk) end
  reaper.Main_SaveProjectEx(0, out .. "serum-init.rpp", 0)
  reaper.TrackFX_Show(track, fx, 3)
  write("probe-status.txt", "success\nREAPER=" .. reaper.GetAppVersion() .. "\nparameters=" .. reaper.TrackFX_GetNumParams(track, fx))
end
local ok, err = xpcall(main, debug.traceback)
if not ok then write("probe-status.txt", "error=" .. tostring(err)) end
