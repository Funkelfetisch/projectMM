#!/usr/bin/env python3
"""Find application images indistinguishable to the existing OTA header gate.

This is a release audit, not a firmware authenticity or hardware check. Labels
come from the caller and are not asserted to be encoded in the image.
"""
import argparse
from collections import defaultdict
import hashlib
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CPP = r'''
#include "core/util/FirmwareImage.h"
#include <array>
#include <cstdio>
#include <fstream>
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  std::ifstream f(argv[1], std::ios::binary);
  std::array<uint8_t, mm::firmware::kVariantOffset + mm::firmware::kVariantBytes> data{};
  f.read(reinterpret_cast<char*>(data.data()), data.size());
  const auto image = mm::firmware::identify(data.data(), static_cast<size_t>(f.gcount()));
  if (!image.valid || !image.described || !mm::firmware::isAppImage(image.project)) return 3;
  const auto variant = mm::firmware::identifyVariant(data.data(), static_cast<size_t>(f.gcount()));
  if (variant.status == mm::firmware::VariantStatus::Malformed) return 4;
  std::printf("%u\n%s\n%s\n%s\n", static_cast<unsigned>(image.chip), image.project, image.version,
      variant.status == mm::firmware::VariantStatus::Valid ? variant.name : "<untagged>");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('images', metavar='LABEL=PATH', nargs='+')
    args = parser.parse_args()
    inputs = []
    for value in args.images:
        label, sep, path = value.partition('=')
        if not sep or not label or not path:
            parser.error('each input must be LABEL=PATH')
        if any(label == entry[0] for entry in inputs):
            parser.error('labels must be unique')
        inputs.append((label, Path(path).resolve(strict=True)))
    groups = defaultdict(list)
    with tempfile.TemporaryDirectory(prefix='ota-identity-') as directory:
        tmp = Path(directory)
        (tmp / 'identity.cpp').write_text(CPP)
        subprocess.run(['g++', '-std=c++20', '-I', str(ROOT / 'src'), '-I', str(ROOT / 'src/platform/desktop'),
                        str(tmp / 'identity.cpp'), '-o', str(tmp / 'identity')], check=True)
        for label, path in inputs:
            lines = subprocess.check_output([str(tmp / 'identity'), str(path)], text=True).splitlines()
            if len(lines) != 4:
                raise SystemExit(f'{label}: incomplete identity')
            # The OTA upload gate checks chip and project, not the version or caller label.
            key = (lines[0], lines[1])
            groups[key].append(label)
            print(f'{label}: chip={lines[0]} project={lines[1]} version={lines[2]} variant={lines[3]} '
                  f'size={path.stat().st_size} sha256={hashlib.sha256(path.read_bytes()).hexdigest()}')
    collisions = [names for names in groups.values() if len(names) > 1]
    for names in collisions:
        print('AMBIGUOUS for legacy OTA gate: ' + ', '.join(names))
    if collisions:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
