-- Run as a job inside reaper_oracle_session.lua; reference data stays private.
local o=assert(ORACLE)
local names={['Main Vol']=true,['Amp']=true,['Direct Vol']=true,['Bus 1 Vol']=true,['A Level']=true,['A Uni Detune']=true,
 ['Filter 1 Freq']=true,['Filter 1 Res']=true,['Filter 1 Drive']=true,
 ['Env 1 Attack']=true,['Env 1 Hold']=true,['Env 1 Decay']=true,
 ['Env 1 Sustain']=true,['Env 1 Release']=true}
local jobs={}
for p=0,reaper.TrackFX_GetNumParams(o.track,o.fx)-1 do
 local _,name=reaper.TrackFX_GetParamName(o.track,o.fx,p,'')
 if names[name] then
  local original=reaper.TrackFX_GetParamNormalized(o.track,o.fx,p)
  for _,v in ipairs({0,0.1,0.25,0.5,0.75,0.9,1}) do jobs[#jobs+1]={p=p,name=name,v=v} end
  jobs[#jobs+1]={p=p,name=name,v=original,restore=true}
 end
end
local rows={'parameter\tnormalized\tformatted\tchunk'}
local index,deadline,pending=1,0,false
local function step()
 local j=jobs[index]
 if not j then o.write('parameter-sweep.tsv',table.concat(rows,'\n'));o.write('sweep-status.txt','complete');return end
 if not pending then
  reaper.TrackFX_SetParamNormalized(o.track,o.fx,j.p,j.v)
  deadline=reaper.time_precise()+0.08;pending=true
 elseif reaper.time_precise()>=deadline then
  if not j.restore then
   local _,display=reaper.TrackFX_GetFormattedParamValue(o.track,o.fx,j.p,'')
   local ok,chunk=reaper.TrackFX_GetNamedConfigParm(o.track,o.fx,'vst_chunk');assert(ok)
   local file=string.format('sweep-%d-%g.base64',j.p,j.v);o.write(file,chunk)
   rows[#rows+1]=string.format('%s\t%.9g\t%s\t%s',j.name,j.v,display,file)
  end
  index=index+1;pending=false
 end
 reaper.defer(step)
end
step()
