#!/usr/bin/env python3
"""Disposable-key App-to-firmware authorization seam; no production transport or trust."""
import argparse
import hashlib
import json
import struct
import subprocess
import tempfile
from pathlib import Path
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils
from check_app_update_transfer import CPP, JS, ROOT

p = argparse.ArgumentParser()
p.add_argument('--app-root', required=True, type=Path)
p.add_argument('--esbuild', required=True, type=Path)
p.add_argument('--psa-include', required=True, type=Path)
a = p.parse_args()
image = bytearray(336)
image[0:2] = bytes([0xe9, 1])
struct.pack_into('<H', image, 12, 9)
struct.pack_into('<III', image, 28, 296, 0xabcd5432, 2)
for offset, value in [(48,b'7.1.0'),(80,b'MoonLight'),(288,b'MMVAR01\0'),(296,b'helio-s3')]:
    image[offset:offset+len(value)] = value
checksum = 0xef
for byte in image[32:328]: checksum ^= byte
image[335] = checksum
digest = hashlib.sha256(image).digest()
signed = b'HELIO-FW-AUTH-V1' + struct.pack('>III', 0x11223344, len(image), 2) + digest
key = ec.generate_private_key(ec.SECP256R1())
pub = key.public_key().public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
def sign(claims):
    der = key.sign(claims, ec.ECDSA(hashes.SHA256()))
    r, s = utils.decode_dss_signature(der)
    return r.to_bytes(32, 'big') + s.to_bytes(32, 'big')

signature = sign(signed)
wrong_target = b'HELIO-FW-AUTH-V1' + struct.pack('>III', 0x55667788, len(image), 2) + digest
old_version = b'HELIO-FW-AUTH-V1' + struct.pack('>III', 0x11223344, len(image), 0) + digest

cpp = CPP.replace('#include <algorithm>', '#include <algorithm>\n#include <sstream>\n#include "core/util/FirmwareAuthorizedBackend.h"\n#include "core/util/FirmwareReleasePolicy.h"')
cpp = cpp.replace('bool failCommit = false;', 'bool failCommit = false; unsigned begins = 0, backendCommits = 0, backendAborts = 0;')
cpp = cpp.replace('bool begin(size_t) {data.clear();return true;}', 'bool begin(size_t) {++begins;data.clear();return true;}')
cpp = cpp.replace('bool end(bool commit) {return !commit || !failCommit;}', 'bool end(bool commit) {if(commit) ++backendCommits; else ++backendAborts; return !commit || !failCommit;}')
cpp = cpp.replace('<< ",\\"hasStatus\\":"', '<< ",\\"begins\\":" << backend.begins << ",\\"backendCommits\\":" << backend.backendCommits << ",\\"backendAborts\\":" << backend.backendAborts << ",\\"hasStatus\\":"')
cpp = cpp.replace('int main(int argc, char**) {', '''struct TestPolicy {
 mm::FirmwareReleaseClaims claims{};
 std::array<uint8_t,65> key = {KEY};
 bool verify(size_t size, const std::array<uint8_t,32>& digest) {
  return mm::FirmwareReleasePolicy(claims, 0x11223344, 1, key).verify(size,digest);
 }
};
std::vector<uint8_t> unhex(const std::string& s) {
 std::vector<uint8_t> v; for(size_t i=0;i+1<s.size();i+=2) v.push_back(std::stoul(s.substr(i,2),nullptr,16)); return v;
}
int main(int argc, char**) {'''.replace('KEY', ','.join(map(str,pub))))
cpp = cpp.replace('mm::FirmwareUploadFragmentBridge<Backend> bridge(backend,1000,10000,1000);', 'TestPolicy policy; mm::FirmwareAuthorizedBackend<Backend,TestPolicy> gated(backend,policy);\n mm::FirmwareUploadFragmentBridge<decltype(gated)> bridge(gated,1000,10000,1000);')
cpp = cpp.replace('  if(input=="disconnect")', '''  if(input.rfind("auth ",0)==0) {
   std::istringstream stream(input.substr(5)); std::string b,s; stream>>b>>s;
   auto bytes=unhex(b), sig=unhex(s);
   bool ok=mm::decodeFirmwareReleaseClaims(bytes.data(),bytes.size(),sig.data(),sig.size(),policy.claims);
   std::cout << (ok?"{\\"decoded\\":true}":"{\\"decoded\\":false}") << std::endl; continue;
  }
  if(input=="disconnect")''')
