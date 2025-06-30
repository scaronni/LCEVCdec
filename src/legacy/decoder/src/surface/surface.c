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

#include "surface/surface.h"

#include "common/log.h"
#include "common/memory.h"
#include "common/threading.h"
#include "common/types.h"
#include "context.h"
#include "surface/blit.h"

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*------------------------------------------------------------------------------*/

/**! Utility function to allocate zero initialised memory for a surface. */
static uint8_t* surfaceAllocateData(Memory_t memory, const uint32_t stride, const uint32_t height,
                                    FixedPoint_t type)
{
    const size_t bpp = ldlFixedPointByteSize(type);
    const size_t allocationSize = (size_t)stride * height * bpp;

    if (allocationSize == 0) {
        return NULL;
    }

    return VN_CALLOC_T_ARR(memory, uint8_t, allocationSize);
}

/*------------------------------------------------------------------------------*/

int32_t surfaceInitialise(Memory_t memory, Surface_t* surface, FixedPoint_t type, uint32_t width,
                          uint32_t height, uint32_t stride, Interleaving_t interleaving)
{
    assert(surfaceIsIdle(surface));

    uint8_t* data = surfaceAllocateData(memory, stride, height, type);
    if (data == NULL) {
        return -1;
    }

    surface->data = data;
    surface->type = type;
    surface->width = width;
    surface->height = height;
    surface->stride = stride;
    surface->interleaving = interleaving;
    surface->external = false;

    return 0;
}

int32_t surfaceInitialiseExt(Surface_t* surface, void* data, FixedPoint_t type, uint32_t width,
                             uint32_t height, uint32_t stride, Interleaving_t interleaving)
{
    assert(surfaceIsIdle(surface));

    surface->data = (uint8_t*)data;
    surface->type = type;
    surface->width = width;
    surface->height = height;
    surface->stride = stride;
    surface->interleaving = interleaving;
    surface->external = true;

    return 0;
}

int32_t surfaceInitialiseExt2(Surface_t* surface, FixedPoint_t type, uint32_t width,
                              uint32_t height, uint32_t stride, Interleaving_t interleaving)
{
    surface->type = type;
    surface->width = width;
    surface->height = height;
    surface->stride = stride;
    surface->interleaving = interleaving;
    surface->external = true;

    return 0;
}

void surfaceRelease(Memory_t memory, Surface_t* surface)
{
    if (!surface->external && surface->data) {
        VN_FREE(memory, surface->data);
    }

    surfaceIdle(surface);
}

void surfaceIdle(Surface_t* surface) { memorySet(surface, 0, sizeof(Surface_t)); }

uint8_t surfaceIsIdle(const Surface_t* surface) { return surface->data == NULL; }

uint8_t surfaceCompatible(const Surface_t* surface, FixedPoint_t type, uint32_t stride,
                          uint32_t height, Interleaving_t interleaving)
{
    if (surfaceIsIdle(surface)) {
        return 0;
    }

    /* @todo think carefully if this condition may be less strict */
    return surface->stride == stride && surface->height == height && surface->type == type &&
           surface->interleaving == interleaving;
}

void surfaceZero(Memory_t memory, Surface_t* surface)
{
    if (surfaceIsIdle(surface)) {
        return;
    }

    if (!surface->external) {
        VN_FREE(memory, surface->data);
        surface->data = surfaceAllocateData(memory, surface->stride, surface->height, surface->type);
    }
}

void surfaceToFile(Logger_t log, Memory_t memory, Context_t* ctx, const Surface_t* surfaces,
                   uint32_t planeCount, const char* path)
{
    if (!surfaces || !planeCount) {
        return;
    }

    FILE* file = fopen(path, "ab+");

    if (!file) {
        return;
    }

    for (uint32_t i = 0; i < planeCount; ++i) {
        const FixedPoint_t type = surfaces[i].type;
        const FixedPoint_t lptype = ldlFixedPointLowPrecision(type);

        if (surfaces[i].interleaving != ILNone) {
            VN_ERROR(log, "Unsupported surface to file. Surface must not have interleaving\n");
            return;
        }

        if (lptype != surfaces[i].type) {
            Surface_t tmp;
            const uint32_t byteSize =
                surfaces[i].width * surfaces[i].height * ldlFixedPointByteSize(lptype);

            surfaceIdle(&tmp);
            surfaceInitialise(memory, &tmp, lptype, surfaces[i].width, surfaces[i].height,
                              surfaces[i].width, ILNone);
            surfaceBlit(log, &(ctx->threadManager), ctx->cpuFeatures, &surfaces[i], &tmp, BMCopy);
            fwrite(surfaces[i].data, byteSize, 1, file);
            surfaceRelease(memory, &tmp);
        } else {
            const uint32_t byteSize =
                surfaces[i].width * surfaces[i].height * ldlFixedPointByteSize(type);
            fwrite(surfaces[i].data, byteSize, 1, file);
        }
    }

    fflush(file);
    fclose(file);
}

