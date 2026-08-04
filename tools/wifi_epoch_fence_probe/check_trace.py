#!/usr/bin/env python3
"""Validate scenario-qualified Wi-Fi epoch-fence JSON-lines traces."""
import argparse,json,sys
from pathlib import Path
PREFIX='EPOCH_TRACE '
REQUIRED={'trace_schema_version','run','scenario','scenario_phase','post_fence_observe_ms','seq','ts_ms','source','base','event_id','event','epoch','attempt_id','generation','owner','state','mode','required_stop_mask','observed_stop_mask','fence_ts_ms','quarantine_deadline_ms','reason','action','result'}
ALLOWED=REQUIRED
FORBIDDEN={'ssid','password','token','authorization','body','credentials','form','complete_form','credential_body'}
DRIVER={'sta_start','sta_stop','sta_connected','sta_disconnected','ap_start','ap_stop','got_ip','lost_ip','wifi_other','ip_other'}
STOP={'sta_stop':1,'ap_stop':2}; MODE_MASK={1:1,2:2,3:3}; SCENARIOS=set('ABCDEFG')
PHASES={
'A':{'first_attempt','first_failed','attempt_timeout','stopping','post_fence_observation','post_fence_complete','second_attempt','terminal'},
'B':{'first_attempt','replacement_pending','stopping','post_fence_observation','post_fence_complete','second_attempt','terminal'},
'C':{'first_attempt','replacement_pending','stopping','post_fence_observation','post_fence_complete','second_attempt','terminal'},
'D':{'first_attempt','candidate_failed','attempt_timeout','stopping','post_fence_observation','post_fence_complete','second_attempt','ap_observation','ap_observation_complete','terminal'},
'E':{'first_attempt','candidate_connected','sta_observation','sta_observation_complete','stopping','post_fence_observation','post_fence_complete','terminal'},
'F':{'first_attempt','attempt_timeout','stopping','post_fence_observation','post_fence_complete','terminal'},
'G':{'first_attempt','replacement_pending','stopping','post_fence_observation','post_fence_complete','second_attempt','terminal'}}

