/*************************************************************************/ /*!
@File
@Title          System Configuration
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
                Copyright (c) 2016 Renesas Electronics Corporation. All rights reserved.
@Description    System Configuration functions
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

#include <linux/clk.h>
#include <linux/pm_runtime.h>

#include <xen/xen.h>

#include "pvrsrv_device.h"
#include "syscommon.h"
#if defined(SUPPORT_PVRSRV_GPUVIRT)
#include "vz_support.h"
#endif
#include "allocmem.h"
#include "sysinfo.h"
#include "sysconfig.h"
#include "physheap.h"
#if defined(SUPPORT_ION)
#include "ion_support.h"
#endif
#include "interrupt_support.h"

/*
 * In systems that support trusted device address protection, there are three
 * physical heaps from which pages should be allocated:
 * - one heap for normal allocations
 * - one heap for allocations holding META code memory
 * - one heap for allocations holding secured DRM data
 */

#define PHYS_HEAP_IDX_GENERAL     0

#if defined(SUPPORT_TRUSTED_DEVICE)
#define PHYS_HEAP_IDX_TDFWCODE  1
#define PHYS_HEAP_IDX_TDSECUREBUF 2

#if defined(SUPPORT_PVRSRV_GPUVIRT)
#define PHYS_HEAP_IDX_VIRTFW      3
#endif

#elif defined(SUPPORT_PVRSRV_GPUVIRT)
#define PHYS_HEAP_IDX_VIRTFW      1
#endif

typedef struct _SYS_DATA_ SYS_DATA;

struct _SYS_DATA_
{
	struct platform_device *pdev;

	struct clk             *psRGX_FCK;

	IMG_HANDLE hSysLISRData;
	PFN_LISR pfnDeviceLISR;
	void *pvDeviceLISRData;
};

static SYS_DATA *gpsSysData = NULL;

/*
	CPU to Device physical address translation
*/
static
void xMAPhysHeapCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
								   IMG_UINT32 ui32NumOfAddr,
								   IMG_DEV_PHYADDR *psDevPAddr,
								   IMG_CPU_PHYADDR *psCpuPAddr)
{
	PVR_UNREFERENCED_PARAMETER(hPrivData);

	/* Optimise common case */
	psDevPAddr[0].uiAddr = psCpuPAddr[0].uiAddr;
	if (ui32NumOfAddr > 1)
	{
		IMG_UINT32 ui32Idx;
		for (ui32Idx = 1; ui32Idx < ui32NumOfAddr; ++ui32Idx)
		{
			psDevPAddr[ui32Idx].uiAddr = psCpuPAddr[ui32Idx].uiAddr;
		}
	}
}

/*
	Device to CPU physical address translation
*/
static
void xMAPhysHeapDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
								   IMG_UINT32 ui32NumOfAddr,
								   IMG_CPU_PHYADDR *psCpuPAddr,
								   IMG_DEV_PHYADDR *psDevPAddr)
{
	PVR_UNREFERENCED_PARAMETER(hPrivData);

	/* Optimise common case */
	psCpuPAddr[0].uiAddr = psDevPAddr[0].uiAddr;
	if (ui32NumOfAddr > 1)
	{
		IMG_UINT32 ui32Idx;
		for (ui32Idx = 1; ui32Idx < ui32NumOfAddr; ++ui32Idx)
		{
			psCpuPAddr[ui32Idx].uiAddr = psDevPAddr[ui32Idx].uiAddr;
		}
	}
}

static PHYS_HEAP_FUNCTIONS gsPhysHeapFuncs =
{
	/* pfnCpuPAddrToDevPAddr */
	xMAPhysHeapCpuPAddrToDevPAddr,
	/* pfnDevPAddrToCpuPAddr */
	xMAPhysHeapDevPAddrToCpuPAddr,
	/* pfnGetRegionId */
	NULL,
};

