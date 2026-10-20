# D3D (Xbox 360) in MGR:R default.xex — catalog

Image base 0x82000000, device pointer global: **0x82833A08** (`lis r30,0x8283; lwz r3,14856(r30)`), device object ~22 KB (offsets up to 22040 seen).
Dirty-flag qword at device+16 (setters do `ld r,16(r3); ori/oris; std`).

## Regions

| range | role |
|---|---|
| 0x826AA000–0x826CC000 | D3D library core (all Vd* import callers 0x826AF430–0x826C8CA0) |
| 0x826B62B0–0x826B8000 | render-state setter family (~70 leaf functions) |
| 0x826CC000–0x826CD000 | CRT/math helpers (626 external call sites) - not D3D |
| 0x8278DFC8–0x82796128 | engine-side wrapper layer: almost all external calls into the D3D core originate here |
| 0x82710000–0x8271E000 | game/CRT helpers (heavily called) - not D3D |

## Entry points (121 functions in the D3D core called from outside it)

| address | insns | call sites | callers | kind | note |
|---|---|---|---|---|---|
| 0x826aa300 | 6 | 1 | 1 |  |  |
| 0x826aa318 | 6 | 1 | 1 |  |  |
| 0x826aa330 | 6 | 1 | 1 |  |  |
| 0x826aa348 | 35 | 6 | 4 |  |  |
| 0x826aa640 | 29 | 2 | 2 |  |  |
| 0x826aa6b8 | 26 | 1 | 1 |  |  |
| 0x826aa720 | 4 | 66 | 49 |  | leaf helper (66 call sites): tail-calls 0x826aa3d8 with r4=[r3+24]&~3 |
| 0x826aa730 | 3 | 11 | 11 |  |  |
| 0x826ab228 | 32 | 223 | 54 |  | IDirect3DResource9::Release-like (atomic dec of refcount at +4, resource type in low nibble of +0) |
| 0x826ab788 | 20 | 6 | 6 |  |  |
| 0x826ab7d8 | 18 | 2 | 2 |  |  |
| 0x826ac0b8 | 54 | 4 | 4 |  |  |
| 0x826ac190 | 54 | 4 | 4 |  |  |
| 0x826ac268 | 23 | 3 | 3 |  |  |
| 0x826ac2c8 | 23 | 3 | 3 |  |  |
| 0x826ac328 | 21 | 3 | 3 |  |  |
| 0x826ac380 | 21 | 3 | 3 |  |  |
| 0x826ac410 | 111 | 1 | 1 |  |  |
| 0x826ac618 | 115 | 1 | 1 |  |  |
| 0x826ac830 | 5 | 1 | 1 | setter |  |
| 0x826ac938 | 32 | 1 | 1 |  |  |
| 0x826ac9b8 | 58 | 1 | 1 |  |  |
| 0x826acad0 | 4 | 1 | 1 |  |  |
| 0x826acc10 | 60 | 1 | 1 |  |  |
| 0x826acdf8 | 60 | 1 | 1 |  |  |
| 0x826acf38 | 38 | 3 | 3 |  |  |
| 0x826ad070 | 5 | 9 | 5 |  |  |
| 0x826ad088 | 6 | 4 | 3 |  |  |
| 0x826ad0a0 | 38 | 4 | 4 |  |  |
| 0x826ad1c0 | 72 | 5 | 4 |  |  |
| 0x826ad660 | 86 | 1 | 1 |  |  |
| 0x826ae488 | 189 | 1 | 1 |  |  |
| 0x826ae9f0 | 4 | 2 | 2 |  |  |
| 0x826aea00 | 1 | 6 | 2 |  |  |
| 0x826aea08 | 46 | 5 | 3 |  |  |
| 0x826aeac0 | 7 | 3 | 3 |  |  |
| 0x826aeae0 | 45 | 4 | 2 |  |  |
| 0x826aeb98 | 1 | 2 | 2 |  |  |
| 0x826aeba0 | 43 | 4 | 2 |  |  |
| 0x826aec50 | 72 | 4 | 4 |  |  |
| 0x826aed70 | 78 | 2 | 2 |  |  |
| 0x826aeea8 | 38 | 3 | 3 |  |  |
| 0x826aef40 | 9 | 4 | 2 |  |  |
| 0x826aef68 | 17 | 2 | 2 |  |  |
| 0x826aefb0 | 7 | 2 | 1 |  |  |
| 0x826aefd0 | 99 | 2 | 2 |  |  |
| 0x826af160 | 5 | 1 | 1 |  |  |
| 0x826af1e0 | 27 | 1 | 1 |  |  |
| 0x826afcf8 | 98 | 5 | 4 |  |  |
| 0x826afe80 | 172 | 2 | 2 |  | GPU sync/resolve-like (EVENT_WRITE + REG_RMW + WAIT_REG_MEM) |
| 0x826b0130 | 239 | 1 | 1 |  |  |
| 0x826b1aa8 | 58 | 1 | 1 |  |  |
| 0x826b2008 | 33 | 4 | 4 |  |  |
| 0x826b2090 | 16 | 11 | 11 |  | sets [dev+11016] via 0x82306c78 (11 call sites) |
| 0x826b20d0 | 4 | 12 | 12 |  | clears [dev+11016] with eieio (12 call sites) |
| 0x826b20e0 | 68 | 1 | 1 |  |  |
| 0x826b21f0 | 2 | 1 | 1 |  |  |
| 0x826b2418 | 53 | 1 | 1 |  |  |
| 0x826b2fc0 | 9 | 1 | 1 |  |  |
| 0x826b2fe8 | 2 | 1 | 1 |  |  |
| 0x826b2ff0 | 23 | 1 | 1 |  |  |
| 0x826b3510 | 474 | 1 | 1 | Vd:VdGetSystemCommandBuffer,VdPersistDisplay,VdSwap | D3DDevice Swap/Present (calls VdSwap, VdGetSystemCommandBuffer, VdPersistDisplay) |
| 0x826b5790 | 13 | 3 | 3 |  |  |
| 0x826b59b0 | 21 | 2 | 2 |  |  |
| 0x826b5a08 | 242 | 1 | 1 | Vd:VdEnableRingBufferRPtrWriteBack,VdInitializeRingBuffer,VdSetSystemCommandBufferGpuIdentifierAddress | D3D device ring-buffer init (VdInitializeRingBuffer, VdEnableRingBufferRPtrWriteBack) |
| 0x826b62b0 | 7 | 1 | 1 | setter |  |
| 0x826b62e0 | 7 | 1 | 1 | setter |  |
| 0x826b6310 | 9 | 23 | 23 | setter | render-state setter (23 call sites): bit3 of [dev+10556], dirty 0x40200 |
| 0x826b6348 | 32 | 1 | 1 | setter |  |
| 0x826b63d8 | 31 | 1 | 1 | setter |  |
| 0x826b6468 | 31 | 1 | 1 | setter |  |
| 0x826b64f8 | 31 | 1 | 1 | setter |  |
| 0x826b6588 | 23 | 1 | 1 | setter |  |
| 0x826b65f8 | 23 | 1 | 1 | setter |  |
| 0x826b6668 | 23 | 1 | 1 | setter |  |
| 0x826b66d8 | 33 | 16 | 16 | setter | render-state setter (16 call sites): bit of [dev+12284], derives [dev+10552..10592] |
| 0x826b6770 | 13 | 2 | 2 | setter |  |
| 0x826b67d0 | 7 | 2 | 2 | setter |  |
| 0x826b6938 | 15 | 2 | 2 |  |  |
| 0x826b69d8 | 14 | 58 | 49 | setter | render-state setter (58 call sites): [dev+12308], bit1 of [dev+10548], dirty 0x20800 |
| 0x826b6a18 | 7 | 52 | 45 | setter | render-state setter (52 call sites): bit2 of [dev+10548], dirty 0x800 |
| 0x826b6a48 | 9 | 1 | 1 | setter |  |
| 0x826b6a80 | 14 | 1 | 1 | setter |  |
| 0x826b6ac0 | 9 | 1 | 1 | setter |  |
| 0x826b6af8 | 7 | 1 | 1 | setter |  |
| 0x826b6b28 | 9 | 1 | 1 | setter |  |
| 0x826b6b60 | 9 | 1 | 1 | setter |  |
| 0x826b6b98 | 7 | 1 | 1 | setter |  |
| 0x826b6c98 | 5 | 1 | 1 | setter |  |
| 0x826b6cb8 | 5 | 1 | 1 | setter |  |
| 0x826b6cd8 | 5 | 1 | 1 | setter |  |
| 0x826b6e80 | 38 | 1 | 1 | setter |  |
| 0x826b6f78 | 14 | 1 | 1 | setter |  |
| 0x826b6fb8 | 14 | 1 | 1 | setter |  |
| 0x826b6ff8 | 14 | 1 | 1 | setter |  |
| 0x826b7038 | 14 | 1 | 1 | setter |  |
| 0x826b7078 | 2 | 2 | 2 |  |  |
| 0x826b7130 | 16 | 1 | 1 |  |  |
| 0x826b7838 | 9 | 1 | 1 |  |  |
| 0x826b7870 | 7 | 1 | 1 | setter |  |
| 0x826b79d8 | 7 | 3 | 3 | setter |  |
| 0x826b7a08 | 21 | 3 | 3 | setter |  |
| 0x826b7b20 | 2 | 7 | 6 |  |  |
| 0x826b7b30 | 4 | 2 | 1 |  |  |
| 0x826b7b50 | 50 | 1 | 1 |  |  |
| 0x826b7cf8 | 50 | 1 | 1 |  |  |
| 0x826b7f98 | 26 | 2 | 2 |  |  |
| 0x826b8578 | 62 | 1 | 1 |  |  |
| 0x826b8670 | 72 | 2 | 2 |  |  |
| 0x826b8790 | 36 | 2 | 2 |  |  |
| 0x826b8820 | 17 | 1 | 1 |  |  |
| 0x826b88a8 | 52 | 1 | 1 |  |  |
| 0x826b8d98 | 3 | 7 | 7 |  |  |
| 0x826b8da8 | 32 | 1 | 1 |  |  |
| 0x826b9268 | 182 | 3 | 3 |  |  |
| 0x826b9540 | 1 | 2 | 2 |  |  |
| 0x826bb0d0 | 19 | 5 | 5 |  |  |
| 0x826bc398 | 47 | 1 | 1 |  |  |
| 0x826bc928 | 261 | 1 | 1 |  | draw call (emits DRAW_INDX) - DrawVertices/DrawIndexed variant |
| 0x826bcd40 | 285 | 1 | 1 |  | draw call (emits DRAW_INDX) - DrawVertices/DrawIndexed variant |
| 0x826cbfb8 | 21 | 3 | 3 |  |  |

