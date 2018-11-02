#define EXTEND_LOCAL_GROUPS
#include "../include/driver.glsl"
#include "../include/mathlib.glsl"
#include "../include/ballotlib.glsl"


// roundly like http://www.heterogeneouscompute.org/wordpress/wp-content/uploads/2011/06/RadixSort.pdf
// partially of https://vgc.poly.edu/~csilva/papers/cgf.pdf + and wide subgroup adaptation
// practice maximal throughput: 256x16 (of Nx256)


// 2-bit
//#define BITS_PER_PASS 2
//#define RADICES 4
//#define RADICES_MASK 0x3

// 4-bit
#define BITS_PER_PASS 4
#define RADICES 16
#define RADICES_MASK 0xF

// 8-bit
//#define BITS_PER_PASS 8
//#define RADICES 256
//#define RADICES_MASK 0xFF



// general work groups
#define Wave_Size_RX Wave_Size_RT
#define Wave_Count_RX Wave_Count_RT //(gl_WorkGroupSize.x / Wave_Size_RT.x)
//#define BLOCK_SIZE (Wave_Size * RADICES / AFFINITION) // how bigger block size, then more priority going to radices (i.e. BLOCK_SIZE / Wave_Size)

#ifdef ENABLE_INT16_SUPPORT
    #define U8s uint16_t
    #define U8v4 u16vec4
#else
    #define U8s uint
    #define U8v4 uvec4
#endif

// default values
#ifndef BLOCK_SIZE
#define BLOCK_SIZE (Wave_Size*16u)
#endif


#define BLOCK_SIZE_RT (gl_WorkGroupSize.x)
#define WRK_SIZE_RT (gl_NumWorkGroups.y * Wave_Count_RX)

#define uint_rdc_wave_lcm uint

// pointer of...
//#define WPTR4 uvec4 // may Turing unfriendly
#define WPTR1 uint
#define PREFER_UNPACKED


#ifdef ENABLE_INT16_SUPPORT
#define utype_t uint16_t
#define utype_v u16vec4
#else
#define utype_t uint
#define utype_v uvec4
#endif


#ifdef USE_MORTON_32
#define KEYTYPE uint
uint BFE(in uint ua, in int o, in int n) { return BFE_HW(ua, o, n); }
#else
#define KEYTYPE uvec2
uint BFE(in uvec2 ua, in int o, in int n) { return uint(o >= 32 ? BFE_HW(ua.y, o-32, n) : BFE_HW(ua.x, o, n)); }
#endif

struct RadicePropStruct { uint Descending; uint IsSigned; };

#ifdef COPY_HACK_IDENTIFY
#define INDIR 0
#define OUTDIR 1
#else
#define INDIR 1
#define OUTDIR 0
#endif

// used when filling
const KEYTYPE OutOfRange = KEYTYPE(0xFFFFFFFFu);

//#define KEYTYPE uint
layout ( binding = 0, set = INDIR, std430 )  readonly coherent buffer KeyInB {KEYTYPE KeyIn[]; };
layout ( binding = 1, set = INDIR, std430 )  readonly coherent buffer ValueInB {uint ValueIn[]; };
layout ( binding = 0, set = OUTDIR, std430 )  coherent buffer KeyTmpB {KEYTYPE KeyTmp[]; };
layout ( binding = 1, set = OUTDIR, std430 )  coherent buffer ValueTmpB {uint ValueTmp[]; };
layout ( binding = 2, set = 0, std430 )  readonly buffer VarsB { RadicePropStruct radProps[]; };
layout ( binding = 3, set = 0, std430 )  restrict buffer HistogramB {uint Histogram[]; };
layout ( binding = 4, set = 0, std430 )  restrict buffer PrefixSumB {uint PrefixSum[]; };

// push constant in radix sort
layout ( push_constant ) uniform PushBlock { uint NumKeys; int Shift; } push_block;

// division of radix sort
struct blocks_info { uint count; uint offset; uint limit; uint r0; };
blocks_info get_blocks_info(in uint n) {
    const uint block_tile = Wave_Size_RT << 0u;
    const uint block_size = tiled(n, gl_NumWorkGroups.x);
    const uint block_count = tiled(n, block_tile * gl_NumWorkGroups.x);
    const uint block_offset = gl_WorkGroupID.x * block_tile * block_count;
    return blocks_info(block_count, block_offset, min(block_offset + tiled(block_size, block_tile)*block_tile, n), 0);
};