static PVRSRV_ERROR PhysHeapsCreate(PHYS_HEAP_CONFIG **ppasPhysHeapsOut,
									IMG_UINT32 *puiPhysHeapCountOut)
{
	PHYS_HEAP_CONFIG *pasPhysHeaps;
	static IMG_UINT32 uiHeapIDBase = 0;
	IMG_UINT32 ui32NextHeapID = 0;
	IMG_UINT32 uiHeapCount = 1;

#if defined(SUPPORT_TRUSTED_DEVICE)
	uiHeapCount += 2;
#endif

#if defined(SUPPORT_PVRSRV_GPUVIRT)
	uiHeapCount++;
#endif

	pasPhysHeaps = OSAllocZMem(sizeof(*pasPhysHeaps) * uiHeapCount);
	if (!pasPhysHeaps)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
#if defined(SUPPORT_FIXED_DEVICE_MEMORY)
	PVR_LOG(("GSX use fixed device memory. addr: 0x%llx, size: 0x%llx",
				SUPPORT_FIXED_DEVICE_MEMORY_ADDR, SUPPORT_FIXED_DEVICE_MEMORY_SIZE));
	pasPhysHeaps[ui32NextHeapID].ui32PhysHeapID = uiHeapIDBase + ui32NextHeapID;
	pasPhysHeaps[ui32NextHeapID].pszPDumpMemspaceName = "LMA";
	pasPhysHeaps[ui32NextHeapID].eType = PHYS_HEAP_TYPE_LMA;
	pasPhysHeaps[ui32NextHeapID].psMemFuncs = &gsPhysHeapFuncs;
	pasPhysHeaps[ui32NextHeapID].pasRegions = OSAllocZMem(sizeof(PHYS_HEAP_REGION));
	pasPhysHeaps[ui32NextHeapID].pasRegions[0].sStartAddr.uiAddr = SUPPORT_FIXED_DEVICE_MEMORY_ADDR;
	pasPhysHeaps[ui32NextHeapID].pasRegions[0].sCardBase.uiAddr = SUPPORT_FIXED_DEVICE_MEMORY_ADDR;
	pasPhysHeaps[ui32NextHeapID].pasRegions[0].uiSize = SUPPORT_FIXED_DEVICE_MEMORY_SIZE;
	pasPhysHeaps[ui32NextHeapID].ui32NumOfRegions = 1;
	ui32NextHeapID++;
#else /* defined(SUPPORT_FIXED_DEVICE_MEMORY) */
	pasPhysHeaps[ui32NextHeapID].ui32PhysHeapID = uiHeapIDBase + ui32NextHeapID;
	pasPhysHeaps[ui32NextHeapID].pszPDumpMemspaceName = "SYSMEM";
	pasPhysHeaps[ui32NextHeapID].eType = PHYS_HEAP_TYPE_UMA;
	pasPhysHeaps[ui32NextHeapID].psMemFuncs = &gsPhysHeapFuncs;
	ui32NextHeapID++;
#endif /* defined(SUPPORT_FIXED_DEVICE_MEMORY) */

#if defined(SUPPORT_TRUSTED_DEVICE)
	pasPhysHeaps[ui32NextHeapID].ui32PhysHeapID = uiHeapIDBase + ui32NextHeapID;
	pasPhysHeaps[ui32NextHeapID].pszPDumpMemspaceName = "TDMETACODEMEM";
	pasPhysHeaps[ui32NextHeapID].eType = PHYS_HEAP_TYPE_UMA;
	pasPhysHeaps[ui32NextHeapID].psMemFuncs = &gsPhysHeapFuncs;
	ui32NextHeapID++;

	pasPhysHeaps[ui32NextHeapID].ui32PhysHeapID = uiHeapIDBase + ui32NextHeapID;
	pasPhysHeaps[ui32NextHeapID].pszPDumpMemspaceName = "TDSECUREBUFMEM";
	pasPhysHeaps[ui32NextHeapID].eType = PHYS_HEAP_TYPE_UMA;
	pasPhysHeaps[ui32NextHeapID].psMemFuncs = &gsPhysHeapFuncs;
	ui32NextHeapID++;
#endif

#if defined(SUPPORT_PVRSRV_GPUVIRT)
	pasPhysHeaps[ui32NextHeapID].ui32PhysHeapID = uiHeapIDBase + ui32NextHeapID;
	ui32NextHeapID++;
#endif

	uiHeapIDBase += uiHeapCount;

	*ppasPhysHeapsOut = pasPhysHeaps;
	*puiPhysHeapCountOut = uiHeapCount;

	return PVRSRV_OK;
}

