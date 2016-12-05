/*************************************************************************/ /*!
@File           rgxworkest.c
@Title          RGX Workload Estimation Functionality
@Codingstyle    IMG
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Kernel mode workload estimation functionality.
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

#include "rgxworkest.h"
#include "rgxfwutils.h"
#include "rgxdevice.h"
#include "rgxpdvfs.h"
#include "device.h"
#include "pvr_debug.h"

static void _AddCleanupNode(WORKLOAD_CLEAN_LIST **ppsCleanList, DEVMEM_MEMDESC *psWorkloadDataMemDesc)
{
	WORKLOAD_CLEAN_LIST *psNewNode = OSAllocMem(sizeof(WORKLOAD_CLEAN_LIST));
	WORKLOAD_CLEAN_LIST *psCurrentNode = NULL;
	WORKLOAD_CLEAN_LIST *psPreviousNode = NULL;

	psCurrentNode = *ppsCleanList;

	while(psCurrentNode != NULL)
	{
		psPreviousNode = psCurrentNode;
		psCurrentNode = psCurrentNode->psNextNode;
	}

	psNewNode->psWorkloadDataMemDesc = psWorkloadDataMemDesc;
	psNewNode->psNextNode = NULL;

	if(psPreviousNode == NULL)
	{
		*ppsCleanList = psNewNode;
	}
	else
	{
		psPreviousNode->psNextNode = psNewNode;
	}
	return;
}

static void _RemoveCleanupNode(WORKLOAD_CLEAN_LIST **ppsCleanList,
                               DEVMEM_MEMDESC *psWorkloadDataMemDesc)
{
	WORKLOAD_CLEAN_LIST *psCurrentNode = NULL;
	WORKLOAD_CLEAN_LIST *psPreviousNode = NULL;
	WORKLOAD_CLEAN_LIST *psFreeNode = NULL;

	psCurrentNode = *ppsCleanList;

	while(psCurrentNode != NULL
	      && psCurrentNode->psWorkloadDataMemDesc != psWorkloadDataMemDesc )
	{
		psPreviousNode = psCurrentNode;
		psCurrentNode = psCurrentNode->psNextNode;
	}

	if(psCurrentNode != NULL)
	{
		psFreeNode = psCurrentNode;

		if(psPreviousNode == NULL)
		{
			*ppsCleanList = psCurrentNode->psNextNode;
		}
		else
		{
			psPreviousNode->psNextNode = psCurrentNode->psNextNode;
		}

		OSFreeMem(psFreeNode);
	}
	return;
}


static void _WorkloadClean(WORKLOAD_CLEAN_LIST **ppsCleanList)
{
	WORKLOAD_CLEAN_LIST *psCleanList = *ppsCleanList;
	WORKLOAD_CLEAN_LIST *psFreeNode = NULL;
	DEVMEM_MEMDESC *psMemDesc;

	while(psCleanList != NULL)
	{
		psMemDesc = psCleanList->psWorkloadDataMemDesc;
		if(psCleanList->psWorkloadDataMemDesc != NULL)
		{
			RGXUnsetFirmwareAddress(psMemDesc);
			DevmemFwFree(psMemDesc);
			psMemDesc = NULL;
		}
		psFreeNode = psCleanList;
		psCleanList = psCleanList->psNextNode;

		OSFreeMem(psFreeNode);
	}
	*ppsCleanList = psCleanList;
	return;
}

void WorkEstRCInit(WORKEST_HOST_DATA *psWorkEstData)
{
	psWorkEstData->ui32HashArrayWO = 0;
	psWorkEstData->bWorkloadDataInvalid = IMG_FALSE;
	psWorkEstData->psWorkloadCleanList = NULL;
#if defined(SUPPORT_PDVFS)
	psWorkEstData->psPDVFSCleanList = NULL;
#endif

	/* Create hash tables for workload matching */
	psWorkEstData->sWorkloadMatchingDataTA.psWorkloadDataHash =
		HASH_Create_Extended(WORKLOAD_HASH_SIZE,
							 sizeof(RGX_WORKLOAD_TA3D *),
							 &WorkEstHashFuncTA3D,
							 (HASH_KEY_COMP *)&WorkEstHashCompareTA3D);

	psWorkEstData->sWorkloadMatchingData3D.psWorkloadDataHash =
		HASH_Create_Extended(WORKLOAD_HASH_SIZE,
							 sizeof(RGX_WORKLOAD_TA3D *),
							 &WorkEstHashFuncTA3D,
							 (HASH_KEY_COMP *)&WorkEstHashCompareTA3D);

	/* Create a lock to protect the hash tables */
	WorkEstHashLockCreate(&(psWorkEstData->psWorkEstHashLock));
}

