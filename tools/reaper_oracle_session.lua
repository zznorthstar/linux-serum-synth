-- Private, disposable Serum measurement session. No shell commands or networking.
-- Launch with -newinst. Jobs are Lua files written by the local operator in
-- ../oracle-private/job.lua; each is consumed once. Never use in a user project.
local script = debug.getinfo(1, "S").source:gsub("^@", "")
local root = assert(script:match("^(.*[\\/])tools[\\/]"))
ORACLE = {root = root, out = root .. "../oracle-private/"}
local function write(name, value)
  local f = assert(io.open(ORACLE.out .. name, "wb")); f:write(value); f:close()
end
ORACLE.write = write
reaper.RecursiveCreateDirectory(ORACLE.out, 0)
assert(reaper.CountTracks(0) == 0, "Oracle requires an empty disposable project")
reaper.InsertTrackAtIndex(0, false)
ORACLE.track = reaper.GetTrack(0, 0)
ORACLE.fx = reaper.TrackFX_AddByName(ORACLE.track, "VST3i: Serum 2 (Xfer Records)", false, -1)
assert(ORACLE.fx >= 0, "Serum 2 not installed")
-- Keep the reference instance silent unless a job explicitly renders offline.
reaper.SetMediaTrackInfo_Value(reaper.GetMasterTrack(0), "B_MUTE", 1)
reaper.SetMediaTrackInfo_Value(ORACLE.track, "I_RECARM", 1)
reaper.SetMediaTrackInfo_Value(ORACLE.track, "I_RECMON", 1)
local started = reaper.time_precise()
local function poll()
  local job = ORACLE.out .. "job.lua"
  local f = io.open(job, "rb")
  if f then
    local code = f:read("*a")
    f:close()
    if os.remove(job) then
      local run, loadError = load(code, "@oracle-job")
      local ok, err = xpcall(function() assert(run, loadError)() end, debug.traceback)
      write("job-status.txt", ok and "success" or tostring(err))
    end
  end
  if reaper.time_precise() - started < 3600 then reaper.defer(poll)
  else write("session-status.txt", "expired") end
end
write("session-status.txt", "ready")
reaper.defer(poll)