static void PhysHeapsDestroy(PHYS_HEAP_CONFIG *pasPhysHeaps)
{
	OSFreeMem(pasPhysHeaps);
}

static IMG_UINT32 RGX_3DGE_ClockFreqGet(IMG_HANDLE hSysData)
{
	SYS_DATA *psSysData = (SYS_DATA *)hSysData;
	IMG_UINT32 rate;

	rate = (IMG_UINT32)clk_get_rate(psSysData->psRGX_FCK);
	if (rate <= 0)
	{
		rate = RGX_3DGE_CORE_CLOCK_SPEED;
	}

	return rate;
}

static PVRSRV_ERROR SetClocks(SYS_DATA *psSysData, unsigned long set_rate)
{
	int res;
	unsigned long new_rate, cur_rate;

	new_rate = clk_round_rate(psSysData->psRGX_FCK, set_rate + 1000000);
	if (new_rate <= 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "EnableClocks: clk_round_rate failed. res = %ld", new_rate));
		return PVRSRV_ERROR_UNABLE_TO_ENABLE_CLOCK;
	}

	cur_rate = clk_get_rate(psSysData->psRGX_FCK);
	if (cur_rate != new_rate)
	{
		res = clk_set_rate(psSysData->psRGX_FCK, new_rate);
		if (res < 0)
		{
			PVR_DPF((PVR_DBG_ERROR, "EnableClocks: clk_set_rate failed. res = %d", res));
			return PVRSRV_ERROR_UNABLE_TO_ENABLE_CLOCK;
		}
	}

        return PVRSRV_OK;
}

static PVRSRV_ERROR InitClocks(SYS_DATA *psSysData)
{
	struct platform_device *pdev = psSysData->pdev;

	if (xen_initial_domain())
	{
		psSysData->psRGX_FCK = devm_clk_get(&pdev->dev, NULL);
		if (IS_ERR(psSysData->psRGX_FCK))
		{
			PVR_DPF((PVR_DBG_ERROR, "clk_get failed. FCK=%p", psSysData->psRGX_FCK));
			return PVRSRV_ERROR_UNABLE_TO_GET_CLOCK;
		}

		SetClocks(psSysData, RGX_3DGE_CORE_CLOCK_SPEED);
	}
	return PVRSRV_OK;
}

static PVRSRV_ERROR DeinitClocks(SYS_DATA *psSysData)
{
	struct platform_device *pdev = psSysData->pdev;

	if (xen_initial_domain())
	{
		if (psSysData->psRGX_FCK)
		{
			devm_clk_put(&pdev->dev, psSysData->psRGX_FCK);
			psSysData->psRGX_FCK = NULL;
		}
	}
	return PVRSRV_OK;
}


static PVRSRV_ERROR EnableClocks(void)
{
	struct platform_device *pdev = gpsSysData->pdev;

	if (xen_initial_domain())
	{
		int res;

		res = pm_runtime_get_sync(&pdev->dev);
		if (res < 0)
		{
			PVR_DPF((PVR_DBG_ERROR, "EnableClocks: pm_runtime_get_sync failed (%d)", res));
			return PVRSRV_ERROR_UNABLE_TO_ENABLE_CLOCK;
		}

		SetClocks(gpsSysData, RGX_3DGE_CORE_CLOCK_SPEED);
	}
	return PVRSRV_OK;
}

static
PVRSRV_ERROR DisableClocks(void)
{
	struct platform_device *pdev = gpsSysData->pdev;

	if (xen_initial_domain())
	{
		int res;

		res = pm_runtime_put_sync(&pdev->dev);
		if (res < 0)
		{
			PVR_DPF((PVR_DBG_ERROR, "DisableClocks: pm_runtime_put_sync failed (%d)", res));
			return PVRSRV_ERROR_DISABLE_CLOCK_FAILURE;
		}
	}
	return PVRSRV_OK;
}

static PVRSRV_ERROR PMRuntimeRegister(void)
{
	struct platform_device *pdev = gpsSysData->pdev;

	if (xen_initial_domain())
		pm_runtime_enable(&pdev->dev);

        return PVRSRV_OK;
}

static PVRSRV_ERROR PMRuntimeUnregister(void)
{
	struct platform_device *pdev = gpsSysData->pdev;

	if (xen_initial_domain())
		pm_runtime_disable(&pdev->dev);

        return PVRSRV_OK;
}