void WorkEstRCDeInit(WORKEST_HOST_DATA *psWorkEstData,
                     PVRSRV_RGXDEV_INFO *psDevInfo)
{
	HASH_TABLE        *psWorkloadDataHash;
	RGX_WORKLOAD_TA3D *pasWorkloadHashKeys;
	RGX_WORKLOAD_TA3D *psWorkloadHashKey;
	IMG_UINT32        ui32i;
	IMG_UINT64        *paui64WorkloadCycleData;

	/* Tell the return logic that incoming workload's may have been deleted
	 * therefore it should discard them.
	 */
	WorkEstClearBuffer(psDevInfo);
	psWorkEstData->bHashInvalid = IMG_TRUE;

	WorkEstWorkloadClean(psWorkEstData);

	pasWorkloadHashKeys = psWorkEstData->sWorkloadMatchingDataTA.asWorkloadHashKeys;
	paui64WorkloadCycleData = psWorkEstData->sWorkloadMatchingDataTA.aui64HashCycleData;
	psWorkloadDataHash = psWorkEstData->sWorkloadMatchingDataTA.psWorkloadDataHash;

	if(psWorkloadDataHash)
	{
		for(ui32i = 0; ui32i < WORKLOAD_HASH_SIZE; ui32i++)
		{
			if(paui64WorkloadCycleData[ui32i] > 0)
			{
				psWorkloadHashKey = &pasWorkloadHashKeys[ui32i];
				HASH_Remove_Extended(psWorkloadDataHash,
									 (uintptr_t*)&psWorkloadHashKey);
			}
		}

		HASH_Delete(psWorkloadDataHash);
	}


	pasWorkloadHashKeys = psWorkEstData->sWorkloadMatchingData3D.asWorkloadHashKeys;
	paui64WorkloadCycleData = psWorkEstData->sWorkloadMatchingData3D.aui64HashCycleData;
	psWorkloadDataHash = psWorkEstData->sWorkloadMatchingData3D.psWorkloadDataHash;

	if(psWorkloadDataHash)
	{
		for(ui32i = 0; ui32i < WORKLOAD_HASH_SIZE; ui32i++)
		{
			if(paui64WorkloadCycleData[ui32i] > 0)
			{
				psWorkloadHashKey = &pasWorkloadHashKeys[ui32i];
				HASH_Remove_Extended(psWorkloadDataHash,
									 (uintptr_t*)&psWorkloadHashKey);
			}
		}

		HASH_Delete(psWorkloadDataHash);
	}

	/* Remove the hash lock */
	WorkEstHashLockDestroy(psWorkEstData->psWorkEstHashLock);

	return;
}
void WorkEstWorkloadClean(WORKEST_HOST_DATA *psWorkloadHostData)
{
	psWorkloadHostData->bWorkloadDataInvalid = IMG_TRUE;
	_WorkloadClean(&(psWorkloadHostData->psWorkloadCleanList));
#if defined(SUPPORT_PDVFS)
	_WorkloadClean(&(psWorkloadHostData->psPDVFSCleanList));
#endif
	return;
}

IMG_BOOL WorkEstHashCompareTA3D(size_t uKeySize,
								void *pKey1,
								void *pKey2)
{
	RGX_WORKLOAD_TA3D *psWorkload1;
	RGX_WORKLOAD_TA3D *psWorkload2;

	if(pKey1 && pKey2)
	{
		psWorkload1 = *((RGX_WORKLOAD_TA3D **)pKey1);
		psWorkload2 = *((RGX_WORKLOAD_TA3D **)pKey2);

		PVR_ASSERT(psWorkload1);
		PVR_ASSERT(psWorkload2);

		if(psWorkload1->ui32RenderTargetSize == psWorkload2->ui32RenderTargetSize
		   && psWorkload1->ui32NumberOfDrawCalls == psWorkload2->ui32NumberOfDrawCalls
		   && psWorkload1->ui32NumberOfIndices == psWorkload2->ui32NumberOfIndices
		   && psWorkload1->ui32NumberOfMRTs == psWorkload2->ui32NumberOfMRTs)
		{
			/* This is added to allow this memory to be freed */
			*(uintptr_t*)pKey2 = *(uintptr_t*)pKey1;
			return IMG_TRUE;
		}
	}
	return IMG_FALSE;
}

