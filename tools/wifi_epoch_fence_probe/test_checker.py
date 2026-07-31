import json,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).parent;CHECK=ROOT/'check_trace.py';TRACES=ROOT/'traces'
class CheckerFixtures(unittest.TestCase):
 def run_check(self,path):return subprocess.run([sys.executable,str(CHECK),str(path)],text=True,capture_output=True)
 def test_all_scenarios_and_evidence_stage_truncations(self):
  for path in sorted(TRACES.glob('pass_scenario_*.jsonl')):
   lines=path.read_text().splitlines();rows=[json.loads(x[12:]) for x in lines]
   self.assertEqual(self.run_check(path).returncode,0,path.name)
   cuts={len(lines)-1}
   for i,r in enumerate(rows,1):
    if r['event'] in {'sta_stop','ap_stop'} or r['action'] in {'fence_observed','post_fence_observation_complete','epoch_release'}:cuts.add(i)
   for length in sorted(cuts):
    if length>=len(lines):continue
    with tempfile.NamedTemporaryFile('w',delete=False) as f:f.write('\n'.join(lines[:length])+'\n');name=f.name
    self.assertNotEqual(self.run_check(name).returncode,0,f'{path.name} prefix {length}')
 def test_each_failure_has_intended_reason(self):
  for name,reason in json.loads((ROOT/'expected_failures.json').read_text()).items():
   result=self.run_check(TRACES/name);self.assertNotEqual(result.returncode,0,name);self.assertIn(reason,result.stderr,name)
if __name__=='__main__':unittest.main()