static PVRSRV_ERROR SysDevicePrePowerState(IMG_HANDLE hSysData,
					   PVRSRV_DEV_POWER_STATE eNewPowerState,
					   PVRSRV_DEV_POWER_STATE eCurrentPowerState,
					   IMG_BOOL bForced)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_UNREFERENCED_PARAMETER(hSysData);
	PVR_UNREFERENCED_PARAMETER(bForced);

	if ((eNewPowerState != eCurrentPowerState) &&
	    (eNewPowerState != PVRSRV_DEV_POWER_STATE_ON))
	{
		/*
		 * This is the point where power should be removed
		 * from RGX on a non-PCI system.
		 */
		PVR_DPF((PVR_DBG_MESSAGE,"SysPrePowerState: Remove RGX power"));
		eError = DisableClocks();
	}

	return eError;
}

static PVRSRV_ERROR SysDevicePostPowerState(IMG_HANDLE hSysData,
					    PVRSRV_DEV_POWER_STATE eNewPowerState,
					    PVRSRV_DEV_POWER_STATE eCurrentPowerState,
					    IMG_BOOL bForced)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_UNREFERENCED_PARAMETER(hSysData);
	PVR_UNREFERENCED_PARAMETER(bForced);

	if ((eNewPowerState != eCurrentPowerState) &&
	    (eCurrentPowerState != PVRSRV_DEV_POWER_STATE_ON))
	{
		if (eCurrentPowerState == PVRSRV_DEV_POWER_STATE_OFF)
		{
			/*
			 * This is the point where power should be restored to RGX on a
			 * non-PCI system.
			 */
			PVR_DPF((PVR_DBG_MESSAGE,"SysPostPowerState: Restore RGX power"));
			eError = EnableClocks();
		}
	}

	return eError;
}

static IMG_BOOL SystemISRHandler(void *pvData)
{
	SYS_DATA *psSysData = pvData;
	IMG_BOOL bHandled;

	bHandled = psSysData->pfnDeviceLISR(psSysData->pvDeviceLISRData);

	return bHandled;
}

PVRSRV_ERROR SysDevInit(void *pvOSDevice, PVRSRV_DEVICE_CONFIG **ppsDevConfig)
{
	PVRSRV_DEVICE_CONFIG *psDevConfig;
	RGX_DATA *psRGXData;
	RGX_TIMING_INFORMATION *psRGXTimingInfo;
	PHYS_HEAP_CONFIG *pasPhysHeaps;
	IMG_UINT32 uiPhysHeapCount;
	PVRSRV_ERROR eError;
#if defined(PVR_LINUX_DYNAMIC_RGX_RESOURCE_INFO)
        struct resource *dev_res = NULL;
        IMG_INT32 dev_irq = 0;
#endif

	gpsSysData = OSAllocZMem(sizeof(*gpsSysData));
	if (!gpsSysData)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psDevConfig = OSAllocZMem(sizeof(*psDevConfig) +
							  sizeof(*psRGXData) +
							  sizeof(*psRGXTimingInfo));
	if (!psDevConfig)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorFreeSysData;
	}

	psRGXData = (RGX_DATA *)((IMG_CHAR *)psDevConfig + sizeof(*psDevConfig));
	psRGXTimingInfo = (RGX_TIMING_INFORMATION *)((IMG_CHAR *)psRGXData + sizeof(*psRGXData));

        gpsSysData->pdev = to_platform_device((struct device *)pvOSDevice);

#if defined(PVR_LINUX_DYNAMIC_RGX_RESOURCE_INFO)
	/* get the resource and IRQ through platform resource API */
	dev_res = platform_get_resource(gpsSysData->pdev, IORESOURCE_MEM, 0);
	if (dev_res == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: platform_get_resource failed", __FUNCTION__));
		eError =  PVRSRV_ERROR_INVALID_DEVICE;
		goto ErrorFreeSysData;
	}

	dev_irq = platform_get_irq(gpsSysData->pdev, 0);
	if (dev_irq < 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: platform_get_irq failed (%d)", __FUNCTION__, -dev_irq));
		eError = PVRSRV_ERROR_INVALID_DEVICE;
		goto ErrorFreeSysData;
	}
