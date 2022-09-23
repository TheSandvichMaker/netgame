#pragma once

// ------------------------------------------------------------------
// standard library includes

#include <stdint.h>
#include <stddef.h>
#include <assert.h>

// ------------------------------------------------------------------
// util.h: just useful stuff

// ------------------------------------------------------------------
// assert macros, useful for writing conditionals for situations that
// should not happen but should still be handled, such as:
// 
// if (ALWAYS(index < count))
//    my_array[index] = ...;
//    
// you can avoid the array out-of-bounds dereference, even if asserts 
// are compiled out in the build

#define ALWAYS(x) (assert(x), x)
#define NEVER(x) (assert(!(x)), x)

// ------------------------------------------------------------------

#define ARRAY_COUNT(arr) (sizeof(arr) / sizeof((arr)[0]))
#define SWAP(t, a, b) do { t __t = a; a = b; b = __t; } while(0)

static inline float Lerp(float a, float b, float t)
{
	return (1.0f - t)*a + t*b;
}
