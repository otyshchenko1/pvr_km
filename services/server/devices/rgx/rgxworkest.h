/*************************************************************************/ /*!
@File           rgxworkest.h
@Title          RGX Workload Estimation Functionality
@Codingstyle    IMG
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for the kernel mode workload estimation functionality.
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

#ifndef RGXWORKEST_H
#define RGXWORKEST_H

#include "img_types.h"
#include "hash.h"
#include "rgxta3d.h"

#define WORKLOAD_HASH_SIZE 64

typedef struct _WORKLOAD_CLEAN_LIST_ WORKLOAD_CLEAN_LIST;

struct _WORKLOAD_CLEAN_LIST_
{
	DEVMEM_MEMDESC			*psWorkloadDataMemDesc;
	WORKLOAD_CLEAN_LIST		*psNextNode;
};

typedef struct _RGX_WORKLOAD_TA3D_
{
	IMG_UINT32				ui32RenderTargetSize;
	IMG_UINT32				ui32NumberOfDrawCalls;
	IMG_UINT32				ui32NumberOfIndices;
	IMG_UINT32				ui32NumberOfMRTs;
} RGX_WORKLOAD_TA3D;

typedef union _RGX_WORKLOAD_
{
	RGX_WORKLOAD_TA3D	sTA3D;
	/* Created as a union to facilitate other DMs
	 * in the future.
	 * for Example:
	 * RGXFWIF_WORKLOAD_CDM     sCDM;
	 */
} RGX_WORKLOAD;

typedef struct _WORKLOAD_MATCHING_DATA_
{
	HASH_TABLE                  *psWorkloadDataHash;
	RGX_WORKLOAD_TA3D           asWorkloadHashKeys[WORKLOAD_HASH_SIZE];
	IMG_UINT64                  aui64HashCycleData[WORKLOAD_HASH_SIZE];
	IMG_UINT32                  ui32HashArrayWO;
} WORKLOAD_MATCHING_DATA;

typedef struct _WORKEST_HOST_DATA_{
	WORKLOAD_MATCHING_DATA      sWorkloadMatchingDataTA;
	WORKLOAD_MATCHING_DATA      sWorkloadMatchingData3D;
	IMG_UINT32                  ui32HashArrayWO;
	POS_LOCK                    psWorkEstHashLock;
	IMG_BOOL                    bHashInvalid;
	IMG_BOOL                    bWorkloadDataInvalid;
	WORKLOAD_CLEAN_LIST*        psWorkloadCleanList;
#if defined(SUPPORT_PDVFS)
	WORKLOAD_CLEAN_LIST         *psPDVFSCleanList;
#endif
} WORKEST_HOST_DATA;

IMG_INTERNAL
void WorkEstRCInit(WORKEST_HOST_DATA *psWorkEstData);

IMG_INTERNAL
void WorkEstRCDeInit(WORKEST_HOST_DATA *psWorkEstData,
                     PVRSRV_RGXDEV_INFO *psDevInfo);
IMG_INTERNAL
PVRSRV_ERROR WorkEstEmptyWorkloadHash(	HASH_TABLE* psHash,
										uintptr_t k,
										uintptr_t v);

IMG_INTERNAL
IMG_BOOL WorkEstHashCompareTA3D(size_t uKeySize,
								 void *pKey1,
								 void *pKey2);

IMG_INTERNAL
IMG_UINT32 WorkEstHashFuncTA3D(size_t uKeySize, void *pKey, IMG_UINT32 uHashTabLen);

IMG_INTERNAL
PVRSRV_ERROR WorkEstPrepare(PVRSRV_RGXDEV_INFO			*psDevInfo,
							WORKEST_HOST_DATA			*psWorkEstData,
							PRGXFWIF_WORKLOAD_DATA		*psWorkloadDataFWAddr,
							WORKLOAD_MATCHING_DATA      *psWorkloadMatchingData,
							IMG_UINT32					ui32RenderTargetSize,
							IMG_UINT32					ui32NumberOfDrawCalls,
							IMG_UINT32					ui32NumberOfIndices,
							IMG_UINT32					ui32NumberOfMRTs,
							IMG_UINT64					ui64DeadlineInus);

IMG_INTERNAL
PVRSRV_ERROR WorkEstWorkloadFinished(DEVMEM_MEMDESC *psWorkloadDataMemDesc);

IMG_INTERNAL
void WorkEstHashLockCreate(POS_LOCK *psWorkEstHashLock);

IMG_INTERNAL
void WorkEstHashLockDestroy(POS_LOCK sWorkEstHashLock);

IMG_INTERNAL
PVRSRV_ERROR WorkEstClearBuffer(PVRSRV_RGXDEV_INFO *psDevInfo);

IMG_INTERNAL
void WorkEstWorkloadClean(WORKEST_HOST_DATA *psWorkEstData);

//The following no longer works due to removal of cleanup globals
/*IMG_INTERNAL
void WorkEstRemovePDVFSCleanupNodes(DEVMEM_MEMDESC *psMemDesc);*/

#endif /* RGXWORKEST_H */
