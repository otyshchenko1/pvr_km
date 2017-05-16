 /*************************************************************************/ /*!
@File
@Title          Rogue firmware virtualization utility routines
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Rogue firmware virtualization utility routines
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#include <stddef.h>

#include "pdump_km.h"
#include "devicemem.h"
#include "pvr_debug.h"
#include "rgxfwutils.h"
#include "physheap.h"
#include "pvrsrv.h"
#include "pvrsrv_vz.h"
#include "physmem_lma.h"
#include "physmem_osmem.h"
#include "rgxfwutils_vz.h"
#include "rgxheapconfig.h"
#include "vz_support.h"
#include "vz_physheap.h"

static PVRSRV_ERROR 
RGXVzDevMemAllocateGuestFwHeap(PVRSRV_DEVICE_NODE *psDeviceNode, IMG_UINT32 ui32OSID)
{
	IMG_CHAR szHeapName[32];
	IMG_DEV_VIRTADDR sTmpDevVAddr;
	IMG_UINT32 uiIdx = ui32OSID - 1;
	PVRSRV_ERROR eError = IMG_FALSE;
	IMG_BOOL bFwLocalIsUMA = IMG_FALSE;
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	PVRSRV_DEVICE_PHYS_HEAP ePhysHeap = PVRSRV_DEVICE_PHYS_HEAP_FW_LOCAL;
	FN_CREATERAMBACKEDPMR *ppfnCreateRamBackedPMR =
							&psDeviceNode->pfnCreateRamBackedPMR[ePhysHeap];

	PDUMPCOMMENT("Mapping firmware physheap for OSID: [%d]", ui32OSID);
	OSSNPrintf(szHeapName, sizeof(szHeapName), "GuestFirmware%d", ui32OSID);

	if (*ppfnCreateRamBackedPMR != PhysmemNewLocalRamBackedPMR)
	{
		/* This needs a more generic framework, allocating from
		   guest physheap in host driver, but now we override
		   momentarily for the duration of the guest physheap 
		   allocation as we need to allocate these using the 
		   guest Fw/RAs; this happens when host driver uses
		   firmware UMA physheaps */
		*ppfnCreateRamBackedPMR = PhysmemNewLocalRamBackedPMR;
		bFwLocalIsUMA = IMG_TRUE;
	}

	/* Target OSID physheap for allocation */
	psDeviceNode->uiKernelFwRAIdx = ui32OSID;

	/* This allocates all available memory in the guest physheap */
	eError = DevmemAllocate(psDevInfo->psGuestFirmwareHeap[uiIdx],
							RGX_FIRMWARE_HEAP_SIZE,
							ROGUE_CACHE_LINE_SIZE,
							PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
							PVRSRV_MEMALLOCFLAG_GPU_READABLE |
							PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_CPU_READABLE |
							PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
							PVRSRV_MEMALLOCFLAG_UNCACHED |
							PVRSRV_MEMALLOCFLAG_FW_LOCAL,
							szHeapName,
							&psDevInfo->psGuestFirmwareMemDesc[uiIdx]);
	if (bFwLocalIsUMA)
	{
		/* If we have overridden this then set it back */
		*ppfnCreateRamBackedPMR = PhysmemNewOSRamBackedPMR;
	}

	if (eError == PVRSRV_OK)
	{
		/* If allocation is successful, permanently map this into device */
		eError = DevmemMapToDevice(psDevInfo->psGuestFirmwareMemDesc[uiIdx],
								   psDevInfo->psGuestFirmwareHeap[uiIdx],
								   &sTmpDevVAddr);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"%s failed (%u)", __FUNCTION__, eError));
		}
	}

	return eError;
}

static void
RGXVzDevMemFreeGuestFwHeap(PVRSRV_DEVICE_NODE *psDeviceNode, IMG_UINT32 ui32OSID)
{
	IMG_UINT32 uiIdx = ui32OSID - 1;
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;

	PDUMPCOMMENT("Unmapping firmware physheap for OSID: [%d]", ui32OSID);

	if (psDevInfo->psGuestFirmwareMemDesc[uiIdx])
	{
		DevmemReleaseDevVirtAddr(psDevInfo->psGuestFirmwareMemDesc[uiIdx]);
		DevmemFree(psDevInfo->psGuestFirmwareMemDesc[uiIdx]);
		psDevInfo->psGuestFirmwareMemDesc[uiIdx] = NULL;
	}
}

