/*************************************************************************/ /*!
@File
@Title          Common Renesas Bridge
@Copyright      Copyright (C) 2013 Renesas Electronics Corporation
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


#ifndef COMMON_RENESAST_BRIDGE_H
#define COMMON_RENESAST_BRIDGE_H

#include "rgx_bridge.h"


/* FIXME: need to create pvrbridge_common.h" */
#include "pvr_bridge.h"

#define PVRSRV_BRIDGE_RENESAS_CMD_FIRST			0

#if defined(ENABLE_DEBUG_LOG_COMMON_REL)

#define PVRSRV_BRIDGE_DEBUG_CMD_FIRST			(PVRSRV_BRIDGE_RENESAS_CMD_FIRST+0)
#define PVRSRV_BRIDGE_DEBUG_GET_INFO			PVRSRV_BRIDGE_DEBUG_CMD_FIRST+0
#define PVRSRV_BRIDGE_DEBUG_CMD_LAST			(PVRSRV_BRIDGE_DEBUG_CMD_FIRST+0)

#else /* defined(ENABLE_DEBUG_LOG_COMMON_REL) */

#define PVRSRV_BRIDGE_DEBUG_CMD_LAST			(PVRSRV_BRIDGE_RENESAS_CMD_FIRST+0)

#endif /* defined(ENABLE_DEBUG_LOG_COMMON_REL) */

#if defined(SUPPORT_EXTENSION_QUERY_PARAM_BUFFER_REL)
#define PVRSRV_BRIDGE_QUERY_PARAMBUFFER_CMD_FIRST	(PVRSRV_BRIDGE_DEBUG_CMD_LAST+1)
#define PVRSRV_BRIDGE_QUERY_PARAMBUFFER_OVERFLOW_REL	PVRSRV_BRIDGE_QUERY_PARAMBUFFER_CMD_FIRST+0
#define PVRSRV_BRIDGE_QUERY_PARAMBUFFER_SIZE_REL		PVRSRV_BRIDGE_QUERY_PARAMBUFFER_CMD_FIRST+1
#define PVRSRV_BRIDGE_QUERY_PARAMBUFFER_CMD_LAST		(PVRSRV_BRIDGE_QUERY_PARAMBUFFER_CMD_FIRST+1)
#else
#define PVRSRV_BRIDGE_QUERY_PARAMBUFFER_CMD_LAST        (PVRSRV_BRIDGE_DEBUG_CMD_LAST)
#endif

#define PVRSRV_BRIDGE_RENESAS_CMD_LAST                  (PVRSRV_BRIDGE_QUERY_PARAMBUFFER_CMD_LAST)


/* [RELCOMMENT STD-0019] Added new structures for getting parameterbuffer infomation. */
#if defined(SUPPORT_EXTENSION_QUERY_PARAM_BUFFER_REL)
typedef struct PVRSRV_BRIDGE_IN_QUERY_PARAMBUFFER_OVERFLOW_TAG
{
	IMG_HANDLE hCookie;
} PVRSRV_BRIDGE_IN_QUERY_PARAMBUFFER_OVERFLOW;

typedef struct PVRSRV_BRIDGE_OUT_QUERY_PARAMBUFFER_OVERFLOW_TAG
{
	PVRSRV_ERROR eError;
	IMG_BOOL     bSPMEvent;
} PVRSRV_BRIDGE_OUT_QUERY_PARAMBUFFER_OVERFLOW;

typedef struct PVRSRV_BRIDGE_IN_QUERY_PARAMBUFFER_SIZE_TAG
{
	IMG_HANDLE hFreeList;
} PVRSRV_BRIDGE_IN_QUERY_PARAMBUFFER_SIZE;

typedef struct PVRSRV_BRIDGE_OUT_QUERY_PARAMBUFFER_SIZE_TAG
{
	PVRSRV_ERROR eError;
	IMG_UINT32   ui32CurrentSize;
} PVRSRV_BRIDGE_OUT_QUERY_PARAMBUFFER_SIZE;
#endif /* defined(SUPPORT_EXTENSION_QUERY_PARAM_BUFFER_REL) */

#endif /* COMMON_RENESAST_BRIDGE_H */
