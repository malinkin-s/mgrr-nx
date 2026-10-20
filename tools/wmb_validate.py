#!/usr/bin/env python3
"""Validate the WMB4 parser on every extracted model; prints invariant failures and summary statistics."""
import os, sys, collections, traceback
sys.path.insert(0, '/home/lucidfear/Projects/mgr-nx/tools')
import numpy as np, wmb
D = '/home/lucidfear/Projects/mgr-nx/x360_work/wmb/'
fail = collections.Counter(); ex = collections.defaultdict(list); st = collections.Counter(); keys = collections.Counter(); roles = collections.Counter()
n = ok = 0; tv = tt = 0
for f in sorted(os.listdir(D)):
    d = open(D + f, 'rb').read()
    if d[:4] != b'WMB4': st['not WMB4: ' + d[:4].decode('latin-1')] += 1; continue
    n += 1
    try:
        if len(d) < 512: st['empty/tiny model'] += 1; continue
        m = wmb.WMB(d)
        bad = []
        # batch prefix sums / ranges
        for k, b in enumerate(m.batches):
            g = m.groups[b['group']]
            if b['vertex_start'] + b['vertex_count'] > g['vertex_count']: bad.append('batch vertex range')
            if b['index_start'] + b['index_count'] > g['index_count']: bad.append('batch index range')
            if b['index_count']:
                ix = m.indices(k); ix = ix[ix != 0xFFFF]
                if len(ix) and ix.max() >= b['vertex_count']: bad.append('index >= vertex_count')
        # meshes cover batches
        cov = collections.Counter(x for me in m.meshes for x in me['batches'])
        if any(x >= len(m.batches) for x in cov): bad.append('mesh batch index out of range')
        if cov and max(cov.values()) > 1: bad.append('batch in several meshes')
        if m.batches and len(cov) < len(m.batches): bad.append('batches without a mesh (%s)' % ('info-less' if any(b['mesh'] is None for b in m.batches) else 'other'))
        for me in m.meshes:
            if any(x >= len(m.materials) for x in me['materials']): bad.append('material index')
        for mt in m.materials:
            keys[mt['shader_key']] += 1
            for role, ti in mt['bindings']:
                roles[role] += 1
                if ti >= len(m.textures): bad.append('texture index')
        # bones: translate table and parents
        for i, b in enumerate(m.bones):
            if m.bone_for_id(b['id']) != i: bad.append('bone translate'); break
        for b in m.bones:
            if b['parent'] >= len(m.bones): bad.append('bone parent'); break
        # skinning: blend indices inside the bone set
        if m.flags & 0x30:
            for k, b in enumerate(m.batches):
                if b['vertex_count'] == 0 or b['bone_set'] is None: continue
                a = m.attributes(b['group'])
                if 'blendindices' not in a: continue
                vs = b['vertex_start']; bi = a['blendindices'][vs:vs + b['vertex_count']]; bw = a['blendweight'][vs:vs + b['vertex_count']]
                used = bi[bw > 0.01]
                if len(used) and used.max() >= len(m.bone_sets[b['bone_set']]): bad.append('blend index >= bone set'); break
        for g in range(len(m.groups)):
            a = m.attributes(g); tv += len(a['position'])
            if not np.isfinite(a['position']).all(): bad.append('non-finite position')
        for k in range(len(m.batches)): tt += len(m.triangles(k))
        for x in set(bad): fail[x] += 1; ex[x].append(f)
        if not bad: ok += 1
        st[(hex(m.flags), m.groups[0]['stride'] if m.groups else 0, m.groups[0]['extra_stride'] if m.groups else 0)] += 1
    except Exception as e:
        fail['exception ' + type(e).__name__] += 1; ex['exception ' + type(e).__name__].append(f + ' ' + str(e)[:60])
print('WMB4 files', n, 'fully valid', ok, 'vertices', tv, 'triangles', tt)
print('failures', dict(fail))
for k, v in ex.items(): print(' ', k, v[:3])
print('format classes', st.most_common(12))
print('material shader keys', keys.most_common(12)); print('binding roles', sorted(roles.items()))
