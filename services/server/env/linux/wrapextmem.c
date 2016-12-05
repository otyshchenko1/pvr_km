/*************************************************************************/ /*!
@File           wrapextmem.c
@Title          PMR Malloc memory wrap
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
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

#include <linux/version.h>

#include <asm/page.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/sched.h>

#include "img_types.h"
#include "pvr_debug.h"
#include "pvrsrv_error.h"
#include "pvrsrv_memallocflags.h"

#include "allocmem.h"
#include "osfunc.h"
#include "pvrsrv.h"
#include "pdump_physmem.h"
#include "pmr.h"
#include "pmr_impl.h"


#include "img_defs.h"
#include "pvr_debug.h"
#include "pmr.h"
#include "allocmem.h"

#include "wrapextmem.h"

#include "sysinfo.h"

typedef enum _eWrapMemType_
{
	WRAP_TYPE_NULL = 0,
	WRAP_TYPE_GET_USER_PAGES,
	WRAP_TYPE_FIND_VMA
} eWrapMemType;


typedef struct _PMR_WRAPEXT_DATA_
{
	eWrapMemType		eType;

	void			*pvCPUVAddr;
	IMG_DEVMEM_SIZE_T	uiOffset;
	IMG_DEVMEM_SIZE_T	uiSize;
	IMG_INT32		i32PageCount;
	IMG_INT32		i32NumPageMapped;
#if defined(CONFIG_ARM_LPAE) || defined(CONFIG_ARCH_PHYS_ADDR_T_64BIT)
	IMG_UINT64		*pasPhysAddr;
#else
	IMG_UINT32		*pasPhysAddr;
#endif
	struct page		**ppsPages;
	PHYS_HEAP		*psPhysHeap;
	IMG_BOOL		*pabFakeMappingTable;
	/*
	  for pdump...
	*/
	IMG_BOOL		bPDumpMalloced;
	IMG_HANDLE		hPDumpAllocInfo;
} PMR_WRAPEXT_DATA;

#if defined (__x86_64__)
	#define UINTPTR_FMT	"%016lX"
#else
	#define UINTPTR_FMT	"%08lX"
#endif
#define SIZE_T_FMT_LEN	"z"


/*****************************************************************************
 *                       Static utility functions                            *
 *****************************************************************************/

static IMG_BOOL CPUVAddrToPFN(struct vm_area_struct *psVMArea, uintptr_t uCPUVAddr, IMG_UINT32 *pui32PFN, struct page **ppsPage)
{
	pgd_t *psPGD;
	pud_t *psPUD;
	pmd_t *psPMD;
	pte_t *psPTE;
	struct mm_struct *psMM = psVMArea->vm_mm;
	spinlock_t *psPTLock;
	IMG_BOOL bRet = IMG_FALSE;

	*pui32PFN = 0;
	*ppsPage = NULL;

	psPGD = pgd_offset(psMM, uCPUVAddr);
	if (pgd_none(*psPGD) || pgd_bad(*psPGD))
		return bRet;

	psPUD = pud_offset(psPGD, uCPUVAddr);
	if (pud_none(*psPUD) || pud_bad(*psPUD))
		return bRet;

	psPMD = pmd_offset(psPUD, uCPUVAddr);
	if (pmd_none(*psPMD) || pmd_bad(*psPMD))
		return bRet;

	psPTE = (pte_t *)pte_offset_map_lock(psMM, psPMD, uCPUVAddr, &psPTLock);

#if !(defined(CONFIG_ARM_LPAE) || defined(CONFIG_ARCH_PHYS_ADDR_T_64BIT))
	if ((pte_none(*psPTE) == 0) && (pte_present(*psPTE) != 0) && (pte_write(*psPTE) != 0))
#else
	if ((pte_none(*psPTE) == 0) && (pte_present(*psPTE) != 0))
#endif
	{
		*pui32PFN = pte_pfn(*psPTE);
		bRet = IMG_TRUE;

		if (pfn_valid(*pui32PFN))
		{
			*ppsPage = pfn_to_page(*pui32PFN);

			get_page(*ppsPage);
		}
	}

	pte_unmap_unlock(psPTE, psPTLock);

	return bRet;
}


