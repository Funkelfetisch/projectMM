#!/usr/bin/env python3
"""Exercise the experimental upload session with a real app image, entirely offline.

This does not exercise flash, BLE, signature verification or device recovery.
"""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CPP = r'''
#include "core/util/FirmwareUploadSession.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
struct Port {
    size_t limit=0, written=0; unsigned commits=0, aborts=0;
    size_t capacity() const { return limit; }
    bool begin(size_t) { written=0; return true; }
    bool write(const uint8_t*,size_t n) { written+=n; return true; }
    bool end(bool commit) { if(commit) ++commits; else ++aborts; return true; }
};
int main(int argc,char** argv) {
    if(argc!=3) return 1;
    std::ifstream in(argv[1],std::ios::binary);
    std::vector<uint8_t> image((std::istreambuf_iterator<char>(in)),{});
    if(image.empty() || std::strlen(argv[2])!=64) return 2;
    using S=mm::FirmwareUploadSession<Port>; using R=S::Result;
    S::Digest digest{};
    for(size_t i=0;i<digest.size();++i) {
        unsigned v=0; if(std::sscanf(argv[2]+2*i,"%2x",&v)!=1) return 3; digest[i]=v;
    }
    Port p; p.limit=image.size(); S upload(p);
    for(int damaged=0;damaged<2;++damaged) {
        if(upload.begin(1,image.size(),digest)!=R::Ok) return 4;
        if(damaged) image[image.size()/2]^=1;
        size_t offset=0;
        while(offset<image.size()) {
            const size_t length=std::min<size_t>(241,image.size()-offset);
            if(upload.append(1,offset,image.data()+offset,length)!=R::Ok) return 5;
            offset+=length;
        }
        const auto result=upload.finish(1);
        if(result!=(damaged?R::Integrity:R::Ok)) return 6;
        if(p.written!=image.size()) return 7;
    }
    if(p.commits!=1 || p.aborts!=1) return 8;
    std::printf("%zu bytes: original commits; one-byte corruption aborts\n", image.size());
}
'''

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--build-dir', type=Path, required=True)
    args = parser.parse_args()
    compiler = shutil.which('c++')
    libraries = [args.build_dir.resolve() / 'libmm_core.a', args.build_dir.resolve() / 'libmm_platform.a']
    if not compiler or not args.image.is_file() or any(not p.is_file() for p in libraries):
        parser.error('C++ compiler, image and desktop libraries required')
    digest = hashlib.file_digest(args.image.open('rb'), 'sha256').hexdigest()
    with tempfile.TemporaryDirectory(prefix='firmware-upload-image-') as directory:
        temp = Path(directory)
        (temp / 'test.cpp').write_text('#include <algorithm>\n#include <cstring>\n' + CPP)
        subprocess.run([compiler, '-std=c++20', '-I', str(ROOT / 'src'), '-I', str(ROOT / 'src/platform/desktop'),
                        str(temp / 'test.cpp'), '-Wl,--start-group', *map(str, libraries), '-Wl,--end-group',
                        '-ldl', '-lpthread', '-o', str(temp / 'test')], check=True)
        print('image sha256:', digest, flush=True)
        subprocess.run([str(temp / 'test'), str(args.image), digest], check=True)

if __name__ == '__main__':
    main()
