#!/usr/bin/env python3
"""MGR:R (Xbox 360) model viewer — OpenGL via moderngl.

  viewer.py em0200 --out x.png                 # offscreen contact sheet (front, 3/4, side, back)
  viewer.py em0200 --window                    # interactive (left drag = orbit, wheel = zoom)   [window mode not exercised in CI]
Options: --role N (binding role used as the albedo texture, default 0), --no-tex, --alpha-test, --cull, --flat, --list (print materials)

This is a *verification* viewer: it renders the decoded WMB geometry with its decoded WTA/WTP textures and simple lighting. It does not run the
original Xenos shaders (those are the XenosRecomp output; see FETCH_AND_SHADERS.md).
"""
import argparse
import math
import sys
sys.path.insert(0, '/home/lucidfear/Projects/mgr-nx/tools')
import numpy as np
import moderngl
import model_loader

VERT = """
#version 330
uniform mat4 mvp;
uniform mat4 model;
in vec3 in_pos; in vec3 in_nrm; in vec2 in_uv;
out vec3 v_nrm; out vec2 v_uv; out vec3 v_pos;
void main() {
    gl_Position = mvp * vec4(in_pos, 1.0);
    v_nrm = normalize(mat3(model) * in_nrm);
    v_uv = in_uv; v_pos = in_pos;
}
"""
FRAG = """
#version 330
uniform sampler2D tex;
uniform int use_tex;
uniform int alpha_test;
uniform vec3 tint;
uniform float gain;
in vec3 v_nrm; in vec2 v_uv; in vec3 v_pos;
out vec4 frag;
void main() {
    vec3 n = normalize(v_nrm);
    if (!gl_FrontFacing) n = -n;
    vec4 albedo = vec4(tint, 1.0);
    if (use_tex != 0) albedo = texture(tex, v_uv);
    if (alpha_test != 0 && albedo.a < 0.5) discard;
    vec3 L1 = normalize(vec3(0.4, 0.7, 0.6));
    vec3 L2 = normalize(vec3(-0.6, 0.2, -0.5));
    float d = max(dot(n, L1), 0.0) * 0.85 + max(dot(n, L2), 0.0) * 0.30;
    float hemi = 0.30 + 0.20 * n.y;
    frag = vec4(albedo.rgb * (hemi + d) * gain, 1.0);
}
"""


def perspective(fovy, aspect, n, f):
    t = 1.0 / math.tan(fovy / 2)
    return np.array([[t / aspect, 0, 0, 0], [0, t, 0, 0], [0, 0, (f + n) / (n - f), 2 * f * n / (n - f)], [0, 0, -1, 0]], np.float32)


def look_at(eye, ctr, up=(0, 1, 0)):
    f = np.array(ctr, np.float32) - eye; f /= np.linalg.norm(f)
    s = np.cross(f, up); s /= np.linalg.norm(s); u = np.cross(s, f)
    m = np.eye(4, dtype=np.float32); m[0, :3] = s; m[1, :3] = u; m[2, :3] = -f
    m[:3, 3] = -m[:3, :3] @ eye
    return m


