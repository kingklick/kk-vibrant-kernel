/**********************************************************************
 *
 * Copyright(c) 2008 Imagination Technologies Ltd. All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope it will be useful but, except 
 * as otherwise stated in writing, without any warranty; without even the 
 * implied warranty of merchantability or fitness for a particular purpose. 
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * 
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * Imagination Technologies Ltd. <gpl-support@imgtec.com>
 * Home Park Estate, Kings Langley, Herts, WD4 8LZ, UK 
 *
 ******************************************************************************/

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/fb.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/notifier.h>

#include "img_defs.h"
#include "servicesext.h"
#include "kerneldisplay.h"
#include "omaplfb.h"

static void *gpvAnchor;

static int fb_idx = 0;

#define OMAPLFB_COMMAND_COUNT		1

#define	OMAPLFB_VSYNC_SETTLE_COUNT	5

static PFN_DC_GET_PVRJTABLE pfnGetPVRJTable = 0;

static inline unsigned long RoundUpToMultiple(unsigned long x, unsigned long y)
{
	unsigned long div = x / y;
	unsigned long rem = x % y;

	return (div + ((rem == 0) ? 0 : 1)) * y;
}

static unsigned long GCD(unsigned long x, unsigned long y)
{
	while (y != 0)
	{
		unsigned long r = x % y;
		x = y;
		y = r;
	}

	return x;
}

static unsigned long LCM(unsigned long x, unsigned long y)
{
	unsigned long gcd = GCD(x, y);

	return (gcd == 0) ? 0 : ((x / gcd) * y);
}

OMAPLFB_DEVINFO *OMAPLFBGetAnchorPtr(void)
{
	return (OMAPLFB_DEVINFO *)gpvAnchor;
}

static inline void OMAPLFBSetAnchorPtr(OMAPLFB_DEVINFO *psDevInfo)
{
	gpvAnchor = (void*)psDevInfo;
}

static inline OMAPLFB_BOOL DontWaitForVSync(OMAPLFB_DEVINFO *psDevInfo)
{
	return psDevInfo->bBlanked || psDevInfo->bFlushCommands;
}

static IMG_VOID SetDCState(IMG_HANDLE hDevice, IMG_UINT32 ui32State)
{
	OMAPLFB_DEVINFO *psDevInfo = (OMAPLFB_DEVINFO *)hDevice;

	switch (ui32State)
	{
		case DC_STATE_FLUSH_COMMANDS:
			psDevInfo->bFlushCommands = OMAPLFB_TRUE;
			break;
		case DC_STATE_NO_FLUSH_COMMANDS:
			psDevInfo->bFlushCommands = OMAPLFB_FALSE;
			break;
		default:
			break;
	}
}

static PVRSRV_ERROR OpenDCDevice(IMG_UINT32 ui32DeviceID,
                                 IMG_HANDLE *phDevice,
                                 PVRSRV_SYNC_DATA* psSystemBufferSyncData)
{
	OMAPLFB_DEVINFO *psDevInfo;
	OMAPLFB_ERROR eError;

	UNREFERENCED_PARAMETER(ui32DeviceID);

	psDevInfo = OMAPLFBGetAnchorPtr();

	
	psDevInfo->sSystemBuffer.psSyncData = psSystemBufferSyncData;
	
	eError = OMAPLFBUnblankDisplay(psDevInfo);
	if (eError != OMAPLFB_OK)
	{
		DEBUG_PRINTK((KERN_WARNING DRIVER_PREFIX
			": OMAPLFBUnblankDisplay failed (%d)", eError));
		return (PVRSRV_ERROR_GENERIC);
	}

	
	*phDevice = (IMG_HANDLE)psDevInfo;
	
	return (PVRSRV_OK);
}

static PVRSRV_ERROR CloseDCDevice(IMG_HANDLE hDevice)
{
	UNREFERENCED_PARAMETER(hDevice);

	return (PVRSRV_OK);
}

static PVRSRV_ERROR EnumDCFormats(IMG_HANDLE hDevice,
                                  IMG_UINT32 *pui32NumFormats,
                                  DISPLAY_FORMAT *psFormat)
{
	OMAPLFB_DEVINFO	*psDevInfo;
	
	if(!hDevice || !pui32NumFormats)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}

	psDevInfo = (OMAPLFB_DEVINFO*)hDevice;
	
	*pui32NumFormats = 1;
	
	if(psFormat)
	{
		psFormat[0] = psDevInfo->sDisplayFormat;
	}

	return (PVRSRV_OK);
}

