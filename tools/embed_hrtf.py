#!/usr/bin/env python3
"""Embed the pinned, public ISC HRIR data in a client library."""
import hashlib
import json
from pathlib import Path
import sys

source, manifest, target = map(Path, sys.argv[1:])
data=source.read_bytes()
if hashlib.sha256(data).hexdigest()!=json.loads(manifest.read_text())['compressed_sha256']:
    raise SystemExit('HRTF data checksum mismatch')
with target.open('w') as out:
    out.write('#include <cstddef>\nnamespace iron {\nextern const unsigned char embeddedHrtf[]={\n')
    for i in range(0,len(data),32):out.write(','.join(str(v) for v in data[i:i+32])+',\n')
    out.write('};\nextern const std::size_t embeddedHrtfSize=sizeof(embeddedHrtf);\n}\n')
