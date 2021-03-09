#ifndef __GAME_DEFINE__
#define __GAME_DEFINE__
#include <cstdint>

/** DEFINES AND TYPEDEFS **/
#define INTERNAL static             // static functions are internal to the translation unit
/* Put "static" in front of all functions, and that prevents them from ever going into the linking table.
This causes the compiler to treat them as intra-unit linkage and it doesn't ever have to even do the work
 of moving to the link phase. */
#define LOCAL_PERSIST static        // local static variables are only accessible within the scope of its declaration
#define GLOBAL_VAR static           // global static variables are global to the translation unit

typedef uint8_t     uint8;
typedef uint16_t    uint16;
typedef uint32_t    uint32;
typedef uint64_t    uint64;

typedef int8_t      int8;
typedef int16_t     int16;
typedef int32_t     int32;
typedef int64_t     int64;

typedef float       real32;
typedef double      real64;

typedef size_t      mi;         // memory index

const float TO_RADIANS = 0.0174532925f; // in_degrees * TO_RADIANS = in_radians
const float TO_DEGREES = 57.2958f;      // in_radians * TO_DEGREES = in_degrees

#define INDEX_NONE -1
#define ASCII_SPACE 32
#define ASCII_TILDE 126

#define array_count(arr) (sizeof(arr) / (sizeof(arr[0])))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

#define _CRT_SECURE_NO_WARNINGS

/** GAME SPECIFIC **/
#define WIDTH 1280
#define HEIGHT 720

#endif