static PVRSRV_ERROR EnumDCDims(IMG_HANDLE hDevice, 
                               DISPLAY_FORMAT *psFormat,
                               IMG_UINT32 *pui32NumDims,
                               DISPLAY_DIMS *psDim)
{
	OMAPLFB_DEVINFO	*psDevInfo;

	if(!hDevice || !psFormat || !pui32NumDims)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}

	psDevInfo = (OMAPLFB_DEVINFO*)hDevice;

	*pui32NumDims = 1;

	
	if(psDim)
	{
		psDim[0] = psDevInfo->sDisplayDim;
	}
	
	return (PVRSRV_OK);
}


static PVRSRV_ERROR GetDCSystemBuffer(IMG_HANDLE hDevice, IMG_HANDLE *phBuffer)
{
	OMAPLFB_DEVINFO	*psDevInfo;
	
	if(!hDevice || !phBuffer)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}

	psDevInfo = (OMAPLFB_DEVINFO*)hDevice;

	*phBuffer = (IMG_HANDLE)&psDevInfo->sSystemBuffer;

	return (PVRSRV_OK);
}


static PVRSRV_ERROR GetDCInfo(IMG_HANDLE hDevice, DISPLAY_INFO *psDCInfo)
{
	OMAPLFB_DEVINFO	*psDevInfo;
	
	if(!hDevice || !psDCInfo)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}

	psDevInfo = (OMAPLFB_DEVINFO*)hDevice;

	*psDCInfo = psDevInfo->sDisplayInfo;

	return (PVRSRV_OK);
}

static PVRSRV_ERROR GetDCBufferAddr(IMG_HANDLE        hDevice,
                                    IMG_HANDLE        hBuffer, 
                                    IMG_SYS_PHYADDR   **ppsSysAddr,
                                    IMG_UINT32        *pui32ByteSize,
                                    IMG_VOID          **ppvCpuVAddr,
                                    IMG_HANDLE        *phOSMapInfo,
                                    IMG_BOOL          *pbIsContiguous)
{
	OMAPLFB_DEVINFO	*psDevInfo;
	OMAPLFB_BUFFER *psSystemBuffer;

	if(!hDevice)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}
	psDevInfo = (OMAPLFB_DEVINFO*)hDevice;
	
	if(!hBuffer)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}
	psSystemBuffer = (OMAPLFB_BUFFER *)hBuffer;

	if (!ppsSysAddr)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}

	*ppsSysAddr = &psSystemBuffer->sSysAddr;

	if (!pui32ByteSize)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}

	*pui32ByteSize = (IMG_UINT32)psDevInfo->sFBInfo.ulBufferSize;

	if (ppvCpuVAddr)
	{
		*ppvCpuVAddr = psSystemBuffer->sCPUVAddr;
	}

	if (phOSMapInfo)
	{
		*phOSMapInfo = (IMG_HANDLE)0;
	}

	if (pbIsContiguous)
	{
		*pbIsContiguous = IMG_TRUE;
	}

	return (PVRSRV_OK);
}

