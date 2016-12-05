/*************************************************************************/ /*!
@File
@Title          Server bridge for wrapextmem
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declares common defines and structures that are used by both
                the client and sever side of the bridge for wrapextmem
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

#include "devicemem_server.h"
#include "pmr.h"
#include "devicemem_heapcfg.h"

#include "common_wrapextmem_bridge.h"

#include "pvr_debug.h"
#include "connection_server.h"
#include "pvr_bridge.h"
#include "rgx_bridge.h"
#include "srvcore.h"
#include "handle.h"

#include "wrapextmem.h"

#include <linux/slab.h>


static IMG_INT
PVRSRVBridgeWrapExtMem(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_WRAPEXTMEM *psWrapExtMemIN,
					 PVRSRV_BRIDGE_OUT_WRAPEXTMEM *psWrapExtMemOUT,
					 CONNECTION_DATA *psConnection)
{
	PMR * psPMRPtrInt;

#if defined(__LP64__) || defined (_LP64)
	void *pvExtMemVAddr = (void *)psWrapExtMemIN->sExtMemVAddr.uiAddr;
#else
	void *pvExtMemVAddr = (void *)((IMG_UINT32)psWrapExtMemIN->sExtMemVAddr.uiAddr);
#endif

	psWrapExtMemOUT->eError =
		WrapExtMemKM(OSGetDevData(psConnection),
					pvExtMemVAddr,
					psWrapExtMemIN->uiExtMemLength,
					psWrapExtMemIN->uiFlags,
					&psPMRPtrInt,
					&psWrapExtMemOUT->uiMappingLength);
	/* Exit early if bridged call fails */
	if(psWrapExtMemOUT->eError != PVRSRV_OK)
	{
		goto WrapExtMem_exit;
	}

	psWrapExtMemOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
					  &psWrapExtMemOUT->hPMRPtr,
					  (void *) psPMRPtrInt,
					  PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
					  PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
					  (PFN_HANDLE_RELEASE)&PMRUnrefPMR);
WrapExtMem_exit:

	return 0;
}

static IMG_BOOL bUseLock = IMG_TRUE;

PVRSRV_ERROR InitWRAPEXTMEMBridge(void);
void DeinitWRAPEXTMEMBridge(void);

/*
 * Register all WRAPEXTMEM functions with services
 */
PVRSRV_ERROR InitWRAPEXTMEMBridge(void)
{
	SetDispatchTableEntry(PVRSRV_BRIDGE_WRAPEXTMEM, PVRSRV_BRIDGE_WRAPEXTMEM_WRAPEXTMEM, PVRSRVBridgeWrapExtMem,
					NULL, bUseLock);

	return PVRSRV_OK;
}

/*
 * Unregister all wrapextmem functions with services
 */
void DeinitWRAPEXTMEMBridge(void)
{
}
