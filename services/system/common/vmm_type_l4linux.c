/*************************************************************************/ /*!
@File			vmm_type_l4linux.c
@Title          Fiasco.OC L4LINUX VM manager type
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Fiasco.OC L4LINUX VM manager implementation
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
#include "pvrsrv.h"
#include "img_types.h"
#include "pvrsrv_error.h"
#include "rgxheapconfig.h"

#include "pvrsrv_vz.h"
#include "vz_physheap.h"

#include "vmm_impl.h"
#if defined(PVRSRV_GPUVIRT_GUESTDRV)
#include "vmm_impl_client.h"
#else
#include "vmm_impl_server.h"
#include "vmm_pvz_server.h"
#endif

/* Valid values for the TC_MEMORY_CONFIG configuration option */
#define TC_MEMORY_LOCAL			(1)
#define TC_MEMORY_HOST			(2)
#define TC_MEMORY_HYBRID		(3)

/*
	Fiasco.OC/L4Linux setup:
		- No explicit para-virtz call support (yet)
		- Therefore, setup is performed at build-time
		- Use identical GPU/FW heap sizes for HOST/GUEST
		- 0 <=(FW/HEAP)=> xMB <=(GPU/HEAP)=> yMB
			- xMB = All OSID firmware heaps
			- yMB = All OSID graphics heaps
	
	Supported platforms:
		ARM32/x86 UMA 
			- DMA memory map:	[0<=(CPU/RAM)=>512MB<=(GPU/RAM)=>1GB]
			- ARM32-VEXPRESS:	512MB offset @ 0x80000000
			- X86-PC:			512MB offset @ 0x20070000

		x86 LMA 
			- Local memory map:	[0<=(GPU/GRAM)=>1GB]
			- X86-PC:			0 offset @ 0x00000000
*/
#ifndef TC_MEMORY_CONFIG
	#if defined (CONFIG_ARM)
		#define FWHEAP_BASE		0x80000000
	#else
		#define FWHEAP_BASE		0xDEADBEEF
	#endif
#else
	#if (TC_MEMORY_CONFIG == TC_MEMORY_LOCAL)
		#define FWHEAP_BASE		0x0
	#elif (TC_MEMORY_CONFIG == TC_MEMORY_HOST)
		#define FWHEAP_BASE		0x20070000
	#elif (TC_MEMORY_CONFIG == TC_MEMORY_HYBRID)
		#error "TC_MEMORY_HYBRID: Not supported"
	#endif
#endif

#define VZ_FWHEAP_SIZE		(RGX_FIRMWARE_HEAP_SIZE)
#define VZ_FWHEAP_BASE		(FWHEAP_BASE+(VZ_FWHEAP_SIZE*PVRSRV_GPUVIRT_OSID))

#define VZ_GPUHEAP_SIZE		(1<<26)
#define VZ_GPUHEAP_BASE		((FWHEAP_BASE+(VZ_FWHEAP_SIZE*PVRSRV_GPUVIRT_NUM_OSID))+(VZ_GPUHEAP_SIZE*PVRSRV_GPUVIRT_OSID))

static PVRSRV_ERROR
L4LinuxVmmpGetDevPhysHeapOrigin(PVRSRV_DEVICE_CONFIG *psDevConfig,
								PVRSRV_DEVICE_PHYS_HEAP eHeap,
								PVRSRV_DEVICE_PHYS_HEAP_ORIGIN *peOrigin)
{
	PVR_UNREFERENCED_PARAMETER(psDevConfig);
	PVR_UNREFERENCED_PARAMETER(eHeap);
	*peOrigin = PVRSRV_DEVICE_PHYS_HEAP_ORIGIN_HOST;
	return PVRSRV_OK; 
}

