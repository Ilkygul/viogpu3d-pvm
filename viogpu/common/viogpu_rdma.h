/*
 * viogpu3d on a Gunyah protected VM (DroidVM).
 *
 * In a protected VM the host can only read and write shared memory. Ordinary guest pages are
 * lent to the guest and invisible to crosvm, so every byte the virtio-gpu device touches has to
 * live in a shared region:
 *   - the vrings              -> a ring area in the restricted DMA pool (rdmapool, ACPI\RDMA0000)
 *   - commands and responses  -> staged per vbuf in a variable-size, page-granular allocator that
 *                                lives in the same pool; copied in on queue, the response copied
 *                                back on dequeue
 *   - framebuffer segment     -> one contiguous region of the pool (single backing entry)
 *   - GPU_GUEST / VENUS_HOST  -> fixed physical windows (ACPI\DRVM0001), see viogpu_pools.*
 * Without rdmapool nothing here is active and the stock paths run unchanged.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

extern "C"
{
#include <ntddk.h>
}

/* Open the pool (PASSIVE_LEVEL, idempotent). FALSE: no pool, stock behaviour. */
BOOLEAN VioGpuRdmaOpen(void);
BOOLEAN VioGpuRdmaActive(void);
/* Start of every adapter init (PASSIVE_LEVEL): rewinds the ring area. */
void VioGpuRdmaReset(void);

/* Vring memory (PASSIVE_LEVEL). NULL when the ring area is used up. */
PVOID VioGpuRdmaRingAlloc(SIZE_T size);
/* TRUE when va lies in any pool region this driver holds; *pa is then its device address. */
BOOLEAN VioGpuRdmaVaToPa(PVOID va, PHYSICAL_ADDRESS *pa);

/*
 * Staging memory for commands/responses: a physically contiguous run of whole pages, any size up
 * to the staging area. Usable at any IRQL <= DISPATCH_LEVEL. NULL when no run is free.
 * The memory is NOT zeroed.
 */
PVOID VioGpuRdmaStageAlloc(SIZE_T bytes, PHYSICAL_ADDRESS *pa);
void VioGpuRdmaStageFree(PVOID va, SIZE_T bytes);

/* A contiguous pool region (PASSIVE_LEVEL), for the frame segment. */
PVOID VioGpuRdmaRegionAlloc(SIZE_T bytes, PHYSICAL_ADDRESS *pa);
void VioGpuRdmaRegionFree(PVOID va);
/* Memory the host can read in a protected VM besides the rdma pool regions: register the GPU_GUEST /
 * VENUS_HOST windows, then ask whether a backing entry [pa, pa+len) lies inside one shared region
 * (crosvm refuses ATTACH_BACKING with ErrUnspec otherwise). TRUE when not in a protected VM. */
void VioGpuRdmaAddSharedRange(ULONGLONG base, ULONGLONG bytes);
BOOLEAN VioGpuRdmaPaIsHostVisible(ULONGLONG pa, ULONGLONG len);
/* Driver teardown (PASSIVE_LEVEL): give every region back to the pool; the next load starts clean. */
void VioGpuRdmaShutdown(void);

/* Registry log (HKLM\SYSTEM\CurrentControlSet\Services\VioGpu3D\DbgLog); see viogpu_rdma.cpp. */
extern "C" void VioGpuRegLog(const char *fmt, ...);
extern "C" void VioGpuRegLogFlush(void);