static PVRSRV_ERROR CreateDCSwapChain(IMG_HANDLE hDevice,
                                      IMG_UINT32 ui32Flags,
                                      DISPLAY_SURF_ATTRIBUTES *psDstSurfAttrib,
                                      DISPLAY_SURF_ATTRIBUTES *psSrcSurfAttrib,
                                      IMG_UINT32 ui32BufferCount,
                                      PVRSRV_SYNC_DATA **ppsSyncData,
                                      IMG_UINT32 ui32OEMFlags,
                                      IMG_HANDLE *phSwapChain,
                                      IMG_UINT32 *pui32SwapChainID)
{
	OMAPLFB_DEVINFO	*psDevInfo;
	OMAPLFB_SWAPCHAIN *psSwapChain;
	OMAPLFB_BUFFER *psBuffer;
	IMG_UINT32 i;
	PVRSRV_ERROR eError = PVRSRV_ERROR_NOT_SUPPORTED;
	IMG_UINT32 ui32BuffersToSkip;

	UNREFERENCED_PARAMETER(ui32OEMFlags);
	UNREFERENCED_PARAMETER(pui32SwapChainID);
	
	
	if(!hDevice
	|| !psDstSurfAttrib
	|| !psSrcSurfAttrib
	|| !ppsSyncData
	|| !phSwapChain)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}

	psDevInfo = (OMAPLFB_DEVINFO*)hDevice;
	
	
	if (psDevInfo->sDisplayInfo.ui32MaxSwapChains == 0)
	{
		return (PVRSRV_ERROR_NOT_SUPPORTED);
	}

	
	if(psDevInfo->psSwapChain != NULL)
	{
		return (PVRSRV_ERROR_FLIP_CHAIN_EXISTS);
	}
	
	
	if(ui32BufferCount > psDevInfo->sDisplayInfo.ui32MaxSwapChainBuffers)
	{
		return (PVRSRV_ERROR_TOOMANYBUFFERS);
	}
	
	if ((psDevInfo->sFBInfo.ulRoundedBufferSize * (unsigned long)ui32BufferCount) > psDevInfo->sFBInfo.ulFBSize)
	{
		return (PVRSRV_ERROR_TOOMANYBUFFERS);
	}

	
	ui32BuffersToSkip = psDevInfo->sDisplayInfo.ui32MaxSwapChainBuffers - ui32BufferCount;

	
	if(psDstSurfAttrib->pixelformat != psDevInfo->sDisplayFormat.pixelformat
	|| psDstSurfAttrib->sDims.ui32ByteStride != psDevInfo->sDisplayDim.ui32ByteStride
	|| psDstSurfAttrib->sDims.ui32Width != psDevInfo->sDisplayDim.ui32Width
	|| psDstSurfAttrib->sDims.ui32Height != psDevInfo->sDisplayDim.ui32Height)
	{
		
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}		

	if(psDstSurfAttrib->pixelformat != psSrcSurfAttrib->pixelformat
	|| psDstSurfAttrib->sDims.ui32ByteStride != psSrcSurfAttrib->sDims.ui32ByteStride
	|| psDstSurfAttrib->sDims.ui32Width != psSrcSurfAttrib->sDims.ui32Width
	|| psDstSurfAttrib->sDims.ui32Height != psSrcSurfAttrib->sDims.ui32Height)
	{
		
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}		

	
	UNREFERENCED_PARAMETER(ui32Flags);
	
#if defined(PVR_OMAPFB3_UPDATE_MODE)
	if (!OMAPLFBSetUpdateMode(psDevInfo, PVR_OMAPFB3_UPDATE_MODE))
	{
		printk(KERN_WARNING DRIVER_PREFIX ": Couldn't set frame buffer update mode %d\n", PVR_OMAPFB3_UPDATE_MODE);
	}
#endif
	
	psSwapChain = (OMAPLFB_SWAPCHAIN*)OMAPLFBAllocKernelMem(sizeof(OMAPLFB_SWAPCHAIN));
	if(!psSwapChain)
	{
		return (PVRSRV_ERROR_OUT_OF_MEMORY);
	}

	psBuffer = (OMAPLFB_BUFFER*)OMAPLFBAllocKernelMem(sizeof(OMAPLFB_BUFFER) * ui32BufferCount);
	if(!psBuffer)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorFreeSwapChain;
	}

	psSwapChain->ulBufferCount = (unsigned long)ui32BufferCount;
	psSwapChain->psBuffer = psBuffer;
	psSwapChain->bNotVSynced = OMAPLFB_TRUE;

	
	for(i=0; i<ui32BufferCount-1; i++)
	{
		psBuffer[i].psNext = &psBuffer[i+1];
	}
	
	psBuffer[i].psNext = &psBuffer[0];

	
	for(i=0; i<ui32BufferCount; i++)
	{
		IMG_UINT32 ui32SwapBuffer = i + ui32BuffersToSkip;
		IMG_UINT32 ui32BufferOffset = ui32SwapBuffer * (IMG_UINT32)psDevInfo->sFBInfo.ulRoundedBufferSize;

		psBuffer[i].psSyncData = ppsSyncData[i];

		psBuffer[i].sSysAddr.uiAddr = psDevInfo->sFBInfo.sSysAddr.uiAddr + ui32BufferOffset;
		psBuffer[i].sCPUVAddr = psDevInfo->sFBInfo.sCPUVAddr + ui32BufferOffset;
		psBuffer[i].ulYOffset = ui32BufferOffset / psDevInfo->sFBInfo.ulByteStride;
		psBuffer[i].psDevInfo = psDevInfo;

		OMAPLFBInitBufferForSwap(&psBuffer[i]);
	}

	if (OMAPLFBCreateSwapQueue(psSwapChain) != OMAPLFB_OK)
	{ 
		printk(KERN_WARNING DRIVER_PREFIX ": ISR handler failed to register\n");
		eError = PVRSRV_ERROR_GENERIC;
		goto ErrorFreeBuffers;
	}

	if (OMAPLFBEnableLFBEventNotification(psDevInfo)!= OMAPLFB_OK)
	{
		eError = PVRSRV_ERROR_GENERIC;
		printk(KERN_WARNING DRIVER_PREFIX ": Couldn't enable framebuffer event notification\n");
		goto ErrorDestroySwapQueue;
	}

	psDevInfo->psSwapChain = psSwapChain;

	*phSwapChain = (IMG_HANDLE)psSwapChain;

	return (PVRSRV_OK);

