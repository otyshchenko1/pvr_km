/*************************************************************************/ /*!
@File           vz_generic_hostc.c
@Title          System virtualization host configuration setup.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    System virtualization host configuration support APIs.
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
#include "pvrsrv_vz.h"
#include "pvrsrv_device.h"

#include "dma_support.h"
#include "vz_support.h"
#include "vz_vmm_pvz.h"
#include "vz_physheap.h"
#include "vmm_pvz_server.h"

PVRSRV_ERROR SysVzDevInit(PVRSRV_DEVICE_CONFIG *psDevConfig)
{
	PVRSRV_ERROR eError;
	PVRSRV_VIRTZ_DATA *psPVRSRVVzData;
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();

	/* Initialise para-virtz */
	eError =  SysVzPvzConnectionInit();
	PVR_ASSERT(eError == PVRSRV_OK);

	psPVRSRVVzData = psPVRSRVData->hVzData;
	psPVRSRVVzData->abVmOnline[0] = IMG_TRUE;

	/* Perform general device physheap initialisation */
	eError =  SysVzInitDevPhysHeaps(psDevConfig);
	PVR_ASSERT(eError == PVRSRV_OK);

	return eError;
}

void SysVzDevDeInit(PVRSRV_DEVICE_CONFIG *psDevConfig)
{
	SysVzDeInitDevPhysHeaps(psDevConfig);
	SysVzPvzConnectionDeInit();
}

PVRSRV_ERROR
SysVzCreateDevConfig(IMG_UINT32 ui32OSID,
					 IMG_UINT32 ui32DevID,
					 IMG_UINT32 *pui32IRQ,
					 IMG_UINT32 *pui32RegsSize,
					 IMG_UINT64 *pui64RegsCpuPBase)
{
	PVRSRV_DEVICE_NODE *psDevNode;
	PVRSRV_DEVICE_CONFIG *psDevConfig;
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();

	PVR_ASSERT(ui32DevID == 0);
	PVR_ASSERT(psPVRSRVData != NULL);
	PVR_ASSERT(ui32OSID > 0 && ui32OSID < RGXFW_NUM_OS);

	/* For now, limit support to single device setups */
	psDevNode = psPVRSRVData->psDeviceNodeList;
	psDevConfig = psDevNode->psDevConfig;

	/* Copy across guest VM device config information, here
	   we assume this is the same across VMs and host */
	*pui64RegsCpuPBase = psDevConfig->sRegsCpuPBase.uiAddr;
	*pui32RegsSize = psDevConfig->ui32RegsSize;
	*pui32IRQ = psDevConfig->ui32IRQ;

	return PVRSRV_OK;
}

PVRSRV_ERROR
SysVzDestroyDevConfig(IMG_UINT32 ui32OSID, IMG_UINT32 ui32DevID)
{
	PVR_UNREFERENCED_PARAMETER(ui32OSID);
	PVR_UNREFERENCED_PARAMETER(ui32DevID);
	return PVRSRV_OK;
}

/******************************************************************************
 End of file (vz_generic_host.c)
******************************************************************************/