static PVRSRV_ERROR
L4LinuxVmmGetDevPhysHeapAddrSize(PVRSRV_DEVICE_CONFIG *psDevConfig,
								 PVRSRV_DEVICE_PHYS_HEAP eHeap,
								 IMG_UINT64 *pui64Size,
								 IMG_UINT64 *pui64Addr)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVR_UNREFERENCED_PARAMETER(eHeap);
	PVR_UNREFERENCED_PARAMETER(psDevConfig);

	switch (eHeap)
	{
		case PVRSRV_DEVICE_PHYS_HEAP_FW_LOCAL:
			*pui64Size = VZ_FWHEAP_SIZE;
			*pui64Addr = VZ_FWHEAP_BASE;
			break;

		case PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL:
			*pui64Size = VZ_GPUHEAP_SIZE;
			*pui64Addr = VZ_GPUHEAP_BASE;
			break;

		default:
			*pui64Size = 0;
			*pui64Addr = 0;
			eError = PVRSRV_ERROR_NOT_IMPLEMENTED;
			PVR_ASSERT(0);
			break;
	}

	return eError;
}

#if defined(PVRSRV_GPUVIRT_GUESTDRV)
static PVRSRV_ERROR
L4LinuxVmmCreateDevConfig(IMG_UINT32 ui32FuncID,
						  IMG_UINT32 ui32DevID,
						  IMG_UINT32 *pui32IRQ,
						  IMG_UINT32 *pui32RegsSize,
						  IMG_UINT64 *pui64RegsCpuPBase)
{
	PVR_UNREFERENCED_PARAMETER(ui32FuncID);
	PVR_UNREFERENCED_PARAMETER(ui32DevID);
	PVR_UNREFERENCED_PARAMETER(pui32IRQ);
	PVR_UNREFERENCED_PARAMETER(pui32RegsSize);
	PVR_UNREFERENCED_PARAMETER(pui64RegsCpuPBase);
	return PVRSRV_ERROR_NOT_IMPLEMENTED;
}

static PVRSRV_ERROR
L4LinuxVmmDestroyDevConfig(IMG_UINT32 ui32FuncID,
						   IMG_UINT32 ui32DevID)
{
	PVR_UNREFERENCED_PARAMETER(ui32FuncID);
	PVR_UNREFERENCED_PARAMETER(ui32DevID);
	return PVRSRV_ERROR_NOT_IMPLEMENTED;
}

static PVRSRV_ERROR
L4LinuxVmmCreateDevPhysHeaps(IMG_UINT32 ui32FuncID,
							 IMG_UINT32 ui32DevID,
							 IMG_UINT32 *peType,
							 IMG_UINT64 *pui64FwSize,
							 IMG_UINT64 *pui64FwAddr,
							 IMG_UINT64 *pui64GpuSize,
							 IMG_UINT64 *pui64GpuAddr)
{
	PVR_UNREFERENCED_PARAMETER(ui32FuncID);
	PVR_UNREFERENCED_PARAMETER(ui32DevID);
	PVR_UNREFERENCED_PARAMETER(peType);
	PVR_UNREFERENCED_PARAMETER(pui64FwSize);
	PVR_UNREFERENCED_PARAMETER(pui64FwAddr);
	PVR_UNREFERENCED_PARAMETER(pui64GpuSize);
	PVR_UNREFERENCED_PARAMETER(pui64GpuAddr);
	return PVRSRV_ERROR_NOT_IMPLEMENTED;
}

static PVRSRV_ERROR
L4LinuxVmmDestroyDevPhysHeaps(IMG_UINT32 ui32FuncID,
							  IMG_UINT32 ui32DevID)
{
	PVR_UNREFERENCED_PARAMETER(ui32FuncID);
	PVR_UNREFERENCED_PARAMETER(ui32DevID);
	return PVRSRV_ERROR_NOT_IMPLEMENTED;
}

