/*************************************************************************/ /*!
@File           vz_physheap_host.c
@Title          System virtualization host physheap configuration
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    System virtualization host physical heap configuration
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
#include "allocmem.h"
#include "physheap.h"
#include "rgxdevice.h"
#include "pvrsrv_device.h"
#include "rgxfwutils_vz.h"

#include "dma_support.h"
#include "vz_support.h"
#include "vz_vmm_pvz.h"
#include "vz_physheap.h"
#include "vmm_impl_server.h"
#include "vmm_pvz_server.h"

PVRSRV_ERROR
SysVzCreateDevPhysHeaps(IMG_UINT32 ui32OSID,
						IMG_UINT32 ui32DevID,
						IMG_UINT32 *pePhysHeapType,
						IMG_UINT64 *pui64FwPhysHeapSize,
						IMG_UINT64 *pui64FwPhysHeapAddr,
						IMG_UINT64 *pui64GpuPhysHeapSize,
						IMG_UINT64 *pui64GpuPhysHeapAddr)
{
	IMG_UINT64 uiHeapSize;
	IMG_DEV_PHYADDR sCardBase;
	IMG_CPU_PHYADDR sStartAddr;
	PHYS_HEAP_CONFIG *psPhysHeap;
	PVRSRV_DEVICE_NODE *psDeviceNode;
	PVRSRV_DEVICE_CONFIG *psDevConfig;
	PVRSRV_DEVICE_PHYS_HEAP ePhysHeap;
	PVRSRV_DEVICE_PHYS_HEAP_ORIGIN eHeapOrigin;
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();

	PVR_ASSERT(ui32DevID == 0);
	PVR_ASSERT(psPVRSRVData != NULL);
	PVR_ASSERT(ui32OSID > 0 && ui32OSID < RGXFW_NUM_OS);

	/* For now, limit support to single device setups */
	psDeviceNode = psPVRSRVData->psDeviceNodeList;
	psDevConfig = psDeviceNode->psDevConfig;

	/* Default is a kernel managed UMA 
	   physheap memory configuration */
	*pui64FwPhysHeapSize = (IMG_UINT64)0;
	*pui64FwPhysHeapAddr = (IMG_UINT64)0;
	*pui64GpuPhysHeapSize = (IMG_UINT64)0;
	*pui64GpuPhysHeapAddr = (IMG_UINT64)0;

	*pePhysHeapType = (IMG_UINT32) SysVzGetMemoryConfigPhysHeapType();
	for (ePhysHeap = 0; ePhysHeap < PVRSRV_DEVICE_PHYS_HEAP_LAST; ePhysHeap++)
	{
		switch (ePhysHeap)
		{
			/* Only interested in these physheaps */
			case PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL:
			case PVRSRV_DEVICE_PHYS_HEAP_FW_LOCAL:
				{
					PVRSRV_ERROR eError;

					eError = SysVzGetPhysHeapOrigin(psDevConfig,
													ePhysHeap,
													&eHeapOrigin);
					PVR_ASSERT(eError == PVRSRV_OK);

					if (eHeapOrigin == PVRSRV_DEVICE_PHYS_HEAP_ORIGIN_GUEST)
					{
						continue;
					}
				}
				break;

			default:
				continue;
		}

		/* Determine what type of physheap backs this phyconfig */
		psPhysHeap = SysVzGetPhysHeapConfig(psDevConfig, ePhysHeap);
		if (psPhysHeap && psPhysHeap->pasRegions)
		{
			/* Services managed physheap (LMA/UMA carve-out) */
			sStartAddr = psPhysHeap->pasRegions[0].sStartAddr;
			sCardBase = psPhysHeap->pasRegions[0].sCardBase;
			uiHeapSize = psPhysHeap->pasRegions[0].uiSize;

			/* Rebase this guest OSID physical heap */
			sStartAddr.uiAddr += ui32OSID * uiHeapSize;
			sCardBase.uiAddr += ui32OSID * uiHeapSize;

			switch (ePhysHeap)
			{
				case PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL:
					*pui64GpuPhysHeapSize = uiHeapSize;
					*pui64GpuPhysHeapAddr = sStartAddr.uiAddr;
					break;

				case PVRSRV_DEVICE_PHYS_HEAP_FW_LOCAL:
					*pui64FwPhysHeapSize = uiHeapSize;
					*pui64FwPhysHeapAddr = sStartAddr.uiAddr;
					break;

				default:
					PVR_ASSERT(0);
					break;
			}
		}
		else
		{
#if defined(DEBUG)
			PVRSRV_ERROR eError;

			eError = SysVzGetPhysHeapOrigin(psDevConfig,
											ePhysHeap,
											&eHeapOrigin);
			PVR_ASSERT(eError == PVRSRV_OK);

			if (eHeapOrigin == PVRSRV_DEVICE_PHYS_HEAP_ORIGIN_HOST)
			{
				PVR_ASSERT(ePhysHeap != PVRSRV_DEVICE_PHYS_HEAP_FW_LOCAL);
			}
#endif
		}
	}

	return PVRSRV_OK;
}

PVRSRV_ERROR
SysVzDestroyDevPhysHeaps(IMG_UINT32 ui32OSID, IMG_UINT32 ui32DevID)
{
	PVR_UNREFERENCED_PARAMETER(ui32OSID);
	PVR_UNREFERENCED_PARAMETER(ui32DevID);
	return PVRSRV_OK;
}

