#!/usr/bin/env python3
"""Shader library for the MGR:R real-shader viewer: build (XenosRecomp -> HLSL -> DXC SPIR-V), parse the interface of a generated shader.

Directories (under x360_work/shaders): all/<archive>/NAME.{vso,pso} (extracted binaries), hlsl_mgr/NAME.hlsl, spv_mgr/NAME.spv.
Tools: XenosRecomp binary + shader_common.h and DXC from the scratchpad builds (override with env XENOSRECOMP / DXC_DIR / COMMON_H).
"""
import os, re, subprocess, glob

ROOT = '/home/lucidfear/Projects/mgr-nx/x360_work/shaders/'
SCR = '/tmp/claude-1000/-home-lucidfear-Projects-mgr-nx/828c1bb3-4343-4664-b577-9048fd98b786/scratchpad/'
XENOS = os.environ.get('XENOSRECOMP', SCR + 'XenosRecomp/build/XenosRecomp/XenosRecomp')
COMMON = os.environ.get('COMMON_H', SCR + 'XenosRecomp/XenosRecomp/shader_common.h')
DXC_DIR = os.environ.get('DXC_DIR', SCR + 'XenosRecomp/thirdparty/dxc-bin')
os.makedirs(ROOT + 'hlsl_mgr', exist_ok=True); os.makedirs(ROOT + 'spv_mgr', exist_ok=True)


def find_binary(name):
    for a in ('shader', 'shader2', 'shadereff'):
        p = ROOT + 'all/%s/%s' % (a, name)
        if os.path.exists(p): return p
    return None


def build(name):
    """returns (hlsl_path, spv_path) for 'modelshaderois00_xxcex.pso'; builds when missing"""
    hl = ROOT + 'hlsl_mgr/%s.hlsl' % name; spv = ROOT + 'spv_mgr/%s.spv' % name
    if not os.path.exists(hl):
        src = find_binary(name)
        if not src: raise FileNotFoundError(name)
        r = subprocess.run([XENOS, src, hl, COMMON], capture_output=True, text=True, timeout=120)
        if r.returncode or not os.path.exists(hl): raise RuntimeError('XenosRecomp failed for %s: %s' % (name, r.stderr[:200]))
    if not os.path.exists(spv):
        stage = 'vs_6_0' if name.endswith('.vso') else 'ps_6_0'
        env = dict(os.environ, LD_LIBRARY_PATH=DXC_DIR + '/lib/x64')
        r = subprocess.run([DXC_DIR + '/bin/x64/dxc-linux', '-spirv', '-T', stage, '-E', 'main', '-fspv-target-env=vulkan1.2', '-HV', '2021', '-Fo', spv, hl],
                           capture_output=True, text=True, env=env, timeout=300)
        if r.returncode or not os.path.exists(spv): raise RuntimeError('DXC failed for %s: %s' % (name, r.stderr[:300]))
    return hl, spv


class Iface:
    pass


_cache = {}


def interface(name):
    """parsed interface of a generated shader: inputs, interpolators read/written, constants, samplers"""
    if name in _cache: return _cache[name]
    hl, spv = build(name)
    t = open(hl).read()
    i = t.index('void main(')
    head = t[i:t.index('{', i)]; body = t[t.index('{', i):]
    f = Iface(); f.name = name; f.hlsl = hl; f.spv = spv
    f.inputs = []                                               # (usage, index, type, location)
    for m in re.finditer(r'(?:\[\[vk::location\((\d+)\)\]\] )?in (\w+) i(\w+?)(\d+) : (\w+)', head):
        if m.group(3) in ('VertexId', 'Face', 'Pos'): continue
        f.inputs.append((m.group(3), int(m.group(4)), m.group(2), int(m.group(1)) if m.group(1) else None))
    f.reads = {(m.group(2), int(m.group(3))) for m in re.finditer(r'float4 r(\d+) = i(TexCoord|Color)(\d+);', body)}
    f.writes = {(m.group(1), int(m.group(2))) for m in re.finditer(r'\bo(TexCoord|Color)(\d+)(?:\.\w+)? \+?= ([^;]*);', body) if m.group(3).strip() != '0.0'}
    f.consts = {}                                               # name -> dict(stage, reg, count)
    for m in re.finditer(r'#define (\w+) vk::RawBufferLoad<float4>\(g_PushConstants\.(Vertex|Pixel)ShaderConstants \+ (\d+), 0x10\)', t):
        f.consts[m.group(1)] = dict(stage=m.group(2), reg=int(m.group(3)) // 16, count=1)
    for m in re.finditer(r'#define (\w+)\(INDEX\) select\(\(INDEX\) < (\d+), vk::RawBufferLoad<float4>\(g_PushConstants\.(Vertex|Pixel)ShaderConstants \+ \((\d+) \+ min\(INDEX, (\d+)\)\) \* 16', t):
        f.consts[m.group(1)] = dict(stage=m.group(3), reg=int(m.group(4)), count=int(m.group(2)))
    f.samplers = {}                                             # name -> fetch slot
    for m in re.finditer(r'#define (\w+)_sampler_Texture2DDescriptorIndex vk::RawBufferLoad<uint>\(g_PushConstants\.SharedConstants \+ (\d+)\)', t):
        f.samplers[m.group(1)] = int(m.group(2)) // 4
    f.uses_cube = 'TextureCubeDescriptorHeap' in body or 'tfetchCube' in body
    f.spec_alpha = 'SPEC_CONSTANT_ALPHA_TEST' in body
    _cache[name] = f
    return f


def ps_name_for_key(key):
    """material shader key -> pixel shader file (the second flag char is vertex-side and is dropped; 99.9% coverage)"""
    pre, _, fl = key.partition('_')
    return ('modelshader%s_%s.pso' % (pre, fl.replace('b', 'x'))).lower()


def all_vs_names():
    return sorted(os.path.basename(p) for a in ('shader', 'shader2', 'shadereff') for p in glob.glob(ROOT + 'all/%s/vs_*.vso' % a))


def choose_vs(ps, available, skinned):
    """pick a vertex shader whose outputs cover what the pixel shader reads and whose inputs the mesh provides.
    available: set of (usage,index) the mesh has; skinned: mesh has blend indices/weights"""
    best = None
    for n in all_vs_names():
        try: v = interface(n)
        except Exception: continue
        ins = {(u, i) for u, i, t, l in v.inputs}
        has_skin = ('BlendIndices', 0) in ins
        if has_skin != skinned: continue
        if not ins <= available: continue
        if not ps.reads <= v.writes: continue
        score = (-len(ins), n.startswith('vs_vs'), -len(n))       # fewest inputs; among equals the plain 'vs_vs*' variant (the 'a'/'n' variants only reorder registers)
        if best is None or score > best[0]: best = (score, n)
    return best[1] if best else None


if __name__ == '__main__':
    import sys
    f = interface(sys.argv[1])
    print('inputs', f.inputs); print('reads', sorted(f.reads)); print('writes', sorted(f.writes)); print('consts', f.consts); print('samplers', f.samplers)