class Scene:
    HIDE = ('nocut', 'cut-', 'danmen', '_dam1', '_dam2', 'dam1_', 'blood', 'broken')         # state variants shown by the game only after damage / dismemberment

    def __init__(self, ctx, model, role=0, use_tex=True, orphans=False, all_meshes=False):
        self.ctx = ctx; self.model = model; w = model.wmb
        self.prog = ctx.program(vertex_shader=VERT, fragment_shader=FRAG)
        self.groups = {}
        for g in range(len(w.groups)):
            if w.groups[g]['vertex_count'] == 0: continue
            a = w.attributes(g)
            vb = np.concatenate([a['position'], a['normal'], a['texcoord0']], 1).astype(np.float32)
            self.groups[g] = ctx.buffer(vb.tobytes())
        self.textures = {}
        if use_tex:
            for tid, img in model.textures.items():
                t = ctx.texture((img.shape[1], img.shape[0]), 4, np.ascontiguousarray(img).tobytes())
                t.build_mipmaps(); t.filter = (moderngl.LINEAR_MIPMAP_LINEAR, moderngl.LINEAR); t.anisotropy = 8.0; t.repeat_x = t.repeat_y = True
                self.textures[tid] = t
        self.draws = []
        mesh_of = {b: m for m, me in enumerate(w.meshes) for b in me['batches']}
        mat_of = {}
        for me in w.meshes:                                   # batches without batch info: first material of their mesh
            for b in me['batches']:
                if me['materials']: mat_of[b] = me['materials'][0]
        for bi, b in enumerate(w.batches):
            if b['group'] not in self.groups: continue
            if w.meshes and bi not in mesh_of and not orphans: continue                        # batches that no mesh lists (extra / low detail geometry)
            if bi in mesh_of and not all_meshes and any(h in w.meshes[mesh_of[bi]]['name'] for h in self.HIDE): continue
            tri = w.triangles(bi)
            if not len(tri): continue
            ib = ctx.buffer((tri.astype(np.uint32) + b['vertex_start']).tobytes())
            vao = ctx.vertex_array(self.prog, [(self.groups[b['group']], '3f 3f 2f', 'in_pos', 'in_nrm', 'in_uv')], ib)
            mt = b.get('material')
            if mt is None: mt = mat_of.get(bi)         # models without batch info: first material of the mesh
            tex = None
            if mt is not None and mt < len(w.materials):
                binds = w.materials[mt]['bindings']
                pick = [ti for r, ti in binds if r == role]
                if pick and pick[0] < len(w.textures): tex = w.textures[pick[0]][1]
            hue = (mesh_of.get(bi, 0) * 0.13) % 1.0
            self.draws.append(dict(vao=vao, tex=tex, mat=mt, mesh=mesh_of.get(bi), batch=bi, hue=hue, n=len(tri)))

    def render(self, fbo, yaw, pitch, dist_scale=1.0, size=(520, 520), alpha_test=False, cull=False, no_tex=False, gain=1.0):
        ctx = self.ctx; w = self.model.wmb
        c = np.array(w.bbox[:3], np.float32); ext = float(np.linalg.norm(w.bbox[3:6]))
        eye = c + dist_scale * ext * 3.0 * np.array([math.sin(yaw) * math.cos(pitch), math.sin(pitch), math.cos(yaw) * math.cos(pitch)], np.float32)
        proj = perspective(math.radians(35), size[0] / size[1], ext * 0.05, ext * 8)
        view = look_at(eye, c)
        mvp = (proj @ view).astype(np.float32)
        fbo.use(); ctx.clear(0.08, 0.08, 0.1, 1.0, depth=1.0); ctx.enable(moderngl.DEPTH_TEST); ctx.front_face = 'cw'      # D3D winding
        if cull: ctx.enable(moderngl.CULL_FACE)
        else: ctx.disable(moderngl.CULL_FACE)
        self.prog['mvp'].write(mvp.T.tobytes()); self.prog['model'].write(np.eye(4, dtype=np.float32).T.tobytes())
        self.prog['alpha_test'].value = 1 if alpha_test else 0; self.prog['gain'].value = gain
        for d in self.draws:
            t = self.textures.get(d['tex']) if (d['tex'] is not None and not no_tex) else None
            if t is not None:
                t.use(0); self.prog['tex'].value = 0; self.prog['use_tex'].value = 1
            else:
                import colorsys
                self.prog['use_tex'].value = 0; self.prog['tint'].value = colorsys.hsv_to_rgb(d['hue'], 0.35, 0.85)
            d['vao'].render()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('model'); ap.add_argument('--out', default=None); ap.add_argument('--size', type=int, default=520)
    ap.add_argument('--role', type=int, default=0); ap.add_argument('--no-tex', action='store_true'); ap.add_argument('--alpha-test', action='store_true')
    ap.add_argument('--cull', action='store_true'); ap.add_argument('--window', action='store_true'); ap.add_argument('--list', action='store_true')
    ap.add_argument('--views', default='0,35,90,180'); ap.add_argument('--gain', type=float, default=1.0); ap.add_argument('--orphans', action='store_true'); ap.add_argument('--all-meshes', action='store_true')
    a = ap.parse_args()
    model = model_loader.load(a.model, textures=not a.no_tex)
    w = model.wmb
    print('%s: %d vertices in %d groups, %d batches, %d meshes, %d materials, %d textures decoded' % (
        a.model, sum(g['vertex_count'] for g in w.groups), len(w.groups), len(w.batches), len(w.meshes), len(w.materials), len(model.textures)))
    if a.list:
        for k, m in enumerate(w.materials): print(k, m['shader_key'], m['bindings'])
    if a.window:
        import glfw
        glfw.init(); glfw.window_hint(glfw.CONTEXT_VERSION_MAJOR, 3); glfw.window_hint(glfw.CONTEXT_VERSION_MINOR, 3)
        win = glfw.create_window(a.size * 2, a.size * 2, 'MGR:R viewer ' + a.model, None, None); glfw.make_context_current(win)
        ctx = moderngl.create_context(); scene = Scene(ctx, model, a.role, not a.no_tex, a.orphans, a.all_meshes)
        st = dict(yaw=0.0, pitch=0.2, dist=1.0, drag=None)
        glfw.set_mouse_button_callback(win, lambda wd, b, act, m: st.update(drag=glfw.get_cursor_pos(wd) if act == 1 else None))
        glfw.set_scroll_callback(win, lambda wd, dx, dy: st.update(dist=max(0.2, st['dist'] * (0.9 ** dy))))
        while not glfw.window_should_close(win):
            if st['drag']:
                x, y = glfw.get_cursor_pos(win); st['yaw'] -= (x - st['drag'][0]) * 0.01; st['pitch'] = float(np.clip(st['pitch'] + (y - st['drag'][1]) * 0.01, -1.5, 1.5)); st['drag'] = (x, y)
            fw, fh = glfw.get_framebuffer_size(win); ctx.viewport = (0, 0, fw, fh)
            scene.render(ctx.screen, st['yaw'], st['pitch'], st['dist'], (fw, fh), a.alpha_test, a.cull, a.no_tex, a.gain)
            glfw.swap_buffers(win); glfw.poll_events()
        return
    ctx = moderngl.create_standalone_context(backend='egl')
    scene = Scene(ctx, model, a.role, not a.no_tex, a.orphans, a.all_meshes)
    S = a.size
    fbo = ctx.framebuffer(ctx.texture((S, S), 4), ctx.depth_renderbuffer((S, S)))
    from PIL import Image
    views = [float(v) for v in a.views.split(',')]
    sheet = Image.new('RGB', (S * len(views), S))
    for i, deg in enumerate(views):
        scene.render(fbo, math.radians(deg), 0.25, 1.0, (S, S), a.alpha_test, a.cull, a.no_tex, a.gain)
        img = Image.frombytes('RGBA', (S, S), fbo.read(components=4)).transpose(Image.FLIP_TOP_BOTTOM).convert('RGB')
        sheet.paste(img, (i * S, 0))
    out = a.out or ('/home/lucidfear/Projects/mgr-nx/x360_work/tex/view_%s.png' % a.model)
    sheet.save(out); print('saved', out)


if __name__ == '__main__':
    main()