static PVRSRV_ERROR OSAcquirePhysPageAddr(PMR_WRAPEXT_DATA  *psAllocPriv)
{
	uintptr_t uStartAddrOrig = (uintptr_t)psAllocPriv->pvCPUVAddr;
	size_t uAddrRangeOrig = psAllocPriv->uiSize;
	uintptr_t uBeyondEndAddrOrig = uStartAddrOrig + uAddrRangeOrig;
	uintptr_t uStartAddr;
	uintptr_t uBeyondEndAddr;
	uintptr_t uAddr;
	IMG_INT i;
	struct vm_area_struct *psVMArea;
	IMG_BOOL bHavePageStructs = IMG_FALSE;
	IMG_BOOL bHaveNoPageStructs = IMG_FALSE;
	IMG_BOOL bMMapSemHeld = IMG_FALSE;
	PVRSRV_ERROR eError = PVRSRV_ERROR_OUT_OF_MEMORY;

	/* Align start and end addresses to page boundaries */
	uStartAddr = uStartAddrOrig & PAGE_MASK;
	uBeyondEndAddr = PAGE_ALIGN(uBeyondEndAddrOrig);

	/*
	 * Check for address range calculation overflow, and attempts to wrap
	 * zero bytes.
	 */
	if (uBeyondEndAddr <= uStartAddr)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"OSAcquirePhysPageAddr: Invalid address range (start " UINTPTR_FMT  ", length %" SIZE_T_FMT_LEN "x)",
				uStartAddrOrig, uAddrRangeOrig));
		goto error;
	}

	/* Allocate physical address array */
	psAllocPriv->pasPhysAddr = OSAllocMem((size_t)psAllocPriv->i32PageCount * sizeof(*psAllocPriv->pasPhysAddr));
	if (psAllocPriv->pasPhysAddr == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"OSAcquirePhysPageAddr: Couldn't allocate page array"));            
		goto error;
	}
	memset(psAllocPriv->pasPhysAddr, 0, (size_t)psAllocPriv->i32PageCount * sizeof(*psAllocPriv->pasPhysAddr));

	/* Allocate page array */
	psAllocPriv->ppsPages = OSAllocMem((size_t)psAllocPriv->i32PageCount * sizeof(*psAllocPriv->ppsPages));
	if (psAllocPriv->ppsPages == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"OSAcquirePhysPageAddr: Couldn't allocate page array"));            
		goto error;
	}
	memset(psAllocPriv->ppsPages, 0, (size_t)psAllocPriv->i32PageCount * sizeof(*psAllocPriv->ppsPages));

	/* Default error code from now on */
	eError = PVRSRV_ERROR_BAD_MAPPING;

	/* Set the mapping type to aid clean up */
	psAllocPriv->eType = WRAP_TYPE_GET_USER_PAGES;

	/* Lock down user memory */
	down_read(&current->mm->mmap_sem);
	bMMapSemHeld = IMG_TRUE;

	/* Get page list */
	psAllocPriv->i32NumPageMapped = get_user_pages(
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0))
				current, current->mm,
