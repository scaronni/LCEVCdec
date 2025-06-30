/* Copyright (c) V-Nova International Limited 2022-2025. All rights reserved.
 * This software is licensed under the BSD-3-Clause-Clear License by V-Nova Limited.
 * No patent licenses are granted under this license. For enquiries about patent licenses,
 * please contact legal@v-nova.com.
 * The LCEVCdec software is a stand-alone project and is NOT A CONTRIBUTION to any other project.
 * If the software is incorporated into another project, THE TERMS OF THE BSD-3-CLAUSE-CLEAR LICENSE
 * AND THE ADDITIONAL LICENSING INFORMATION CONTAINED IN THIS FILE MUST BE MAINTAINED, AND THE
 * SOFTWARE DOES NOT AND MUST NOT ADOPT THE LICENSE OF THE INCORPORATING PROJECT. However, the
 * software may be incorporated into a project under a compatible license provided the requirements
 * of the BSD-3-Clause-Clear license are respected, and V-Nova Limited remains
 * licensor of the software ONLY UNDER the BSD-3-Clause-Clear license (not the compatible license).
 * ANY ONWARD DISTRIBUTION, WHETHER STAND-ALONE OR AS PART OF ANY OTHER PROJECT, REMAINS SUBJECT TO
 * THE EXCLUSION OF PATENT LICENSES PROVISION OF THE BSD-3-CLAUSE-CLEAR LICENSE. */

#ifndef VN_LCEVC_LEGACY_MEMORY_H
#define VN_LCEVC_LEGACY_MEMORY_H

#include "common/platform.h"

/*! \file
 * \brief This file provides dynamic memory functionality.
 *
 * There are 2 primary functions provided here:
 *
 * # Heap allocations
 *
 * This provides memory allocation and freeing routines, with 2 features:
 *
 *      1. The user of the library may provide their own functions
 *         that will be invoked. If none are supplied then the C standard
 *         library functions are used (`malloc`, `calloc`, `realloc`, `free`).
 *
 *      2. Optional memory tracing, this will store the location and type
 *         of allocation performed, and any leaks are reported upon decoder
 *         shutdown. Additionally an allocation histogram is logged which can
 *         be used to analyse the decoder memory characteristics.
 *
 * For all allocation functions the allocation may fail, and the user must
 * check for this and react accordingly.
 *
 * # Memory modifications
 *
 * This provides the common memory copying and setting behaviors, similarly to
 * allocations they just wrap the C standard library functions.
 */

/*------------------------------------------------------------------------------*/

typedef void* (*AllocateFunction_t)(void* userData, size_t size);
typedef void* (*AllocateZeroFunction_t)(void* userData, size_t size);
typedef void (*FreeFunction_t)(void* userData, void* ptr);
typedef void* (*ReallocFunction_t)(void* userData, void* ptr, size_t size);

/*! Memory interface initialization settings.
 *
 *  For the user supplied functions if one is supplied, then all must be supplied
 *  except for userAllocateZero, this function is fully optional. When it is null
 *  then `userAllocate` is invoked followed by a memorySet to zero of the returned
 *  allocation. */
typedef struct MemorySettings
{
    void* userData; /**< User data pointer that is passed through to the user functions. */
    AllocateFunction_t userAllocate;         /**< User allocate function */
    AllocateZeroFunction_t userAllocateZero; /**< User allocate zero function */
    FreeFunction_t userFree;                 /**< User free function */
    ReallocFunction_t userReallocate;        /**< User reallocate function */
} MemorySettings_t;

/*! Opaque handle for the memory interface. */
typedef struct Memory* Memory_t;
typedef struct Logger* Logger_t;

/*------------------------------------------------------------------------------*/

/*! Create an instance of the memory system. */
bool memoryInitialise(Memory_t* handle, const MemorySettings_t* settings);

/*! Destroy an instance of the memory system. */
void memoryRelease(Memory_t memory);

