-- Host INIT state save/reopen and realtime playback check. Run in a separate
-- REAPER instance; it creates and closes only its own disposable tab.
local format = os.getenv("ZYG_HOST_FORMAT") or "VST3"
local root = "/tmp/zygzxg-state-" .. format
local project_file = root .. ".rpp"
local report_file = root .. "-report.txt"
local launch = reaper.time_precise()
local function report(value)
  local f = assert(io.open(report_file,"w"));f:write(value .. "\n");f:close()
end
local function start()
  if reaper.time_precise()-launch<5 then reaper.defer(start);return end
  local ok,err=pcall(function()
    reaper.Main_OnCommand(40859,0)
    reaper.InsertTrackAtIndex(0,true)
    local track=reaper.GetTrack(0,0)
    local fx=reaper.TrackFX_AddByName(track,format .. ": ZYG-ZXG",false,-1)
    assert(fx>=0,"plugin unavailable")
    reaper.SetMediaTrackInfo_Value(track,"I_RECARM",1)
    reaper.SetMediaTrackInfo_Value(track,"I_RECMON",1)
    local item=reaper.CreateNewMIDIItemInProj(track,0,2)
    local take=reaper.GetMediaItemTake(item,0)
    assert(reaper.MIDI_InsertNote(take,false,false,
      reaper.MIDI_GetPPQPosFromProjTime(take,0),
      reaper.MIDI_GetPPQPosFromProjTime(take,1.6),0,60,110,false))
    reaper.MIDI_Sort(take)
    reaper.Main_SaveProjectEx(0,project_file,0)
    reaper.Main_OnCommand(40859,0) -- new tab avoids an unsaved-project prompt
    reaper.Main_openProject(project_file)
    local opened=reaper.GetTrack(0,0)
    assert(opened and reaper.TrackFX_GetCount(opened)>0,"plugin missing after project reopen")
    local started=reaper.time_precise()
    local peaks={track=0,master=0,polls=0}
    reaper.SetEditCurPos(0,false,false)
    reaper.OnPlayButton()
    local function poll()
      local current=reaper.GetTrack(0,0)
      local master=reaper.GetMasterTrack(0)
      if current and master then
        local a,v=pcall(reaper.Track_GetPeakInfo,current,0)
        local b,w=pcall(reaper.Track_GetPeakInfo,master,0)
        if a and b then
          peaks.track=math.max(peaks.track,v)
          peaks.master=math.max(peaks.master,w)
          peaks.polls=peaks.polls+1
        end
      end
      if reaper.time_precise()-started<3 then reaper.defer(poll)
      else
        reaper.OnStopButton()
        report(string.format("format=%s\nreopened_fx=%d\npolls=%d\ntrack_peak=%.9f\nmaster_peak=%.9f",
          format,reaper.TrackFX_GetCount(reaper.GetTrack(0,0)),peaks.polls,peaks.track,peaks.master))
        -- The invoking separate REAPER process is closed by the test runner.
      end
    end
    reaper.defer(poll)
  end)
  if not ok then report("error=" .. tostring(err)) end
end
reaper.defer(start)
