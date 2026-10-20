#!/usr/bin/env python3
"""Load one MGR:R (Xbox 360) model with its textures straight from the CPK archives.

    res = load('em0200')       # -> Model(wmb, textures {id: RGBA ndarray}, names)
Looks for <dir>/<name>.dat (WMB + WTA) and <dir>/<name>.dtt (WTP) in data000/data001.
"""
import os
import sys
sys.path.insert(0, '/home/lucidfear/Projects/mgr-nx/tools')
import numpy as np
import cpk, pgdat, wtb, xtex, wmb

ROOT = '/home/lucidfear/Projects/mgr-nx/x360_extracted/media/'
_cache = {}


def _cpk(a):
    if a not in _cache:
        c = cpk.CPK(ROOT + a)
        _cache[a] = (c, {(e['dir'] + '/' + e['name']).lstrip('/'): e for e in c.files})
    return _cache[a]


def find(path):
    for a in ('data000.cpk', 'data001.cpk'):
        c, idx = _cpk(a)
        if path in idx: return c, idx[path]
    raise FileNotFoundError(path)


class Model:
    pass


def load(name, folder=None, textures=True, max_tex=None):
    """name like 'em0200' (folder guessed from the prefix: em/, pl/, ba/, ...)"""
    folder = folder or {'em': 'em', 'ba': 'st1', 'bh': 'st1', 'bm': 'st1', 'pl': 'pl', 'et': 'et', 'wp': 'wp'}.get(name[:2], name[:2])
    candidates = [folder + '/' + name] + [f + '/' + name for f in ('em', 'pl', 'et', 'wp', 'st0', 'st1', 'st2', 'st3', 'st4', 'st5', 'core', 'bg', 'ba', 'bm', 'bh')]
    last = None
    for base in candidates:
        try:
            c, e = find(base + '.dat'); break
        except FileNotFoundError as ex:
            last = ex
    else:
        raise last
    dat = c.read(e)
    mem = {m['name']: dat[m['off']:m['off'] + m['size']] for m in pgdat.parse(dat)}
    model = Model(); model.name = name; model.base = base
    wmb_name = [n for n in mem if n.endswith('.wmb')][0]
    model.wmb = wmb.WMB(mem[wmb_name])
    model.textures = {}; model.texinfo = {}
    wta_names = [n for n in mem if n.endswith('.wta')]
    if textures and wta_names:
        wta = mem[wta_names[0]]
        try:
            cc, ee = find(base + '.dtt'); dtt = cc.read(ee)
            wtp_m = {m['name']: dtt[m['off']:m['off'] + m['size']] for m in pgdat.parse(dtt)}
            wtp = next((v for k, v in wtp_m.items() if k.endswith('.wtp')), b'')
        except FileNotFoundError:
            wtp = b''
        for t in wtb.parse(wta):
            if 'format' not in t: continue
            blob = wtp[t['off']:t['off'] + t['size']]
            if len(blob) < t['size']: continue
            img = xtex.decode(t, blob)
            if img is not None:
                model.textures[t['id']] = np.ascontiguousarray(img)
                model.texinfo[t['id']] = t
    # shared textures (table flag 0x63) live in core.dtt::pre_texture.wtb
    if textures and any(i not in model.textures for f, i in model.wmb.textures):
        for t in _shared():
            if t['id'] in dict((i, 1) for f, i in model.wmb.textures) and t['id'] not in model.textures:
                model.textures[t['id']] = t['img']; model.texinfo[t['id']] = t
    # anything still missing (stage props use textures of the level archives): look it up by id in the global texture index
    if textures:
        for f, i in model.wmb.textures:
            if i not in model.textures:
                rec = _global().get(i)
                if rec is not None:
                    img = _load_indexed(rec)
                    if img is not None: model.textures[i] = img; model.texinfo[i] = rec
    return model


_gidx = None
_blob_cache = {}


def _global():
    global _gidx
    if _gidx is None:
        import json
        _gidx = {}
        for t in json.load(open('/home/lucidfear/Projects/mgr-nx/x360_work/textures_index_all.json')):
            if 'id' in t and 'format' in t and t['id'] not in _gidx: _gidx[t['id']] = t
    return _gidx


def _archive_member(path, member):
    key = (path, member)
    if key not in _blob_cache:
        c, e = find(path.lstrip('/'))
        d = c.read(e)
        if member:
            m = [x for x in pgdat.parse(d) if x['name'] == member][0]; d = d[m['off']:m['off'] + m['size']]
        _blob_cache[key] = d
    return _blob_cache[key]


def _load_indexed(rec):
    """decode texture `rec` (from textures_index_all.json): src = '<archive>:<path>[::member]', kind = wtb (data inline) or wta (data in the sibling .dtt .wtp)"""
    try:
        a, rest = rec['src'].split(':', 1); path, _, member = rest.partition('::')
        blob = _archive_member(path, member)
        t = wtb.parse(blob)[rec['idx']]
        if rec.get('kind') == 'wta':
            dtt = path[:-4] + '.dtt'
            c, e = find(dtt.lstrip('/'))
            d = _blob_cache.setdefault((dtt, None), c.read(e))
            wtp = next(d[m['off']:m['off'] + m['size']] for m in pgdat.parse(d) if m['name'].endswith('.wtp'))
            data = wtp[t['off']:t['off'] + t['size']]
        else:
            data = blob[t['off']:t['off'] + t['size']]
        img = xtex.decode(t, data)
        return None if img is None else np.ascontiguousarray(img)
    except Exception:
        return None


_shared_list = None


def _shared():
    global _shared_list
    if _shared_list is None:
        _shared_list = []
        c, e = find('core.dtt')
        d = c.read(e)
        for m in pgdat.parse(d):
            if m['name'] == 'pre_texture.wtb':
                blob = d[m['off']:m['off'] + m['size']]
                for t in wtb.parse(blob):
                    if 'format' not in t: continue
                    img = xtex.decode(t, blob[t['off']:t['off'] + t['size']])
                    if img is not None:
                        t = dict(t); t['img'] = np.ascontiguousarray(img); _shared_list.append(t)
    return _shared_list


if __name__ == '__main__':
    m = load(sys.argv[1])
    print(m.base, 'textures decoded', len(m.textures), 'of', len(m.wmb.textures), 'referenced; missing ids:',
          [hex(i) for f, i in m.wmb.textures if i not in m.textures][:6])