js = JS.replace("const failCommit=", "const mode=process.argv[3]; const tamper=mode==='tamper';\nconst failCommit=")
js = js.replace("async function send(frame:Buffer){child.stdin.write", "async function send(frame:Buffer){if(mode==='image-tamper' && frame[1]===2 && frame.readUInt32LE(2)===100) frame[6]^=1;child.stdin.write")
js = js.replace('let rejectedCommit:any=null;', 'let rejectedCommit:any=null, rejectedBegin:any=null;')
js = js.replace("if(frame[1]===3 && r.result!==0)rejectedCommit=r;", "if(frame[1]===3 && r.result!==0)rejectedCommit=r;if(frame[1]===1 && r.result!==0)rejectedBegin=r;")
js = js.replace('signedBytes:[1,2],signature:[3,4]', 'signedBytes:(mode===\"target\"?TARGET:mode===\"rollback\"?OLD:SIGNED),signature:(mode===\"target\"?TARGETSIG:mode===\"rollback\"?OLDSIG:SIGNATURE)'.replace('TARGETSIG',json.dumps(list(sign(wrong_target)))).replace('OLDSIG',json.dumps(list(sign(old_version)))).replace('TARGET',json.dumps(list(wrong_target))).replace('OLD',json.dumps(list(old_version))).replace('SIGNED',json.dumps(list(signed))).replace('SIGNATURE',json.dumps(list(signature))))
js = js.replace('begin:async()=>{const frame=', '''begin:async(j:any)=>{
  const auth=j.release.authorization;
  if(JSON.stringify(auth.signedBytes)!==JSON.stringify(release.authorization.signedBytes)||JSON.stringify(auth.signature)!==JSON.stringify(release.authorization.signature))throw Error('authorization changed in journal');
  const signature=Buffer.from(auth.signature); if(tamper)signature[0]^=1;
  const decoded=await control('auth '+Buffer.from(auth.signedBytes).toString('hex')+' '+signature.toString('hex'));
  if(!decoded.decoded)throw Error('firmware failed to decode authorization');
  const frame=''')
js = js.replace("const state=app.getSnapshot();if(loseBeginReply)", "const state=app.getSnapshot();if(mode==='image-tamper'){if(state.phase!=='recovery-required'||!saved||writes!==4||commits!==1||!rejectedCommit||rejectedCommit.result!==6||rejectedCommit.phase!==3||rejectedCommit.digest||rejectedCommit.backendCommits!==0||rejectedCommit.backendAborts!==1)throw Error('modified image was not rejected and aborted');console.log('PASS: modified image rejected before backend commit; App retained recovery journal');return;}if(mode!=='valid'){if(!rejectedBegin||rejectedBegin.begins!==0||writes!==0||commits!==0||!saved)throw Error('tampered authorization reached flash');console.log('PASS: '+mode+' authorization denied before flash begin; App retained recovery journal');return;}if(loseBeginReply)")
with tempfile.TemporaryDirectory(prefix='helio-signed-seam-') as name:
    t=Path(name); (t/'firmware.cpp').write_text(cpp)
    subprocess.run(['c++','-std=c++20','-I',str(ROOT/'src'),'-I',str(a.psa_include),str(t/'firmware.cpp'),str(ROOT/'src/core/util/sha256.cpp'),str(ROOT/'src/core/util/FirmwareSignature.cpp'),'-l:libmbedcrypto.so.16','-o',str(t/'firmware')],check=True)
    path=a.app_root.resolve()/'apps/mobile/src/firmware/firmwareUpdate.ts'
    (t/'app.ts').write_text(js.replace('UPDATE',json.dumps(str(path)),1))
    subprocess.run([str(a.esbuild.resolve()),str(t/'app.ts'),'--bundle','--platform=node','--format=cjs','--external:expo-crypto','--outfile='+str(t/'app.cjs')],check=True)
    for mode in ['valid','tamper','target','rollback','image-tamper']:
        subprocess.run(['node',str(t/'app.cjs'),str(t/'firmware'),mode],check=True,timeout=30)
