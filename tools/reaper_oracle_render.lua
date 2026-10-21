-- Run as a job after writing ../oracle-private/case.base64.
-- Applies the host FX offline to a fixed 48 kHz, C4, velocity-127 MIDI item.
local o=assert(ORACLE)
local label=o.caseName or 'case'
assert(label:match('^[%w_.-]+$'), 'Invalid case name')
local f=assert(io.open(o.out..label..'.base64','rb'));local chunk=f:read('*a');f:close()
assert(reaper.TrackFX_SetNamedConfigParm(o.track,o.fx,'vst_chunk',chunk))
local deadline=reaper.time_precise()+0.5
local function save()
 if reaper.time_precise()<deadline then reaper.defer(save);return end
 local rows={}
 for _,p in ipairs({0,20,21,59,224,226,227,228}) do
  local _,name=reaper.TrackFX_GetParamName(o.track,o.fx,p,'')
  local _,value=reaper.TrackFX_GetFormattedParamValue(o.track,o.fx,p,'')
  rows[#rows+1]=name..'\t'..reaper.TrackFX_GetParamNormalized(o.track,o.fx,p)..'\t'..value
 end
 o.write('case-load-check.tsv',table.concat(rows,'\n'))
 reaper.GetSetProjectInfo(0,'PROJECT_SRATE',48000,true)
 reaper.GetSetProjectInfo(0,'PROJECT_SRATE_USE',1,true)
 -- 32-bit float apply-FX output retains raw peaks above 0 dBFS. The ordinary
 -- auto-depth setting can clip integer WAVs and bias reference measurements.
 assert(reaper.GetSetProjectInfo_String(0,'APPLYFX_FORMAT','ZXZhdyAAAQ==',true))
 assert(reaper.GetSetProjectInfo_String(0,'OPENCOPY_CFGIDX','1',true))
 reaper.GetSetProjectInfo_String(0,'RECORD_PATH',o.out,true)
 reaper.Main_SaveProjectEx(0,o.out..'serum-envelope.rpp',8)
 while reaper.CountTrackMediaItems(o.track)>0 do reaper.DeleteTrackMediaItem(o.track,reaper.GetTrackMediaItem(o.track,0)) end
 local hold=o.noteHoldSeconds or 2
 local start=o.noteStartSeconds or 0
 local item=reaper.CreateNewMIDIItemInProj(o.track,0,start+hold+(o.renderTailSeconds or 1))
 local take=reaper.GetActiveTake(item)
 reaper.GetSetMediaItemTakeInfo_String(take,'P_NAME',label,true)
 if o.primeNoteHoldSeconds and o.primeNoteHoldSeconds>0 then
  assert(start>o.primeNoteHoldSeconds, 'Priming note must end before the measured note')
  reaper.MIDI_InsertNote(take,false,false,reaper.MIDI_GetPPQPosFromProjTime(take,0),reaper.MIDI_GetPPQPosFromProjTime(take,o.primeNoteHoldSeconds),0,o.noteNumber or 60,o.noteVelocity or 127,false)
 end
 reaper.MIDI_InsertNote(take,false,false,reaper.MIDI_GetPPQPosFromProjTime(take,start),reaper.MIDI_GetPPQPosFromProjTime(take,start+hold),0,o.noteNumber or 60,o.noteVelocity or 127,false)
 reaper.MIDI_Sort(take)
 reaper.SetMediaItemSelected(item,true)
 reaper.Main_OnCommand(40209,0)
 local source=reaper.GetMediaItemTake_Source(reaper.GetActiveTake(item))
 local path=reaper.GetMediaSourceFileName(source,'')
 o.write('case-render-path.txt',path)
 o.write(label..'-render-path.txt',path)
 reaper.Main_SaveProjectEx(0,o.out..'serum-envelope.rpp',8)
 if o.onRendered then o.onRendered(label,path) end
end
reaper.defer(save)
