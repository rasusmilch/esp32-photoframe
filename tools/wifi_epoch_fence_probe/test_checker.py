import json,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).parent; CHECK=ROOT/'check_trace.py'; TRACES=ROOT/'traces'
class CheckerFixtures(unittest.TestCase):
 def run_check(self,path): return subprocess.run([sys.executable,str(CHECK),str(path)],text=True,capture_output=True)
 def test_all_scenarios_and_multiple_truncations(self):
  for path in sorted(TRACES.glob('pass_scenario_*.jsonl')):
   lines=path.read_text().splitlines(); self.assertIn('"run_complete"',lines[-1]); self.assertEqual(self.run_check(path).returncode,0,path.name)
   for cut in (4,3,1):
    with tempfile.NamedTemporaryFile('w',delete=False) as f: f.write('\n'.join(lines[:-cut])+'\n'); name=f.name
    self.assertNotEqual(self.run_check(name).returncode,0,f'{path.name} cut {cut}')
 def test_each_failure_has_intended_reason(self):
  for name,reason in json.loads((ROOT/'expected_failures.json').read_text()).items():
   result=self.run_check(TRACES/name); self.assertNotEqual(result.returncode,0,name); self.assertIn(reason,result.stderr,name)
if __name__=='__main__': unittest.main()