/*! \brief Perform a dynamic memory allocation.
 *
 * If successful this function will allocate at least `size` bytes of memory.
 *
 * \param memory  The memory system to allocate with.
 * \param size    The number of bytes to allocate.
 * \param zero    If the allocation should be zeroed.
 *
 * \return A valid pointer to some memory of at least `size` bytes, or NULL on failure. */
void* memoryAllocate(Memory_t memory, size_t size, bool zero);

/*! Perform a dynamic memory reallocation.
 *
 * If successful this function will allocate at least `size` bytes of memory.
 *
 * This `ptr` parameter may be passed as NULL, in this situation the function
 * behaves just like `memoryAllocate` with `zero` set to false.
 *
 * If `ptr` is passed in the memory must have been either allocated or reallocated
 * using the same `memory` system (i.e. allocations are constrained to a single
 * memory instance).
 *
 * \param memory  The memory system to allocate with.
 * \param ptr     A pointer to memory to reallocate, or NULL.
 * \param size    The number of bytes to reallocate to.
 *
 * \return A valid pointer to some memory of at least `size` bytes, or NULL on failure. */
void* memoryReallocate(Memory_t memory, void* ptr, size_t size);

/*! Perform dynamic memory freeing.
 *
 * The memory being freed in this function must have been allocated or reallocated
 * with the same `memory` system that is being used to free.
 *
 * This function will zero `*ptr` as a user convenience.
 *
 * \param memory The memory to free with.
 * \param ptr    Pointer to the pointer of memory to free.
 */
void memoryFree(Memory_t memory, void** ptr);

/*! Copy memory of `size` bytes from `src` to `dst`.
 *
 * There are no requirements about where the memory is allocated for this
 * function to succeed.
 *
 * The user is responsible for ensuring that `dst` has at least `size` bytes
 * available.
 *
 * \param dst   The destination memory to copy into.
 * \param src   The source memory to copy from.
 * \param size  The number of bytes to copy from.
 */
void memoryCopy(void* dst, const void* src, size_t size);

/*! Set memory of `size` bytes to `value` for each byte.
 *
 * There are no requirements about where the memory is allocated for this
 * function to succeed.
 *
 * The user is responsible for ensuring that `dst` has at least `size` bytes
 * available.
 *
 * This function sets the value of each byte to `value`. The type of
 * value is chosen to mirror that of the C standard function `memset`.
 *
 * \param dst    The destination memory to set.
 * \param value  The value to set the bytes within `dst` to.
 * \param size   The number of bytes to set. */
void memorySet(void* dst, int32_t value, size_t size);

/*------------------------------------------------------------------------------*/

/* clang-format off */

/**! Helper for performing malloc for a single object. */
#define VN_MALLOC_T(memory, type) (type*)memoryAllocate(memory, sizeof(type), false)

/**! Helper for performing malloc for an array of objects. */
#define VN_MALLOC_T_ARR(memory, type, count) (type*)memoryAllocate(memory, sizeof(type) * (count), false)

/**! Helper for performing calloc for a single object. */
#define VN_CALLOC_T(memory, type) (type*)memoryAllocate(memory, sizeof(type), true)

/**! Helper for performing calloc for an array of objects. */
#define VN_CALLOC_T_ARR(memory, type, count) (type*)memoryAllocate(memory, sizeof(type) * (count), true)

/**! Helper for performing realloc for a single object. */
#define VN_REALLOC_T(memory, ptr, type) (type*)memoryReallocate(memory, (void*)(ptr), sizeof(type))

/**! Helper for performing realloc for an array of objects. */
#define VN_REALLOC_T_ARR(memory, ptr, type, count) (type*)memoryReallocate(memory, (void*)(ptr), sizeof(type) * (count))

/**! Helper for freeing an allocation performed with one of the above macros. */
#define VN_FREE(memory, ptr) do { memoryFree(memory, (void**)&(ptr)); } while(false)

/* clang-format on */

/*------------------------------------------------------------------------------*/

#endif // VN_LCEVC_LEGACY_MEMORY_H
