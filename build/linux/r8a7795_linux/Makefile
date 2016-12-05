########################################################################### ###
#@Copyright     Copyright (c) Imagination Technologies Ltd. All Rights Reserved
#               Copyright (C) 2016 Renesas Electronics Corporation
#@License       Dual MIT/GPLv2
# 
# The contents of this file are subject to the MIT license as set out below.
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
# 
# Alternatively, the contents of this file may be used under the terms of
# the GNU General Public License Version 2 ("GPL") in which case the provisions
# of GPL are applicable instead of those above.
# 
# If you wish to allow use of your version of this file only under the terms of
# GPL, and not to allow others to use your version of this file under the terms
# of the MIT license, indicate your decision by deleting the provisions above
# and replace them with the notice and other provisions required by GPL as set
# out in the file called "GPL-COPYING" included in this distribution. If you do
# not delete the provisions above, a recipient may use your version of this file
# under the terms of either the MIT license or GPL.
# 
# This License is also included in this distribution in the file called
# "MIT-COPYING".
# 
# EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
# PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
# BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
# PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
# COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
# IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
### ###########################################################################

WINDOW_SYSTEM ?= wayland_wsegl

include ../config/preconfig.mk
include ../config/window_system.mk

PVR_SYSTEM := rgx_rcar
RGX_BVNC := 4.43.6.62
LDM_PLATFORM := 1
NO_HARDWARE := 0
# PDUMP ?= 1

RGX_3DGE_REG_BASE_ADDR := 0xFD000000
RGX_3DGE_REG_SIZE := 0xFFFF
RGX_3DGE_IRQ_NUM := (119 + 32) # = gic_spi(119)
RGX_3DGE_CORE_CLOCK_SPEED := 600000000
RGX_3DGE_SYSTEM_NAME := "\"GX6650\""

SUPPORT_ACTIVE_POWER_MANAGEMENT ?= 1
SYS_RGX_ACTIVE_POWER_LATENCY_MS := 100

SYS_RGX_OF_COMPATIBLE := "\"renesas,gsx\""

PVR_LDM_PLATFORM_PRE_REGISTERED := 1
PVR_LDM_PLATFORM_PRE_REGISTERED_DEV := "\"pvrsrvkm\""

ifeq ($(SUPPORT_KMS),1)
 PVR_DRM_MODESET_DRIVER_NAME := rcar-du
 PVR_DRM_MODESET_MODULE_NAME := dumb
else
 ifeq ($(SUPPORT_DISPLAY_CLASS),1)
  DISPLAY_CONTROLLER ?= dc_example
  SUPPORT_DRM := 0
 else
  PVR_DRM_MODESET_DRIVER_NAME := rcar-du
  PVR_DRM_MODESET_MODULE_NAME := dumb
 endif
endif

KERNEL_COMPONENTS := srvkm $(DISPLAY_CONTROLLER)

SYS_CFLAGS := -march=armv8-a

SUPPORT_FIXED_DEVICE_MEMORY ?= 0
ifeq ($(SUPPORT_FIXED_DEVICE_MEMORY),1)
SUPPORT_FIXED_DEVICE_MEMORY_ADDR := 0x0700000000ULL
SUPPORT_FIXED_DEVICE_MEMORY_SIZE := 0x40000000ULL
endif

# Yocto 64bit SDK settings
SYSROOT := $(SDKTARGETSYSROOT)

SUPPORT_WRAPEXTMEM ?= 1

SUPPORT_EXTENSION_REL ?= 1
ifeq ($(SUPPORT_EXTENSION_REL),1)
SUPPORT_EGL_EXTENSION_GET_MEMINFO_REL ?= 1
SUPPORT_EXTENSION_QUERY_PARAM_BUFFER_REL ?= 1
endif # ifeq ($(SUPPORT_EXTENSION_REL),1)

# Should be last
include ../config/core.mk
-include ../common/lws.mk
include ../common/pvrgdb.mk
include ../common/3rdparty.mk
include ../common/renesas.mk
include ../common/renesas_ext.mk