#endif /* defined(PVR_LINUX_DYNAMIC_RGX_RESOURCE_INFO) */

	eError = InitClocks(gpsSysData);
	if (eError)
	{
		goto ErrorFreeDevConfig;
	}

	eError = PhysHeapsCreate(&pasPhysHeaps, &uiPhysHeapCount);
	if (eError)
	{
		goto ErrorFreeDevConfig;
	}

	/* Setup RGX specific timing data */
	psRGXTimingInfo->ui32CoreClockSpeed        = RGX_3DGE_ClockFreqGet((IMG_HANDLE)gpsSysData);;
#if defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
	psRGXTimingInfo->bEnableActivePM           = IMG_TRUE;
#else
	psRGXTimingInfo->bEnableActivePM           = IMG_FALSE;
#endif
	psRGXTimingInfo->bEnableRDPowIsland        = IMG_FALSE;
	psRGXTimingInfo->ui32ActivePMLatencyms     = SYS_RGX_ACTIVE_POWER_LATENCY_MS;

	/* Set up the RGX data */
	psRGXData->psRGXTimingInfo = psRGXTimingInfo;

#if defined(SUPPORT_TRUSTED_DEVICE)
	psRGXData->bHasTDMetaCodePhysHeap = IMG_TRUE;
	psRGXData->uiTDMetaCodePhysHeapID = 
		pasPhysHeaps[PHYS_HEAP_IDX_TDFWCODE].ui32PhysHeapID;

	psRGXData->bHasTDSecureBufPhysHeap = IMG_TRUE;
	psRGXData->uiTDSecureBufPhysHeapID = 
		pasPhysHeaps[PHYS_HEAP_IDX_TDSECUREBUF].ui32PhysHeapID;
#endif

	/* Setup the device config */
	psDevConfig->pvOSDevice             = pvOSDevice;
	psDevConfig->pszName                = "RGX";
	psDevConfig->pszVersion             = NULL;

	/* Device setup information */
#if defined(PVR_LINUX_DYNAMIC_RGX_RESOURCE_INFO)
	psDevConfig->sRegsCpuPBase.uiAddr   = dev_res->start;
	psDevConfig->ui32RegsSize           = (unsigned int)(dev_res->end - dev_res->start);
	psDevConfig->ui32IRQ                = dev_irq;
#else
	psDevConfig->sRegsCpuPBase.uiAddr   = RGX_3DGE_REG_BASE_ADDR;
	psDevConfig->ui32RegsSize           = RGX_3DGE_REG_SIZE;
	psDevConfig->ui32IRQ                = RGX_3DGE_IRQ_NUM;
#endif

#if defined(RGX_FEATURE_AXI_ACELITE) && defined(SUPPORT_AXI_ACE_TEST)
	psDevConfig->eCacheSnoopingMode		= PVRSRV_DEVICE_SNOOP_CPU_ONLY;
#elif defined(RGX_FEATURE_GPU_CPU_COHERENCY)
	psDevConfig->eCacheSnoopingMode		= PVRSRV_DEVICE_SNOOP_CROSS;
#else
	psDevConfig->eCacheSnoopingMode		= PVRSRV_DEVICE_SNOOP_NONE;
#endif

	psDevConfig->pasPhysHeaps		= pasPhysHeaps;
	psDevConfig->ui32PhysHeapCount		= uiPhysHeapCount;

	psDevConfig->aui32PhysHeapID[PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL] =
		pasPhysHeaps[PHYS_HEAP_IDX_GENERAL].ui32PhysHeapID;
	psDevConfig->aui32PhysHeapID[PVRSRV_DEVICE_PHYS_HEAP_CPU_LOCAL] =
		pasPhysHeaps[PHYS_HEAP_IDX_GENERAL].ui32PhysHeapID;
	psDevConfig->aui32PhysHeapID[PVRSRV_DEVICE_PHYS_HEAP_FW_LOCAL] =
		pasPhysHeaps[PHYS_HEAP_IDX_GENERAL].ui32PhysHeapID;

	psDevConfig->eBIFTilingMode = geBIFTilingMode;
	psDevConfig->pui32BIFTilingHeapConfigs = gauiBIFTilingHeapXStrides;
	psDevConfig->ui32BIFTilingHeapCount = IMG_ARR_NUM_ELEMS(gauiBIFTilingHeapXStrides);

	/* No power management on no HW system */
	psDevConfig->pfnPrePowerState       = SysDevicePrePowerState;
	psDevConfig->pfnPostPowerState      = SysDevicePostPowerState;

	/* No clock frequency either */
	psDevConfig->pfnClockFreqGet        = NULL;

	psDevConfig->hDevData               = psRGXData;
	psDevConfig->hSysData               = gpsSysData;

	/* Setup other system specific stuff */
	PMRuntimeRegister();
