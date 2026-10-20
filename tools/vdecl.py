#!/usr/bin/env python3
"""Xbox 360 D3DVERTEXELEMENT9 (12 bytes) and D3DDECLTYPE word decoder.

element: stream u16, offset u16, type u32, method u8, usage u8, usage_index u8, pad u8   (big-endian)
type word: [0..5] Xenos vertex data format, [6..7] endian swap (1=8in16, 2=8in32), [8] signed, [9] integer (not normalized),
           [11] BGRA/colour swizzle (D3DCOLOR), upper bits: default-fill/swizzle of missing components (kept opaque).
"""
FMT = {6: '8_8_8_8', 16: '10_11_11', 17: '11_11_10', 31: '16_16_FLOAT', 32: '16_16_16_16_FLOAT', 36: '32_FLOAT',
       37: '32_32_FLOAT', 38: '32_32_32_32_FLOAT', 57: '32_32_32_FLOAT'}
KNOWN = {  # type word -> (D3DDECLTYPE name used by the PC build, Vulkan format / note)
    0x2c83a4: ('FLOAT1',   'R32_SFLOAT'),
    0x2c23a5: ('FLOAT2',   'R32G32_SFLOAT'),
    0x2a23b9: ('FLOAT3',   'R32G32B32_SFLOAT'),
    0x1a23a6: ('FLOAT4',   'R32G32B32A32_SFLOAT'),
    0x2c235f: ('FLOAT16_2', 'R16G16_SFLOAT'),
    0x1a2360: ('FLOAT16_4', 'R16G16B16A16_SFLOAT'),
    0x1a2086: ('UBYTE4N',  'R8G8B8A8_UNORM'),
    0x1a2286: ('UBYTE4',   'R8G8B8A8_UINT'),
    0x182886: ('D3DCOLOR', 'B8G8R8A8_UNORM (bit 11 = colour swizzle; verify on data)'),
    0x2a2190: ('(Xbox only) 10_11_11 signed-normalized', 'no native format: unpack in shader, X=bits 22..31 (10), Y=bits 11..21, Z=bits 0..10 of the big-endian dword'),
}
def decode(t):
    f = t & 63
    return dict(format=FMT.get(f, 'fmt%d' % f), endian=(t >> 6) & 3, signed=(t >> 8) & 1, integer=(t >> 9) & 1, color_swizzle=(t >> 11) & 1, raw=hex(t))
if __name__ == '__main__':
    for t, (n, v) in KNOWN.items(): print(hex(t), n, decode(t), v)