int32_t surfaceGetChannelSkipOffset(const Surface_t* surface, uint32_t channelIdx, uint32_t* skip,
                                    uint32_t* offset)
{
    return interleavingGetChannelSkipOffset(surface->interleaving, channelIdx, skip, offset);
}

uint8_t* surfaceGetLine(const Surface_t* surface, uint32_t y)
{
    return surface->data + (size_t)(y * surface->stride * ldlFixedPointByteSize(surface->type));
}

size_t surfaceGetStrideInPixels(const Surface_t* surface) { return surface->stride; }

/*------------------------------------------------------------------------------*/

static const int32_t kFormatBufferLength = 16384;
static VN_THREADLOCAL() char tlPathFormatBuffer[16384];
static VN_THREADLOCAL() char tlIDFormatBuffer[16384];

static const char* fixedPointToVooyaString(FixedPoint_t fpType)
{
    switch (fpType) {
        case FPU8: return "8bit";
        case FPU10: return "10bit";
        case FPU12: return "12bit";
        case FPU14: return "14bit";
        case FPS8:
        case FPS10:
        case FPS12:
        case FPS14: return "-16bit";
        case FPCount: break;
    }

    return "error_fp_type";
}

/* Entry in the surface dump cache */
typedef struct SurfaceDumpEntry
{
    FILE* file;        /**< File handle for writing surface to. */
    const char* id;    /**< Identifier used to look up this dump. */
    FixedPoint_t type; /**< Fixed point type initialised with. */
    uint32_t stride;   /**< Stride in pixels initialised with. */
    uint32_t height;   /**< Height in pixels initialised with. */
} SurfaceDumpEntry_t;

/* Cache to store unique surface dump instances keyed on user supplied ID. */
typedef struct SurfaceDumpCache
{
    Memory_t memory;
    SurfaceDumpEntry_t* entries;
    int32_t entryCount;
    Mutex_t* mutex;
} SurfaceDumpCache_t;

int32_t surfaceDumpCacheInitialise(Memory_t memory, Logger_t log, SurfaceDumpCache_t** cache)
{
    SurfaceDumpCache_t* newCache = VN_CALLOC_T(memory, SurfaceDumpCache_t);

    if (!newCache) {
        return -1;
    }

    if ((mutexInitialise(memory, &newCache->mutex) != 0) || newCache->mutex == NULL) {
        VN_ERROR(log, "Failed to create surface dump cache mutex\n");
        VN_FREE(memory, newCache);
        return -1;
    }

    newCache->memory = memory;

    *cache = newCache;
    return 0;
}

void surfaceDumpCacheRelease(SurfaceDumpCache_t* cache)
{
    if (!cache) {
        return;
    }

    SurfaceDumpEntry_t* entries = cache->entries;

    Memory_t memory = cache->memory;

    /* Either both are empty, or both are populated */
    assert((cache->entryCount != 0) == (entries != NULL));

    for (int32_t i = 0; (i < cache->entryCount) && entries; ++i) {
        SurfaceDumpEntry_t* entry = &entries[i];

        if (entry->id) {
            VN_FREE(memory, entry->id);
        }

        if (entry->file) {
            fclose(entry->file);
            entry->file = NULL;
        }
    }

    mutexRelease(cache->mutex);

    VN_FREE(memory, entries);
    VN_FREE(memory, cache);
}

static void surfaceDumpCacheLock(SurfaceDumpCache_t* cache)
{
    assert(cache);
    mutexLock(cache->mutex);
}

static void surfaceDumpCacheUnlock(SurfaceDumpCache_t* cache)
{
    assert(cache);
    mutexUnlock(cache->mutex);
}

static SurfaceDumpEntry_t* surfaceDumpCacheQuery(SurfaceDumpCache_t* cache, const char* id)
{
    if (!cache->entries) {
        return NULL;
    }

    for (int32_t i = 0; i < cache->entryCount; ++i) {
        SurfaceDumpEntry_t* entry = &cache->entries[i];
        if (strcmp(entry->id, id) == 0) {
            return entry;
        }
    }

    return NULL;
}