#endif
				uStartAddr, psAllocPriv->i32PageCount, 1, 0, psAllocPriv->ppsPages, NULL);
	if (psAllocPriv->i32NumPageMapped >= 0)
	{
		/* See if we got all the pages we wanted */
		if (psAllocPriv->i32NumPageMapped != psAllocPriv->i32PageCount)
		{
			PVR_TRACE(("OSAcquirePhysPageAddr: Couldn't map all the pages needed (wanted: %d, got %d)",
					psAllocPriv->i32PageCount, psAllocPriv->i32NumPageMapped));
			goto error;
		}

		/* Build list of physical page addresses */
		for (i = 0; i < psAllocPriv->i32PageCount; i++)
		{
			IMG_CPU_PHYADDR CPUPhysAddr;
			IMG_UINT32 ui32PFN;

			ui32PFN = page_to_pfn(psAllocPriv->ppsPages[i]);
#if defined(CONFIG_ARM_LPAE) || defined(CONFIG_ARCH_PHYS_ADDR_T_64BIT)
			CPUPhysAddr.uiAddr = __pfn_to_phys(ui32PFN);
			if (__phys_to_pfn(CPUPhysAddr.uiAddr) != ui32PFN)
#else
			CPUPhysAddr.uiAddr = ui32PFN << PAGE_SHIFT;
			if ((CPUPhysAddr.uiAddr >> PAGE_SHIFT) != ui32PFN)
#endif
			{
				PVR_DPF((PVR_DBG_ERROR,
						"OSAcquirePhysPageAddr: Page frame number out of range (%x)", ui32PFN));
				goto error;
			}

			psAllocPriv->pasPhysAddr[i] = CPUPhysAddr.uiAddr;
		}

		goto exit;
	}

	PVR_DPF((PVR_DBG_MESSAGE, "OSAcquirePhysPageAddr: get_user_pages failed (%d), using CPU page table", psAllocPriv->i32NumPageMapped));
    
	/* Reset some fields */
	psAllocPriv->eType = WRAP_TYPE_NULL;
	psAllocPriv->i32NumPageMapped = 0;
	memset(psAllocPriv->ppsPages, 0, (size_t)psAllocPriv->i32PageCount * sizeof(*psAllocPriv->ppsPages));

	/*
	 * get_user_pages didn't work.  If this is due to the address range
	 * representing memory mapped I/O, then we'll look for the pages
	 * in the appropriate memory region of the process.
	 */

	/* Set the mapping type to aid clean up */
	psAllocPriv->eType = WRAP_TYPE_FIND_VMA;

	psVMArea = find_vma(current->mm, uStartAddrOrig);
	if (psVMArea == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"OSAcquirePhysPageAddr: Couldn't find memory region containing start address " UINTPTR_FMT, 
				uStartAddrOrig));
		goto error;
	}

	/*
	 * find_vma locates a region with an end point past a given
	 * virtual address.  So check the address is actually in the region.
	 */
	if (uStartAddrOrig < psVMArea->vm_start)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"OSAcquirePhysPageAddr: Start address " UINTPTR_FMT " is outside of the region returned by find_vma", 
				uStartAddrOrig));
		goto error;
	}

	/* Now check the end address is in range */
	if (uBeyondEndAddrOrig > psVMArea->vm_end)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"OSAcquirePhysPageAddr: End address " UINTPTR_FMT " is outside of the region returned by find_vma",
				uBeyondEndAddrOrig));
		goto error;
	}

	/* Does the region represent memory mapped I/O? */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0))
	if ((psVMArea->vm_flags & (VM_IO | VM_RESERVED)) != (VM_IO | VM_RESERVED))
#else
	if ((psVMArea->vm_flags & (VM_IO | VM_DONTDUMP)) != (VM_IO | VM_DONTDUMP))
