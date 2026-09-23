-- Live REAPER transport/MIDI/VST3 meter check. This is intentionally not an
-- offline render. Run against the developer's already-running REAPER
-- instance with `reaper -newinst tests/reaper_realtime.lua`. Set
-- ZYG_HOST_FORMAT=CLAP to test CLAP; VST3 is the default. Each format writes
-- a separate report under /tmp. Use a separate instance so the user's open
-- project is never modified.
--
-- Every step is wrapped so a failure still produces a diagnostic report
-- instead of silently leaving a half-built disposable project tab open with
-- no explanation. Disposable tabs this script creates are identified by
-- content (unsaved + a ZYG-ZXG track) on the next run so leftovers from a
-- crashed run get swept up without ever touching a tab it did not create.

local plugin_format = os.getenv("ZYG_HOST_FORMAT") or "VST3"
local report_path = "/tmp/zygzxg-realtime-" .. plugin_format .. "-report.txt"
local log = {}
local function step(msg)
  log[#log + 1] = string.format("[%.3f] %s", reaper.time_precise(), msg)
end

local function write_report(extra)
  local file = assert(io.open(report_path, "w"))
  file:write(table.concat(log, "\n") .. "\n")
  if extra then file:write(extra .. "\n") end
  file:close()
end

-- Saving before closing avoids REAPER's "save changes?" modal prompt, which
-- would otherwise hang forever since nothing here can click a dialog button.
-- The throwaway .rpp is scratch state, not a real project; overwritten each run.
local function save_and_close_current_tab(scratch_suffix)
  reaper.Main_SaveProjectEx(0, "/tmp/zygzxg-disposable-" .. scratch_suffix .. ".rpp", 0)
  reaper.Main_OnCommand(40860, 0) -- File: Close current project tab
end

-- Close any leftover disposable tab from a previous (e.g. crashed) run of
-- this script. A tab is identified as our own debris purely by content --
-- unsaved (no filename) AND its first track's FX chain contains "ZYG-ZXG" --
-- never by tab index/position, since new-tab creation order is not
-- guaranteed to put the pre-existing session at index 0 (observed directly:
-- a disposable tab this script created ended up at index 0 in a later
-- EnumProjects scan). A content match this specific is never a real user
-- project by coincidence. ProjExtState marker tagging was tried first but
-- did not reliably survive across -nonewinst invocations in this
-- environment, so this checks actual track/FX content instead.
local function is_our_disposable_tab(proj)
  local _, fn = reaper.GetProjectName(proj, "")
  if fn ~= "" then return false end
  local ntracks = reaper.CountTracks(proj)
  for t = 0, ntracks - 1 do
    local track = reaper.GetTrack(proj, t)
    local nfx = reaper.TrackFX_GetCount(track)
    for f = 0, nfx - 1 do
      local _, fxname = reaper.TrackFX_GetFXName(track, f, "")
      if fxname:find("ZYG%-ZXG") then return true end
    end
  end
  return false
end

local function close_stale_disposable_tabs()
  local closed = 0
  local i = 0
  while true do
    local proj = reaper.EnumProjects(i)
    if not proj then break end
    if is_our_disposable_tab(proj) then
      reaper.SelectProjectInstance(proj)
      save_and_close_current_tab("stale-" .. tostring(closed))
      closed = closed + 1
      -- do not advance i: the tab list shifted after closing
    else
      i = i + 1
    end
  end
  return closed
end

local function run_test()
local ok, err = pcall(function()
  step("script start")
  local closed = close_stale_disposable_tabs()
  step("closed_stale_disposable_tabs=" .. closed)

  if reaper.GetPlayState() ~= 0 then
    step("blocked: a REAPER transport is currently active, refusing to touch it")
    write_report("blocked=existing REAPER transport is active")
    return
  end

  reaper.Main_OnCommand(40859, 0) -- File: New project tab
  step("created new disposable project tab")

  reaper.InsertTrackAtIndex(0, true)
  local track = reaper.GetTrack(0, 0)
  local master = reaper.GetMasterTrack(0)
  step("created track 0")

  local fx = reaper.TrackFX_AddByName(track, plugin_format .. ": ZYG-ZXG", false, -1)
  step("TrackFX_AddByName returned " .. tostring(fx))
  if fx < 0 then
    write_report("error=ZYG-ZXG " .. plugin_format .. " unavailable via TrackFX_AddByName")
    error("ZYG-ZXG " .. plugin_format .. " unavailable")
  end
  reaper.TrackFX_Show(track, fx, 0) -- 0 = hide floating/chain window
  step("hid FX chain window")

  reaper.SetMediaTrackInfo_Value(track, "I_RECARM", 1)
  reaper.SetMediaTrackInfo_Value(track, "I_RECMON", 1)
  step("armed track for monitoring")

  local item = reaper.CreateNewMIDIItemInProj(track, 0.0, 2.0)
  if not item then error("CreateNewMIDIItemInProj returned nil") end
  local take = reaper.GetMediaItemTake(item, 0)
  if not take then error("GetMediaItemTake returned nil") end
  step("created MIDI item + take")

  local ppq_start = reaper.MIDI_GetPPQPosFromProjTime(take, 0.0)
  local ppq_end = reaper.MIDI_GetPPQPosFromProjTime(take, 1.6)
  local inserted = reaper.MIDI_InsertNote(take, false, false, ppq_start, ppq_end, 0, 60, 110, false)
  step(string.format("MIDI_InsertNote(ppq_start=%.3f ppq_end=%.3f) returned %s", ppq_start, ppq_end, tostring(inserted)))
  if not inserted then error("MIDI_InsertNote failed") end
  reaper.MIDI_Sort(take)
  step("sorted MIDI take")

  local started = reaper.time_precise()
  local max_track, max_master, max_position, play_blocks = 0, 0, 0, 0
  reaper.SetEditCurPos(0, false, false)
  reaper.OnPlayButton()
  step("pressed play")

  -- REAPER may briefly invalidate track pointers during project creation.
  -- Re-fetch and validate them at each meter poll.
  local poll
  local function poll_body()
    local live_track = reaper.GetTrack(0, 0)
    local live_master = reaper.GetMasterTrack(0)
    local track_ok = live_track and pcall(reaper.Track_GetPeakInfo, live_track, 0)
    local master_ok = live_master and pcall(reaper.Track_GetPeakInfo, live_master, 0)
    if not track_ok or not master_ok then
      step("poll tick skipped: track/master unavailable this frame")
      if reaper.time_precise() - started < 3.0 then reaper.defer(poll)
      else
        reaper.OnStopButton()
        write_report("error=track/master pointer unavailable during live playback")
        save_and_close_current_tab("unavailable")
      end
      return
    end
    local playing = (reaper.GetPlayState() & 1) ~= 0
    if playing then play_blocks = play_blocks + 1 end
    max_position = math.max(max_position, reaper.GetPlayPosition())
    max_track = math.max(max_track, reaper.Track_GetPeakInfo(live_track, 0), reaper.Track_GetPeakInfo(live_track, 1))
    max_master = math.max(max_master, reaper.Track_GetPeakInfo(live_master, 0), reaper.Track_GetPeakInfo(live_master, 1))
    if reaper.time_precise() - started < 3.0 then
      reaper.defer(poll)
    else
      reaper.OnStopButton()
      step("stopped after poll loop")
      write_report(string.format(
        "fx_index=%d\nplay_polls=%d\nmax_position=%.6f\ntrack_peak=%.9f\nmaster_peak=%.9f",
        fx, play_blocks, max_position, max_track, max_master))
      save_and_close_current_tab("final")
      step("closed disposable test tab")
    end
  end
  poll = function()
    local poll_ok, poll_err = pcall(poll_body)
    if not poll_ok then
      step("SCRIPT ERROR in poll: " .. tostring(poll_err))
      write_report("error=" .. tostring(poll_err))
    end
  end
  reaper.defer(poll)
end)

if not ok then
  step("SCRIPT ERROR: " .. tostring(err))
  write_report("error=" .. tostring(err))
end
end

-- Command-line scripts may start before REAPER finishes restoring its first
-- project. Delay track creation so pointers survive into deferred meter polls.
local launch_time = reaper.time_precise()
local function wait_for_host()
  if reaper.time_precise() - launch_time < 5.0 then reaper.defer(wait_for_host)
  else run_test() end
end
reaper.defer(wait_for_host)
