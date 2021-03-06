#ifndef _STRUCTS_H
#define _STRUCTS_H

#include "../include/mathlib.glsl"

#ifdef USE_F32_BVH 
    #define UNPACK_LROW(m) uintBitsToFloat(m)
#else
    #ifdef USE_F16_BVH
        #define UNPACK_LROW(m) unpackFloat4x16(m.xy)
    #else
        #define UNPACK_LROW(m) unpackHalf4x16(m.xy)
    #endif
#endif

#ifdef USE_F32_BVH
    #define UNPACK_RCRC(m) vec4(0.f)
#else
    #ifdef USE_F16_BVH
        #define UNPACK_RCRC(m) unpackFloat4x16(m.zw)
    #else
        #define UNPACK_RCRC(m) unpackHalf4x16(m.zw)
    #endif
#endif

#ifdef USE_F32_BVH
    #define UNPACK_HF(m) vec4(0.f)
#else
    #ifdef USE_F16_BVH
        #define UNPACK_HF(m) unpackFloat4x16(m)
    #else
        #define UNPACK_HF(m) unpackHalf4x16(m)
    #endif
#endif


// default built-in ray structure
//struct VtRay { vec4 origin; vec2 cdirect; uvec2 dcolor; };
struct VtRay { f32vec4 fdata; u32vec4 bitspace; };
#ifdef EXPERIMENTAL_UNORM16_DIRECTION
#define cdirect fdata.w
#else
#define cdirect bitspace.zw
#endif

#define origin fdata.xyz
#define dcolor bitspace.xy // RGB16F


// write color, but don't write (save) last element
u32vec2 vtWriteColor(inout u32vec2 rwby, in vec4 color) {
    const u32vec2 clr = f32_f16(color);
    rwby = u32vec2(clr.x, BFI_HW(clr.y, BFE_HW(rwby.y, 16, 16), 16, 16));
    return rwby;
};



// max attribute packing count
const int ATTRIB_EXTENT = 4;



struct VtHitData {
    //int next; uint bitfield; int r0, r1;
    int32_t attribID, rayID, payloadID, nextID; //materialID;
    f32vec4 uvt, hitVD; i32vec4 idat; // UV, distance, triangle (base data), normal
};




// 16-bit offset of rays
const lowp int B16FT = 16;

// ray bitfield
const lowp ivec2 
    RAY_ACTIVED = {B16FT+0, 1},
    RAY_TYPE = {B16FT+1, 2},
    RAY_TARGET_LIGHT = {B16FT+3, 5},
    RAY_BOUNCE = {B16FT+8, 3},
    RAY_DBOUNCE = {B16FT+11, 3},
    RAY_RAY_DL = {B16FT+14, 1};

// vertex bitfield
const lowp ivec2 
    VTX_TYPE = {0, 2},
    VTX_FRONT_FACE = {2, 1}, // 0 is enabled, 1 is disabled
    VTX_BACK_FACE = {3, 1}; // 0 is enabled, 1 is disabled




// getters 
bool vtParameterb( in const lowp ivec2 parameter, in  uint bitfield) { return bool(BFE_HW(bitfield, parameter.x, 1)); };
bool vtParameterb( in const lowp ivec2 parameter, in float bitfield) { return vtParameterb(parameter, floatBitsToUint(bitfield)); };
uint vtParameteri( in const lowp ivec2 parameter, in  uint bitfield) { return BFE_HW(bitfield, parameter.x, parameter.y); };
uint vtParameteri( in const lowp ivec2 parameter, in float bitfield) { return vtParameteri(parameter, floatBitsToUint(bitfield)); };

// setters
void vtParameteri( in const lowp ivec2 parameter, inout  uint bitfield, in uint  value) { bitfield = BFI_HW(bitfield, value, parameter.x, parameter.y); };
void vtParameteri( in const lowp ivec2 parameter, inout float bitfield, in uint  value) { bitfield = uintBitsToFloat(BFI_HW(floatBitsToUint(bitfield), value, parameter.x, parameter.y)); };

// boolean based
void vtParameterb( in const lowp ivec2 parameter, inout  uint bitfield, in bool  value) { bitfield = BFI_HW(bitfield, uint(value), parameter.x, 1); };
void vtParameterb( in const lowp ivec2 parameter, inout float bitfield, in bool  value) { bitfield = uintBitsToFloat(BFI_HW(floatBitsToUint(bitfield), uint(value), parameter.x, 1));};

// integer based
void vtParameteri( in const lowp ivec2 parameter, inout  uint bitfield, in  int  value) { vtParameteri(parameter, bitfield, uint(value)); };
void vtParameteri( in const lowp ivec2 parameter, inout float bitfield, in  int  value) { vtParameteri(parameter, bitfield, uint(value)); };

struct bbox_t { f32vec4 mn, mx; };
struct leaf_t { bbox_t lbox; i32vec4 pdata; };

#endif
