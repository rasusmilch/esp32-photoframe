#!/usr/bin/env python3
"""Strict standard-library validator for EPOCH_TRACE JSON-lines captures."""
import argparse,json,sys
from pathlib import Path
PREFIX='EPOCH_TRACE '
REQUIRED={'run','scenario','scenario_phase','post_fence_observe_ms','seq','ts_ms','source','base','event_id','event','epoch','attempt_id','generation','owner','state','mode','reason','action','result'}
FORBIDDEN={'ssid','password','token','authorization','body','credentials'}
DRIVER={'sta_start','sta_stop','sta_connected','sta_disconnected','ap_start','ap_stop','got_ip','lost_ip','wifi_other','ip_other'}
SCENARIOS=set('ABCDEFG')

def check_records(lines):
 failures=[]; runs={}; records=0
 def fail(n,m): failures.append((n,m))
 for n,line in enumerate(lines,1):
  if PREFIX not in line: continue
  try: r=json.loads(line.split(PREFIX,1)[1])
  except json.JSONDecodeError as e: fail(n,f'invalid JSON: {e.msg}'); continue
  records+=1; missing=REQUIRED-r.keys()
  if missing: fail(n,f'missing fields: {sorted(missing)}'); continue
  if FORBIDDEN & {str(k).lower() for k in r}: fail(n,'secret fields present')
  if r['scenario'] not in SCENARIOS: fail(n,'unknown scenario')
  q=runs.setdefault(r['run'],{'scenario':r['scenario'],'observe':r['post_fence_observe_ms'],'seq':-1,'ts':-1,'active':None,'epochs':{},'attempts':set(),'complete':0,'fault':False,'records':[],'line':n})
  q['records'].append((n,r)); q['line']=n
  if r['scenario']!=q['scenario']: fail(n,'scenario changed within run')
  if r['post_fence_observe_ms']!=q['observe'] or r['post_fence_observe_ms']<=0: fail(n,'post-fence observation setting changed or invalid')
  if q['complete']: fail(n,'trace record after run_complete')
  if r['seq']<=q['seq']: fail(n,'trace sequence is not strictly increasing')
  if r['ts_ms']<q['ts']: fail(n,'timestamp moved backwards')
  q['seq'],q['ts']=r['seq'],r['ts_ms']
  a,e,ep=r['action'],r['event'],r['epoch']; ctx=(ep,r['attempt_id'],r['generation'],r['owner'])
  if a=='probe_fault' or (a in {'post_fence','queue_send','driver_call','set_mode_sta'} and r['result']!='ok'):
   q['fault']=True; fail(n,f'probe fault: {e}')
  if a=='epoch_start':
   if q['active'] is not None: fail(n,'new physical epoch before prior release')
   if ep in q['epochs']: fail(n,'epoch reused')
   if r['attempt_id'] in q['attempts']: fail(n,'attempt ID reused')
   q['epochs'][ep]={'ctx':ctx,'mode':r['mode'],'ap':False,'sta':False,'stop':False,'fence':False,'observe':False,'release':False,'fenced':False}
   q['attempts'].add(r['attempt_id']); q['active']=ep
  elif ep in q['epochs'] and a!='desired_generation_update' and ctx!=q['epochs'][ep]['ctx']: fail(n,'physical epoch context changed or ownership overlap')
  if e in DRIVER:
   if not ep or ep not in q['epochs']: fail(n,'driver event has no attributable physical context')
   elif q['epochs'][ep]['fence']: fail(n,'driver event during post-fence observation')
   if e in {'sta_stop','ap_stop'} and ep in q['epochs']: q['epochs'][ep]['stop']=True
  if a=='ap_configured' and r['result']=='ok' and ep in q['epochs']: q['epochs'][ep]['ap']=True
  if a=='sta_configured' and r['result']=='ok' and ep in q['epochs']: q['epochs'][ep]['sta']=True
  if a=='stop_requested' and ep in q['epochs']:
   if q['epochs'][ep].get('stopping'): fail(n,'repeated stop request while stopping')
   q['epochs'][ep]['stopping']=True
  if a=='callback_reconnect': fail(n,'hidden callback reconnect')
  if a in {'cancel_ack','retry_start'} and ep in q['epochs'] and not q['epochs'][ep]['fence']:
   fail(n,f'{a} occurred before fence')
  if a=='fence_observed':
   if ep not in q['epochs']: fail(n,'fence for unknown epoch')
   else:
    if not q['epochs'][ep]['stop']: fail(n,'fence observed before stop event')
    q['epochs'][ep]['fence']=True
  if a=='post_fence_observation_complete':
   if ep not in q['epochs'] or not q['epochs'][ep]['fence']: fail(n,'observation completed before fence')
   else: q['epochs'][ep]['observe']=True
  if a=='epoch_release':
   if ep not in q['epochs'] or not q['epochs'][ep]['observe']: fail(n,'release before post-fence observation completes')
   else:
    q['epochs'][ep]['release']=True
    if q['active']!=ep: fail(n,'released epoch is not active')
    q['active']=None
  if a=='run_complete':
   q['complete']+=1
   if q['complete']!=1: fail(n,'duplicate run_complete')
   if r['scenario_phase']!='terminal': fail(n,'run_complete before terminal scenario phase')
   if q['active'] is not None: fail(n,'run_complete while epoch active')
   if q['fault']: fail(n,'run_complete after probe or scenario fault')
 for rid,q in runs.items():
  rec=[r for _,r in q['records']]; actions=[r['action'] for r in rec]; events=[r['event'] for r in rec]; starts=[r for r in rec if r['action']=='epoch_start']
  for ep,x in q['epochs'].items():
   if x['mode'] in (2,3) and not x['ap']: fail(q['line'],f'epoch {ep} missing AP configuration')
   if x['mode'] in (1,3) and not x['sta']: fail(q['line'],f'epoch {ep} missing STA configuration')
   if not x['stop']: fail(q['line'],f'epoch {ep} ended before stop event')
   elif not x['fence']: fail(q['line'],f'epoch {ep} ended before fence')
   elif not x['observe']: fail(q['line'],f'epoch {ep} ended during post-fence observation')
   elif not x['release']: fail(q['line'],f'epoch {ep} ended before release')
  s=q['scenario']
  def require(cond,msg):
   if not cond: fail(q['line'],msg)
  if s=='A':
   require(len(starts)==2,'scenario A requires exactly one retry'); require(any(a in actions for a in ('attempt_failed','attempt_timeout')),'scenario A first attempt did not fail or time out')
   if starts and any(r['event']=='got_ip' and r['epoch']==starts[0]['epoch'] for r in rec): fail(q['line'],'scenario A first attempt succeeded')
  elif s=='B':
   updates=[i for i,r in enumerate(rec) if r['action']=='desired_generation_update' and r['generation']==2]; releases=[i for i,r in enumerate(rec) if r['action']=='epoch_release']; require(len(starts)==2 and updates and releases and updates[0]<releases[0] and rec[updates[0]]['epoch']==starts[0]['epoch'],'scenario B replacement missing or late')
  elif s=='C':
   require(len(starts)==2,'scenario C requires two epochs'); require([r['generation'] for r in rec if r['action']=='desired_generation_update'][:2]==[2,3],'scenario C requires ordered generation 2 and 3 updates')
   if any(r['generation']==2 for r in starts): fail(q['line'],'scenario C started generation 2')
  elif s=='D':
   require(len(starts)==2,'scenario D requires portal and AP-only epochs'); require(starts and starts[0]['mode']==3 and starts[1]['mode']==2,'scenario D second epoch must be AP-only'); require(any(a in actions for a in ('attempt_failed','attempt_timeout','candidate_failed')),'scenario D candidate did not fail'); require('response_complete' in actions,'scenario D response marker missing')
   if starts and any(r['event']=='got_ip' and r['epoch']==starts[0]['epoch'] for r in rec): fail(q['line'],'scenario D candidate succeeded')
  elif s=='E':
   require(len(starts)==1 and 'got_ip' in events,'scenario E missing GOT_IP'); require('persist_simulated' in actions,'scenario E missing persistence marker'); require('response_complete' in actions,'scenario E missing response marker'); require(any(r['action']=='set_mode_sta' and r['result']=='ok' for r in rec),'scenario E mode transition missing or failed'); require('sta_observation_complete' in actions,'scenario E stable observation missing')
   if any(r['event']=='sta_disconnected' and r['scenario_phase']=='sta_observation' for r in rec): fail(q['line'],'scenario E disconnected during observation')
  elif s=='F':
   require('timeout' in actions or 'attempt_timeout' in actions,'scenario F completed without timeout')
   if 'got_ip' in events: fail(q['line'],'scenario F connected before timeout')
  elif s=='G':
   require(len(starts)==2,'scenario G requires replacement epoch'); require('configuration_api_submission' in actions,'scenario G API submission missing'); require(any(r['action']=='desired_generation_update' and r['owner']==starts[0]['owner'] for r in rec),'scenario G replacement used a different owner')
  if q['complete']!=1: fail(q['line'],f'run {rid} missing run_complete')
  if q['active'] is not None: fail(q['line'],f'run {rid} ended with active epoch')
 if not records: fail(0,'no trace records found')
 return failures,records

def main(argv=None):
 p=argparse.ArgumentParser();p.add_argument('trace',type=Path);a=p.parse_args(argv); failures,count=check_records(a.trace.read_text().splitlines())
 if failures:
  for n,m in failures: print(f'line {n}: {m}',file=sys.stderr)
  return 1
 print(f'trace valid: {count} records');return 0
if __name__=='__main__': raise SystemExit(main())