#endif
	{
		PVR_DPF((PVR_DBG_ERROR,
				"OSAcquirePhysPageAddr: Memory region does not represent memory mapped I/O (VMA flags: 0x%lx)", psVMArea->vm_flags));
		goto error;
	}

	/* We require read and write access */
	if ((psVMArea->vm_flags & (VM_READ | VM_WRITE)) != (VM_READ | VM_WRITE))
	{
		PVR_DPF((PVR_DBG_ERROR,
				"OSAcquirePhysPageAddr: No read/write access to memory region (VMA flags: 0x%lx)", psVMArea->vm_flags));
		goto error;
	}

	for (uAddr = uStartAddrOrig, i = 0; uAddr < uBeyondEndAddrOrig; uAddr += PAGE_SIZE, i++)
	{
		IMG_CPU_PHYADDR CPUPhysAddr;
		IMG_UINT32 ui32PFN = 0;

		PVR_ASSERT(i < psAllocPriv->i32PageCount);

		if (!CPUVAddrToPFN(psVMArea, uAddr, &ui32PFN, &psAllocPriv->ppsPages[i]))
		{
			PVR_DPF((PVR_DBG_ERROR,
				"OSAcquirePhysPageAddr: Invalid CPU virtual address"));
			goto error;
		}
		if (psAllocPriv->ppsPages[i] == NULL)
		{
            		bHaveNoPageStructs = IMG_TRUE;
		}
		else
		{
			bHavePageStructs = IMG_TRUE;
			psAllocPriv->i32NumPageMapped++;

			PVR_ASSERT(ui32PFN == page_to_pfn(psAllocPriv->ppsPages[i]));
		}

#if defined(CONFIG_ARM_LPAE) || defined(CONFIG_ARCH_PHYS_ADDR_T_64BIT)
		CPUPhysAddr.uiAddr = __pfn_to_phys(ui32PFN);
		if (__phys_to_pfn(CPUPhysAddr.uiAddr) != ui32PFN)
#else
		CPUPhysAddr.uiAddr = ui32PFN << PAGE_SHIFT;
		if ((CPUPhysAddr.uiAddr >> PAGE_SHIFT) != ui32PFN)
#endif
		{
			PVR_DPF((PVR_DBG_ERROR,
					"OSAcquirePhysPageAddr: Page frame number out of range (%x)", ui32PFN));
			goto error;
		}

		psAllocPriv->pasPhysAddr[i] = CPUPhysAddr.uiAddr;
	}
	PVR_ASSERT(i ==  psAllocPriv->i32PageCount);

#if defined(VM_MIXEDMAP)
	if ((psVMArea->vm_flags & VM_MIXEDMAP) != 0)
	{
		goto exit;
	}
#endif

	if (bHavePageStructs && bHaveNoPageStructs)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"OSAcquirePhysPageAddr: Region is VM_MIXEDMAP, but isn't marked as such"));
		goto error;
	}

	if (!bHaveNoPageStructs)
	{
		/* The ideal case; every page has a page structure */
		goto exit;
	}

#if defined(VM_PFNMAP)
	if ((psVMArea->vm_flags & VM_PFNMAP) == 0)
#endif
	{
		PVR_DPF((PVR_DBG_ERROR,
				"OSAcquirePhysPageAddr: Region is VM_PFNMAP, but isn't marked as such"));
		goto error;
	}

exit:
	PVR_ASSERT(bMMapSemHeld);
	up_read(&current->mm->mmap_sem);

	if (bHaveNoPageStructs)
	{
		PVR_DPF((PVR_DBG_MESSAGE,
				"OSAcquirePhysPageAddr: Region contains pages which can't be locked down (no page structures)"));
	}

	PVR_ASSERT(psAllocPriv->eType != 0);

	return PVRSRV_OK;

error:
	if (bMMapSemHeld)
	{
		up_read(&current->mm->mmap_sem);
	}

	PVR_ASSERT(eError != PVRSRV_OK);

	return eError;
}

