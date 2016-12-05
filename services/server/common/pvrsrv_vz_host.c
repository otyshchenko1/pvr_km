/*************************************************************************/ /*!
@File           pvrsrv_virt_host.c
@Title          Core services virtualization functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Host virtualization APIs for core services functions
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

#include "ra.h"
#include "pmr.h"
#include "log2.h"
#include "lists.h"
#include "pvrsrv.h"
#include "dllist.h"
#include "pdump_km.h"
#include "allocmem.h"
#include "rgxdebug.h"
#include "pvr_debug.h"
#include "devicemem.h"
#include "syscommon.h"
#include "pvrversion.h"
#include "pvrsrv_vz.h"
#include "physmem_lma.h"
#include "dma_support.h"
#include "vz_support.h"
#include "pvrsrv_device.h"
#include "physmem_osmem.h"
#include "rgxheapconfig.h"
#include "connection_server.h"
#include "vz_physheap.h"

PVRSRV_ERROR IMG_CALLCONV PVRSRVVzRegisterFirmwarePhysHeap(PVRSRV_DEVICE_NODE *psDeviceNode,
															IMG_DEV_PHYADDR sDevPAddr,
															IMG_UINT64 ui64DevPSize,
															IMG_UINT32 uiOSID)
{
	RA_BASE_T uBase;
	RA_LENGTH_T uSize;
	IMG_UINT64 ui64Size;
	PHYS_HEAP *psPhysHeap;
	PVRSRV_ERROR eError;

	if (uiOSID == 0 || uiOSID >= RGXFW_NUM_OS)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* Verify guest size with host size  (support only same sized FW heaps) */
	psPhysHeap = psDeviceNode->apsPhysHeap[PVRSRV_DEVICE_PHYS_HEAP_FW_LOCAL];
	ui64Size = RGX_FIRMWARE_HEAP_SIZE;

	PVR_DPF((PVR_DBG_MESSAGE, "===== Registering OSID: %d fw physheap memory", uiOSID));

	if (ui64DevPSize != ui64Size)
	{
		PVR_DPF((PVR_DBG_WARNING,
				"OSID: %d fw physheap size 0x%llx differs from host fw phyheap size 0x%llx",
				uiOSID, 
				ui64DevPSize,
				ui64Size));

		PVR_DPF((PVR_DBG_WARNING,
				"Truncating OSID: %d requested fw physheap to: 0x%llx\n",
				uiOSID, 
				ui64Size));
	}

	PVR_DPF((PVR_DBG_MESSAGE, "Creating RA for fw 0x%016llx-0x%016llx [DEV/PA]",
			(IMG_UINT64) sDevPAddr.uiAddr, sDevPAddr.uiAddr + ui64Size - 1));

	/* Now we construct RA to manage FW heap */
	uBase = sDevPAddr.uiAddr;
	uSize = (RA_LENGTH_T) ui64Size;
	PVR_ASSERT(uSize == ui64Size);

	OSSNPrintf(psDeviceNode->szKernelFwRAName[uiOSID],
			   sizeof(psDeviceNode->szKernelFwRAName[uiOSID]),
			   "[OSID: %d]: fw mem", uiOSID);

	psDeviceNode->psKernelFwMemArena[uiOSID] =
		RA_Create(psDeviceNode->szKernelFwRAName[uiOSID],
					OSGetPageShift(),	/* Use host page size, keeps things simple */
					RA_LOCKCLASS_0,		/* This arena doesn't use any other arenas */
					NULL,				/* No Import */
					NULL,				/* No free import */
					NULL,				/* No import handle */
					IMG_FALSE);
	if (psDeviceNode->psKernelFwMemArena[uiOSID] == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e0;
	}

	if (!RA_Add(psDeviceNode->psKernelFwMemArena[uiOSID], uBase, uSize, 0 , NULL))
	{
		RA_Delete(psDeviceNode->psKernelFwMemArena[uiOSID]);
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e0;
	}

	psDeviceNode->ui64RABase[uiOSID] = uBase;
	return PVRSRV_OK;
e0:
	return eError;
}

PVRSRV_ERROR IMG_CALLCONV PVRSRVVzUnregisterFirmwarePhysHeap(PVRSRV_DEVICE_NODE *psDeviceNode,
																IMG_UINT32 uiOSID)
{
	if (uiOSID == 0 || uiOSID >= RGXFW_NUM_OS)
	{
		PVR_ASSERT(0);
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	PVR_DPF((PVR_DBG_MESSAGE, "===== Deregistering OSID: %d fw physheap memory", uiOSID));

	if (psDeviceNode->psKernelFwMemArena[uiOSID])
	{
		RA_Free(psDeviceNode->psKernelFwMemArena[uiOSID], psDeviceNode->ui64RABase[uiOSID]);
		RA_Delete(psDeviceNode->psKernelFwMemArena[uiOSID]);
		psDeviceNode->psKernelFwMemArena[uiOSID] = NULL;
	}

	return PVRSRV_OK;
}

/*****************************************************************************
 End of file (pvrsrv_virt_host.c)
*****************************************************************************/