static inline IMG_UINT32 WorkEstDoHash(IMG_UINT32 ui32Input)
{
	IMG_UINT32 ui32HashPart;

	ui32HashPart = ui32Input;
	ui32HashPart += (ui32HashPart << 12);
	ui32HashPart ^= (ui32HashPart >> 22);
	ui32HashPart += (ui32HashPart << 4);
	ui32HashPart ^= (ui32HashPart >> 9);
	ui32HashPart += (ui32HashPart << 10);
	ui32HashPart ^= (ui32HashPart >> 2);
	ui32HashPart += (ui32HashPart << 7);
	ui32HashPart ^= (ui32HashPart >> 12);

	return ui32HashPart;
}

IMG_UINT32 WorkEstHashFuncTA3D(size_t uKeySize, void *pKey, IMG_UINT32 uHashTabLen)
{
	RGX_WORKLOAD_TA3D *psWorkload = *((RGX_WORKLOAD_TA3D**)pKey);
	IMG_UINT32 ui32HashKey = 0;
	PVR_UNREFERENCED_PARAMETER(uHashTabLen);
	PVR_UNREFERENCED_PARAMETER(uKeySize);

	ui32HashKey += WorkEstDoHash(psWorkload->ui32RenderTargetSize);
	ui32HashKey += WorkEstDoHash(psWorkload->ui32NumberOfDrawCalls);
	ui32HashKey += WorkEstDoHash(psWorkload->ui32NumberOfIndices);
	ui32HashKey += WorkEstDoHash(psWorkload->ui32NumberOfMRTs);

	return ui32HashKey;
}

PVRSRV_ERROR WorkEstPrepare(PVRSRV_RGXDEV_INFO			*psDevInfo,
							WORKEST_HOST_DATA			*psWorkloadHostData,
							PRGXFWIF_WORKLOAD_DATA		*psWorkloadDataFWAddr,
							WORKLOAD_MATCHING_DATA      *psWorkloadMatchingData,
							IMG_UINT32					ui32RenderTargetSize,
							IMG_UINT32					ui32NumberOfDrawCalls,
							IMG_UINT32					ui32NumberOfIndices,
							IMG_UINT32					ui32NumberOfMRTs,
							IMG_UINT64					ui64DeadlineInus)
{
	RGX_WORKLOAD_TA3D		*psWorkloadCharacterisitics;
	IMG_UINT64					*pui64CyclePrediction;
	RGXFWIF_WORKLOAD_DATA		*psWorkloadData;
	PVRSRV_ERROR				eError;
	DEVMEM_MEMDESC				*psWorkloadDataMemDesc;
	POS_LOCK					psWorkEstHashLock;
	IMG_UINT64					ui64CurrentTime;
	HASH_TABLE					*psWorkloadDataHash =
									psWorkloadMatchingData->psWorkloadDataHash;

#if defined(SUPPORT_PDVFS)
	DEVMEM_MEMDESC* psDeadlineListNodeMemdesc;
	DEVMEM_MEMDESC* psWorkloadListNodeMemdesc;
	PRGXFWIF_DEADLINE_LIST_NODE sDeadlineNodeFWAddress;
	PRGXFWIF_WORKLOAD_LIST_NODE  sWorkloadNodeFWAddress;
	RGXFWIF_DEADLINE_LIST_NODE *psDeadlineListNode;
	RGXFWIF_WORKLOAD_LIST_NODE *psWorkloadListNode;
	sDeadlineNodeFWAddress.ui32Addr = 0;
	sWorkloadNodeFWAddress.ui32Addr = 0;
#endif

	if(psDevInfo == NULL || psWorkloadDataHash == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"WorkEstPrepare: Invalid Parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	eError = OSKClockus64(&ui64CurrentTime);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"WorkEstPrepare: Unable to get the current time"));
		return eError;
	}

	/* Allocate firmware memory for the workload data. */
	eError = DevmemFwAllocate(psDevInfo,
							  sizeof(RGXFWIF_WORKLOAD_DATA),
							  PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
							  PVRSRV_MEMALLOCFLAG_GPU_READABLE |
							  PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
							  PVRSRV_MEMALLOCFLAG_CPU_READABLE |
							  PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
							  PVRSRV_MEMALLOCFLAG_UNCACHED |
							  PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
							  PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC,
							  "FwWorkloadData",
							  &psWorkloadDataMemDesc);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "WorkEstPrepare: Failed to allocate device memory for Workload (%s)",
				 PVRSRVGETERRORSTRING(eError)));
		return eError;
	}

	/* Add a node to the clean list.
	 * This is necessary as workload data needs to be returned from the
	 * firmware is batches to avoid excessive interrupts on the host.
	 */
	_AddCleanupNode(&(psWorkloadHostData->psWorkloadCleanList),
	                psWorkloadDataMemDesc);

	DevmemAcquireCpuVirtAddr(psWorkloadDataMemDesc, (void**)&psWorkloadData);

	/* Put the memdesc inside the structure so it can be referenced later */
	psWorkloadData->ui64SelfMemDesc =
		(IMG_UINT64)(uintptr_t)psWorkloadDataMemDesc;
	/* Assign the next node to null */
	psWorkloadData->ui64NextNodeMemdesc = (IMG_UINT64)(uintptr_t)NULL;
	/* Pass a reference to the host data for memory management and hash
	 * tables */
	psWorkloadData->ui64WorkloadHostData =
		(IMG_UINT64)(uintptr_t)(psWorkloadHostData);

	RGXSetFirmwareAddress(psWorkloadDataFWAddr,
	                      psWorkloadDataMemDesc,
	                      0,
	                      RFW_FWADDR_FLAG_NONE);