static PVRSRV_ERROR OSReleasePhysPageAddr(PMR_WRAPEXT_DATA  *psAllocPriv)
{
	IMG_INT i;

	if (psAllocPriv == NULL)
	{
		PVR_DPF((PVR_DBG_WARNING,
				"OSReleasePhysPageAddr: called with null wrap handle"));
		return PVRSRV_OK;
	}

	switch (psAllocPriv->eType)
	{
		case WRAP_TYPE_NULL:
		{
			PVR_DPF((PVR_DBG_WARNING,
					"OSReleasePhysPageAddr: called with wrap type WRAP_TYPE_NULL"));
			break;
		}
		case WRAP_TYPE_GET_USER_PAGES:
		{
			for (i = 0; i < psAllocPriv->i32NumPageMapped; i++)
			{
				struct page *psPage = psAllocPriv->ppsPages[i];

				PVR_ASSERT(psPage != NULL);

				/*
				 * If the number of pages mapped is not the same as
				 * the number of pages in the address range, then
				 * get_user_pages must have failed, so we are cleaning
				 * up after failure, and the pages can't be dirty.
				 */
				if (psAllocPriv->i32NumPageMapped == psAllocPriv->i32PageCount)
				{
					if (!PageReserved(psPage))
					{
						SetPageDirty(psPage);
					}
				}
				put_page(psPage);
			}
			break;
		}
		case WRAP_TYPE_FIND_VMA:
		{
			for (i = 0; i < psAllocPriv->i32PageCount; i++)
			{
				if (psAllocPriv->ppsPages[i] != NULL)
				{
					put_page(psAllocPriv->ppsPages[i]);
				}
			}
			break;
		}
		default:
		{
			PVR_DPF((PVR_DBG_ERROR,
					"OSReleasePhysPageAddr: Unknown wrap type (%d)", psAllocPriv->eType));
			return PVRSRV_ERROR_INVALID_WRAP_TYPE;
		}
	}

	if (psAllocPriv->ppsPages != NULL)
	{
		OSFreeMem(psAllocPriv->ppsPages);
	}

	if (psAllocPriv->pasPhysAddr != NULL)
	{
		OSFreeMem(psAllocPriv->pasPhysAddr);
	}

	return PVRSRV_OK;
}

/*****************************************************************************
 *                       PMR callback functions                              *
 *****************************************************************************/


static PVRSRV_ERROR
PMRFinalizeWrap(PMR_IMPL_PRIVDATA pvPriv)
{
	PMR_WRAPEXT_DATA * psAllocPriv = pvPriv;

	PVR_ASSERT(pvPriv != NULL);

	/* Free mapping table
	   TODO: check if it is safe to free this now */
	OSFreeMem(psAllocPriv->pabFakeMappingTable);

	/* Release phys heap */
	PhysHeapRelease(psAllocPriv->psPhysHeap);

	/* Unlock and release PFN */
	OSReleasePhysPageAddr(psAllocPriv);

	/* Free kernel mapping, if MEM_RELEASE flag is specified, then
	   supplied size must be zero */

	/* Free private data */
	OSFreeMem(psAllocPriv);

	return PVRSRV_OK;
}


static PVRSRV_ERROR
PMRLockPhysAddressesWrap(PMR_IMPL_PRIVDATA pvPriv)
{
	/* Lock and unlock function for physical address
		don't do anything as we aqcuired the physical
		address at create time. */
	PVR_UNREFERENCED_PARAMETER(pvPriv);
	return PVRSRV_OK;
}

static PVRSRV_ERROR
PMRUnlockPhysAddressesWrap(PMR_IMPL_PRIVDATA pvPriv)
{
	PVR_UNREFERENCED_PARAMETER(pvPriv);
	return PVRSRV_OK;
}

static PVRSRV_ERROR
PMRDevPhysAddrWrap(PMR_IMPL_PRIVDATA pvPriv,
				IMG_UINT32 ui32NumOfAddr,
				IMG_DEVMEM_OFFSET_T *puiOffset,
				IMG_BOOL *pbValid,
				IMG_DEV_PHYADDR *psDevAddrPtr)
{
#if 1
	PMR_WRAPEXT_DATA * psPrivData = pvPriv;	
	size_t uiPageNum;
	off_t uiInPageOffset;
	IMG_UINT32 idx;

	for (idx = 0; idx < ui32NumOfAddr; idx++)
	{
		if (pbValid[idx])
		{
			uiPageNum = puiOffset[idx] >> PAGE_SHIFT;
			uiInPageOffset = offset_in_page(puiOffset[idx]);

			PVR_ASSERT(uiPageNum < psPrivData->i32PageCount);

			psDevAddrPtr[idx].uiAddr = psPrivData->pasPhysAddr[uiPageNum];
			psDevAddrPtr[idx].uiAddr += uiInPageOffset;
		}
	}
#else
	IMG_INT32 index;
	PMR_WRAPEXT_DATA * psPrivData = pvPriv;	

	PVR_ASSERT(pvPriv != NULL);

	if (uiOffset >= (psPrivData->i32PageCount * OSGetPageSize()))
	{
		/* This checks whether the requested map-offset into the underlying
		   PMR pages using the mapped-size and _not_ the actual-size as the
		   limit is out of range */
		PVR_DBG_BREAK;
		return PVRSRV_ERROR_PMR_INVALID_CHUNK;
	}	

	/* Calculate requested offset to pfn-index, taking possible non-alignment offset
	   into consideration and return the shifted-page physical address */
	index = (IMG_INT32) (psPrivData->uiOffset + uiOffset) >> OSGetPageShift();
	psDevPAddr->uiAddr = psPrivData->pasPhysAddr[index];
#endif

	return PVRSRV_OK;
}

