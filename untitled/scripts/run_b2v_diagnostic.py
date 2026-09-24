"""Run a diagnostic child with a watchdog that also covers QApplication startup."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--name', default='startup')
parser.add_argument('--timeout', type=float, default=40)
parser.add_argument('--qt-first', action='store_true')
parser.add_argument('--command', nargs=argparse.REMAINDER, required=True)
args = parser.parse_args()
out = root / 'outputs/slot-batch2-verification'
out.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
runtime = ['D:/qt5/Tools/mingw730_64/bin', 'D:/qt5/5.14.2/mingw73_64/bin', 'D:/msys64/mingw64/bin']
if args.qt_first:
    runtime[0], runtime[1] = runtime[1], runtime[0]
env['PATH'] = ';'.join(runtime) + ';' + env['PATH']
env['QT_QPA_PLATFORM_PLUGIN_PATH'] = 'D:/qt5/5.14.2/mingw73_64/plugins/platforms'
env['QT_QPA_PLATFORM'] = 'offscreen'
env['QT_DEBUG_PLUGINS'] = '1'
env['QT_LOGGING_TO_CONSOLE'] = '1'
record = {'command': args.command, 'cwd': str(root), 'environment': {
    key: env.get(key) for key in ['PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_QPA_PLATFORM',
                                'QT_DEBUG_PLUGINS', 'QT_LOGGING_TO_CONSOLE', 'QT_QPA_FONTDIR', 'QT_PLUGIN_PATH']},
    'timeout_seconds': args.timeout, 'timed_out': False}
binary = Path(args.command[0]).resolve()
if binary.is_file():
    record['executable_sha256'] = hashlib.sha256(binary.read_bytes()).hexdigest()
    record['beside_executable'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
        for p in binary.parent.iterdir() if p.is_file() and (p.suffix.lower() == '.dll' or p.name == 'qt.conf')}
start = time.monotonic()
with (out / (args.name + '.stdout.log')).open('wb') as stdout, (out / (args.name + '.stderr.log')).open('wb') as stderr:
    child = subprocess.Popen(args.command, cwd=root, env=env, stdout=stdout, stderr=stderr,
                             creationflags=subprocess.CREATE_NO_WINDOW)
    record['pid'] = child.pid
    try:
        record['exit_code'] = child.wait(timeout=args.timeout)
    except subprocess.TimeoutExpired:
        record['timed_out'] = True
        child.kill()
        record['child_exit_code'] = child.wait()
        record['exit_code'] = 124
record['elapsed_seconds'] = time.monotonic() - start
(out / (args.name + '.json')).write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({k: record[k] for k in ['exit_code', 'timed_out', 'elapsed_seconds']}))
raise SystemExit(record['exit_code'])
