#pragma once

// CUSTOM OTR COMMANDS
#define G_SETTIMG_OTR_HASH 0x20
#define G_SETFB 0x21
#define G_RESETFB 0x22
#define G_SETTIMG_FB 0x23
#define G_VTX_OTR_FILEPATH 0x24
#define G_SETTIMG_OTR_FILEPATH 0x25
#define G_TRI1_OTR 0x26
#define G_DL_OTR_FILEPATH 0x27
#define G_PUSHCD 0x28
#define G_MTX_OTR2 0x29
#define G_DL_OTR_HASH 0x31
#define G_VTX_OTR_HASH 0x32
#define G_MARKER 0x33
#define G_INVALTEXCACHE 0x34
#define G_BRANCH_Z_OTR 0x35
#define G_MTX_OTR 0x36
#define G_TEXRECT_WIDE 0x37
#define G_FILLWIDERECT 0x38
#define G_DL_OTR_INDEX 0x3D
#define G_MOVEMEM_OTR_HASH 0x42

// DL FLAGS
#define G_DL_PUSH 0x00
#define G_DL_NO_PUSH 0x01

/* GFX Effects */
#define G_SETGRAYSCALE 0x39
#define G_EXTRAGEOMETRYMODE 0x3a
#define G_SETINTENSITY 0x40
#define G_SETFILTERING 0x41

#define gDPFillWideRectangle(pkt, ulx, uly, lrx, lry)                           \
{                                                                           \
    N64Gfx *_g0 = (N64Gfx*)(pkt), *_g1 = (N64Gfx*)(pkt);                             \
    _g0->words.w0 = _SHIFTL(G_FILLWIDERECT, 24, 8) | _SHIFTL((lrx), 2, 22); \
    _g0->words.w1 = _SHIFTL((lry), 2, 22);                                  \
    _g1->words.w0 = _SHIFTL((ulx), 2, 22);                                  \
    _g1->words.w1 = _SHIFTL((uly), 2, 22);                                  \
}

#define gSPWideTextureRectangle(pkt, xl, yl, xh, yh, tile, s, t, dsdx, dtdy)   \
{                                                                          \
    N64Gfx *_g0 = (N64Gfx*)(pkt), *_g1 = (N64Gfx*)(pkt), *_g2 = (N64Gfx*)(pkt);        \
                                                                            \
    _g0->words.w0 = _SHIFTL(G_TEXRECT_WIDE, 24, 8) | _SHIFTL((xh), 0, 24); \
    _g0->words.w1 = _SHIFTL((yh), 0, 24);                                  \
    _g1->words.w0 = (_SHIFTL(tile, 24, 3) | _SHIFTL((xl), 0, 24));         \
    _g1->words.w1 = _SHIFTL((yl), 0, 24);                                  \
    _g2->words.w0 = (_SHIFTL(s, 16, 16) | _SHIFTL(t, 0, 16));              \
    _g2->words.w1 = (_SHIFTL(dsdx, 16, 16) | _SHIFTL(dtdy, 0, 16));        \
}

#define gsSPWideTextureRectangle(xl, yl, xh, yh, tile, s, t, dsdx, dtdy)                         \
{ {                                                                                          \
    (_SHIFTL(G_TEXRECT_WIDE, 24, 8) | _SHIFTL((xh), 0, 24)),                                 \
    _SHIFTL((yh), 0, 24),                                                                    \
} },                                                                                         \
    { {                                                                                      \
        (_SHIFTL((tile), 24, 3) | _SHIFTL((xl), 0, 24)),                                     \
        _SHIFTL((yl), 0, 24),                                                                \
    } },                                                                                     \
{                                                                                            \
    { _SHIFTL(s, 16, 16) | _SHIFTL(t, 0, 16), _SHIFTL(dsdx, 16, 16) | _SHIFTL(dtdy, 0, 16) } \
}

#define gSPExtraGeometryMode(pkt, c, s)                                                 \
_DW({                                                                               \
    N64Gfx* _g = (N64Gfx*)(pkt);                                                          \
                                                                                    \
    _g->words.w0 = _SHIFTL(G_EXTRAGEOMETRYMODE, 24, 8) | _SHIFTL(~(u32)(c), 0, 24); \
    _g->words.w1 = (u32)(s);                                                        \
})

#define gSPSetExtraGeometryMode(pkt, word) gSPExtraGeometryMode((pkt), 0, word)
#define gSPClearExtraGeometryMode(pkt, word) gSPExtraGeometryMode((pkt), word, 0)

#define __gSPInvalidateTexCache(pkt, addr)              \
    _DW({                                               \
        N64Gfx* _g = (N64Gfx*)(pkt);                          \
                                                        \
        _g->words.w0 = _SHIFTL(G_INVALTEXCACHE, 24, 8); \
        _g->words.w1 = addr;                            \
    })

#define gsSPInvalidateTexCache() \
    { _SHIFTL(G_INVALTEXCACHE, 24, 8), 0 }

