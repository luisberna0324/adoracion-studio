"""CLI boundary checks; never compile or publish."""
import json, subprocess, sys, tempfile, unittest
from pathlib import Path
SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/local_pipeline.py'
class PipelineCliTests(unittest.TestCase):
    def invoke(self, *args):
        return subprocess.run([sys.executable,str(SCRIPT),*args],text=True,capture_output=True)
    def test_dry_run_has_no_commands(self):
        p=self.invoke('--dry-run','--jobs','99');self.assertEqual(p.returncode,0,p.stderr)
        r=json.loads(p.stdout);self.assertTrue(r['no_commands_executed']);self.assertEqual(r['jobs'],3)
        self.assertFalse(Path(r['output']).exists())
    def test_credentials_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            f=Path(folder)/'settings.json';f.write_text('{"private_key": "fixture"}')
            p=self.invoke('--config',str(f),'--dry-run')
            self.assertEqual(p.returncode,2);self.assertIn('must not contain credentials',p.stderr)
    def test_unknown_target_rejected(self):
        self.assertEqual(self.invoke('--targets','publish').returncode,2)
if __name__=='__main__':unittest.main()