ErrorDestroySwapQueue:
	OMAPLFBDestroySwapQueue(psSwapChain);
ErrorFreeBuffers:
	OMAPLFBFreeKernelMem(psBuffer);
ErrorFreeSwapChain:
	OMAPLFBFreeKernelMem(psSwapChain);
	return eError;
}

static PVRSRV_ERROR DestroyDCSwapChain(IMG_HANDLE hDevice,
	IMG_HANDLE hSwapChain)
{
	OMAPLFB_DEVINFO	*psDevInfo;
	OMAPLFB_SWAPCHAIN *psSwapChain;
	OMAPLFB_ERROR eError;

	
	if(!hDevice || !hSwapChain)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}
	
	psDevInfo = (OMAPLFB_DEVINFO*)hDevice;
	psSwapChain = (OMAPLFB_SWAPCHAIN*)hSwapChain;
	if (psSwapChain != psDevInfo->psSwapChain)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}

	OMAPLFBDestroySwapQueue(psSwapChain);

	eError = OMAPLFBDisableLFBEventNotification(psDevInfo);
	if (eError != OMAPLFB_OK)
	{
		printk(KERN_WARNING DRIVER_PREFIX ": Couldn't disable framebuffer event notification\n");
	}

	
	OMAPLFBFreeKernelMem(psSwapChain->psBuffer);
	OMAPLFBFreeKernelMem(psSwapChain);

	psDevInfo->psSwapChain = NULL;

	OMAPLFBFlip(psDevInfo, &psDevInfo->sSystemBuffer);
	(void) OMAPLFBCheckModeAndSync(psDevInfo);

	return (PVRSRV_OK);
}

static PVRSRV_ERROR SetDCDstRect(IMG_HANDLE hDevice,
	IMG_HANDLE hSwapChain,
	IMG_RECT *psRect)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hSwapChain);
	UNREFERENCED_PARAMETER(psRect);

	
	
	return (PVRSRV_ERROR_NOT_SUPPORTED);
}

static PVRSRV_ERROR SetDCSrcRect(IMG_HANDLE hDevice,
                                 IMG_HANDLE hSwapChain,
                                 IMG_RECT *psRect)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hSwapChain);
	UNREFERENCED_PARAMETER(psRect);

	

	return (PVRSRV_ERROR_NOT_SUPPORTED);
}

static PVRSRV_ERROR SetDCDstColourKey(IMG_HANDLE hDevice,
                                      IMG_HANDLE hSwapChain,
                                      IMG_UINT32 ui32CKColour)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hSwapChain);
	UNREFERENCED_PARAMETER(ui32CKColour);

	

	return (PVRSRV_ERROR_NOT_SUPPORTED);
}

static PVRSRV_ERROR SetDCSrcColourKey(IMG_HANDLE hDevice,
                                      IMG_HANDLE hSwapChain,
                                      IMG_UINT32 ui32CKColour)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hSwapChain);
	UNREFERENCED_PARAMETER(ui32CKColour);

	

	return (PVRSRV_ERROR_NOT_SUPPORTED);
}

static PVRSRV_ERROR GetDCBuffers(IMG_HANDLE hDevice,
                                 IMG_HANDLE hSwapChain,
                                 IMG_UINT32 *pui32BufferCount,
                                 IMG_HANDLE *phBuffer)
{
	OMAPLFB_DEVINFO   *psDevInfo;
	OMAPLFB_SWAPCHAIN *psSwapChain;
	unsigned i;
	
	
	if(!hDevice 
	|| !hSwapChain
	|| !pui32BufferCount
	|| !phBuffer)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}
	
	psDevInfo = (OMAPLFB_DEVINFO*)hDevice;
	psSwapChain = (OMAPLFB_SWAPCHAIN*)hSwapChain;
	if (psSwapChain != psDevInfo->psSwapChain)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}
	
	
	*pui32BufferCount = (IMG_UINT32)psSwapChain->ulBufferCount;
	
	
	for(i=0; i<psSwapChain->ulBufferCount; i++)
	{
		phBuffer[i] = (IMG_HANDLE)&psSwapChain->psBuffer[i];
	}
	
	return (PVRSRV_OK);
}

