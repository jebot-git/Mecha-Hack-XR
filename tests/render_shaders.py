#!/usr/bin/env python3
"""Compile and link exterior/cockpit shaders as desktop GLSL and GLES."""
import os,re,shutil,subprocess,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
validator=os.environ.get('GLSLANG_VALIDATOR') or shutil.which('glslangValidator')
if not validator:raise SystemExit('Set GLSLANG_VALIDATOR to the glslangValidator executable')
count=0
with tempfile.TemporaryDirectory() as temporary:
    for filename in ['render.cpp','world_render.cpp','art.cpp','cockpit.cpp','cockpit_fx_render.cpp','searchlight_render.cpp']:
        shaders=re.findall(r'R"\((#version 330 core.*?)\)"',(root/'src'/filename).read_text(),re.S)
        assert shaders and len(shaders)%2==0,(filename,len(shaders))
        for pair in range(0,len(shaders),2):
            for es in [False,True]:
                files=[]
                for code in shaders[pair:pair+2]:
                    stage='vert' if 'gl_Position' in code else 'frag'
                    if es:code=code.replace('#version 330 core','#version 300 es\nprecision highp float;\nprecision highp int;',1)
                    target=Path(temporary)/(filename+str(pair)+('.es' if es else '')+'.'+stage)
                    target.write_text(code);files.append(str(target))
                result=subprocess.run([validator,'-l',*files],capture_output=True,text=True)
                if result.returncode:raise SystemExit(result.stdout+result.stderr)
                count+=1
print(f'Compiled and linked {count} desktop/GLES shader pairs')
