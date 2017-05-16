/*************************************************************************/ /*!
@File
@Title          Server bridge for rgxinit
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for rgxinit
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
#include <asm/uaccess.h>

#include "img_defs.h"

#include "rgxinit.h"
#include "pmr.h"


#include "common_rgxinit_bridge.h"

#include "allocmem.h"
#include "pvr_debug.h"
#include "connection_server.h"
#include "pvr_bridge.h"
#include "rgx_bridge.h"
#include "srvcore.h"
#include "handle.h"

#include <linux/slab.h>



static PVRSRV_ERROR ReleaseFWCodePMR(void *pvData)
{
	PVR_UNREFERENCED_PARAMETER(pvData);

	return PVRSRV_OK;
}
static PVRSRV_ERROR ReleaseFWDataPMR(void *pvData)
{
	PVR_UNREFERENCED_PARAMETER(pvData);

	return PVRSRV_OK;
}
static PVRSRV_ERROR ReleaseFWCorememPMR(void *pvData)
{
	PVR_UNREFERENCED_PARAMETER(pvData);

	return PVRSRV_OK;
}
static PVRSRV_ERROR ReleaseHWPerfPMR(void *pvData)
{
	PVR_UNREFERENCED_PARAMETER(pvData);

	return PVRSRV_OK;
}
static PVRSRV_ERROR ReleaseHWPerfPMR2(void *pvData)
{
	PVR_UNREFERENCED_PARAMETER(pvData);

	return PVRSRV_OK;
}


/* ***************************************************************************
 * Server-side bridge entry points
 */
 
static IMG_INT
PVRSRVBridgeRGXInitAllocFWImgMem(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXINITALLOCFWIMGMEM *psRGXInitAllocFWImgMemIN,
					  PVRSRV_BRIDGE_OUT_RGXINITALLOCFWIMGMEM *psRGXInitAllocFWImgMemOUT,
					 CONNECTION_DATA *psConnection)
{
	PMR * psFWCodePMRInt = NULL;
	PMR * psFWDataPMRInt = NULL;
	PMR * psFWCorememPMRInt = NULL;






	psRGXInitAllocFWImgMemOUT->eError =
		PVRSRVRGXInitAllocFWImgMemKM(psConnection, OSGetDevData(psConnection),
					psRGXInitAllocFWImgMemIN->uiFWCodeLen,
					psRGXInitAllocFWImgMemIN->uiFWDataLen,
					psRGXInitAllocFWImgMemIN->uiFWCoremem,
					&psFWCodePMRInt,
					&psRGXInitAllocFWImgMemOUT->sFWCodeDevVAddrBase,
					&psFWDataPMRInt,
					&psRGXInitAllocFWImgMemOUT->sFWDataDevVAddrBase,
					&psFWCorememPMRInt,
					&psRGXInitAllocFWImgMemOUT->sFWCorememDevVAddrBase,
					&psRGXInitAllocFWImgMemOUT->sFWCorememMetaVAddrBase);
	/* Exit early if bridged call fails */
	if(psRGXInitAllocFWImgMemOUT->eError != PVRSRV_OK)
	{
		goto RGXInitAllocFWImgMem_exit;
	}






	psRGXInitAllocFWImgMemOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,

							&psRGXInitAllocFWImgMemOUT->hFWCodePMR,
							(void *) psFWCodePMRInt,
							PVRSRV_HANDLE_TYPE_PMR_LOCAL_EXPORT_HANDLE,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							,(PFN_HANDLE_RELEASE)&ReleaseFWCodePMR);
	if (psRGXInitAllocFWImgMemOUT->eError != PVRSRV_OK)
	{
		goto RGXInitAllocFWImgMem_exit;
	}






	psRGXInitAllocFWImgMemOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,

							&psRGXInitAllocFWImgMemOUT->hFWDataPMR,
							(void *) psFWDataPMRInt,
							PVRSRV_HANDLE_TYPE_PMR_LOCAL_EXPORT_HANDLE,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							,(PFN_HANDLE_RELEASE)&ReleaseFWDataPMR);
	if (psRGXInitAllocFWImgMemOUT->eError != PVRSRV_OK)
	{
		goto RGXInitAllocFWImgMem_exit;
	}






	psRGXInitAllocFWImgMemOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,

							&psRGXInitAllocFWImgMemOUT->hFWCorememPMR,
							(void *) psFWCorememPMRInt,
							PVRSRV_HANDLE_TYPE_PMR_LOCAL_EXPORT_HANDLE,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							,(PFN_HANDLE_RELEASE)&ReleaseFWCorememPMR);
	if (psRGXInitAllocFWImgMemOUT->eError != PVRSRV_OK)
	{
		goto RGXInitAllocFWImgMem_exit;
	}