#if defined(SUPPORT_PDVFS)
	/* Allocate memory for the PDVFS deadline node for the PDVFS tree */

	psDevInfo->psDeviceNode->psDevConfig->sDVFS.sPDVFSData.bWorkInFrame = IMG_TRUE;

	eError = DevmemFwAllocate(psDevInfo,
							  sizeof(RGXFWIF_DEADLINE_LIST_NODE),
							  PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
							  PVRSRV_MEMALLOCFLAG_GPU_READABLE |
							  PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
							  PVRSRV_MEMALLOCFLAG_CPU_READABLE |
							  PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
							  PVRSRV_MEMALLOCFLAG_UNCACHED |
							  PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
							  PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC,
							  "FwDeadlineListNode",
							  &psDeadlineListNodeMemdesc);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "WorkEstPrepare: Failed to allocate device memory for PDVFS deadline node  (%s)",
		         PVRSRVGETERRORSTRING(eError)));
		goto deadlinenodeerror;
	}

	DevmemAcquireCpuVirtAddr(psDeadlineListNodeMemdesc,
	                         (void**)&psDeadlineListNode);
	/* Put the memdesc inside the structure so it can be referenced later */
	psDeadlineListNode->ui64SelfMemDesc =
		(IMG_UINT64)(uintptr_t)psDeadlineListNodeMemdesc;
	
	/* Set the default state to the node not being released from the tree */
	psDeadlineListNode->bReleased = IMG_FALSE;

	/* Put a reference to which workload this node belongs to for when it
	 * completes
	 */
	psDeadlineListNode->ui64WorkloadDataMemDesc =
		(IMG_UINT64)(uintptr_t)psWorkloadDataMemDesc;

	psDeadlineListNode->psNextNode = NULL;

	DevmemReleaseCpuVirtAddr(psDeadlineListNodeMemdesc);
	psDeadlineListNode = NULL;

	/* Add this node to the clean list for PDVFS nodes */
	_AddCleanupNode(&(psWorkloadHostData->psPDVFSCleanList),
	                psDeadlineListNodeMemdesc);

	/* Allocate memory for the PDVFS workload node for the PVDFS tree */
	eError = DevmemFwAllocate(psDevInfo,
							  sizeof(RGXFWIF_WORKLOAD_LIST_NODE),
							  PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
							  PVRSRV_MEMALLOCFLAG_GPU_READABLE |
							  PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
							  PVRSRV_MEMALLOCFLAG_CPU_READABLE |
							  PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
							  PVRSRV_MEMALLOCFLAG_UNCACHED |
							  PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
							  PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC,
							  "FwWorkloadListNode",
							  &psWorkloadListNodeMemdesc);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "WorkEstPrepare: Failed to allocate device memory for PDVFS workload node  (%s)",
		         PVRSRVGETERRORSTRING(eError)));
		
		goto workloadnodeerror;
	}

	DevmemAcquireCpuVirtAddr(psWorkloadListNodeMemdesc,
	                         (void**)&psWorkloadListNode);

	/* Put the memdesc inside the structure so it can be referenced later */
	psWorkloadListNode->ui64SelfMemDesc =
		(IMG_UINT64)(uintptr_t)psWorkloadListNodeMemdesc;

	/* Set the default state to the node not being released from the tree */
	psWorkloadListNode->bReleased = IMG_FALSE;

	/* Put a reference to which workload this node belongs to for when it
	 * completes
	 */
	psWorkloadListNode->ui64WorkloadDataMemDesc =
		(IMG_UINT64)(uintptr_t)psWorkloadDataMemDesc;

	psWorkloadListNode->psNextNode = NULL;

	DevmemReleaseCpuVirtAddr(psWorkloadListNodeMemdesc);
	psWorkloadListNode = NULL;

	/* Add this node to the clean list for PDVFS nodes */
	_AddCleanupNode(&(psWorkloadHostData->psPDVFSCleanList),
	                psWorkloadListNodeMemdesc);

	RGXSetFirmwareAddress(&sDeadlineNodeFWAddress,
	                      psDeadlineListNodeMemdesc,
	                      0,
	                      RFW_FWADDR_FLAG_NONE);
	RGXSetFirmwareAddress(&sWorkloadNodeFWAddress,
	                      psWorkloadListNodeMemdesc,
	                      0,
	                      RFW_FWADDR_FLAG_NONE);

	/* Store the firmware addresses for the nodes */
	psWorkloadData->sDeadlineNodeFWAddress = sDeadlineNodeFWAddress;
	psWorkloadData->sWorkloadNodeFWAddress = sWorkloadNodeFWAddress;

	/* Store memory descriptors so they can be freed on the return path. */
	psWorkloadData->ui64DeadlineNodeMemDesc =
		(IMG_UINT64)(uintptr_t)psDeadlineListNodeMemdesc;
	psWorkloadData->ui64WorkloadNodeMemDesc =
		(IMG_UINT64)(uintptr_t)psWorkloadListNodeMemdesc;