static PVRSRV_ERROR
L4LinuxVmmMapDevPhysHeap(IMG_UINT32 ui32FuncID,
						 IMG_UINT32 ui32DevID,
						 IMG_UINT64 ui64Size,
						 IMG_UINT64 ui64Addr)
{
	PVR_UNREFERENCED_PARAMETER(ui32FuncID);
	PVR_UNREFERENCED_PARAMETER(ui32DevID);
	PVR_UNREFERENCED_PARAMETER(ui64Size);
	PVR_UNREFERENCED_PARAMETER(ui64Addr);
	return PVRSRV_ERROR_NOT_IMPLEMENTED;
}

static PVRSRV_ERROR
L4LinuxVmmUnmapDevPhysHeap(IMG_UINT32 ui32FuncID,
						   IMG_UINT32 ui32DevID)
{
	PVR_UNREFERENCED_PARAMETER(ui32FuncID);
	PVR_UNREFERENCED_PARAMETER(ui32DevID);
	return PVRSRV_ERROR_NOT_IMPLEMENTED;
}
static VMM_PVZ_CONNECTION gsL4LinuxVmmPvz =
{
	.sHostFuncTab = {
		/* pfnCreateDevConfig */
		&L4LinuxVmmCreateDevConfig,

		/* pfnDestroyDevConfig */
		&L4LinuxVmmDestroyDevConfig,

		/* pfnCreateDevPhysHeaps */
		&L4LinuxVmmCreateDevPhysHeaps,

		/* pfnDestroyDevPhysHeaps */
		&L4LinuxVmmDestroyDevPhysHeaps,

		/* pfnMapDevPhysHeap */
		&L4LinuxVmmMapDevPhysHeap,

		/* pfnUnmapDevPhysHeap */
		&L4LinuxVmmUnmapDevPhysHeap
	},

	.sConfigFuncTab = {
		/* pfnGetDevPhysHeapOrigin */
		&L4LinuxVmmpGetDevPhysHeapOrigin,

		/* pfnGetGPUDevPhysHeap */
		&L4LinuxVmmGetDevPhysHeapAddrSize
	}
};
#else /* defined(PVRSRV_GPUVIRT_GUESTDRV) */
static VMM_PVZ_CONNECTION gsL4LinuxVmmPvz =
{
	.sGuestFuncTab = {
		/* pfnCreateDevConfig */
		&PvzServerCreateDevConfig,

		/* pfnDestroyDevConfig */
		&PvzServerDestroyDevConfig,

		/* pfnCreateDevPhysHeaps */
		&PvzServerCreateDevPhysHeaps,

		/* pfnDestroyDevPhysHeaps */
		PvzServerDestroyDevPhysHeaps,

		/* pfnMapDevPhysHeap */
		&PvzServerMapDevPhysHeap,

		/* pfnUnmapDevPhysHeap */
		&PvzServerUnmapDevPhysHeap
	},

	.sConfigFuncTab = {
		/* pfnGetDevPhysHeapOrigin */
		&L4LinuxVmmpGetDevPhysHeapOrigin,

		/* pfnGetDevPhysHeapAddrSize */
		&L4LinuxVmmGetDevPhysHeapAddrSize
	},

	.sVmmFuncTab = {
		/* pfnOnVmOnline */
		&PvzServerOnVmOnline,

		/* pfnOnVmOffline */
		&PvzServerOnVmOffline,

		/* pfnVMMConfigure */
		&PvzServerVMMConfigure
	}
};
#endif

PVRSRV_ERROR VMMCreatePvzConnection(VMM_PVZ_CONNECTION **psPvzConnection)
{
	PVR_ASSERT(psPvzConnection);
	*psPvzConnection = &gsL4LinuxVmmPvz;
	return PVRSRV_OK;
}

void VMMDestroyPvzConnection(VMM_PVZ_CONNECTION *psPvzConnection)
{
	PVR_ASSERT(psPvzConnection == &gsL4LinuxVmmPvz);
}

/******************************************************************************
 End of file (vmm_type_l4linux.c)
******************************************************************************/