static SurfaceDumpEntry_t* surfaceDumpCacheAdd(SurfaceDumpCache_t* cache, Memory_t memory, Logger_t log,
                                               Context_t* ctx, const char* id, const Surface_t* surface)
{
    int32_t res = 0;

    /* Format filepath with ID and Vooya specifiers */
    if (ctx->dumpPath) {
        res = snprintf(tlPathFormatBuffer, kFormatBufferLength, "%s/%s_%ux%u_%s.y", ctx->dumpPath,
                       id, surface->stride, surface->height, fixedPointToVooyaString(surface->type));
    } else {
        res = snprintf(tlPathFormatBuffer, kFormatBufferLength, "%s_%ux%u_%s.y", id,
                       surface->stride, surface->height, fixedPointToVooyaString(surface->type));
    }

    /* Check formatting was successful */
    if (res < 0 || res >= kFormatBufferLength) {
        VN_ERROR(log, "Failed to format surface dump file path\n");
        return NULL;
    }

    /* Try opening the file. */
    FILE* handle = fopen(tlPathFormatBuffer, "wb");
    if (!handle) {
        VN_ERROR(log, "Failed to open surface dump file: %s [%s]\n", tlPathFormatBuffer, strerror(errno));
        return NULL;
    }

    const int32_t newCount = cache->entryCount + 1;

    /* Expand cache */
    SurfaceDumpEntry_t* newEntries =
        VN_REALLOC_T_ARR(memory, cache->entries, SurfaceDumpEntry_t, newCount);

    if (!newEntries) {
        VN_ERROR(log, "Failed to expand surface dump cache entry memory\n");
        goto error_exit;
    }

    /* Cache surface settings. */
    SurfaceDumpEntry_t* entry = &newEntries[cache->entryCount];
    entry->file = handle;
    entry->type = surface->type;
    entry->stride = surface->stride;
    entry->height = surface->height;

    if (strcpyDeep(memory, id, &entry->id) != 0) {
        VN_ERROR(log, "Failed to store ID on cache entry");
        goto error_exit;
    }

    cache->entries = newEntries;
    cache->entryCount = newCount;

    return entry;

error_exit:
    VN_FREE(memory, newEntries);
    fclose(handle);
    return NULL;
}

static int32_t surfaceDumpValidateSettings(const SurfaceDumpEntry_t* dump, const Surface_t* surface)
{
    if (surface->type != dump->type || surface->stride != dump->stride || surface->height != dump->height) {
        return -1;
    }

    return 0;
}

int32_t surfaceDump(Memory_t memory, Logger_t log, Context_t* ctx, const Surface_t* surface,
                    const char* idFormat, ...)
{
    if (!ctx->dumpSurfaces) {
        return 0;
    }

    /* Generate ID */
    va_list args;
    va_start(args, idFormat);
    int32_t res = vsnprintf(tlIDFormatBuffer, kFormatBufferLength, idFormat, args);
    va_end(args);

    /* Check formatting was successful */
    if (res < 0 || res >= kFormatBufferLength) {
        VN_ERROR(log, "Failed to format surface dump ID\n");
        return -1;
    }

    /* Global lock cache whilst we mutate state. */
    surfaceDumpCacheLock(ctx->surfaceDumpCache);

    /* Grab or add cache entry. */
    SurfaceDumpEntry_t* entry = surfaceDumpCacheQuery(ctx->surfaceDumpCache, tlIDFormatBuffer);

    if (!entry) {
        /* New entry, let's register it */
        entry = surfaceDumpCacheAdd(ctx->surfaceDumpCache, memory, log, ctx, tlIDFormatBuffer, surface);

        if (!entry) {
            VN_ERROR(log, "Failed to add entry to the surface dump cache\n");
            res = -1;
            goto error_exit;
        }
    }

    /* Ensure we have consistent surface settings. */
    if (surfaceDumpValidateSettings(entry, surface) != 0) {
        VN_ERROR(log, "Surface dump entry was initialised with settings that differ to the input "
                      "surface, dynamic surface changes are not supported\n");
        res = -1;
        goto error_exit;
    }

    fwrite(surface->data, (size_t)ldlFixedPointByteSize(surface->type),
           (size_t)surface->stride * surface->height, entry->file);
    fflush(entry->file);

error_exit:
    surfaceDumpCacheUnlock(ctx->surfaceDumpCache);
    return res;
}

/*------------------------------------------------------------------------------*/
