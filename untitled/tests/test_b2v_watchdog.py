import json
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
DRIVER = ROOT / 'untitled/scripts/run_b2v_diagnostic.py'

class WatchdogTest(unittest.TestCase):
    def test_timeout_covers_process_startup_and_keeps_diagnostics(self):
        self.assertTrue(DRIVER.exists(), 'missing process-level diagnostic driver')
        result = subprocess.run([sys.executable, str(DRIVER), '--name', 'watchdog-test',
                                 '--timeout', '1', '--command', sys.executable, '-c',
                                 'import time; print("application startup", flush=True); time.sleep(30)'],
                                cwd=ROOT, timeout=8)
        self.assertEqual(result.returncode, 124)
        out = ROOT / 'outputs/slot-batch2-verification'
        record = json.loads((out / 'watchdog-test.json').read_text())
        self.assertTrue(record['timed_out'])
        self.assertIn('application startup', (out / 'watchdog-test.stdout.log').read_text())
        self.assertEqual(record['cwd'], str(ROOT))

if __name__ == '__main__':
    unittest.main()
