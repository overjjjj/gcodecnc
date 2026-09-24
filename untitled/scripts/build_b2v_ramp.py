from pathlib import Path
import subprocess,os,re,sys
root=Path(__file__).resolve().parents[2]
out=root/'outputs/slot-batch2-verification';phase=sys.argv[1];build=out/('ramp-'+phase);build.mkdir(exist_ok=True)
make=(root/'Makefile.Debug').read_text(encoding='utf-8')
def var(k):return re.search('^'+k+r'\s*=\s*(.*)$',make,re.M).group(1).split()
env=os.environ.copy();env['PATH']='D:/qt5/Tools/mingw730_64/bin;D:/qt5/5.14.2/mingw73_64/bin;D:/msys64/mingw64/bin;'+env['PATH']
cc='D:/qt5/Tools/mingw730_64/bin/g++.exe'
objs=[]
for src in ['untitled/tests/slot_b2v_ramp_test.cpp','untitled/src/strategies/mill/BlindSlotMillingStrategy.cpp']:
 obj=build/(Path(src).stem+'.o');
 if '--before' in sys.argv and src.endswith('BlindSlotMillingStrategy.cpp'):src=str(out/'ramp-before/BlindSlotMillingStrategy.cpp')
 subprocess.run([cc,'-std=gnu++17',*var('DEFINES'),*var('INCPATH'),'-Iuntitled/src/strategies/mill','-c',src,'-o',str(obj)],cwd=root,env=env,check=True,timeout=60);objs.append(str(obj))
objs += [str(root/'debug'/s) for s in ['Cq8PostProcessor.o','FanucPostProcessor.o','PostProcessorBase.o']]
exe=build/'slot_b2v_ramp_test.exe';subprocess.run([cc,'-mthreads','-o',str(exe),*objs,*var('LIBS')],cwd=root,env=env,check=True,timeout=60)
r=subprocess.run([str(exe),str(build)],cwd=root,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30);(build/'run.log').write_bytes(r.stdout);print(r.stdout.decode(errors='replace'));sys.exit(r.returncode)
