import copy,json,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).parent;CHECK=ROOT/'check_trace.py';TRACES=ROOT/'traces'
class CheckerFixtures(unittest.TestCase):
 def run_check(self,path):return subprocess.run([sys.executable,str(CHECK),str(path)],text=True,capture_output=True)
 def check_rows(self,rows):
  with tempfile.NamedTemporaryFile('w',delete=False) as f:
   f.write(''.join('EPOCH_TRACE '+json.dumps(r)+'\n' for r in rows));name=f.name
  return self.run_check(name)
 def test_all_scenarios_and_evidence_stage_truncations(self):
  for path in sorted(TRACES.glob('pass_scenario_*.jsonl')):
   lines=path.read_text().splitlines();rows=[json.loads(x[12:]) for x in lines]
   self.assertEqual(self.run_check(path).returncode,0,path.name)
   cuts={len(lines)-1}
   for i,r in enumerate(rows,1):
    if r['event'] in {'sta_stop','ap_stop'} or r['action'] in {'attempt_outcome','stop_requested','fence_observed','post_fence_observation_complete','epoch_release','run_complete'}:cuts.update({i-1,i})
   for length in sorted(cuts):
    if length>=len(lines):continue
    with tempfile.NamedTemporaryFile('w',delete=False) as f:f.write('\n'.join(lines[:length])+'\n');name=f.name
    self.assertNotEqual(self.run_check(name).returncode,0,f'{path.name} prefix {length}')
 def test_each_failure_has_intended_reason(self):
  expected=json.loads((ROOT/'expected_failures.json').read_text())
  self.assertEqual(set(expected),{p.name for p in TRACES.glob('fail_*.jsonl')})
  for name,reason in expected.items():
   result=self.run_check(TRACES/name);self.assertNotEqual(result.returncode,0,name);self.assertIn(reason,result.stderr,name)
 def test_table_driven_schema_outcome_and_evidence_rejections(self):
  base=[json.loads(x[12:]) for x in (TRACES/'pass_scenario_f.jsonl').read_text().splitlines()]
  oi=next(i for i,r in enumerate(base) if r['action']=='attempt_outcome')
  cases=[]
  def add(name,mutate,reason):
   rows=copy.deepcopy(base);mutate(rows);cases.append((name,rows,reason))
  add('invalid result',lambda r:r[oi].__setitem__('result','cancelled'),'invalid attempt outcome result')
  add('duplicate',lambda r:r.insert(oi+1,dict(r[oi],seq=r[oi]['seq']+0.5)),'duplicate attempt outcome')
  add('conflicting',lambda r:r.insert(oi+1,dict(r[oi],seq=r[oi]['seq']+0.5,base='WIFI_EVENT',event_id=5,event='sta_disconnected',result='failure')),'conflicting attempt outcome')
  add('attempt id',lambda r:r[oi].__setitem__('attempt_id',99),'context changed')
  add('generation',lambda r:r[oi].__setitem__('generation',99),'context changed')
  add('owner',lambda r:r[oi].__setitem__('owner','portal'),'context changed')
  add('mode',lambda r:r[oi].__setitem__('mode',2),'context changed')
  add('wrong evidence',lambda r:r[oi].update(base='IP_EVENT',event='got_ip',result='timeout'),'invalid attempt outcome evidence')
  add('missing schema',lambda r:r[0].pop('trace_schema_version'),'missing fields')
  add('unknown schema',lambda r:r[0].__setitem__('trace_schema_version',2),'unknown trace schema version')
  add('unexpected field',lambda r:r[0].__setitem__('extra','x'),'unexpected fields')
  add('secret field',lambda r:r[0].__setitem__('password','x'),'secret fields present')
  add('unstarted epoch',lambda r:r[oi].update(epoch=99,attempt_id=99),'attempt outcome without started epoch')
  def after_stop(rows):
   outcome=rows.pop(oi);stop=next(i for i,r in enumerate(rows) if r['action']=='stop_requested');rows.insert(stop+1,outcome)
  add('after stop',after_stop,'attempt outcome after stop')
  add('probe fault outcome',lambda r:r[oi].update(event='probe_fault',result='failure'),'invalid attempt outcome evidence')
  for name,rows,reason in cases:
   result=self.check_rows(rows);self.assertNotEqual(result.returncode,0,name);self.assertIn(reason,result.stderr,name)
 def test_exact_driver_event_mappings(self):
  sources=[('pass_scenario_a.jsonl','got_ip','got_ip raw event mismatch'),('pass_scenario_a.jsonl','sta_disconnected','sta_disconnected raw event mismatch'),('pass_scenario_d.jsonl','ap_start','ap_start raw event mismatch')]
  for filename,event,reason in sources:
   rows=[json.loads(x[12:]) for x in (TRACES/filename).read_text().splitlines()]
   row=next(r for r in rows if r['event']==event);row['event_id']=99
   result=self.check_rows(rows);self.assertNotEqual(result.returncode,0,filename);self.assertIn(reason,result.stderr)
 def test_generic_success_stale_event_and_second_timeout_fail(self):
  for filename,event,missing in [('pass_scenario_d.jsonl','ap_start','missing attempt outcome'),('pass_scenario_e.jsonl','got_ip','missing attempt outcome')]:
   rows=[json.loads(x[12:]) for x in (TRACES/filename).read_text().splitlines()]
   row=next(r for r in rows if r['event']==event);row.update(action='event_observed',result='ok')
   result=self.check_rows(rows);self.assertNotEqual(result.returncode,0,filename);self.assertIn(missing,result.stderr)
  rows=[json.loads(x[12:]) for x in (TRACES/'pass_scenario_a.jsonl').read_text().splitlines()]
  second=next(r for r in rows if r['action']=='attempt_outcome' and r['epoch']==2);second.update(epoch=1,attempt_id=1,generation=1)
  result=self.check_rows(rows);self.assertNotEqual(result.returncode,0);self.assertIn('inactive epoch',result.stderr)
  rows=[json.loads(x[12:]) for x in (TRACES/'pass_scenario_e.jsonl').read_text().splitlines()]
  oi=next(i for i,r in enumerate(rows) if r['action']=='attempt_outcome');rows.insert(oi+1,dict(rows[oi],seq=rows[oi]['seq']+0.5,base='PROBE',event_id=0,event='attempt_timeout',result='timeout'))
  result=self.check_rows(rows);self.assertNotEqual(result.returncode,0);self.assertIn('conflicting attempt outcome',result.stderr)
if __name__=='__main__':unittest.main()