#if defined(SUPPORT_ION)
	IonInit(NULL);
#endif

#if defined(SUPPORT_PVRSRV_GPUVIRT)
	/* Virtualization support services needs to know which heap ID corresponds to FW */
	psDevConfig->aui32PhysHeapID[PVRSRV_DEVICE_PHYS_HEAP_FW_LOCAL] =
		pasPhysHeaps[PHYS_HEAP_IDX_VIRTFW].ui32PhysHeapID;
	SysVzDevInit(psDevConfig);
#endif

	*ppsDevConfig = psDevConfig;

	return PVRSRV_OK;

ErrorFreeDevConfig:
	OSFreeMem(psDevConfig);
ErrorFreeSysData:
	OSFreeMem(gpsSysData);
	return eError;
}

void SysDevDeInit(PVRSRV_DEVICE_CONFIG *psDevConfig)
{
	SYS_DATA *psSysData = (SYS_DATA *)psDevConfig->hSysData;

#if defined(SUPPORT_PVRSRV_GPUVIRT)
	SysVzDevDeInit(psDevConfig);
#endif

#if defined(SUPPORT_ION)
	IonDeinit();
#endif
	PMRuntimeUnregister();
	DeinitClocks(psSysData);

	PhysHeapsDestroy(psDevConfig->pasPhysHeaps);
	OSFreeMem(psDevConfig);
	OSFreeMem(psSysData);
}

PVRSRV_ERROR SysInstallDeviceLISR(IMG_HANDLE hSysData,
								  IMG_UINT32 ui32IRQ,
								  const IMG_CHAR *pszName,
								  PFN_LISR pfnLISR,
								  void *pvData,
								  IMG_HANDLE *phLISRData)
{
	SYS_DATA *psSysData = (SYS_DATA *)hSysData;
	PVRSRV_ERROR eError;

	if (psSysData->hSysLISRData)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: ISR for %s already installed!", __FUNCTION__, pszName));
		return PVRSRV_ERROR_CANT_REGISTER_CALLBACK;
	}

        /* Wrap the device LISR */
        psSysData->pfnDeviceLISR = pfnLISR;
        psSysData->pvDeviceLISRData = pvData;

	eError = OSInstallSystemLISR(&psSysData->hSysLISRData, ui32IRQ, pszName,
							SystemISRHandler, psSysData,
							SYS_IRQ_FLAG_TRIGGER_DEFAULT);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	*phLISRData = psSysData;

#if 0
	PVR_LOG(("Installed device LISR %s on IRQ %d", pszName, ui32IRQ));
#endif

	return PVRSRV_OK;
}

PVRSRV_ERROR SysUninstallDeviceLISR(IMG_HANDLE hLISRData)
{
	SYS_DATA *psSysData = (SYS_DATA *)hLISRData;
	PVRSRV_ERROR eError;

	PVR_ASSERT(psSysData);

	eError = OSUninstallSystemLISR(psSysData->hSysLISRData);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	/* Reset the interrupt data */
	psSysData->pfnDeviceLISR    = NULL;
	psSysData->pvDeviceLISRData = NULL;
	psSysData->hSysLISRData     = NULL;

	return PVRSRV_OK;
}

PVRSRV_ERROR SysDebugInfo(PVRSRV_DEVICE_CONFIG *psDevConfig,
				DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				void *pvDumpDebugFile)
{
	PVR_UNREFERENCED_PARAMETER(psDevConfig);
	PVR_UNREFERENCED_PARAMETER(pfnDumpDebugPrintf);
	PVR_UNREFERENCED_PARAMETER(pvDumpDebugFile);
	return PVRSRV_OK;
}

/******************************************************************************
 End of file (sysconfig.c)
******************************************************************************/
