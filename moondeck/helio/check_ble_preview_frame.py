#!/usr/bin/env python3
"""Actual preview driver, frame snapshots and BLE dispatcher; offline only."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[2]
CPP=r'''
#include "core/services/BleControlService.h"
#include "helio/BlePreviewFramePage.h"
#include <cstdio>
#include <string>
int main() {
 mm::Scheduler scheduler;
 auto* root=new mm::MoonModule();root->setName("Drivers");root->setTypeName("Drivers");
 auto* preview=new mm::PreviewDriver();preview->setTypeName("PreviewDriver");root->addChild(preview);scheduler.addModule(root);
 mm::Buffer data;if(!data.allocate(65,4))return 2;
 for(int i=0;i<65;i++){data.data()[i*4]=i;data.data()[i*4+1]=2*i;data.data()[i*4+2]=3*i;data.data()[i*4+3]=10;}
 preview->setPreviewWhitePoint(255,128,0);preview->setSourceBuffer(&data);
 mm::BleControlService service;service.setScheduler(&scheduler);service.setPreviewFrameReader(&mm::BlePreviewFramePage::writeCurrent);
 service.setEpochForTest("00000000000000000000000000000001");
 service.handle(R"({"$v":1,"$id":1,"$op":"hello","$nonce":"00000000000000000000000000000002"})",mm::platform::millis());
 auto read=[&](int id,int offset,const char* revision){char msg[256];std::snprintf(msg,sizeof(msg),"{\"$v\":1,\"$id\":%d,\"$op\":\"preview-frame\",\"$e\":\"00000000000000000000000000000001\",\"$s\":1,\"$offset\":%d%s%s%s}",id,offset,revision?",\"$rev\":\"":"",revision?revision:"",revision?"\"":"");service.handle(msg,mm::platform::millis());std::puts(service.response());};
 service.handle(R"({"$v":1,"$id":2,"$op":"info","$e":"00000000000000000000000000000001","$s":1})",mm::platform::millis());std::puts(service.response());
 read(3,0,nullptr);preview->tick();read(4,0,nullptr);
 std::string response(service.response());auto start=response.find("\"revision\":\"")+12;auto rev=response.substr(start,32);
 read(5,32,rev.c_str());read(6,64,rev.c_str());
 data.data()[0]=77;preview->tick();read(7,32,rev.c_str());
 read(8,0,nullptr); preview->setSourceBuffer(&data);read(9,0,nullptr);
 mm::Buffer sphere;if(!sphere.allocate(222,3))return 3;
 for(int i=0;i<222;i++){sphere.data()[i*3]=i;sphere.data()[i*3+1]=255-i;sphere.data()[i*3+2]=42;}
 preview->setSourceBuffer(&sphere);read(10,0,nullptr);preview->tick();read(11,0,nullptr);
 response=service.response();start=response.find("\"revision\":\"")+12;rev=response.substr(start,32);
 for(int offset=32;offset<222;offset+=32)read(11+offset/32,offset,rev.c_str());
 mm::Buffer oversized;if(!oversized.allocate(513,3))return 4;
 preview->setSourceBuffer(&oversized);read(18,0,nullptr);preview->tick();read(19,0,nullptr);
 // MINI80 source-profile size, still a synthetic buffer and not a board test.
 mm::Buffer mini;if(!mini.allocate(52,4))return 5;
 for(int i=0;i<52;i++){mini.data()[i*4]=i;mini.data()[i*4+1]=2*i;mini.data()[i*4+2]=3*i;mini.data()[i*4+3]=10;}
 preview->setSourceBuffer(&mini);read(20,0,nullptr);preview->tick();read(21,0,nullptr);
 response=service.response();start=response.find("\"revision\":\"")+12;rev=response.substr(start,32);
 read(22,32,rev.c_str());
 scheduler.release();
}
'''
def main():
 build=Path(sys.argv[1]);
 with tempfile.TemporaryDirectory(prefix='preview-frame-') as temp:
  temp=Path(temp);source=temp/'test.cpp';exe=temp/'test';source.write_text(CPP)
  subprocess.run(['c++','-std=c++20','-I',str(ROOT/'src'),'-I',str(ROOT/'src/platform/desktop'),str(source),'-Wl,--start-group',str(build/'libmm_core.a'),str(build/'libmm_platform.a'),'-Wl,--end-group','-ldl','-lpthread','-o',str(exe)],check=True)
  lines=subprocess.check_output([str(exe)]).decode().splitlines()
 assert len(lines)==21 and all(len(line.encode())<=512 for line in lines)
 info,pending,a,b,c,stale,new,invalid,*rest=[json.loads(line) for line in lines]
 assert info['previewFrameOp'] and pending['error']=='frame-pending'
 assert a['frame']['total']==65 and a['frame']['next']==32 and b['frame']['next']==64 and c['frame']['next'] is None
 assert a['frame']['revision']==b['frame']['revision']==c['frame']['revision']
 rgb=bytes.fromhex(a['frame']['rgb']+b['frame']['rgb']+c['frame']['rgb'])
 expected=bytes(v for i in range(65) for v in (min(255,i+10),min(255,2*i+5),3*i))
 assert rgb==expected
 assert stale['error']=='frame-changed' and new['frame']['revision']!=a['frame']['revision']
 assert bytes.fromhex(new['frame']['rgb'])[0]==87 and invalid['error']=='frame-pending'
 pending_sphere,*rest=rest
 assert pending_sphere['error']=='frame-pending'
 sphere_pages=rest[:7];assert len(sphere_pages)==7
 assert all(p['frame']['total']==222 and p['frame']['revision']==sphere_pages[0]['frame']['revision'] for p in sphere_pages)
 assert [p['frame']['offset'] for p in sphere_pages]==[0,32,64,96,128,160,192]
 assert sphere_pages[-1]['frame']['next'] is None
 pixels=bytes.fromhex(''.join(p['frame']['rgb'] for p in sphere_pages))
 assert pixels==bytes(v for i in range(222) for v in (i,255-i,42))
 assert rest[7]['error']=='frame-pending' and rest[8]['error']=='frame-too-large'
 assert rest[9]['error']=='frame-pending'
 mini=rest[10:12]; assert len(mini)==2 and mini[0]['frame']['total']==52 and mini[1]['frame']['next'] is None
 assert mini[0]['frame']['revision']==mini[1]['frame']['revision']
 assert bytes.fromhex(''.join(p['frame']['rgb'] for p in mini))==bytes(v for i in range(52) for v in (i+10,2*i+5,3*i))
 print('BLE preview: actual driver, 52 and 65 RGBW and 222 RGB outputs, paging, changed frame, limit and invalidation passed offline')
if __name__=='__main__':main()
