"""Textured, ground-referenced renders of the native rigid tracks."""

from contextlib import contextmanager
import subprocess

import moderngl
import numpy as np
from PIL import Image, ImageDraw, ImageFont

from .glb import atlas, streams


def mesh_binding(model, rig):
    triangles, uv, color = streams(model)
    triangles *= rig["mesh_scale"]
    owners = np.full(len(triangles), -1, dtype=int)
    for i, part in enumerate(rig["parts"]):
        for first, count in part["ranges"]:
            if (
                first % 3
                or count % 3
                or first < 0
                or count < 0
                or first + count > 3 * len(owners)
            ):
                raise ValueError(f"Invalid triangle range on {part['name']}")
            group = owners[first // 3 : (first + count) // 3]
            if np.any(group >= 0):
                raise ValueError("Overlapping triangle groups")
            group[:] = i
    if np.any(owners < 0):
        raise ValueError("Unbound triangles")
    return triangles, uv, color, owners


@contextmanager
def video(path, size, fps):
    args = [
        "ffmpeg",
        "-nostdin",
        "-v",
        "error",
        "-y",
        "-f",
        "rawvideo",
        "-pix_fmt",
        "rgb24",
        "-s",
        f"{size[0]}x{size[1]}",
        "-r",
        str(fps),
        "-i",
        "-",
        "-an",
        "-c:v",
        "libx264",
        "-threads",
        "2",
        "-preset",
        "veryfast",
        "-crf",
        "20",
        "-pix_fmt",
        "yuv420p",
        "-movflags",
        "+faststart",
        str(path),
    ]
    with subprocess.Popen(args, stdin=subprocess.PIPE) as encoder:
        try:
            yield encoder.stdin
        finally:
            encoder.stdin.close()
        if encoder.wait():
            raise RuntimeError(f"Video encoder failed for {path}")


class Studio:
    width, height = 576, 352
    view_height = 308

    def __init__(self, pack):
        self.gl = gl = moderngl.create_standalone_context(backend="egl")
        self.frame = gl.simple_framebuffer((self.width, self.view_height))
        self.frame.use()
        gl.enable(moderngl.DEPTH_TEST)
        self.shader = gl.program(
            vertex_shader="""#version 330
            in vec3 p, color; in vec2 tex; in float joint;
            uniform mat4 bones[64]; uniform vec3 eye, right, up, shift;
            uniform vec2 mid; uniform float scale, aspect, far_plane, grid;
            uniform int ground;
            out vec2 uv; out vec3 tint, pos;
            void main(){
                vec3 v=ground==1?p*grid:(bones[int(joint+.5)]*vec4(p,1)).xyz;
                pos=v; uv=tex; tint=color;
                vec3 rel=v+shift;
                gl_Position=vec4((dot(right,rel)-mid.x)/(scale*aspect),
                                 (dot(up,rel)-mid.y)/scale,ground==1?.9999:-dot(eye,rel)/far_plane,1);
            }""",
            fragment_shader="""#version 330
            in vec2 uv; in vec3 tint, pos; uniform sampler2D paint;
            uniform int ground; uniform float spacing;
            out vec4 frag;
            void main(){
                if(ground==1){
                    vec2 p=pos.xz/spacing;
                    vec2 line=abs(fract(p+.5)-.5)/max(fwidth(p),vec2(.001));
                    float g=1.-clamp(min(line.x,line.y),0.,1.);
                    frag=vec4(mix(vec3(.084,.12,.14),vec3(.19,.26,.29),g*.8),1);
                }else{
                    vec3 n=normalize(cross(dFdx(pos),dFdy(pos)));
                    float lit=.55+.5*abs(dot(n,normalize(vec3(-.5,1.,-.65))));
                    vec4 tile=texture(paint,uv);
                    vec3 albedo=clamp((.40+.80*tile.rgb)*pow(tint,vec3(.78)),0.,1.);
                    frag=vec4(albedo*lit+tile.a*.065,1);
                }
            }""",
        )
        self.paint = gl.texture((256, 256), 4, atlas(pack).tobytes())
        self.paint.filter = (moderngl.NEAREST, moderngl.NEAREST)
        self.paint.use()
        self.eye = np.array([1.15, 0.47, -0.83])
        self.eye /= np.linalg.norm(self.eye)
        self.right = np.cross([0.0, 1.0, 0.0], self.eye)
        self.right /= np.linalg.norm(self.right)
        self.up = np.cross(self.eye, self.right)
        for name in ("eye", "right", "up"):
            self.shader[name].value = tuple(getattr(self, name))
        self.shader["aspect"].value = self.width / self.view_height
        g = np.zeros((6, 9), np.float32)
        g[:, :3] = [
            [-1, 0, -1],
            [1, 0, -1],
            [1, 0, 1],
            [-1, 0, -1],
            [1, 0, 1],
            [-1, 0, 1],
        ]
        self.gbuf, self.ground = self.array(g)
        self.actor = self.abuf = None
        self.font = ImageFont.load_default(size=16)
        self.small = ImageFont.load_default(size=14)

    def array(self, data):
        buf = self.gl.buffer(np.ascontiguousarray(data, np.float32).tobytes())
        vao = self.gl.vertex_array(
            self.shader, [(buf, "3f 3f 2f 1f", "p", "color", "tex", "joint")]
        )
        return buf, vao

    def bind(self, geom, rig):
        triangles, uv, colors, owners = geom
        if self.actor is not None:
            self.actor.release()
            self.abuf.release()
        self.floor = rig["floor"]
        self.joints = len(rig["parts"])
        if self.joints > 64:
            raise ValueError("Preview shader joint limit exceeded")
        xyz = triangles.reshape(-1, 3)
        self.span = max(0.1, float(np.ptp(xyz, axis=0).max()))
        data = np.concatenate(
            [
                xyz,
                np.repeat(colors, 3, axis=0),
                uv.reshape(-1, 2),
                np.repeat(owners, 3)[:, None],
            ],
            axis=1,
        )
        self.abuf, self.actor = self.array(data)
        self.shader["spacing"].value = self.span * 0.22
        # Per-part bounds make camera fitting inexpensive for the entire track.
        self.corners = np.zeros((self.joints, 8, 3))
        self.visible = []
        for i in range(self.joints):
            verts = triangles[owners == i].reshape(-1, 3)
            if not len(verts):
                continue
            self.visible.append(i)
            lo, hi = verts.min(0), verts.max(0)
            self.corners[i] = [
                [x, y, z]
                for x in (lo[0], hi[0])
                for y in (lo[1], hi[1])
                for z in (lo[2], hi[2])
            ]

    def camera(self, track, root):
        # Follow 85% of horizontal travel. World grid stays fixed under the feet.
        relative = root.copy()
        relative[:, [0, 2]] *= 0.15
        relative[:, 1] -= self.floor
        ids = self.visible
        visible = track[:, ids]
        bounds = (
            np.einsum("tjab,jkb->tjka", visible[:, :, :3, :3], self.corners[ids])
            + visible[:, :, None, :3, 3]
            + relative[:, None, None, :]
        )
        view = bounds.reshape(-1, 3) @ np.stack((self.right, self.up), axis=1)
        lo, hi = view.min(0), view.max(0)
        # Include the initial floor under elevated / airborne actors.
        lo[1] = min(lo[1], 0.0)
        half = np.maximum((hi - lo) * 0.5, 0.01)
        scale = (
            max(half[0] * self.view_height / self.width, half[1], self.span * 0.25)
            * 1.15
        )
        self.shader["mid"].value = tuple((lo + hi) * 0.5)
        self.shader["scale"].value = scale
        extent = (float(np.max(np.abs(root))) + self.span + scale) * 12
        self.shader["grid"].value = extent
        self.shader["far_plane"].value = extent * 4

    def draw(self, matrix, root):
        self.frame.clear(0.07, 0.10, 0.13, 1)
        camera = np.array([-root[0] * 0.85, 0, -root[2] * 0.85])
        self.shader["shift"].value = tuple(camera)
        self.shader["ground"].value = 1
        self.ground.render()
        # Also show subsurface and destroyed poses in this cutaway review.
        bones = np.broadcast_to(np.eye(4, dtype=np.float32), (64, 4, 4)).copy()
        bones[: self.joints] = matrix
        self.shader["bones"].write(bones.transpose(0, 2, 1).copy().tobytes())
        self.shader["ground"].value = 0
        self.shader["shift"].value = tuple(camera + root - [0, self.floor, 0])
        self.actor.render()
        return Image.frombytes(
            "RGB", self.frame.size, self.frame.read(components=3)
        ).transpose(Image.Transpose.FLIP_TOP_BOTTOM)

    def labelled(self, matrix, travel, actor, action, progress):
        image = Image.new("RGB", (self.width, self.height), (16, 25, 31))
        image.paste(self.draw(matrix, travel), (0, 23))
        draw = ImageDraw.Draw(image)
        draw.text(
            (9, 3), actor.replace("_", " ").upper(), font=self.font, fill="#e3f1ee"
        )
        draw.text(
            (9, self.height - 19),
            action.replace("-", " ").replace("_", " ").title(),
            font=self.small,
            fill="#a6e0c8",
        )
        draw.rectangle(
            (self.width - 108, self.height - 11, self.width - 9, self.height - 9),
            fill="#435650",
        )
        draw.rectangle(
            (
                self.width - 108,
                self.height - 11,
                self.width - 108 + round(progress * 99),
                self.height - 9,
            ),
            fill="#83d8ba",
        )
        return image
