"""Studio review and editable glTF rigid assembly with baked local joint channels."""
import subprocess
import numpy as np
import moderngl
from PIL import Image, ImageDraw, ImageFont

WIDTH,HEIGHT=480,440


# Use the same hierarchy and animation writer as the full-roster bake.
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[4]/'tools'))
from mecha_motion.glb import atlas, export_rigid_glb, streams


def export_glb(path,model,parts,owners,track,inputs,root,fps):
    export_rigid_glb(path,model,parts,owners,track,[[0,0,-r[1]] for r in inputs],root,fps)


def render(out,root,model,old_owner,new_owner,live,track,inputs,fps):
    gl=moderngl.create_standalone_context(backend='egl')
    fbo=gl.simple_framebuffer((WIDTH*2,HEIGHT*2))
    fbo.use()
    gl.enable(moderngl.DEPTH_TEST)
    prog=gl.program(vertex_shader="""#version 330
    in vec3 p; in vec3 n; in vec3 c; in vec2 tex; uniform vec3 right,up,eye;
    uniform float travel; uniform int mode;
    out vec3 normal, color, pos; out vec2 uv;
    void main(){vec3 v=p;
    if(mode==2){v.x+=v.y*.44; v.z+=v.y*.28;v.y=.013;}
    vec3 rel=v-vec3(0,5.45,0);
    gl_Position=vec4(dot(rel,right)/7.9,dot(rel,up)/7.24,-dot(rel,eye)/180.,1);
    normal=n;color=c;uv=tex;pos=v+vec3(0,0,-travel);}""",
    fragment_shader="""#version 330
    in vec3 normal,color,pos; in vec2 uv;uniform int mode;uniform sampler2D paint;out vec4 frag;
    void main(){if(mode==0){
        vec2 line=abs(fract(pos.xz*.5+.5)-.5)/max(fwidth(pos.xz*.5),vec2(.001));
        float grid=1.-clamp(min(line.x,line.y),0.,1.);
        float fade=exp(-abs(pos.x)*.025);
        vec3 base=mix(vec3(.15,.185,.20),vec3(.24,.29,.30),grid*.66);
        frag=vec4(mix(base,vec3(.09,.12,.14),1.-fade),1);
    }else if(mode==2){frag=vec4(.095,.117,.125,1);}
    else{vec3 N=normalize(normal);float key=max(0.,dot(N,normalize(vec3(-.45,1.,-.82))));
        float rim=max(0.,dot(N,normalize(vec3(.85,.3,.4))));
        vec4 tile=texture(paint,uv);
        vec3 albedo=clamp((.4+tile.rgb*.8)*pow(color,vec3(.78)),0.,1.);
        frag=vec4(albedo*(.65+.51*key)+vec3(.08,.10,.13)*rim+tile.a*.08,1);}}""")
    eye=np.array([1.35,.48,-.68]);eye/=np.linalg.norm(eye)
    right=np.cross([0,1,0],eye);right/=np.linalg.norm(right);up=np.cross(eye,right)
    for k,v in [('eye',eye),('right',right),('up',up)]:prog[k].value=tuple(v)
    tex=gl.texture((256,256),4,atlas(root).tobytes());tex.filter=(moderngl.NEAREST,moderngl.NEAREST);tex.use()
    tris,uv,colors=streams(model)
    def array(v):
        buf=gl.buffer(v.astype('f4').tobytes())
        return buf,gl.vertex_array(prog,[(buf,'3f 3f 3f 2f','p','n','c','tex')])
    floor=np.zeros((6,11))
    floor[:,:3]=[[-180,0,-180],[180,0,-180],[180,0,180],[-180,0,-180],[180,0,180],[-180,0,180]]
    floorbuf,ground=array(floor)
    dynamic=np.zeros((len(tris),3,11))
    dynamic[:,:,6:9]=colors[:,None,:];dynamic[:,:,9:]=uv
    buf,actor=array(dynamic)
    def panel(matrices,owners,distance):
        world=matrices[owners]
        xyz=np.einsum('fij,fkj->fki',world[:,:3,:3],tris)+world[:,None,:3,3]
        n=np.cross(xyz[:,1]-xyz[:,0],xyz[:,2]-xyz[:,0]);n/=np.maximum(.0001,np.linalg.norm(n,axis=1)[:,None])
        dynamic[:,:,:3]=xyz;dynamic[:,:,3:6]=n[:,None,:]
        buf.write(dynamic.astype('f4').tobytes())
        fbo.clear(.19,.225,.24,1)
        prog['travel'].value=distance
        prog['mode'].value=0;ground.render()
        prog['mode'].value=2;actor.render()
        prog['mode'].value=1;actor.render()
        raw=fbo.read(components=3)
        return Image.frombytes('RGB',fbo.size,raw).transpose(Image.Transpose.FLIP_TOP_BOTTOM).resize((WIDTH,HEIGHT),Image.Resampling.LANCZOS)
    font=ImageFont.load_default(size=18);small=ImageFont.load_default(size=14)
    dims=(2*WIDTH,HEIGHT+84)
    ff=subprocess.Popen(['ffmpeg','-loglevel','error','-y','-f','rawvideo','-pix_fmt','rgb24','-s',
        f'{dims[0]}x{dims[1]}','-r',str(fps),'-i','-','-an','-c:v','libx264','-preset','fast','-crf','17',
        '-pix_fmt','yuv420p','-movflags','+faststart',str(out/'mecha-ace.mp4')],stdin=subprocess.PIPE)
    try:
        for i,(t,d,_,_) in enumerate(inputs):
            image=Image.new('RGB',dims,(19,27,31))
            image.paste(panel(live[i],old_owner,d),(0,40));image.paste(panel(track[i],new_owner,d),(WIDTH,40))
            draw=ImageDraw.Draw(image)
            draw.text((18,10),'CURRENT  /  Live NPC gait',font=font,fill='#c7d2d4')
            draw.text((WIDTH+18,10),'PROTOTYPE  /  Planted gait + elbow hinges',font=font,fill='#92d9cf')
            draw.line((WIDTH,0,WIDTH,HEIGHT+40),fill='#405456',width=2)
            stage='Approach' if t<3.2 else 'Brake' if t<4.8 else 'Brace' if t<6.35 else 'Recover'
            draw.text((18,HEIGHT+52),f'MECHA ACE     /     {stage.upper()}',font=small,fill='#e0e9e9')
            draw.text((WIDTH+18,HEIGHT+52),f'{t:.2f} s     /     {d:.1f} m',font=small,fill='#b6c9c9')
            if i==170:image.save(out/'mecha-ace.jpg',quality=92)
            ff.stdin.write(image.tobytes())
    finally:
        ff.stdin.close()
    if ff.wait():raise RuntimeError('Video encoding failed')