static PVRSRV_ERROR
PMRAcquireKernelMappingDataWrap(PMR_IMPL_PRIVDATA pvPriv,
							   size_t uiOffset,
							   size_t uiSize,
							   void **ppvKernelAddressOut,
							   IMG_HANDLE *phHandleOut,
							   PMR_FLAGS_T ulFlags)
{
	PMR_WRAPEXT_DATA * psPrivData = pvPriv;

	PVR_UNREFERENCED_PARAMETER(ulFlags);
	PVR_UNREFERENCED_PARAMETER(phHandleOut);

	PVR_ASSERT(pvPriv != NULL);
	PVR_ASSERT(ppvKernelAddressOut != NULL);


	if ((uiOffset + uiSize) >= psPrivData->uiSize)
	{
		/* This checks whether the requested map-offset into the underlying
		   PMR pages, using the actual-size _not_ the mapped-size has the
		   limit is out of range */

		/* TODO: Kernel mapping can be acquired multiple times
		   so we should use a reference counter here */
		PVR_DBG_BREAK;
		return PVRSRV_ERROR_PMR_INVALID_CHUNK;
	}

	/* We must return the offset'ed (requested + allocation) address */
	*ppvKernelAddressOut = (void *)(
							(unsigned long)psPrivData->pvCPUVAddr +
							(unsigned long)psPrivData->uiOffset +
							(unsigned long)uiOffset
						);
	return PVRSRV_OK;
}

static void PMRReleaseKernelMappingDataWrap(PMR_IMPL_PRIVDATA pvPriv,
											   IMG_HANDLE hHandle)
{
	/* Do nothing, release the mapping in finalize */
	PVR_UNREFERENCED_PARAMETER(pvPriv);
	PVR_UNREFERENCED_PARAMETER(hHandle);
}

static PMR_IMPL_FUNCTAB _sPMRWrapFuncTab =
{
	.pfnLockPhysAddresses		= PMRLockPhysAddressesWrap,
	.pfnUnlockPhysAddresses		= PMRUnlockPhysAddressesWrap,
	.pfnDevPhysAddr			= PMRDevPhysAddrWrap,
	.pfnAcquireKernelMappingData	= PMRAcquireKernelMappingDataWrap,
	.pfnReleaseKernelMappingData	= PMRReleaseKernelMappingDataWrap,
	.pfnReadBytes			= NULL,
	.pfnWriteBytes			= NULL,
	.pfnUnpinMem			= NULL,
	.pfnPinMem			= NULL,
	.pfnChangeSparseMem		= NULL,
	.pfnChangeSparseMemCPUMap	= NULL,
	.pfnMMap			= NULL,
	.pfnFinalize			= PMRFinalizeWrap,
};


/*****************************************************************************
 *                       Public facing interface                             *
 *****************************************************************************/

PVRSRV_ERROR WrapExtMemKM(PVRSRV_DEVICE_NODE *psDevNode,
						  void ** pvUserVA,
						  IMG_DEVMEM_SIZE_T uiLength,
						  PVRSRV_MEMALLOCFLAGS_T Flags,
						  PMR **ppsPMRPtr,
						  IMG_DEVMEM_SIZE_T *puiMappingLengthOut)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