RGXInitAllocFWImgMem_exit:


	if (psRGXInitAllocFWImgMemOUT->eError != PVRSRV_OK)
	{
	}


	return 0;
}


static IMG_INT
PVRSRVBridgeRGXInitFirmware(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXINITFIRMWARE *psRGXInitFirmwareIN,
					  PVRSRV_BRIDGE_OUT_RGXINITFIRMWARE *psRGXInitFirmwareOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_UINT32 *ui32RGXFWAlignChecksInt = NULL;
	PMR * psHWPerfPMRInt = NULL;




	if (psRGXInitFirmwareIN->ui32RGXFWAlignChecksArrLength != 0)
	{
		ui32RGXFWAlignChecksInt = OSAllocZMemNoStats(psRGXInitFirmwareIN->ui32RGXFWAlignChecksArrLength * sizeof(IMG_UINT32));
		if (!ui32RGXFWAlignChecksInt)
		{
			psRGXInitFirmwareOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXInitFirmware_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (void*) psRGXInitFirmwareIN->pui32RGXFWAlignChecks, psRGXInitFirmwareIN->ui32RGXFWAlignChecksArrLength * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32RGXFWAlignChecksInt, psRGXInitFirmwareIN->pui32RGXFWAlignChecks,
				psRGXInitFirmwareIN->ui32RGXFWAlignChecksArrLength * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXInitFirmwareOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXInitFirmware_exit;
			}


	psRGXInitFirmwareOUT->eError =
		PVRSRVRGXInitFirmwareKM(psConnection, OSGetDevData(psConnection),
					&psRGXInitFirmwareOUT->spsRGXFwInit,
					psRGXInitFirmwareIN->bEnableSignatureChecks,
					psRGXInitFirmwareIN->ui32SignatureChecksBufSize,
					psRGXInitFirmwareIN->ui32HWPerfFWBufSizeKB,
					psRGXInitFirmwareIN->ui64HWPerfFilter,
					psRGXInitFirmwareIN->ui32RGXFWAlignChecksArrLength,
					ui32RGXFWAlignChecksInt,
					psRGXInitFirmwareIN->ui32ConfigFlags,
					psRGXInitFirmwareIN->ui32LogType,
					psRGXInitFirmwareIN->ui32FilterFlags,
					psRGXInitFirmwareIN->ui32JonesDisableMask,
					psRGXInitFirmwareIN->ui32ui32HWRDebugDumpLimit,
					&psRGXInitFirmwareIN->sClientBVNC,
					psRGXInitFirmwareIN->ui32HWPerfCountersDataSize,
					&psHWPerfPMRInt,
					psRGXInitFirmwareIN->eRGXRDPowerIslandConf,
					psRGXInitFirmwareIN->eFirmwarePerf);
	/* Exit early if bridged call fails */
	if(psRGXInitFirmwareOUT->eError != PVRSRV_OK)
	{
		goto RGXInitFirmware_exit;
	}






	psRGXInitFirmwareOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,

							&psRGXInitFirmwareOUT->hHWPerfPMR,
							(void *) psHWPerfPMRInt,
							PVRSRV_HANDLE_TYPE_PMR_LOCAL_EXPORT_HANDLE,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							,(PFN_HANDLE_RELEASE)&ReleaseHWPerfPMR);
	if (psRGXInitFirmwareOUT->eError != PVRSRV_OK)
	{
		goto RGXInitFirmware_exit;
	}




RGXInitFirmware_exit:


	if (psRGXInitFirmwareOUT->eError != PVRSRV_OK)
	{
	}

	if (ui32RGXFWAlignChecksInt)
		OSFreeMemNoStats(ui32RGXFWAlignChecksInt);

	return 0;
}


