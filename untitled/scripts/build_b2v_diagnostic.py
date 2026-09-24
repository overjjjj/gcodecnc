"""Recompile MainWindow, service, fingerprint, blind-slot strategy, status and diagnostic entry; reuse other 8279 Debug objects, not a clean rebuild."""
from pathlib import Path
import os
import re
import subprocess

root = Path(__file__).resolve().parents[2]
makefile = (root / 'Makefile.Debug').read_text()
def variable(name):
    return re.search(r'^' + name + r'\s*=\s*(.*)$', makefile, re.M).group(1).split()
env = os.environ.copy()
env['PATH'] = 'D:/qt5/Tools/mingw730_64/bin;D:/qt5/5.14.2/mingw73_64/bin;D:/msys64/mingw64/bin;' + env['PATH']
compiler = 'D:/qt5/Tools/mingw730_64/bin/g++.exe'
for source, objname in [('src/strategies/mill/BlindSlotMillingStrategy.cpp', 'BlindSlotMillingStrategy'),
    ('src/gcode/ProgramSnapshotStatus.cpp', 'ProgramSnapshotStatus'),
    ('src/app/MainWindow.cpp', 'MainWindow'),
    ('src/workspaces/program_verification/generation/ProgramGenerationService.cpp', 'ProgramGenerationService'),
    ('src/workspaces/program_verification/fingerprint/ProgramSnapshotFingerprint.cpp', 'ProgramSnapshotFingerprint')]:
    subprocess.run([compiler, '-std=gnu++17', '-g', '-fno-keep-inline-dllexport', '-fexceptions', '-mthreads',
                    *variable('DEFINES'), *variable('INCPATH'), '-c',
                    'untitled/' + source, '-o', 'debug/' + objname + '.o'],
                   cwd=root, env=env, check=True, timeout=150)
obj = 'debug/slot_b2v_startup_test.o'
subprocess.run([compiler, '-std=gnu++17', *variable('DEFINES'), *variable('INCPATH'), '-c',
                'untitled/tests/slot_b2v_startup_test.cpp', '-o', obj], cwd=root, env=env, check=True, timeout=90)
objects = [name for name in (root / 'object_script.CNEXT-CAM.Debug').read_text().splitlines()
           if name != 'debug/main.o'] + [obj]
response = root / 'debug/slot_b2v_objects.txt'
response.write_text('\n'.join(objects))
subprocess.run([compiler, '-mthreads', '-o', 'debug/slot_b2v_startup_test.exe', '@' + str(response),
                *variable('LIBS')], cwd=root, env=env, check=True, timeout=90)
