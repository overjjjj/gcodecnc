"""Link the real MainWindow test against the already-built Debug application objects."""
from pathlib import Path
import os
import re
import subprocess
import sys

root = Path(__file__).absolute().parents[2]
os.chdir(root)
makefile = (root / 'Makefile.Debug').read_text()
def variable(name):
    return re.search(r'^' + name + r'\s*=\s*(.*)$', makefile, re.M).group(1).split()
compiler = 'D:/qt5/Tools/mingw730_64/bin/g++.exe'
env = os.environ.copy()
env['PATH'] = 'D:/qt5/Tools/mingw730_64/bin;D:/qt5/5.14.2/mingw73_64/bin;D:/msys64/mingw64/bin;' + env['PATH']
env['QT_QPA_PLATFORM_PLUGIN_PATH'] = 'D:/qt5/5.14.2/mingw73_64/plugins/platforms'
obj = 'debug/slot_output_gate_mainwindow_test.o'
subprocess.run([compiler, '-std=gnu++17', *variable('DEFINES'), *variable('INCPATH'), '-c',
                'untitled/tests/slot_output_gate_mainwindow_test.cpp', '-o', obj], env=env, check=True)
objects = (root / 'object_script.CNEXT-CAM.Debug').read_text().splitlines()
objects = [name for name in objects if name != 'debug/main.o'] + [obj]
if '--pre-revision-policy' in sys.argv:
    old = 'outputs/slot-batch2-r1/baseline/untitled/src/gcode/ProgramSnapshotStatus.cpp'
    old_obj = 'debug/slot_output_gate_old_status.o'
    subprocess.run([compiler, '-std=gnu++17', *variable('DEFINES'), *variable('INCPATH'), '-c',
                    old, '-o', old_obj], env=env, check=True)
    objects = [name for name in objects if name != 'debug/ProgramSnapshotStatus.o'] + [old_obj]
response = root / 'debug/slot_output_gate_objects.txt'
response.write_text('\n'.join(objects))
binary = 'debug/slot_output_gate_mainwindow_test.exe'
subprocess.run([compiler, '-mthreads', '-o', binary, '@' + str(response), *variable('LIBS')], env=env, check=True)
raise SystemExit(subprocess.run([str(root / binary)], env=env, timeout=40).returncode)