#define gsSPSetFB(pkt, fb)                      \
    {                                           \
        N64Gfx* _g = (N64Gfx*)(pkt);                  \
                                                \
        _g->words.w0 = _SHIFTL(G_SETFB, 24, 8); \
        _g->words.w1 = fb;                      \
    }

#define gsSPResetFB(pkt)                          \
    {                                             \
        N64Gfx* _g = (N64Gfx*)(pkt);                    \
                                                  \
        _g->words.w0 = _SHIFTL(G_RESETFB, 24, 8); \
        _g->words.w1 = 0;                         \
    }

#define gSPGrayscale(pkt, state)                       \
    {                                                  \
        N64Gfx* _g = (N64Gfx*)(pkt);                         \
                                                       \
        _g->words.w0 = _SHIFTL(G_SETGRAYSCALE, 24, 8); \
        _g->words.w1 = state;                          \
    }

#define gsSPGrayscale(state) \
    { (_SHIFTL(G_SETGRAYSCALE, 24, 8)), (state) }

#define gSPDisableFiltering(pkt, state)                       \
    {                                                  \
        N64Gfx* _g = (N64Gfx*)(pkt);                         \
                                                       \
        _g->words.w0 = _SHIFTL(G_SETFILTERING, 24, 8); \
        _g->words.w1 = state;                          \
    }

#define gsSPDisableFiltering(state) \
    { (_SHIFTL(G_SETFILTERING, 24, 8)), (state) }

#define gsSP1TriangleOTR(v0, v1, v2, flag) \
    { _SHIFTL(G_TRI1_OTR, 24, 8) | __gsSP1Triangle_w1f(v0, v1, v2, flag), 0 }

#define gsSPVertexOTR(v, n, v0) \
    { (_SHIFTL(G_VTX_OTR_HASH, 24, 8) | _SHIFTL((n), 12, 8) | _SHIFTL((v0) + (n), 1, 7)), (uint32_t)(v) }

#define gsSPRawOpcode(opcode) \
    { _SHIFTL(opcode, 24, 8), 0 }

#define	gsDPSetTextureOTRImage(fmt, siz, width, i)	\
    {{									\
        _SHIFTL(G_SETTIMG_OTR_HASH, 24, 8) | _SHIFTL(fmt, 21, 3) |			\
        _SHIFTL(siz, 19, 2) | _SHIFTL((width)-1, 0, 12),		\
        (uint32_t)(i)						\
    }}

#define gsSPBranchListOTRHash(dl) \
    {{									\
        (_SHIFTL((G_DL_OTR_HASH), 24, 8) | _SHIFTL((0x01), 16, 8) | 			\
         _SHIFTL((0), 1, 16)), 						\
            (uint32_t)(dl)						\
    }}

#define gsSPBranchListOTRFilePath(dl) \
    {{									\
        (_SHIFTL((G_DL_OTR_FILEPATH), 24, 8) | _SHIFTL((0x01), 16, 8) | 			\
         _SHIFTL((0), 0, 16)), 						\
            (uint32_t)(dl)						\
    }}

#define gsSPDisplayListOTRHash(dl) \
    {{									\
        (_SHIFTL((G_DL_OTR_HASH), 24, 8) | _SHIFTL((0x00), 16, 8) | 			\
         _SHIFTL((0), 0, 16)), 						\
            (uint32_t)(dl)						\
    }}

#define gsSPDisplayListOTRIndex(dl) \
    {{                                  \
        (_SHIFTL((G_DL_OTR_INDEX), 24, 8) | _SHIFTL((0x00), 16, 8) | 			\
         _SHIFTL((0), 0, 16)), 						\
            (uint32_t)((SEGMENT_NUMBER(dl) << 24) | ((SEGMENT_OFFSET(dl) / 8) & 0x00FFFFFF))	\
    }}

#define gsSPDisplayListOTRFilePath(dl) \
    {{									\
        (_SHIFTL((G_DL_OTR_FILEPATH), 24, 8) | _SHIFTL((0x00), 16, 8) | 			\
         _SHIFTL((0), 0, 16)), 						\
            (uint32_t)(dl)						\
    }}
#define gDPSetGrayscaleColor(pkt, r, g, b, lerp) DPRGBColor(pkt, G_SETINTENSITY, r, g, b, lerp)
#define gsDPSetGrayscaleColor(r, g, b, a) sDPRGBColor(G_SETINTENSITY, r, g, b, a)

#define gsDPSetCombineLERP_NoMacros(a0, b0, c0, d0, Aa0, Ab0, Ac0, Ad0, a1, b1, c1, d1, Aa1, Ab1, Ac1, Ad1) \
    {                                                                                                       \
        _SHIFTL(G_SETCOMBINE, 24, 8) | _SHIFTL(GCCc0w0(a0, c0, Aa0, Ac0) | GCCc1w0(a1, c1), 0, 24),         \
            (unsigned int)(GCCc0w1(b0, d0, Ab0, Ad0) | GCCc1w1(b1, Aa1, Ac1, d1, Ab1, Ad1))                 \
    }