static IMG_INT
PVRSRVBridgeRGXInitFinaliseFWImage(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXINITFINALISEFWIMAGE *psRGXInitFinaliseFWImageIN,
					  PVRSRV_BRIDGE_OUT_RGXINITFINALISEFWIMAGE *psRGXInitFinaliseFWImageOUT,
					 CONNECTION_DATA *psConnection)
{

	PVR_UNREFERENCED_PARAMETER(psRGXInitFinaliseFWImageIN);





	psRGXInitFinaliseFWImageOUT->eError =
		PVRSRVRGXInitFinaliseFWImageKM(psConnection, OSGetDevData(psConnection)
					);







	return 0;
}


static IMG_INT
PVRSRVBridgeRGXInitDevPart2(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXINITDEVPART2 *psRGXInitDevPart2IN,
					  PVRSRV_BRIDGE_OUT_RGXINITDEVPART2 *psRGXInitDevPart2OUT,
					 CONNECTION_DATA *psConnection)
{
	RGX_INIT_COMMAND *psDbgScriptInt = NULL;
	PMR * psFWCodePMRInt = NULL;
	PMR * psFWDataPMRInt = NULL;
	PMR * psFWCorememPMRInt = NULL;
	PMR * psHWPerfPMRInt = NULL;




	
	{
		psDbgScriptInt = OSAllocZMemNoStats(RGX_MAX_DEBUG_COMMANDS * sizeof(RGX_INIT_COMMAND));
		if (!psDbgScriptInt)
		{
			psRGXInitDevPart2OUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXInitDevPart2_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (void*) psRGXInitDevPart2IN->psDbgScript, RGX_MAX_DEBUG_COMMANDS * sizeof(RGX_INIT_COMMAND))
				|| (OSCopyFromUser(NULL, psDbgScriptInt, psRGXInitDevPart2IN->psDbgScript,
				RGX_MAX_DEBUG_COMMANDS * sizeof(RGX_INIT_COMMAND)) != PVRSRV_OK) )
			{
				psRGXInitDevPart2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXInitDevPart2_exit;
			}


	psRGXInitDevPart2OUT->eError =
		PVRSRVRGXInitDevPart2KM(psConnection, OSGetDevData(psConnection),
					psDbgScriptInt,
					psRGXInitDevPart2IN->ui32DeviceFlags,
					psRGXInitDevPart2IN->ui32HWPerfHostBufSize,
					psRGXInitDevPart2IN->ui32HWPerfHostFilter,
					psRGXInitDevPart2IN->ui32RGXActivePMConf,
					psFWCodePMRInt,
					psFWDataPMRInt,
					psFWCorememPMRInt,
					psHWPerfPMRInt);






	psRGXInitDevPart2OUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXInitDevPart2IN->hFWCodePMR,
					PVRSRV_HANDLE_TYPE_PMR_LOCAL_EXPORT_HANDLE);
	if ((psRGXInitDevPart2OUT->eError != PVRSRV_OK) && (psRGXInitDevPart2OUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_ASSERT(0);
		goto RGXInitDevPart2_exit;
	}






	psRGXInitDevPart2OUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXInitDevPart2IN->hFWDataPMR,
					PVRSRV_HANDLE_TYPE_PMR_LOCAL_EXPORT_HANDLE);
	if ((psRGXInitDevPart2OUT->eError != PVRSRV_OK) && (psRGXInitDevPart2OUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_ASSERT(0);
		goto RGXInitDevPart2_exit;
	}






	psRGXInitDevPart2OUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXInitDevPart2IN->hFWCorememPMR,
					PVRSRV_HANDLE_TYPE_PMR_LOCAL_EXPORT_HANDLE);
	if ((psRGXInitDevPart2OUT->eError != PVRSRV_OK) && (psRGXInitDevPart2OUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_ASSERT(0);
		goto RGXInitDevPart2_exit;
	}






	psRGXInitDevPart2OUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXInitDevPart2IN->hHWPerfPMR,
					PVRSRV_HANDLE_TYPE_PMR_LOCAL_EXPORT_HANDLE);
	if ((psRGXInitDevPart2OUT->eError != PVRSRV_OK) && (psRGXInitDevPart2OUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_ASSERT(0);
		goto RGXInitDevPart2_exit;
	}




RGXInitDevPart2_exit:


	if (psDbgScriptInt)
		OSFreeMemNoStats(psDbgScriptInt);

	return 0;
}