def check_records(lines):
 failures=[];runs={};count=0
 def bad(n,m):failures.append((n,m))
 for n,line in enumerate(lines,1):
  if PREFIX not in line:continue
  try:r=json.loads(line.split(PREFIX,1)[1])
  except json.JSONDecodeError as e:bad(n,f'invalid JSON: {e.msg}');continue
  count+=1;miss=REQUIRED-r.keys()
  if FORBIDDEN & {str(k).lower() for k in r}:bad(n,'secret fields present')
  if miss:bad(n,f'missing fields: {sorted(miss)}');continue
  if r.keys()-ALLOWED:bad(n,f'unexpected fields: {sorted(r.keys()-ALLOWED)}');continue
  if r['trace_schema_version']!=1:bad(n,'unknown trace schema version');continue
  if r['scenario'] not in SCENARIOS:bad(n,'unknown scenario');continue
  q=runs.setdefault(r['run'],{'s':r['scenario'],'observe':r['post_fence_observe_ms'],'seq':-1,'ts':-1,'active':None,'epochs':{},'attempts':set(),'complete':0,'fault':False,'rows':[],'line':n})
  q['rows'].append((n,r));q['line']=n
  if r['scenario']!=q['s']:bad(n,'scenario changed within run')
  if r['scenario_phase'] not in PHASES[r['scenario']]:bad(n,'unknown or foreign scenario phase')
  if r['post_fence_observe_ms']!=q['observe'] or r['post_fence_observe_ms']<=0:bad(n,'invalid or changed quarantine interval')
  if q['complete']:bad(n,'trace record after run_complete')
  if r['seq']<=q['seq']:bad(n,'trace sequence is not strictly increasing')
  if r['ts_ms']<q['ts']:bad(n,'timestamp moved backwards')
  q['seq'],q['ts']=r['seq'],r['ts_ms'];a,e,ep=r['action'],r['event'],r['epoch'];ctx=(ep,r['attempt_id'],r['generation'],r['owner'],r['mode'])
  expected_phase={'stop_requested':'stopping','fence_observed':'post_fence_observation','post_fence_observation_complete':'post_fence_complete','epoch_release':'post_fence_complete','run_complete':'terminal'}.get(a)
  if expected_phase and r['scenario_phase']!=expected_phase:bad(n,'action inconsistent with scenario phase')
  if a=='epoch_start' and r['scenario_phase'] not in {'first_attempt','second_attempt'}:bad(n,'epoch start has invalid scenario phase')
  if a=='probe_fault' or (a in {'post_fence','queue_send','driver_call','set_mode_sta'} and r['result']!='ok'):q['fault']=True;bad(n,f'probe fault: {e}')
  if a=='epoch_start':
   if q['active'] is not None:bad(n,'new physical epoch before prior release')
   if ep in q['epochs']:bad(n,'epoch reused')
   if r['attempt_id'] in q['attempts']:bad(n,'attempt ID reused')
   expected=MODE_MASK.get(r['mode'],0)
   if r['required_stop_mask']!=expected:bad(n,'wrong required stop mask')
   q['epochs'][ep]={'ctx':ctx,'req':expected,'obs':0,'stopping':False,'fence':False,'fence_ts':None,'deadline':None,'observe':False,'release':False,'ap':False,'sta':False,'outcome':None}
   q['attempts'].add(r['attempt_id']);q['active']=ep
  elif ep in q['epochs'] and a!='desired_generation_update' and ctx!=q['epochs'][ep]['ctx']:bad(n,'physical epoch context changed or ownership overlap')
  x=q['epochs'].get(ep)
  terminal={'got_ip':('IP_EVENT',0,'success'),'sta_disconnected':('WIFI_EVENT',5,'failure'),'ap_start':('WIFI_EVENT',12,'success')}
  if e in terminal:
   eb,eid,_=terminal[e]
   if r['base']!=eb or r['event_id']!=eid:bad(n,f'{e} raw event mismatch')
  for symbol,(eb,eid,_) in terminal.items():
   if r['base']==eb and r['event_id']==eid and e!=symbol:bad(n,f'{symbol} raw event mismatch')
  if x and q['active']==ep and not x['stopping'] and not x['fence'] and x['outcome'] is None and e in terminal:
   _,_,physical_result=terminal[e]
   attributable=(e in {'got_ip','sta_disconnected'} and x['ctx'][4] in (1,3)) or (e=='ap_start' and x['ctx'][4]==2)
   if attributable and (a!='attempt_outcome' or r['result']!=physical_result):bad(n,'active terminal event must be attempt outcome')
  if a=='attempt_outcome':
   if r['result'] not in {'success','failure','timeout','replaced'}:bad(n,'invalid attempt outcome result')
   if not x:bad(n,'attempt outcome without started epoch')
   elif q['active']!=ep or x['release']:bad(n,'attempt outcome for inactive epoch')
   elif ctx!=x['ctx']:bad(n,'attempt outcome context changed')
   elif x['stopping'] or x['fence']:bad(n,'attempt outcome after stop')
   else:
    evidence=(r['base'],r['event_id'],e,r['result'])
    valid={('IP_EVENT',0,'got_ip','success'),('WIFI_EVENT',5,'sta_disconnected','failure'),('WIFI_EVENT',12,'ap_start','success'),('PROBE',0,'attempt_timeout','timeout'),('PROBE',0,'replacement_requested','replaced')}
    if evidence not in valid:bad(n,'invalid attempt outcome evidence')
    if x['outcome'] is not None:bad(n,'duplicate attempt outcome' if x['outcome']==evidence else 'conflicting attempt outcome')
    else:x['outcome']=evidence
  if x and r['required_stop_mask']!=x['req']:
   if q['s']=='E' and a=='set_mode_sta' and r['result']=='ok' and r['required_stop_mask']==1:x['req']=1
   else:bad(n,'required stop mask changed unexpectedly')
  if a=='ap_configured' and r['result']=='ok' and x:x['ap']=True
  if a=='sta_configured' and r['result']=='ok' and x:x['sta']=True
  if a=='stop_requested' and x:
   if x['outcome'] is None:bad(n,'stop requested before attempt outcome')
   if x['stopping']:bad(n,'repeated stop request while stopping')
   x['stopping']=True
  if e in DRIVER:
   if not x:bad(n,'driver event has no attributable physical context')
   elif x['fence']:bad(n,'driver event during post-fence observation')
   if e in STOP and x:
    bit=STOP[e]
    if not x['stopping']:
     if a=='fence_observed':bad(n,'stop event produced fence while context active')
    else:
     if not (x['req']&bit):bad(n,'stop event does not apply to requested mode')
     elif x['obs']&bit:bad(n,'duplicate stop event')
     else:x['obs']|=bit
     if r['observed_stop_mask']!=x['obs']:bad(n,'observed stop mask mismatch')
  if a=='fence_observed' and x:
   if not x['stopping'] or x['obs']!=x['req']:bad(n,'fence before required stop mask complete')
   if x['fence']:bad(n,'duplicate fence')
   x['fence']=True;x['fence_ts']=r['ts_ms'];x['deadline']=r['quarantine_deadline_ms']
   if r['fence_ts_ms']!=r['ts_ms']:bad(n,'fence timestamp inconsistent')
   if x['deadline']<x['fence_ts'] or x['deadline']-x['fence_ts']!=q['observe']:bad(n,'quarantine deadline inconsistent or overflowed')
  if x and x['fence'] and a not in {'epoch_start'} and r['quarantine_deadline_ms']!=x['deadline']:bad(n,'queue activity reset quarantine deadline')
  if a=='post_fence_observation_complete' and x:
   if not x['fence']:bad(n,'observation completed before fence')
   elif r['ts_ms']<x['deadline'] or r['ts_ms']-x['fence_ts']<q['observe']:bad(n,'quarantine interval too short')
   else:x['observe']=True
  if a=='epoch_release' and x:
   if x['outcome'] is None:bad(n,'release before attempt outcome')
   if not x['observe']:bad(n,'release before post-fence observation completes')
   else:x['release']=True;q['active']=None
  if a=='callback_reconnect':bad(n,'hidden callback reconnect')
  if a in {'cancel_ack','retry_start'} and x and not x['fence']:bad(n,f'{a} occurred before fence')
  if a=='run_complete':
   q['complete']+=1
   if q['complete']!=1:bad(n,'duplicate run_complete')
   if r['scenario_phase']!='terminal':bad(n,'run_complete before terminal scenario phase')
   if q['active'] is not None:bad(n,'run_complete while epoch active')
   if q['fault']:bad(n,'run_complete after probe or scenario fault')
   if any(x['outcome'] is None for x in q['epochs'].values()):bad(n,'run_complete with missing attempt outcome')
 for rid,q in runs.items():
  rr=[r for _,r in q['rows']];starts=[r for r in rr if r['action']=='epoch_start']; pos=lambda pred:[i for i,r in enumerate(rr) if pred(r)]
  def req(c,m):
   if not c:bad(q['line'],m)
  for ep,x in q['epochs'].items():
   if x['outcome'] is None:bad(q['line'],f'epoch {ep} missing attempt outcome')
   if x['ctx'][4] in (2,3) and not x['ap']:bad(q['line'],f'epoch {ep} missing AP configuration')
   if x['ctx'][4] in (1,3) and not x['sta']:bad(q['line'],f'epoch {ep} missing STA configuration')
   if x['obs']!=x['req']:bad(q['line'],f'epoch {ep} incomplete stop mask')
   if not x['fence']:bad(q['line'],f'epoch {ep} ended before fence')
   elif not x['observe']:bad(q['line'],f'epoch {ep} ended during quarantine')
   elif not x['release']:bad(q['line'],f'epoch {ep} ended before release')
  s=q['s']
  def order(spec,msg):
   indexes=[]
   for action,epoch in spec:
    found=pos(lambda r,a=action,e=epoch:r['action']==a and (e is None or r['epoch']==e))
    if not found:req(False,msg);return
    indexes.append(found[0])
   req(indexes==sorted(indexes) and len(set(indexes))==len(indexes),msg)
  outcome=lambda ep,result:pos(lambda r:r['action']=='attempt_outcome' and r['epoch']==ep and r['result']==result)
  if s=='A':req(len(starts)==2,'scenario A requires exactly one retry');req(bool(outcome(1,'failure') or outcome(1,'timeout')),'scenario A first outcome invalid');req(bool(outcome(2,'success')),'scenario A second outcome invalid');order([('epoch_start',1),('stop_requested',1),('epoch_release',1),('epoch_start',2),('stop_requested',2),('epoch_release',2),('run_complete',None)],'scenario A ordered evidence invalid')
  elif s=='B':order([('epoch_start',1),('desired_generation_update',1),('attempt_outcome',1),('stop_requested',1),('epoch_release',1),('epoch_start',2),('attempt_outcome',2),('epoch_release',2)],'scenario B replacement ordering invalid');req(bool(outcome(1,'replaced') and outcome(2,'success')) and len(starts)==2 and starts[1]['generation']==2,'scenario B replacement generation invalid')
  elif s=='C':
   update2=pos(lambda r:r['action']=='desired_generation_update' and r['epoch']==1 and r['generation']==2);update3=pos(lambda r:r['action']=='desired_generation_update' and r['epoch']==1 and r['generation']==3);replaced=outcome(1,'replaced');stop=pos(lambda r:r['action']=='stop_requested' and r['epoch']==1)
   req(len(update2)==1 and len(update3)==1 and len(replaced)==1 and len(stop)==1 and update2[0]<update3[0]<replaced[0]<stop[0],'scenario C update ordering invalid')
   req(bool(outcome(2,'success')) and len(starts)==2 and starts[1]['generation']==3 and all(r['generation']!=2 for r in starts),'scenario C generation sequence invalid')
  elif s=='D':order([('epoch_start',1),('attempt_outcome',1),('response_complete',1),('stop_requested',1),('epoch_release',1),('epoch_start',2),('ap_configured',2),('attempt_outcome',2),('ap_observation_complete',2),('stop_requested',2),('epoch_release',2)],'scenario D ordered evidence invalid');req(bool(outcome(1,'failure') or outcome(1,'timeout')) and bool(outcome(2,'success')),'scenario D outcomes invalid');req(len(starts)==2 and starts[0]['owner']=='portal' and starts[0]['mode']==3 and starts[1]['owner']=='portal' and starts[1]['mode']==2,'scenario D epoch modes invalid')
  elif s=='E':order([('ap_configured',1),('sta_configured',1),('attempt_outcome',1),('persist_simulated',1),('response_complete',1),('set_mode_sta',1),('sta_observation_complete',1),('stop_requested',1),('fence_observed',1),('post_fence_observation_complete',1),('epoch_release',1),('run_complete',None)],'scenario E ordered evidence invalid');req(bool(outcome(1,'success')),'scenario E exact GOT_IP outcome missing');mode=pos(lambda r:r['action']=='set_mode_sta');obs=pos(lambda r:r['action']=='sta_observation_complete');req(not any(r['event']=='sta_disconnected' for r in rr[mode[0]:obs[0]+1]) if mode and obs else False,'scenario E disconnected during observation')
  elif s=='F':order([('epoch_start',1),('attempt_outcome',1),('stop_requested',1),('fence_observed',1),('epoch_release',1)],'scenario F timeout ordering invalid');req(bool(outcome(1,'timeout')) and not any(r['event']=='got_ip' for r in rr),'scenario F connected before timeout')
  elif s=='G':order([('epoch_start',1),('configuration_api_submission',1),('desired_generation_update',1),('attempt_outcome',1),('stop_requested',1),('epoch_release',1),('epoch_start',2),('attempt_outcome',2),('epoch_release',2)],'scenario G replacement ordering invalid');req(bool(outcome(1,'replaced') and outcome(2,'success')) and len(starts)==2 and starts[0]['owner']==starts[1]['owner'] and starts[1]['generation']==2,'scenario G replacement owner or generation invalid')
  if q['complete']!=1:bad(q['line'],f'run {rid} missing run_complete')
  if q['active'] is not None:bad(q['line'],f'run {rid} ended with active epoch')
 if not count:bad(0,'no trace records found')
 return failures,count

def main(argv=None):
 p=argparse.ArgumentParser();p.add_argument('trace',type=Path);a=p.parse_args(argv);f,c=check_records(a.trace.read_text().splitlines())
 if f:
  for n,m in f:print(f'line {n}: {m}',file=sys.stderr)
  return 1
 print(f'trace valid: {c} records');return 0
if __name__=='__main__':raise SystemExit(main())