static PVRSRV_ERROR SwapToDCBuffer(IMG_HANDLE hDevice,
                                   IMG_HANDLE hBuffer,
                                   IMG_UINT32 ui32SwapInterval,
                                   IMG_HANDLE hPrivateTag,
                                   IMG_UINT32 ui32ClipRectCount,
                                   IMG_RECT *psClipRect)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hBuffer);
	UNREFERENCED_PARAMETER(ui32SwapInterval);
	UNREFERENCED_PARAMETER(hPrivateTag);
	UNREFERENCED_PARAMETER(ui32ClipRectCount);
	UNREFERENCED_PARAMETER(psClipRect);
	
	
	return (PVRSRV_OK);
}

static PVRSRV_ERROR SwapToDCSystem(IMG_HANDLE hDevice,
                                   IMG_HANDLE hSwapChain)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hSwapChain);
	
	
	return (PVRSRV_OK);
}

static OMAPLFB_BOOL WaitForVSyncSettle(OMAPLFB_DEVINFO *psDevInfo)
{
		unsigned i;

		for(i = 0; i < OMAPLFB_VSYNC_SETTLE_COUNT; i++)
		{
			if (DontWaitForVSync(psDevInfo) || !OMAPLFBWaitForVSync(psDevInfo))
			{
				return OMAPLFB_FALSE;
			}
		}

		return OMAPLFB_TRUE;
}

void OMAPLFBSwapHandler(OMAPLFB_BUFFER *psBuffer)
{
	OMAPLFB_DEVINFO *psDevInfo = psBuffer->psDevInfo;
	OMAPLFB_SWAPCHAIN *psSwapChain = psDevInfo->psSwapChain;
	OMAPLFB_BOOL bPreviouslyNotVSynced;

	OMAPLFBFlip(psDevInfo, psBuffer);

	bPreviouslyNotVSynced = psSwapChain->bNotVSynced;
	psSwapChain->bNotVSynced = OMAPLFB_TRUE;


	if (!DontWaitForVSync(psDevInfo))
	{
		OMAPLFB_UPDATE_MODE eMode = OMAPLFBGetUpdateMode(psDevInfo);

		switch(eMode)
		{
			case OMAPLFB_UPDATE_MODE_AUTO:
				psSwapChain->bNotVSynced = OMAPLFB_FALSE;
				if (bPreviouslyNotVSynced)
				{
					psSwapChain->bNotVSynced = !WaitForVSyncSettle(psDevInfo);
				} else if (psBuffer->ulSwapInterval != 0)
				{
					psSwapChain->bNotVSynced = !OMAPLFBWaitForVSync(psDevInfo);
				}
				break;
#if defined(PVR_OMAPFB3_MANUAL_UPDATE_SYNC_IN_SWAP)
			case OMAPLFB_UPDATE_MODE_MANUAL:
				if (psBuffer->ulSwapInterval != 0)
				{
					(void) OMAPLFBManualSync(psDevInfo);
				}
				break;
#endif
			default:
				break;
		}
	}

	psDevInfo->sPVRJTable.pfnPVRSRVCmdComplete((IMG_HANDLE)psBuffer->hCmdComplete, IMG_TRUE);
}

static IMG_BOOL ProcessFlip(IMG_HANDLE  hCmdCookie,
                            IMG_UINT32  ui32DataSize,
                            IMG_VOID   *pvData)
{
	DISPLAYCLASS_FLIP_COMMAND *psFlipCmd;
	OMAPLFB_DEVINFO *psDevInfo;
	OMAPLFB_BUFFER *psBuffer;
	OMAPLFB_SWAPCHAIN *psSwapChain;

	
	if(!hCmdCookie || !pvData)
	{
		return IMG_FALSE;
	}

	
	psFlipCmd = (DISPLAYCLASS_FLIP_COMMAND*)pvData;

	if (psFlipCmd == IMG_NULL || sizeof(DISPLAYCLASS_FLIP_COMMAND) != ui32DataSize)
	{
		return IMG_FALSE;
	}

	
	psDevInfo = (OMAPLFB_DEVINFO*)psFlipCmd->hExtDevice;
	psBuffer = (OMAPLFB_BUFFER*)psFlipCmd->hExtBuffer;
	psSwapChain = (OMAPLFB_SWAPCHAIN*) psFlipCmd->hExtSwapChain;

	psBuffer->hCmdComplete = (OMAPLFB_HANDLE)hCmdCookie;
	psBuffer->ulSwapInterval = (unsigned long)psFlipCmd->ui32SwapInterval;

	OMAPLFBQueueBufferForSwap(psSwapChain, psBuffer);

	return IMG_TRUE;
}