PVRSRV_ERROR
SysVzRegisterFwPhysHeap(IMG_UINT32 ui32OSID,
						IMG_UINT32 ui32DevID,
						IMG_UINT64 ui64Size,
						IMG_UINT64 ui64PAddr)
{
	PVRSRV_ERROR eError;
	PVRSRV_DEVICE_NODE* psDeviceNode;
	PVRSRV_DEVICE_CONFIG *psDevConfig;
	PVRSRV_DEVICE_PHYS_HEAP_ORIGIN eHeapOrigin;
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_DEVICE_PHYS_HEAP eHeapType = PVRSRV_DEVICE_PHYS_HEAP_FW_LOCAL;

	PVR_ASSERT(psPVRSRVData != NULL);
	PVR_ASSERT(ui32DevID == 0);

	psDeviceNode = psPVRSRVData->psDeviceNodeList;
	psDevConfig = psDeviceNode->psDevConfig;

	eError = SysVzGetPhysHeapOrigin(psDevConfig,
									eHeapType,
									&eHeapOrigin);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	if (eHeapOrigin == PVRSRV_DEVICE_PHYS_HEAP_ORIGIN_HOST)
	{
		eError = PVRSRV_OK;
	}
	else
	{
		IMG_DEV_PHYADDR sDevPAddr = {ui64PAddr};

		eError = RGXVzRegisterFirmwarePhysHeap(psDeviceNode,
											   ui32OSID,
											   sDevPAddr,
											   ui64Size);
		PVR_ASSERT(eError == PVRSRV_OK);
	}

	return eError;
}

PVRSRV_ERROR
SysVzUnregisterFwPhysHeap(IMG_UINT32 ui32OSID, IMG_UINT32 ui32DevID)
{
	PVRSRV_ERROR eError;
	PVRSRV_DEVICE_NODE *psDeviceNode;
	PVRSRV_DEVICE_CONFIG *psDevConfig;
	PVRSRV_DEVICE_PHYS_HEAP_ORIGIN eHeapOrigin;
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_DEVICE_PHYS_HEAP eHeap = PVRSRV_DEVICE_PHYS_HEAP_FW_LOCAL;

	PVR_ASSERT(psPVRSRVData != NULL);
	PVR_ASSERT(ui32DevID == 0);

	psDeviceNode = psPVRSRVData->psDeviceNodeList;
	psDevConfig = psDeviceNode->psDevConfig;

	eError = SysVzGetPhysHeapOrigin(psDevConfig,
									eHeap,
									&eHeapOrigin);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	if (eHeapOrigin == PVRSRV_DEVICE_PHYS_HEAP_ORIGIN_HOST)
	{
		eError = PVRSRV_OK;
	}
	else
	{
		psDeviceNode = psPVRSRVData->psDeviceNodeList;
		eError = RGXVzUnregisterFirmwarePhysHeap(psDeviceNode, ui32OSID);
		PVR_ASSERT(eError == PVRSRV_OK);
	}

	return eError;
}

PVRSRV_ERROR SysVzGetPhysHeapAddrSize(PVRSRV_DEVICE_CONFIG *psDevConfig,
									  PVRSRV_DEVICE_PHYS_HEAP ePhysHeap,
									  PHYS_HEAP_TYPE eHeapType,
									  IMG_DEV_PHYADDR *psAddr,
									  IMG_UINT64 *pui64Size)
{
	IMG_UINT64 uiAddr;
	PVRSRV_ERROR eError;
	VMM_PVZ_CONNECTION *psVmmPvz;

	PVR_UNREFERENCED_PARAMETER(eHeapType);

	psVmmPvz = SysVzPvzConnectionAcquire();
	PVR_ASSERT(psVmmPvz);

	PVR_ASSERT(psVmmPvz->sConfigFuncTab.pfnGetDevPhysHeapAddrSize);

	eError = psVmmPvz->sConfigFuncTab.pfnGetDevPhysHeapAddrSize(psDevConfig,
																ePhysHeap,
																pui64Size,
																&uiAddr);
	if (eError != PVRSRV_OK)
	{
		if (eError == PVRSRV_ERROR_NOT_IMPLEMENTED)
		{
			PVR_ASSERT(0);
		}

		goto e0;
	}

	psAddr->uiAddr = uiAddr;
e0:
	SysVzPvzConnectionRelease(psVmmPvz);
	return eError;
}

PVRSRV_ERROR SysVzGetPhysHeapOrigin(PVRSRV_DEVICE_CONFIG *psDevConfig,
									PVRSRV_DEVICE_PHYS_HEAP eHeap,
									PVRSRV_DEVICE_PHYS_HEAP_ORIGIN *peOrigin)
{
	PVRSRV_ERROR eError;
	VMM_PVZ_CONNECTION *psVmmPvz;

	psVmmPvz = SysVzPvzConnectionAcquire();
	PVR_ASSERT(psVmmPvz);

	PVR_ASSERT(psVmmPvz->sConfigFuncTab.pfnGetDevPhysHeapOrigin);

	eError = psVmmPvz->sConfigFuncTab.pfnGetDevPhysHeapOrigin(psDevConfig,
															  eHeap,
															  peOrigin);
	if (eError != PVRSRV_OK)
	{
		if (eError == PVRSRV_ERROR_NOT_IMPLEMENTED)
		{
			PVR_ASSERT(0);
		}

		goto e0;
	}

e0:
	SysVzPvzConnectionRelease(psVmmPvz);
	return eError;
}

/******************************************************************************
 End of file (vz_physheap_host.c)
******************************************************************************/