//	void *pvKernelVA = NULL;
	PMR_WRAPEXT_DATA * psAllocPriv = NULL;
	IMG_DEVMEM_OFFSET_T uiOffset;
	PMR_FLAGS_T uiPMRFlags;
	size_t ui32Size;
	IMG_INT i = 0;

	/*
	 * User VA may be non-page-alligned, so get page-offset
	 * as kernel-mapped-wraping will be done on page-boundary
	 */
	uiOffset = (IMG_DEVMEM_OFFSET_T)((unsigned long)pvUserVA & OSGetPageMask());

	/* Allocate private data for the PMR */
	psAllocPriv = OSAllocMem(sizeof(PMR_WRAPEXT_DATA));
	if(psAllocPriv == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto failAllocPrivData;
	}

	/* Populate private data */
	OSMemSet(psAllocPriv, 0, sizeof(PMR_WRAPEXT_DATA));
	psAllocPriv->pvCPUVAddr = pvUserVA;
	psAllocPriv->uiOffset = uiOffset;
	psAllocPriv->uiSize = uiLength;

	/* Calculate the required size needed for locking */
	ui32Size = (size_t)psAllocPriv->uiOffset +
			   (size_t)psAllocPriv->uiSize;

	/* Convert byte-size to page-count */
	psAllocPriv->i32PageCount = ui32Size >> OSGetPageShift();
	if (ui32Size & OSGetPageMask())
	{
		/* Add an extra page if ui32Size % PAGE_SIZE */
		psAllocPriv->i32PageCount += 1;
	}

	/* Lock pages and get PFNs of the system memory*/
	eError = OSAcquirePhysPageAddr(psAllocPriv);
	if (eError != PVRSRV_OK)
	{
		goto failAcquirePhysAddr; 	
	}

	uiPMRFlags = Flags & PVRSRV_MEMALLOCFLAGS_PMRFLAGSMASK;

	/* SYS_PHYS_HEAP_ID_WRAP is Heap ID for WRAP, this is defined in renesas.mk */
	eError = PhysHeapAcquire(SYS_PHYS_HEAP_ID_WRAP, &psAllocPriv->psPhysHeap);
	if (eError != PVRSRV_OK)
	{
		goto failPhysHeapAcquire;
	}

	/* We need to indicated virt-to-phys per-page validility */
	psAllocPriv->pabFakeMappingTable = OSAllocMem(sizeof(IMG_BOOL) * psAllocPriv->i32PageCount);
	if (psAllocPriv->pabFakeMappingTable == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto failAllocMappingTable;
	}

	/* Fake mapping table */
	for (i = 0; i < psAllocPriv->i32PageCount; i++)
	{
		psAllocPriv->pabFakeMappingTable[i] = IMG_TRUE;
	}

	eError = PMRCreatePMR(psDevNode,
						psAllocPriv->psPhysHeap, 
						(size_t)(psAllocPriv->i32PageCount * OSGetPageSize()),
						OSGetPageSize(),
						psAllocPriv->i32PageCount,
						psAllocPriv->i32PageCount,
						psAllocPriv->pabFakeMappingTable,
						OSGetPageShift(),
						uiPMRFlags,
						"WRAP PDUMP",
						&_sPMRWrapFuncTab,
						psAllocPriv,
						ppsPMRPtr,
						NULL,	// XXX: dummy data
						IMG_FALSE	// XXX: dummy data
						);
	if (eError != PVRSRV_OK)
	{
		goto failCreatePMR;
	}

	*puiMappingLengthOut = (IMG_DEVMEM_SIZE_T)psAllocPriv->i32PageCount * OSGetPageSize();
	return PVRSRV_OK;

failCreatePMR:
	OSFreeMem(psAllocPriv->pabFakeMappingTable);
failAllocMappingTable:
	PhysHeapRelease(psAllocPriv->psPhysHeap);
failPhysHeapAcquire:
	OSReleasePhysPageAddr(psAllocPriv);
failAcquirePhysAddr:
	OSFreeMem(psAllocPriv);
failAllocPrivData:
	PVR_ASSERT(eError != PVRSRV_OK);

	return eError;
}