static OMAPLFB_ERROR InitDev(OMAPLFB_DEVINFO *psDevInfo)
{
	struct fb_info *psLINFBInfo;
	struct module *psLINFBOwner;
	OMAPLFB_FBINFO *psPVRFBInfo = &psDevInfo->sFBInfo;
	OMAPLFB_ERROR eError = OMAPLFB_ERROR_GENERIC;
	unsigned long FBSize;
	unsigned long ulLCM;

	acquire_console_sem();

	if (fb_idx < 0 || fb_idx >= num_registered_fb)
	{
		eError = OMAPLFB_ERROR_INVALID_DEVICE;
		goto errRelSem;
	}

	psLINFBInfo = registered_fb[fb_idx];

	psLINFBOwner = psLINFBInfo->fbops->owner;
	if (!try_module_get(psLINFBOwner))
	{
		printk(KERN_INFO DRIVER_PREFIX
			": Couldn't get framebuffer module\n");

		goto errRelSem;
	}

	if (psLINFBInfo->fbops->fb_open != NULL)
	{
		int res;

		res = psLINFBInfo->fbops->fb_open(psLINFBInfo, 0);
		if (res != 0)
		{
			printk(KERN_INFO DRIVER_PREFIX
				": Couldn't open framebuffer: %d\n", res);

			goto errModPut;
		}
	}

	psDevInfo->psLINFBInfo = psLINFBInfo;

	FBSize = (psLINFBInfo->screen_size) != 0 ?
					psLINFBInfo->screen_size :
					psLINFBInfo->fix.smem_len;

	ulLCM = LCM(psLINFBInfo->fix.line_length, OMAPLFB_PAGE_SIZE);

	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Framebuffer physical address: 0x%lx\n",
			psLINFBInfo->fix.smem_start));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Framebuffer virtual address: 0x%lx\n",
			(unsigned long)psLINFBInfo->screen_base));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Framebuffer size: %lu\n",
			FBSize));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Framebuffer virtual width: %u\n",
			psLINFBInfo->var.xres_virtual));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Framebuffer virtual height: %u\n",
			psLINFBInfo->var.yres_virtual));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Framebuffer width: %u\n",
			psLINFBInfo->var.xres));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Framebuffer height: %u\n",
			psLINFBInfo->var.yres));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Framebuffer stride: %u\n",
			psLINFBInfo->fix.line_length));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": LCM of stride and page size: %lu\n",
			ulLCM));

	
	psPVRFBInfo->sSysAddr.uiAddr = psLINFBInfo->fix.smem_start;
	psPVRFBInfo->sCPUVAddr = psLINFBInfo->screen_base;

	psPVRFBInfo->ulWidth = psLINFBInfo->var.xres;
	psPVRFBInfo->ulHeight = psLINFBInfo->var.yres;
	psPVRFBInfo->ulByteStride =  psLINFBInfo->fix.line_length;
	psPVRFBInfo->ulFBSize = FBSize;
	psPVRFBInfo->ulBufferSize = psPVRFBInfo->ulHeight * psPVRFBInfo->ulByteStride;
	
	psPVRFBInfo->ulRoundedBufferSize = RoundUpToMultiple(psPVRFBInfo->ulBufferSize, ulLCM);

	if(psLINFBInfo->var.bits_per_pixel == 16)
	{
		if((psLINFBInfo->var.red.length == 5) &&
			(psLINFBInfo->var.green.length == 6) && 
			(psLINFBInfo->var.blue.length == 5) && 
			(psLINFBInfo->var.red.offset == 11) &&
			(psLINFBInfo->var.green.offset == 5) && 
			(psLINFBInfo->var.blue.offset == 0) && 
			(psLINFBInfo->var.red.msb_right == 0))
		{
			psPVRFBInfo->ePixelFormat = PVRSRV_PIXEL_FORMAT_RGB565;
		}
		else
		{
			printk("Unknown FB format\n");
		}
	}
	else if(psLINFBInfo->var.bits_per_pixel == 32)
	{
		if((psLINFBInfo->var.red.length == 8) &&
			(psLINFBInfo->var.green.length == 8) && 
			(psLINFBInfo->var.blue.length == 8) && 
			(psLINFBInfo->var.red.offset == 16) &&
			(psLINFBInfo->var.green.offset == 8) && 
			(psLINFBInfo->var.blue.offset == 0) && 
			(psLINFBInfo->var.red.msb_right == 0))
		{
			psPVRFBInfo->ePixelFormat = PVRSRV_PIXEL_FORMAT_ARGB8888;
		}
		else
		{
			printk("Unknown FB format\n");
		}
	}	
	else
	{
		printk("Unknown FB format\n");
	}

	psDevInfo->sFBInfo.ulPhysicalWidthmm =
		((int)psLINFBInfo->var.width  > 0) ? psLINFBInfo->var.width  : 90;

	psDevInfo->sFBInfo.ulPhysicalHeightmm =
		((int)psLINFBInfo->var.height > 0) ? psLINFBInfo->var.height : 54;

	
	psDevInfo->sFBInfo.sSysAddr.uiAddr = psPVRFBInfo->sSysAddr.uiAddr;
	psDevInfo->sFBInfo.sCPUVAddr = psPVRFBInfo->sCPUVAddr;

	eError = OMAPLFB_OK;
	goto errRelSem;

