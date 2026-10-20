# Xbox 360 D3D render-state setters decoded to Xenos registers

How: the DrawVertices flush (0x826BC928) walks five 64-bit dirty masks at dev+0..32 and, for each group, calls a register-run emitter
(0x826CA450: `cntlzd` over the mask, header `((n-1)<<16)|reg`, then `n` shadow words). Mask bit ↔ register is fixed, so every setter that ORs a
bit into the mask and rewrites a bit field of a shadow word can be translated to “register + bit field”. Field names follow the public Xenos
register layouts (from memory); the bit positions found in the code matched these layouts in every case checked (RB_DEPTHCONTROL, RB_BLENDCONTROL_0 fully).

| setter | register | field | D3D9 analog | wrapper(s) (callers) | direct call sites |
|---|---|---|---|---|---|
| 0x826AC3D8 | ? | unknown |  |  | 1 |
| 0x826AC830 | ? | unknown |  | 827913b0(1) | 2 |
| 0x826B62B0 | 0x2205 | PA_SU_SC_MODE_CNTL[0..2] cull_front/cull_back/face | CULLMODE (+front face) | 82791148(80) | 1 |
| 0x826B62E0 | 0x2205 | PA_SU_SC_MODE_CNTL[3..10] poly_mode/ptypes | FILLMODE | 827911b0(0) | 1 |
| 0x826B6310 | 0x2202 | ALPHA_TEST_ENABLE | ALPHATESTENABLE | 82790fa8(1) | 23 |
| 0x826B6348 | 0x2201 | (shadow flag) blend enable | ALPHABLENDENABLE | 82790ed8(107) | 1 |
| 0x826B63D8 | 0x2201 | COLOR_COMB_FCN | BLENDOP | 827903e0(24) | 1 |
| 0x826B6468 | 0x2201 | COLOR_SRCBLEND | SRCBLEND | 827903e0(24) | 1 |
| 0x826B64F8 | 0x2201 | COLOR_DESTBLEND | DESTBLEND | 827903e0(24) | 1 |
| 0x826B6588 | 0x2201 | ALPHA_COMB_FCN | BLENDOPALPHA | 82790368(6) | 1 |
| 0x826B65F8 | 0x2201 | ALPHA_SRCBLEND | SRCBLENDALPHA | 82790368(6) | 1 |
| 0x826B6668 | 0x2201 | ALPHA_DESTBLEND | DESTBLENDALPHA | 82790368(6) | 1 |
| 0x826B66D8 | 0x2201 | (shadow flag) separate alpha blend | SEPARATEALPHABLENDENABLE | 82790e88(11) | 16 |
| 0x826B6770 | 0x210E | RB_ALPHA_REF (int→float) | ALPHAREF | 827904a0(1) | 2 |
| 0x826B67D0 | 0x2202 | RB_COLORCONTROL[0..2] ALPHA_FUNC | ALPHAFUNC | 827904a0(1) | 2 |
| 0x826B6800 | ? | unknown |  |  | 0 |
| 0x826B69D8 | 0x2200 | Z_ENABLE | ZENABLE | 827910e0(33), 82794068(1) | 58 |
| 0x826B6A18 | 0x2200 | Z_WRITE_ENABLE | ZWRITEENABLE | 82791078(32) | 52 |
| 0x826B6A48 | 0x2200 | ZFUNC | ZFUNC | 82791010(5) | 1 |
| 0x826B6A80 | 0x2200 | STENCIL_ENABLE | STENCILENABLE | 82790768(29) | 1 |
| 0x826B6AC0 | 0x2200 | BACKFACE_ENABLE (two-sided stencil) | TWOSIDEDSTENCILMODE | 82790768(29) | 1 |
| 0x826B6AF8 | 0x2200 | STENCILFUNC | STENCILFUNC | 827906a8(1) | 1 |
| 0x826B6B28 | 0x2200 | STENCILFAIL | STENCILFAIL | 82790708(1) | 1 |
| 0x826B6B60 | 0x2200 | STENCILZFAIL | STENCILZFAIL | 82790708(1) | 1 |
| 0x826B6B98 | 0x2200 | STENCILZPASS | STENCILPASS | 82790708(1) | 1 |
| 0x826B6BC8 | 0x2200 | STENCILFUNC_BF | CCW_STENCILFUNC |  | 0 |
| 0x826B6BF8 | 0x2200 | STENCILFAIL_BF | CCW_STENCILFAIL |  | 0 |
| 0x826B6C30 | 0x2200 | STENCILZFAIL_BF | CCW_STENCILZFAIL |  | 0 |
| 0x826B6C68 | 0x2200 | STENCILZPASS_BF | CCW_STENCILPASS |  | 0 |
| 0x826B6C98 | 0x210D | RB_STENCILREFMASK.REF [byte 0] | STENCILREF | 827906a8(1) | 1 |
| 0x826B6CB8 | 0x210D | RB_STENCILREFMASK.MASK [byte 1] | STENCILMASK | 827906a8(1) | 1 |
| 0x826B6CD8 | 0x210D | RB_STENCILREFMASK.WRITEMASK [byte 2] | STENCILWRITEMASK | 82790aa0(10) | 1 |
| 0x826B6CF8 | 0x210C | RB_STENCILREFMASK_BF.REF | CCW stencil ref |  | 0 |
| 0x826B6D18 | 0x210C | RB_STENCILREFMASK_BF.MASK | CCW stencil mask |  | 0 |
| 0x826B6D38 | 0x210C | RB_STENCILREFMASK_BF.WRITEMASK | CCW stencil writemask |  | 0 |
| 0x826B6D58 | 0x2204 | PA_CL_CLIP_CNTL + reg 0x200D (user clip planes) | CLIPPLANEENABLE |  | 0 |
| 0x826B6E80 | 0x2381 | PA_SU_SC_MODE_CNTL[11,12] + poly offset regs 0x2381/0x2383 | DEPTHBIAS / SLOPESCALEDEPTHBIAS | 827905d8(1) | 1 |
| 0x826B6F28 | 0x2205 | MSAA_ENABLE | MULTISAMPLEANTIALIAS |  | 0 |
| 0x826B6F78 | 0x2104 | RT0 write mask (low 4 bits) | COLORWRITEENABLE | 82790d60(112) | 1 |
| 0x826B6FB8 | 0x2104 | RT1 write mask | COLORWRITEENABLE1 | 827902e8(1) | 1 |
| 0x826B6FF8 | 0x2104 | RT2 write mask | COLORWRITEENABLE2 | 827902e8(1) | 1 |
| 0x826B7038 | 0x2104 | RT3 write mask | COLORWRITEENABLE3 | 827902e8(1) | 1 |
| 0x826B7180 | 0x2183 | reg 0x2183  |  |  | 0 |
| 0x826B71A0 | 0x2183 | reg 0x2183  |  |  | 0 |
| 0x826B71C8 | 0x2183 | reg 0x2183  |  |  | 0 |
| 0x826B71F0 | 0x2183 | reg 0x2183  |  |  | 0 |
| 0x826B7218 | 0x2183 | reg 0x2183  |  |  | 0 |
| 0x826B7240 | 0x2183 | reg 0x2183  |  |  | 0 |
| 0x826B7268 | 0x2183 | reg 0x2183  |  |  | 0 |
| 0x826B7290 | 0x2183 | reg 0x2183  bits[28..31] |  |  | 0 |
| 0x826B72B0 | 0x2184 | reg 0x2184  |  |  | 0 |
| 0x826B72D0 | 0x2184 | reg 0x2184  |  |  | 0 |
| 0x826B72F8 | 0x2184 | reg 0x2184  |  |  | 0 |
| 0x826B7320 | 0x2184 | reg 0x2184  |  |  | 0 |
| 0x826B7348 | 0x2184 | reg 0x2184  |  |  | 0 |
| 0x826B7370 | 0x2184 | reg 0x2184  |  |  | 0 |
| 0x826B7398 | 0x2184 | reg 0x2184  |  |  | 0 |
| 0x826B73C0 | 0x2184 | reg 0x2184  bits[28..31] |  |  | 0 |
| 0x826B74E0 | 0x2206 | reg 0x2206 PA_CL_VTE_CNTL bits[16..16] |  |  | 2 |
| 0x826B7530 | 0x2001 | reg 0x2001  bits[20..15] |  |  | 0 |
| 0x826B75C8 | 0x2003 | reg 0x2003  bits[20..15] |  |  | 0 |
| 0x826B7660 | 0x2004 | reg 0x2004  bits[20..15] |  |  | 0 |
| 0x826B76F8 | 0x2005 | reg 0x2005  bits[20..15] |  |  | 0 |
| 0x826B7870 | 0x2205 | reg 0x2205 PA_SU_SC_MODE_CNTL bits[21..21] |  | 82796128(1) | 1 |
| 0x826B78A0 | 0x2103 | VGT_MULTI_PRIM_IB_RESET_INDX | (primitive restart index) |  | 0 |
| 0x826B78C8 | 0x2202 | ALPHA_TO_MASK_ENABLE | (alpha-to-coverage) |  | 0 |
| 0x826B78F8 | 0x2202 | reg 0x2202 RB_COLORCONTROL bits[24..31] |  |  | 0 |
| 0x826B79D8 | ? | unknown bits[20..22] |  | 82790238(1) | 3 |
| 0x826B7A08 | 0x2203 | reg 0x2203 RB_HIZCONTROL bits[17..19] |  | 82790238(1) | 3 |
| 0x826B7A70 | 0x2203 | reg 0x2203 RB_HIZCONTROL bits[3..3] |  |  | 0 |
| 0x826B7AA0 | 0x2203 | reg 0x2203 RB_HIZCONTROL bits[2..2] |  |  | 0 |
| 0x826B7AD0 | 0x2203 | reg 0x2203 RB_HIZCONTROL bits[5..5] |  |  | 0 |
| 0x826B7B00 | 0x2203 | reg 0x2203 RB_HIZCONTROL |  |  | 0 |
| 0x826B8E48 | 0x2000 | reg 0x2000  |  |  | 2 |