#endif

	/* Assign workload characteristics for the return path */
	psWorkloadCharacterisitics = OSAllocZMem(sizeof(RGX_WORKLOAD_TA3D));

	psWorkloadData->ui64WorkloadCharacteristics = (IMG_UINT64)(uintptr_t)psWorkloadCharacterisitics;

	psWorkloadCharacterisitics->ui32RenderTargetSize = ui32RenderTargetSize;
	psWorkloadCharacterisitics->ui32NumberOfDrawCalls = ui32NumberOfDrawCalls;
	psWorkloadCharacterisitics->ui32NumberOfIndices = ui32NumberOfIndices;
	psWorkloadCharacterisitics->ui32NumberOfMRTs = ui32NumberOfMRTs;

	/* Store the deadline in the Data struct */
	OSKClockus64(&ui64CurrentTime);

	if(ui64DeadlineInus > ui64CurrentTime)
	{
		psWorkloadData->ui64DeadlineInus = ui64DeadlineInus;
	}
	else
	{
		/* If the deadline has already passed assign as zero to suggest full
		 * frequency
		 */
		psWorkloadData->ui64DeadlineInus = 0;
	}

	/* Store reference to the hash table */
	psWorkloadData->ui64WorkloadMatchingData =
		(IMG_UINT64)(uintptr_t)(psWorkloadMatchingData);

	psWorkEstHashLock = psWorkloadHostData->psWorkEstHashLock;

	if(psWorkEstHashLock == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "WorkEstPrepare: Hash lock not available, unable to retrieve cycles"
		        ));
		eError = PVRSRV_ERROR_UNABLE_TO_RETRIEVE_HASH_VALUE;
		goto hashlockerror;
	}

	/* Acquire the lock to access hash */
	OSLockAcquire(psWorkEstHashLock);

	/* Check if there is a prediction for this workload */
	pui64CyclePrediction =
		(IMG_UINT64*) HASH_Retrieve(psWorkloadDataHash,
		                            (uintptr_t)psWorkloadCharacterisitics);

	/* Release lock */
	OSLockRelease(psWorkEstHashLock);

	if(pui64CyclePrediction != NULL)
	{
		/* Cycle prediction is available, store this prediction */
		psWorkloadData->ui64CyclesPrediction = *pui64CyclePrediction;
	}

	DevmemReleaseCpuVirtAddr(psWorkloadDataMemDesc);
	psWorkloadData = (RGXFWIF_WORKLOAD_DATA*) NULL;

	return PVRSRV_OK;