errModPut:
	module_put(psLINFBOwner);
errRelSem:
	release_console_sem();
	return eError;
}

static void DeInitDev(OMAPLFB_DEVINFO *psDevInfo)
{
	struct fb_info *psLINFBInfo = psDevInfo->psLINFBInfo;
	struct module *psLINFBOwner;

	acquire_console_sem();

	psLINFBOwner = psLINFBInfo->fbops->owner;

	if (psLINFBInfo->fbops->fb_release != NULL) 
	{
		(void) psLINFBInfo->fbops->fb_release(psLINFBInfo, 0);
	}

	module_put(psLINFBOwner);

	release_console_sem();
}

OMAPLFB_ERROR OMAPLFBInit(void)
{
	OMAPLFB_DEVINFO		*psDevInfo;

	psDevInfo = OMAPLFBGetAnchorPtr();
	
	if (psDevInfo == NULL)
	{
		PFN_CMD_PROC	 		pfnCmdProcList[OMAPLFB_COMMAND_COUNT];
		IMG_UINT32				aui32SyncCountList[OMAPLFB_COMMAND_COUNT][2];
		
		psDevInfo = (OMAPLFB_DEVINFO *)OMAPLFBAllocKernelMem(sizeof(OMAPLFB_DEVINFO));

		if(!psDevInfo)
		{
			return (OMAPLFB_ERROR_OUT_OF_MEMORY);
		}

		
		memset(psDevInfo, 0, sizeof(OMAPLFB_DEVINFO));

		
		OMAPLFBSetAnchorPtr((void*)psDevInfo);

		
		if(InitDev(psDevInfo) != OMAPLFB_OK)
		{
			return (OMAPLFB_ERROR_INIT_FAILURE);
		}

		if(OMAPLFBGetLibFuncAddr ("PVRGetDisplayClassJTable", &pfnGetPVRJTable) != OMAPLFB_OK)
		{
			return (OMAPLFB_ERROR_INIT_FAILURE);
		}

		
		if(!(*pfnGetPVRJTable)(&psDevInfo->sPVRJTable))
		{
			return (OMAPLFB_ERROR_INIT_FAILURE);
		}

		psDevInfo->sDisplayInfo.ui32MaxSwapChainBuffers = (IMG_UINT32)(psDevInfo->sFBInfo.ulFBSize / psDevInfo->sFBInfo.ulRoundedBufferSize);
		if (psDevInfo->sDisplayInfo.ui32MaxSwapChainBuffers != 0)
		{
			psDevInfo->sDisplayInfo.ui32MaxSwapChains = 1;
			psDevInfo->sDisplayInfo.ui32MaxSwapInterval = 1;
		}

		psDevInfo->sDisplayInfo.ui32PhysicalWidthmm = psDevInfo->sFBInfo.ulPhysicalWidthmm;
		psDevInfo->sDisplayInfo.ui32PhysicalHeightmm = psDevInfo->sFBInfo.ulPhysicalHeightmm;

		strncpy(psDevInfo->sDisplayInfo.szDisplayName, DISPLAY_DEVICE_NAME, MAX_DISPLAY_NAME_SIZE);
	
		psDevInfo->sDisplayFormat.pixelformat = psDevInfo->sFBInfo.ePixelFormat;
		psDevInfo->sDisplayDim.ui32Width      = (IMG_UINT32)psDevInfo->sFBInfo.ulWidth;
		psDevInfo->sDisplayDim.ui32Height     = (IMG_UINT32)psDevInfo->sFBInfo.ulHeight;
		psDevInfo->sDisplayDim.ui32ByteStride = (IMG_UINT32)psDevInfo->sFBInfo.ulByteStride;

		DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Maximum number of swap chain buffers: %lu\n",
			psDevInfo->sDisplayInfo.ui32MaxSwapChainBuffers));

		
		psDevInfo->sSystemBuffer.sSysAddr = psDevInfo->sFBInfo.sSysAddr;
		psDevInfo->sSystemBuffer.sCPUVAddr = psDevInfo->sFBInfo.sCPUVAddr;
		psDevInfo->sSystemBuffer.psDevInfo = psDevInfo;

		OMAPLFBInitBufferForSwap(&psDevInfo->sSystemBuffer);

		

		psDevInfo->sDCJTable.ui32TableSize = sizeof(PVRSRV_DC_SRV2DISP_KMJTABLE);
		psDevInfo->sDCJTable.pfnOpenDCDevice = OpenDCDevice;
		psDevInfo->sDCJTable.pfnCloseDCDevice = CloseDCDevice;
		psDevInfo->sDCJTable.pfnEnumDCFormats = EnumDCFormats;
		psDevInfo->sDCJTable.pfnEnumDCDims = EnumDCDims;
		psDevInfo->sDCJTable.pfnGetDCSystemBuffer = GetDCSystemBuffer;
		psDevInfo->sDCJTable.pfnGetDCInfo = GetDCInfo;
		psDevInfo->sDCJTable.pfnGetBufferAddr = GetDCBufferAddr;
		psDevInfo->sDCJTable.pfnCreateDCSwapChain = CreateDCSwapChain;
		psDevInfo->sDCJTable.pfnDestroyDCSwapChain = DestroyDCSwapChain;
		psDevInfo->sDCJTable.pfnSetDCDstRect = SetDCDstRect;
		psDevInfo->sDCJTable.pfnSetDCSrcRect = SetDCSrcRect;
		psDevInfo->sDCJTable.pfnSetDCDstColourKey = SetDCDstColourKey;
		psDevInfo->sDCJTable.pfnSetDCSrcColourKey = SetDCSrcColourKey;
		psDevInfo->sDCJTable.pfnGetDCBuffers = GetDCBuffers;
		psDevInfo->sDCJTable.pfnSwapToDCBuffer = SwapToDCBuffer;
		psDevInfo->sDCJTable.pfnSwapToDCSystem = SwapToDCSystem;
		psDevInfo->sDCJTable.pfnSetDCState = SetDCState;

		
		if(psDevInfo->sPVRJTable.pfnPVRSRVRegisterDCDevice (
			&psDevInfo->sDCJTable,
			&psDevInfo->ulDeviceID ) != PVRSRV_OK)
		{
			return (OMAPLFB_ERROR_DEVICE_REGISTER_FAILED);
		}
		
		
		pfnCmdProcList[DC_FLIP_COMMAND] = ProcessFlip;

		
		aui32SyncCountList[DC_FLIP_COMMAND][0] = 0; 
		aui32SyncCountList[DC_FLIP_COMMAND][1] = 2; 

		



		if (psDevInfo->sPVRJTable.pfnPVRSRVRegisterCmdProcList (psDevInfo->ulDeviceID,
																&pfnCmdProcList[0],
																aui32SyncCountList,
																OMAPLFB_COMMAND_COUNT) != PVRSRV_OK)
		{
			printk(KERN_WARNING DRIVER_PREFIX ": Can't register callback\n");
			return (OMAPLFB_ERROR_CANT_REGISTER_CALLBACK);
		}

	}

	
	psDevInfo->ulRefCount++;

	
	return (OMAPLFB_OK);
	
	}

