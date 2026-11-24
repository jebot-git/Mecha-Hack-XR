#!/usr/bin/env python3
"""Fetch checksum-pinned toolchains and libraries into the ignored .deps tree."""
import argparse
import hashlib
import json
from pathlib import Path
import tarfile
import urllib.request
import zipfile

root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--platform',choices=['all','linux','windows','quest'],default='all')
args=parser.parse_args()
needed={'json','enet','jplspatial','libmysofa','zlib'}
if args.platform in ['all','windows','quest']:needed|={'sdl2','opus'}
if args.platform in ['all','windows']:needed|={'llvm-mingw','glew','openxr'}
if args.platform in ['all','quest']:needed|={'openxr-android'}
cache=root/'.deps/downloads';cache.mkdir(parents=True,exist_ok=True)
for row in json.loads((Path(__file__).parent/'sources.json').read_text()):
    if row['name'] not in needed:continue
    archive=cache/row['archive']
    if not archive.exists():
        temporary=archive.with_name(archive.name+'.download')
        with urllib.request.urlopen(row['url'],timeout=120) as source,temporary.open('wb') as target:
            while block:=source.read(1024*1024):target.write(block)
        temporary.rename(archive)
    if hashlib.file_digest(archive.open('rb'),'sha256').hexdigest()!=row['sha256']:
        raise SystemExit('Checksum mismatch: '+str(archive))
    destination=root/'.deps/src'/row['directory']
    if not destination.exists():
        destination.mkdir(parents=True)
        if archive.suffix=='.aar':
            with zipfile.ZipFile(archive) as source:source.extractall(destination)
        else:
            with tarfile.open(archive) as source:
                prefix=source.getnames()[0].split('/')[0]+'/'
                for member in source.getmembers():
                    member.name=member.name[len(prefix):] if member.name.startswith(prefix) else ''
                    if member.name:source.extract(member,destination,filter='data')
    print('Verified',row['name'],row['sha256'],flush=True)