workloadnodeerror:
	DevmemReleaseCpuVirtAddr(psWorkloadDataMemDesc);
	_RemoveCleanupNode(&(psWorkloadHostData->psWorkloadCleanList),
	                   psWorkloadDataMemDesc);

	/* Free the memory */
	RGXUnsetFirmwareAddress(psWorkloadDataMemDesc);
	DevmemFwFree(psWorkloadDataMemDesc);

deadlinenodeerror:
	_RemoveCleanupNode(&(psWorkloadHostData->psPDVFSCleanList),
	                   psDeadlineListNodeMemdesc);
	
	/* Free the memory */
	RGXUnsetFirmwareAddress(psDeadlineListNodeMemdesc);
	DevmemFwFree(psDeadlineListNodeMemdesc);

	return eError;

hashlockerror:
	DevmemReleaseCpuVirtAddr(psWorkloadDataMemDesc);
	psWorkloadData = (RGXFWIF_WORKLOAD_DATA*) NULL;

	return eError;
}

PVRSRV_ERROR WorkEstWorkloadFinished(DEVMEM_MEMDESC *psWorkloadDataMemDesc)
{
	RGX_WORKLOAD_TA3D          *psWorkloadCharacteristics;
	RGX_WORKLOAD_TA3D           *pasWorkloadHashKeys;
	IMG_UINT64                  *paui64HashCycleData;
	IMG_UINT32                  *pui32HashArrayWO;
	RGXFWIF_WORKLOAD_DATA       *psWorkloadData;
	RGX_WORKLOAD_TA3D           *psWorkloadHashKey;
	IMG_UINT64                  *pui64CyclesTaken;
	DEVMEM_MEMDESC              *psWorkloadDataMemDescNext;
	HASH_TABLE                  *psWorkloadHash;
	WORKEST_HOST_DATA           *psWorkloadHostData;
	WORKLOAD_MATCHING_DATA      *psWorkloadMatchingData;
	POS_LOCK                    psWorkEstHashLock;
	IMG_BOOL                    *pbWorkloadDataInvalid;
	PVRSRV_ERROR                eError;
	IMG_BOOL                    bHashSucess;
#if defined(SUPPORT_PDVFS)
	DEVMEM_MEMDESC              *psNodeMemDesc;
	RGXFWIF_DEADLINE_LIST_NODE  *DeadlineListNode;
	RGXFWIF_WORKLOAD_LIST_NODE  *WorkloadListNode;
	IMG_BOOL                    bFree = IMG_FALSE;
#endif

	if(psWorkloadDataMemDesc == NULL)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	eError =
		DevmemAcquireCpuVirtAddr(psWorkloadDataMemDesc, (void **)&psWorkloadData);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "WorkEstWorkloadFinished: Failed to acquire CPU Virtual Address: (%s)",
		         PVRSRVGETERRORSTRING(eError)));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psWorkloadHostData =
		(WORKEST_HOST_DATA*)(uintptr_t)(psWorkloadData->ui64WorkloadHostData);
	pbWorkloadDataInvalid = &(psWorkloadHostData->bWorkloadDataInvalid);

	/* Check if the data is still valid and not already freed */
	if(*pbWorkloadDataInvalid == IMG_TRUE)
	{
		*pbWorkloadDataInvalid = IMG_FALSE;
		return PVRSRV_OK;
	}

	DevmemReleaseCpuVirtAddr(psWorkloadDataMemDesc);

	/* Loop through the batch of workload data. */
	while(psWorkloadDataMemDesc != NULL)
	{
		eError = DevmemAcquireCpuVirtAddr(psWorkloadDataMemDesc,
		                                  (void **)&psWorkloadData);

		if(eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
			         "WorkEstWorkloadFinished: Failed to acquire CPU Virtual Address: (%s)",
			         PVRSRVGETERRORSTRING(eError)));
			return PVRSRV_ERROR_INVALID_PARAMS;
		}

		psWorkloadCharacteristics = (RGX_WORKLOAD_TA3D*)(uintptr_t)(psWorkloadData->ui64WorkloadCharacteristics);
		psWorkloadMatchingData = (WORKLOAD_MATCHING_DATA*)(uintptr_t)(psWorkloadData->ui64WorkloadMatchingData);

		psWorkloadHash = psWorkloadMatchingData->psWorkloadDataHash;

		psWorkloadHostData = (WORKEST_HOST_DATA*)(uintptr_t)(psWorkloadData->ui64WorkloadHostData);
		psWorkEstHashLock = psWorkloadHostData->psWorkEstHashLock;

		pasWorkloadHashKeys = psWorkloadMatchingData->asWorkloadHashKeys;
		paui64HashCycleData = psWorkloadMatchingData->aui64HashCycleData;
		pui32HashArrayWO = &(psWorkloadMatchingData->ui32HashArrayWO);

		if(psWorkEstHashLock != NULL)
		{
			OSLockAcquire(psWorkEstHashLock);
		}

		/* Check for false data */
		if(psWorkloadCharacteristics != NULL &&
		   psWorkloadHash != NULL &&
		   psWorkloadData->bComplete == IMG_TRUE)
		{
			/* Check that the hash has not already been freed */
			if(psWorkloadHostData->bHashInvalid == IMG_FALSE)
			{
				pui64CyclesTaken =
					(IMG_UINT64*) HASH_Remove_Extended(psWorkloadHash,
							(uintptr_t*)&psWorkloadCharacteristics);

				/* Remove the oldest Hash data before it becomes overwritten */
				if(paui64HashCycleData[*pui32HashArrayWO] > 0)
				{
					psWorkloadHashKey = &pasWorkloadHashKeys[*pui32HashArrayWO];
					HASH_Remove_Extended(psWorkloadHash,
					                     (uintptr_t*)&psWorkloadHashKey);
				}

				if(pui64CyclesTaken == NULL)
				{
					pasWorkloadHashKeys[*pui32HashArrayWO] = *psWorkloadCharacteristics;

					paui64HashCycleData[*pui32HashArrayWO] = psWorkloadData->ui64CyclesTaken;
				}
				else
				{
					*pui64CyclesTaken =
						(*pui64CyclesTaken + psWorkloadData->ui64CyclesTaken)/2;

					pasWorkloadHashKeys[*pui32HashArrayWO] = *psWorkloadCharacteristics;

					paui64HashCycleData[*pui32HashArrayWO] = *pui64CyclesTaken;

					/* Set the old value to 0 so it is known to be invalid */
					*pui64CyclesTaken = 0;
				}


				bHashSucess = HASH_Insert((HASH_TABLE*)(psWorkloadHash),
				                          (uintptr_t)&pasWorkloadHashKeys[*pui32HashArrayWO],
				                          (uintptr_t)&paui64HashCycleData[*pui32HashArrayWO]);
				PVR_ASSERT(bHashSucess);

				if(*pui32HashArrayWO == WORKLOAD_HASH_SIZE-1)
				{
					 *pui32HashArrayWO = 0;
				}
				else
				{
					(*pui32HashArrayWO)++;
				}

			}
			else
			{
				/* If the hash was invalid, set back to valid */
				psWorkloadHostData->bHashInvalid = IMG_FALSE;
			}
		}