OMAPLFB_ERROR OMAPLFBDeinit(void)
{
	OMAPLFB_DEVINFO *psDevInfo, *psDevFirst;

	psDevFirst = OMAPLFBGetAnchorPtr();
	psDevInfo = psDevFirst;

	
	if (psDevInfo == NULL)
	{
		return (OMAPLFB_ERROR_GENERIC);
	}

	
	psDevInfo->ulRefCount--;

	if (psDevInfo->ulRefCount == 0)
	{
		
		PVRSRV_DC_DISP2SRV_KMJTABLE	*psJTable = &psDevInfo->sPVRJTable;

		if (psDevInfo->sPVRJTable.pfnPVRSRVRemoveCmdProcList (psDevInfo->ulDeviceID, OMAPLFB_COMMAND_COUNT) != PVRSRV_OK)
		{
			return (OMAPLFB_ERROR_GENERIC);
		}

		
		if (psJTable->pfnPVRSRVRemoveDCDevice(psDevInfo->ulDeviceID) != PVRSRV_OK)
		{
			return (OMAPLFB_ERROR_GENERIC);
		}
		
		DeInitDev(psDevInfo);

		
		OMAPLFBFreeKernelMem(psDevInfo);
	}
	
	
	OMAPLFBSetAnchorPtr(NULL);

	
	return (OMAPLFB_OK);
}