PVRSRV_ERROR RGXVzSetupFirmware(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	IMG_UINT32 ui32OSID;
	PVRSRV_ERROR eError;
	PVRSRV_DEVICE_PHYS_HEAP_ORIGIN eHeapOrigin;
	PVRSRV_DEVICE_PHYS_HEAP eHeapType = PVRSRV_DEVICE_PHYS_HEAP_FW_LOCAL;

	eError = SysVzGetPhysHeapOrigin(psDeviceNode->psDevConfig,
									   eHeapType,
									   &eHeapOrigin);
	if (eError != PVRSRV_OK)
	{
		PVR_ASSERT(0);
		return eError;
	}

	if (eHeapOrigin == PVRSRV_DEVICE_PHYS_HEAP_ORIGIN_HOST)
	{
		for (ui32OSID=1; ui32OSID < RGXFW_NUM_OS; ui32OSID++)
		{
			eError = RGXVzDevMemAllocateGuestFwHeap(psDeviceNode, ui32OSID);
			PVR_ASSERT(eError == PVRSRV_OK);
		}
	}

	return eError;
}

PVRSRV_ERROR RGXVzRegisterFirmwarePhysHeap(PVRSRV_DEVICE_NODE *psDeviceNode,
										   IMG_UINT32 ui32OSID,
										   IMG_DEV_PHYADDR sDevPAddr,
										   IMG_UINT64 ui64DevPSize)
{
	PVRSRV_ERROR eError;

	PVR_ASSERT(ui32OSID && sDevPAddr.uiAddr);

	/* Registration creates internal RA to maintain the heap */
	eError = PVRSRVVzRegisterFirmwarePhysHeap (psDeviceNode,
												  sDevPAddr,
												  ui64DevPSize,
												  ui32OSID);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "Registering guest %d fw physheap failed\n", ui32OSID));
		return eError;
	}

	/* Map guest DMA fw physheap into the fw kernel memory context */
	eError = RGXVzDevMemAllocateGuestFwHeap(psDeviceNode, ui32OSID);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "Mapping guest %d fw physheap failed\n", ui32OSID));
		return eError;
	}

	return eError;
}

PVRSRV_ERROR RGXVzUnregisterFirmwarePhysHeap(PVRSRV_DEVICE_NODE *psDeviceNode,
											 IMG_UINT32 ui32OSID)
{
	PVRSRV_ERROR eError;

	PVR_ASSERT(ui32OSID);

	/* Free guest fw physheap from fw kernel memory context */
	RGXVzDevMemFreeGuestFwHeap(psDeviceNode, ui32OSID);

	/* Unregistration deletes state required to maintain heap */
	eError = PVRSRVVzUnregisterFirmwarePhysHeap (psDeviceNode, ui32OSID);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "Registering guest %d fw physheap failed\n", ui32OSID));
		return eError;
	}

	return eError;
}

PVRSRV_ERROR RGXVzCreateFWKernelMemoryContext(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_CHAR szHeapName[32];
	IMG_UINT32 uiIdx;

	/* Initialise each guest OSID allocation heaps */
	for (uiIdx = 1; uiIdx < RGXFW_NUM_OS; uiIdx++)
	{
		OSSNPrintf(szHeapName, sizeof(szHeapName), "GuestFirmware%d", uiIdx);

		eError = DevmemFindHeapByName(psDevInfo->psKernelDevmemCtx, szHeapName,
									  &psDevInfo->psGuestFirmwareHeap[uiIdx-1]);
		PVR_ASSERT (eError == PVRSRV_OK);
	}

	return eError;
}

PVRSRV_ERROR RGXVzDestroyFWKernelMemoryContext(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	return PVRSRV_OK; 
}

/******************************************************************************
End of file (rgxfwutils_virt.c)
******************************************************************************/