#if defined(SUPPORT_PDVFS)

		/* DEADLINE NODE */
		psNodeMemDesc =
			(DEVMEM_MEMDESC*)(uintptr_t)(psWorkloadData->ui64DeadlineNodeMemDesc);

		if(psNodeMemDesc != NULL)
		{
			eError = DevmemAcquireCpuVirtAddr(psNodeMemDesc,
									 (void**)&DeadlineListNode);

			/* Check that the memory has been removed from the tree in the
			 * firmware
			 */
			if(eError == PVRSRV_OK)
			{
				if(DeadlineListNode->bReleased)
				{
					bFree = IMG_TRUE;
				}

				DevmemReleaseCpuVirtAddr(psNodeMemDesc);
				DeadlineListNode = NULL;

				if(bFree)
				{
					/* Remove the node from the clean list */
					_RemoveCleanupNode(&(psWorkloadHostData->psPDVFSCleanList),
									   psNodeMemDesc);
					/* Free the memory */
					RGXUnsetFirmwareAddress(psNodeMemDesc);
					DevmemFwFree(psNodeMemDesc);
					psNodeMemDesc = NULL;
				}
				bFree = IMG_FALSE;
			}
			/* If not then this will be handled by the clean list */
		}
		/* WORKLOAD NODE */
		psNodeMemDesc =
			(DEVMEM_MEMDESC*)(uintptr_t)(psWorkloadData->ui64WorkloadNodeMemDesc);

		if(psNodeMemDesc != NULL)
		{
			eError = DevmemAcquireCpuVirtAddr(psNodeMemDesc,
									 (void**)&WorkloadListNode);

			/* Check that the memory has been removed from the tree in the
			 * firmware
			 */
			if(eError == PVRSRV_OK)
			{
				if(WorkloadListNode->bReleased)
				{
					bFree = IMG_TRUE;
				}
				DevmemReleaseCpuVirtAddr(psNodeMemDesc);
				WorkloadListNode = NULL;
				if(bFree)
				{
					/* Remove the node from the clean list */
					_RemoveCleanupNode(&(psWorkloadHostData->psPDVFSCleanList),
									   psNodeMemDesc);
					/* Free the memory */
					RGXUnsetFirmwareAddress(psNodeMemDesc);
					DevmemFwFree(psNodeMemDesc);
					psNodeMemDesc = NULL;
				}
				bFree = IMG_FALSE;
			}
		}
