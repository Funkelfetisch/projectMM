#!/usr/bin/env python3
"""Check a saved BLE scene through filesystem flush and process restart, offline."""
import argparse
import importlib.util, pathlib, tempfile, subprocess, json
root=pathlib.Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('group',root/'moondeck/helio/check_app_group_control.py'); mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
s=mod.CPP.replace('#include "core/services/BleControlService.h"','#include "core/services/BleControlService.h"\n#include "core/system/FilesystemModule.h"\n#include "platform/platform.h"')
s=s.replace('mm::Scheduler scheduler;', 'mm::platform::fsSetRoot(argv[2]); mm::Scheduler scheduler; auto *fs = new mm::FilesystemModule(); fs->setTypeName("FilesystemModule"); fs->setScheduler(&scheduler); scheduler.addModule(fs);')
s=s.replace('if (argc != 2) return 2;', 'if (argc != 3) return 2; scheduler.setup();')
s=s.replace('scheduler.release();','fs->flush(); scheduler.release();')
s=s.replace('scheduler.setup();', 'scheduler.setup(); if (effects->child(0) && effects->child(0)->child(0)) std::cerr << "LOADED=" << effects->child(0)->child(0)->typeName() << std::endl;')
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=pathlib.Path, required=True)
b=parser.parse_args().build_dir.resolve()
with tempfile.TemporaryDirectory(prefix='scene-persist-') as t:
 t=pathlib.Path(t); (t/'firmware.cpp').write_text(s);
 libs=list(b.glob('libmm_*.a')); subprocess.run(['c++','-std=c++20','-I',str(root/'src'),'-I',str(root/'src/platform/desktop'),str(t/'firmware.cpp'),'-Wl,--start-group',*map(str,libs),'-Wl,--end-group','-ldl','-lpthread','-o',str(t/'firmware')],check=True)
 def launch(state):
  state.mkdir()
  return subprocess.Popen([str(t/'firmware'),'1',str(state)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
 def req(p, obj):
  p.stdin.write(json.dumps(obj)+'\n'); p.stdin.flush(); line=p.stdout.readline()
  while line and not line.startswith('{'): line=p.stdout.readline()
  return json.loads(line)
 def stop(p):
  p.stdin.close(); assert p.wait(timeout=10)==0
  return p.stderr.read()
 base={'$v':1,'$e':'00000000000000000000000000000001','$s':1}
 for save in (False, True):
  state=t/('saved' if save else 'transient'); p=launch(state)
  catalog=req(p,base|{'$id':2,'$op':'scenes','$offset':0})['catalog']
  reply=req(p,base|{'$id':3,'$op':'scene','$scene':'liquid-mirror','$catalog':catalog,'$live':True}); assert reply['ok']
  reply=req(p,base|{'$id':4,'$op':'get'}); assert reply['terminal']['state']=='applied'
  if save:
   reply=req(p,base|{'$id':5,'$op':'scene','$scene':'liquid-mirror','$catalog':catalog,'$live':False}); assert reply['ok']
   reply=req(p,base|{'$id':6,'$op':'get'}); assert reply['terminal']['state']=='applied'
  stop(p)
  config=state/'.config/Effects.json'
  persisted=config.exists() and 'PlasmaEffect' in config.read_text()
  assert persisted == save, (save, persisted)
  p=subprocess.Popen([str(t/'firmware'),'1',str(state)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
  req(p,base|{'$id':7,'$op':'get'})
  loaded='LOADED=PlasmaEffect' in stop(p)
  assert loaded == save, (save, loaded)
 print('Transient BLE scene does not survive restart; saving the same scene restores it after restart (offline, forced flush).')