static IMG_INT
PVRSRVBridgeGPUVIRTPopulateLMASubArenas(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_GPUVIRTPOPULATELMASUBARENAS *psGPUVIRTPopulateLMASubArenasIN,
					  PVRSRV_BRIDGE_OUT_GPUVIRTPOPULATELMASUBARENAS *psGPUVIRTPopulateLMASubArenasOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_UINT32 *ui32ElementsInt = NULL;




	if (psGPUVIRTPopulateLMASubArenasIN->ui32NumElements != 0)
	{
		ui32ElementsInt = OSAllocZMemNoStats(psGPUVIRTPopulateLMASubArenasIN->ui32NumElements * sizeof(IMG_UINT32));
		if (!ui32ElementsInt)
		{
			psGPUVIRTPopulateLMASubArenasOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto GPUVIRTPopulateLMASubArenas_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (void*) psGPUVIRTPopulateLMASubArenasIN->pui32Elements, psGPUVIRTPopulateLMASubArenasIN->ui32NumElements * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ElementsInt, psGPUVIRTPopulateLMASubArenasIN->pui32Elements,
				psGPUVIRTPopulateLMASubArenasIN->ui32NumElements * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psGPUVIRTPopulateLMASubArenasOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto GPUVIRTPopulateLMASubArenas_exit;
			}


	psGPUVIRTPopulateLMASubArenasOUT->eError =
		PVRSRVGPUVIRTPopulateLMASubArenasKM(psConnection, OSGetDevData(psConnection),
					psGPUVIRTPopulateLMASubArenasIN->ui32NumElements,
					ui32ElementsInt,
					psGPUVIRTPopulateLMASubArenasIN->bEnableTrustedDeviceAceConfig);




GPUVIRTPopulateLMASubArenas_exit:


	if (ui32ElementsInt)
		OSFreeMemNoStats(ui32ElementsInt);

	return 0;
}


static IMG_INT
PVRSRVBridgeRGXInitGuest(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXINITGUEST *psRGXInitGuestIN,
					  PVRSRV_BRIDGE_OUT_RGXINITGUEST *psRGXInitGuestOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_UINT32 *ui32RGXFWAlignChecksInt = NULL;




	if (psRGXInitGuestIN->ui32RGXFWAlignChecksArrLength != 0)
	{
		ui32RGXFWAlignChecksInt = OSAllocZMemNoStats(psRGXInitGuestIN->ui32RGXFWAlignChecksArrLength * sizeof(IMG_UINT32));
		if (!ui32RGXFWAlignChecksInt)
		{
			psRGXInitGuestOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXInitGuest_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (void*) psRGXInitGuestIN->pui32RGXFWAlignChecks, psRGXInitGuestIN->ui32RGXFWAlignChecksArrLength * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32RGXFWAlignChecksInt, psRGXInitGuestIN->pui32RGXFWAlignChecks,
				psRGXInitGuestIN->ui32RGXFWAlignChecksArrLength * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXInitGuestOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXInitGuest_exit;
			}


	psRGXInitGuestOUT->eError =
		PVRSRVRGXInitGuestKM(psConnection, OSGetDevData(psConnection),
					psRGXInitGuestIN->bEnableSignatureChecks,
					psRGXInitGuestIN->ui32SignatureChecksBufSize,
					psRGXInitGuestIN->ui32RGXFWAlignChecksArrLength,
					ui32RGXFWAlignChecksInt,
					psRGXInitGuestIN->ui32DeviceFlags,
					&psRGXInitGuestIN->sClientBVNC);




RGXInitGuest_exit:


	if (ui32RGXFWAlignChecksInt)
		OSFreeMemNoStats(ui32RGXFWAlignChecksInt);

	return 0;
}


static IMG_INT
PVRSRVBridgeRGXInitFirmwareExtended(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXINITFIRMWAREEXTENDED *psRGXInitFirmwareExtendedIN,
					  PVRSRV_BRIDGE_OUT_RGXINITFIRMWAREEXTENDED *psRGXInitFirmwareExtendedOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_UINT32 *ui32RGXFWAlignChecksInt = NULL;
	PMR * psHWPerfPMR2Int = NULL;




	if (psRGXInitFirmwareExtendedIN->ui32RGXFWAlignChecksArrLength != 0)
	{
		ui32RGXFWAlignChecksInt = OSAllocZMemNoStats(psRGXInitFirmwareExtendedIN->ui32RGXFWAlignChecksArrLength * sizeof(IMG_UINT32));
		if (!ui32RGXFWAlignChecksInt)
		{
			psRGXInitFirmwareExtendedOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXInitFirmwareExtended_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (void*) psRGXInitFirmwareExtendedIN->pui32RGXFWAlignChecks, psRGXInitFirmwareExtendedIN->ui32RGXFWAlignChecksArrLength * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32RGXFWAlignChecksInt, psRGXInitFirmwareExtendedIN->pui32RGXFWAlignChecks,
				psRGXInitFirmwareExtendedIN->ui32RGXFWAlignChecksArrLength * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXInitFirmwareExtendedOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXInitFirmwareExtended_exit;
			}


	psRGXInitFirmwareExtendedOUT->eError =
		PVRSRVRGXInitFirmwareExtendedKM(psConnection, OSGetDevData(psConnection),
					psRGXInitFirmwareExtendedIN->ui32RGXFWAlignChecksArrLength,
					ui32RGXFWAlignChecksInt,
					&psRGXInitFirmwareExtendedOUT->spsRGXFwInit,
					&psHWPerfPMR2Int,
					&psRGXInitFirmwareExtendedIN->spsInParams);
	/* Exit early if bridged call fails */
	if(psRGXInitFirmwareExtendedOUT->eError != PVRSRV_OK)
	{
		goto RGXInitFirmwareExtended_exit;
	}






	psRGXInitFirmwareExtendedOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,

							&psRGXInitFirmwareExtendedOUT->hHWPerfPMR2,
							(void *) psHWPerfPMR2Int,
							PVRSRV_HANDLE_TYPE_PMR_LOCAL_EXPORT_HANDLE,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							,(PFN_HANDLE_RELEASE)&ReleaseHWPerfPMR2);
	if (psRGXInitFirmwareExtendedOUT->eError != PVRSRV_OK)
	{
		goto RGXInitFirmwareExtended_exit;
	}




RGXInitFirmwareExtended_exit:


	if (psRGXInitFirmwareExtendedOUT->eError != PVRSRV_OK)
	{
	}

	if (ui32RGXFWAlignChecksInt)
		OSFreeMemNoStats(ui32RGXFWAlignChecksInt);

	return 0;
}




/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */

static IMG_BOOL bUseLock = IMG_TRUE;

PVRSRV_ERROR InitRGXINITBridge(void);
PVRSRV_ERROR DeinitRGXINITBridge(void);

/*
 * Register all RGXINIT functions with services
 */
PVRSRV_ERROR InitRGXINITBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXINIT, PVRSRV_BRIDGE_RGXINIT_RGXINITALLOCFWIMGMEM, PVRSRVBridgeRGXInitAllocFWImgMem,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXINIT, PVRSRV_BRIDGE_RGXINIT_RGXINITFIRMWARE, PVRSRVBridgeRGXInitFirmware,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXINIT, PVRSRV_BRIDGE_RGXINIT_RGXINITFINALISEFWIMAGE, PVRSRVBridgeRGXInitFinaliseFWImage,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXINIT, PVRSRV_BRIDGE_RGXINIT_RGXINITDEVPART2, PVRSRVBridgeRGXInitDevPart2,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXINIT, PVRSRV_BRIDGE_RGXINIT_GPUVIRTPOPULATELMASUBARENAS, PVRSRVBridgeGPUVIRTPopulateLMASubArenas,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXINIT, PVRSRV_BRIDGE_RGXINIT_RGXINITGUEST, PVRSRVBridgeRGXInitGuest,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXINIT, PVRSRV_BRIDGE_RGXINIT_RGXINITFIRMWAREEXTENDED, PVRSRVBridgeRGXInitFirmwareExtended,
					NULL, bUseLock);


	return PVRSRV_OK;
}

/*
 * Unregister all rgxinit functions with services
 */
PVRSRV_ERROR DeinitRGXINITBridge(void)
{
	return PVRSRV_OK;
}