#endif

		/* Remove workload data from the clean list */
		_RemoveCleanupNode(&(psWorkloadHostData->psWorkloadCleanList),
		                   psWorkloadDataMemDesc);

		if(psWorkEstHashLock != NULL)
		{
			OSLockRelease(psWorkEstHashLock);
		}

		/* Move to the next piece of workload data in the batch */
		psWorkloadDataMemDescNext =
			(DEVMEM_MEMDESC*)(uintptr_t)(psWorkloadData->ui64NextNodeMemdesc);

		/* Free the data */
		RGXUnsetFirmwareAddress(psWorkloadDataMemDesc);
		DevmemReleaseCpuVirtAddr(psWorkloadDataMemDesc);
		DevmemFwFree(psWorkloadDataMemDesc);
		psWorkloadDataMemDesc = psWorkloadDataMemDescNext;
	}
	return PVRSRV_OK;
}

void WorkEstHashLockCreate(POS_LOCK *psWorkEstHashLock)
{
	if(*psWorkEstHashLock == NULL)
	{
		OSLockCreate(psWorkEstHashLock, LOCK_TYPE_DISPATCH);
	}
	return;
}

void WorkEstHashLockDestroy(POS_LOCK sWorkEstHashLock)
{
	if(sWorkEstHashLock != NULL)
	{
		OSLockDestroy(sWorkEstHashLock);
		sWorkEstHashLock = NULL;
	}
	return;
}


PVRSRV_ERROR WorkEstClearBuffer(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	RGXFWIF_KCCB_CMD		sGPCCBCmd;
	PVRSRV_ERROR			eError;
	sGPCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_WORKEST_CLEAR_BUFFER;

	/* Submit command to the firmware. */
	LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
	{
		eError = RGXScheduleCommand(psDevInfo,
									RGXFWIF_DM_GP,
									&sGPCCBCmd,
									sizeof(sGPCCBCmd),
									0,
									IMG_TRUE);
		if (eError != PVRSRV_ERROR_RETRY)
		{
			break;
		}
		OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
	} END_LOOP_UNTIL_TIMEOUT();
	
	return PVRSRV_OK;
}