## Render-state setters (leaf, no calls)

| address | insns | state words (dev+) | dirty bits |
|---|---|---|---|
| 0x826ac3d8 | 14 | 0, 4, 20, 24 |  |
| 0x826ac830 | 5 | 12240 | oris8 |
| 0x826b62b0 | 7 | 10568 | ori64 |
| 0x826b62e0 | 7 | 10568 | ori64 |
| 0x826b6310 | 9 | 10556 | ori512 oris4 |
| 0x826b6348 | 32 | 10552, 10584, 10588, 10592, 12284 | ori1024 ori4 ori2 ori1 |
| 0x826b63d8 | 31 | 10552, 10584, 10588, 10592, 12280 | ori1024 ori4 ori2 ori1 |
| 0x826b6468 | 31 | 10552, 10584, 10588, 10592, 12280 | ori1024 ori4 ori2 ori1 |
| 0x826b64f8 | 31 | 10552, 10584, 10588, 10592, 12280 | ori1024 ori4 ori2 ori1 |
| 0x826b6588 | 23 | 10552, 10584, 10588, 10592, 12280 | ori1024 ori4 ori2 ori1 |
| 0x826b65f8 | 23 | 10552, 10584, 10588, 10592, 12280 | ori1024 ori4 ori2 ori1 |
| 0x826b6668 | 23 | 10552, 10584, 10588, 10592, 12280 | ori1024 ori4 ori2 ori1 |
| 0x826b66d8 | 33 | 10552, 10584, 10588, 10592, 12284 | ori1024 ori4 ori2 ori1 |
| 0x826b6770 | 13 |  | oris2048 |
| 0x826b67d0 | 7 | 10556 | ori512 |
| 0x826b6800 | 36 |  |  |
| 0x826b69d8 | 14 | 10548, 12308 | ori2048 oris2 |
| 0x826b6a18 | 7 | 10548 | ori2048 |
| 0x826b6a48 | 9 | 10548 | ori2048 oris2 |
| 0x826b6a80 | 14 | 10548, 12312 | ori2048 oris2 |
| 0x826b6ac0 | 9 | 10548 | ori2048 oris2 |
| 0x826b6af8 | 7 | 10548 | ori2048 |
| 0x826b6b28 | 9 | 10548 | ori2048 oris2 |
| 0x826b6b60 | 9 | 10548 | ori2048 oris2 |
| 0x826b6b98 | 7 | 10548 | ori2048 |
| 0x826b6bc8 | 7 | 10548 | ori2048 |
| 0x826b6bf8 | 9 | 10548 | ori2048 oris2 |
| 0x826b6c30 | 9 | 10548 | ori2048 oris2 |
| 0x826b6c68 | 7 | 10548 | ori2048 |
| 0x826b6c98 | 5 |  | oris4096 |
| 0x826b6cb8 | 5 |  | oris4096 |
| 0x826b6cd8 | 5 |  | oris4096 |
| 0x826b6cf8 | 5 |  | oris8192 |
| 0x826b6d18 | 5 |  | oris8192 |
| 0x826b6d38 | 5 |  | oris8192 |
| 0x826b6d58 | 17 | 10420, 10564 | ori128 |
| 0x826b6e80 | 38 | 10568 | ori64 |
| 0x826b6f28 | 7 | 10568 | ori64 |
| 0x826b6f78 | 14 | 10460, 12292 |  |
| 0x826b6fb8 | 14 | 10460, 12296 |  |
| 0x826b6ff8 | 14 | 10460, 12300 |  |
| 0x826b7038 | 14 | 10460, 12304 |  |
| 0x826b7180 | 8 | 10540 | ori8192 |
| 0x826b71a0 | 9 | 10540 | ori8192 |
| 0x826b71c8 | 9 | 10540 | ori8192 |
| 0x826b71f0 | 9 | 10540 | ori8192 |
| 0x826b7218 | 9 | 10540 | ori8192 |
| 0x826b7240 | 9 | 10540 | ori8192 |
| 0x826b7268 | 9 | 10540 | ori8192 |
| 0x826b7290 | 7 | 10540 | ori8192 |
| 0x826b72b0 | 8 | 10544 | ori4096 |
| 0x826b72d0 | 9 | 10544 | ori4096 |
| 0x826b72f8 | 9 | 10544 | ori4096 |
| 0x826b7320 | 9 | 10544 | ori4096 |
| 0x826b7348 | 9 | 10544 | ori4096 |
| 0x826b7370 | 9 | 10544 | ori4096 |
| 0x826b7398 | 9 | 10544 | ori4096 |
| 0x826b73c0 | 7 | 10544 | ori4096 |
| 0x826b74e0 | 16 | 10564, 10572 | ori32 ori128 |
| 0x826b7530 | 36 | 10372, 12408 |  |
| 0x826b75c8 | 36 | 10380, 12412 |  |
| 0x826b7660 | 36 | 10384, 12416 |  |
| 0x826b76f8 | 36 | 10388, 12420 |  |
| 0x826b7870 | 7 | 10568 | ori64 |
| 0x826b78a0 | 7 | 10456 |  |
| 0x826b78c8 | 7 | 10556 | ori512 |
| 0x826b78f8 | 7 | 10556 | ori512 |
| 0x826b79d8 | 7 | 12284 | oris2 |
| 0x826b7a08 | 21 | 12284 | ori256 |
| 0x826b7a70 | 7 | 10560 | ori256 |
| 0x826b7aa0 | 7 | 10560 | ori256 |
| 0x826b7ad0 | 7 | 10560 | ori256 |
| 0x826b7b00 | 5 |  | ori256 |
| 0x826b8e48 | 36 | 10368, 10684, 13052, 13056, 13060, 13064 |  |
