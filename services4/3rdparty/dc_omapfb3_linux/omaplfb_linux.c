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

#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
#endif

#include <linux/version.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/hardirq.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/fb.h>
#include <linux/console.h>
#include <linux/omapfb.h>

#include <mach/vrfb.h>
#if defined(DEBUG)
#define	PVR_DEBUG DEBUG
#undef DEBUG
#endif
#include <omapfb/omapfb.h>
#if defined(DEBUG)
#undef DEBUG
#endif
#if defined(PVR_DEBUG)
#define	DEBUG PVR_DEBUG
#undef PVR_DEBUG
#endif

#include "img_defs.h"
#include "servicesext.h"
#include "kerneldisplay.h"
#include "omaplfb.h"
#include "pvrmodule.h"

#if !defined(PVR_LINUX_USING_WORKQUEUES)
#error "PVR_LINUX_USING_WORKQUEUES must be defined"
#endif

MODULE_SUPPORTED_DEVICE(DEVNAME);

void *OMAPLFBAllocKernelMem(unsigned long ulSize)
{
	return kmalloc(ulSize, GFP_KERNEL);
}

void OMAPLFBFreeKernelMem(void *pvMem)
{
	kfree(pvMem);
}


OMAPLFB_ERROR OMAPLFBGetLibFuncAddr (char *szFunctionName, PFN_DC_GET_PVRJTABLE *ppfnFuncTable)
{
	if(strcmp("PVRGetDisplayClassJTable", szFunctionName) != 0)
	{
		return (OMAPLFB_ERROR_INVALID_PARAMS);
	}

	
	*ppfnFuncTable = PVRGetDisplayClassJTable;

	return (OMAPLFB_OK);
}

void OMAPLFBQueueBufferForSwap(OMAPLFB_SWAPCHAIN *psSwapChain, OMAPLFB_BUFFER *psBuffer)
{
	int res = queue_work(psSwapChain->psWorkQueue, &psBuffer->sWork);

	if (res == 0)
	{
		printk(KERN_WARNING DRIVER_PREFIX ": %s: buffer already on work queue\n", __FUNCTION__);
	}
}

static void WorkQueueHandler(struct work_struct *psWork)
{
	OMAPLFB_BUFFER *psBuffer = container_of(psWork, OMAPLFB_BUFFER, sWork);

	OMAPLFBSwapHandler(psBuffer);
}

OMAPLFB_ERROR OMAPLFBCreateSwapQueue(OMAPLFB_SWAPCHAIN *psSwapChain)
{
	
	psSwapChain->psWorkQueue = __create_workqueue(DEVNAME, 1, 1, 1);
	if (psSwapChain->psWorkQueue == NULL)
	{
		printk(KERN_WARNING DRIVER_PREFIX ": %s: create_singlethreaded_workqueue failed\n", __FUNCTION__);

		return (OMAPLFB_ERROR_INIT_FAILURE);
	}

	return (OMAPLFB_OK);
}

void OMAPLFBInitBufferForSwap(OMAPLFB_BUFFER *psBuffer)
{
	INIT_WORK(&psBuffer->sWork, WorkQueueHandler);
}

void OMAPLFBDestroySwapQueue(OMAPLFB_SWAPCHAIN *psSwapChain)
{
	destroy_workqueue(psSwapChain->psWorkQueue);
}

void OMAPLFBFlip(OMAPLFB_DEVINFO *psDevInfo, OMAPLFB_BUFFER *psBuffer)
{
	struct fb_var_screeninfo sFBVar;
	int res;
	unsigned long ulYResVirtual;

	acquire_console_sem();

	sFBVar = psDevInfo->psLINFBInfo->var;

	sFBVar.xoffset = 0;
	sFBVar.yoffset = psBuffer->ulYOffset;

	ulYResVirtual = psBuffer->ulYOffset + sFBVar.yres;

	
	if (sFBVar.xres_virtual != sFBVar.xres || sFBVar.yres_virtual < ulYResVirtual)
	{
		sFBVar.xres_virtual = sFBVar.xres;
		sFBVar.yres_virtual = ulYResVirtual;

		sFBVar.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;

		res = fb_set_var(psDevInfo->psLINFBInfo, &sFBVar);
		if (res != 0)
		{
			printk(KERN_INFO DRIVER_PREFIX ": %s: fb_set_var failed (Y Offset: %lu, Error: %d)\n", __FUNCTION__, psBuffer->ulYOffset, res);
		}
	}
	else
	{
		res = fb_pan_display(psDevInfo->psLINFBInfo, &sFBVar);
		if (res != 0)
		{
			printk(KERN_INFO DRIVER_PREFIX ": %s: fb_pan_display failed (Y Offset: %lu, Error: %d)\n", __FUNCTION__, psBuffer->ulYOffset, res);
		}
	}

	release_console_sem();
}

OMAPLFB_UPDATE_MODE OMAPLFBGetUpdateMode(OMAPLFB_DEVINFO *psDevInfo)
{
	struct omap_dss_device *psDSSDev = fb2display(psDevInfo->psLINFBInfo);
	enum omap_dss_update_mode eMode;

	if (psDSSDev == NULL || psDSSDev->get_update_mode == NULL)
	{
		DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX ": %s: can't get update mode\n", __FUNCTION__));
		return OMAPLFB_UPDATE_MODE_UNDEFINED;
	}

	eMode = psDSSDev->get_update_mode(psDSSDev);
	switch(eMode)
	{
		case OMAP_DSS_UPDATE_AUTO:
			return OMAPLFB_UPDATE_MODE_AUTO;
		case OMAP_DSS_UPDATE_MANUAL:
			return OMAPLFB_UPDATE_MODE_MANUAL;
		case OMAP_DSS_UPDATE_DISABLED:
			return OMAPLFB_UPDATE_MODE_DISABLED;
		default:
			DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX ": %s: unknown update mode (%d)\n", __FUNCTION__, eMode));
			break;
	}

	return OMAPLFB_UPDATE_MODE_UNDEFINED;
}

OMAPLFB_BOOL OMAPLFBSetUpdateMode(OMAPLFB_DEVINFO *psDevInfo, OMAPLFB_UPDATE_MODE eMode)
{
	struct omap_dss_device *psDSSDev = fb2display(psDevInfo->psLINFBInfo);
	enum omap_dss_update_mode eDSSMode;
	int res;

	if (psDSSDev == NULL || psDSSDev->set_update_mode == NULL)
	{
		DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX ": %s: can't set update mode\n", __FUNCTION__));
		return OMAPLFB_FALSE;
	}

	switch(eMode)
	{
		case OMAPLFB_UPDATE_MODE_AUTO:
			eDSSMode = OMAP_DSS_UPDATE_AUTO;
			break;
		case OMAPLFB_UPDATE_MODE_MANUAL:
			eDSSMode = OMAP_DSS_UPDATE_MANUAL;
			break;
		case OMAPLFB_UPDATE_MODE_DISABLED:
			eDSSMode = OMAP_DSS_UPDATE_DISABLED;
			break;
		default:
			DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX ": %s: unknown update mode (%d)\n", __FUNCTION__, eMode));
			return OMAPLFB_FALSE;
	}

	res = psDSSDev->set_update_mode(psDSSDev, eDSSMode);
	if (res != 0)
	{
		DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX ": %s: set_update_mode failed (%d)\n", __FUNCTION__, res));
	}

	return (res == 0);
}

OMAPLFB_BOOL OMAPLFBWaitForVSync(OMAPLFB_DEVINFO *psDevInfo)
{
	struct omap_dss_device *psDSSDev = fb2display(psDevInfo->psLINFBInfo);

	if (psDSSDev->wait_vsync != NULL)
	{
		int res = psDSSDev->wait_vsync(psDSSDev);
		if (res != 0)
		{
			DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX ": %s: wait for vsync failed (%d)\n", __FUNCTION__, res));
			return OMAPLFB_FALSE;
		}
	}

	return OMAPLFB_TRUE;
}

OMAPLFB_BOOL OMAPLFBManualSync(OMAPLFB_DEVINFO *psDevInfo)
{
	struct omap_dss_device *psDSSDev = fb2display(psDevInfo->psLINFBInfo);

	if (psDSSDev->sync != NULL)
	{
		int res = psDSSDev->sync(psDSSDev);
		if (res != 0)
		{
			printk(KERN_INFO DRIVER_PREFIX ": %s: sync failed (%d)\n", __FUNCTION__, res);
			return OMAPLFB_FALSE;
		}
	}

	return OMAPLFB_TRUE;
}

OMAPLFB_BOOL OMAPLFBCheckModeAndSync(OMAPLFB_DEVINFO *psDevInfo)
{
	OMAPLFB_UPDATE_MODE eMode = OMAPLFBGetUpdateMode(psDevInfo);

	switch(eMode)
	{
		case OMAPLFB_UPDATE_MODE_AUTO:
		case OMAPLFB_UPDATE_MODE_MANUAL:
			return OMAPLFBManualSync(psDevInfo);
		default:
			break;
	}

	return OMAPLFB_TRUE;
}

static int FrameBufferEvents(struct notifier_block *psNotif,
                             unsigned long event, void *data)
{
	OMAPLFB_DEVINFO *psDevInfo;
	struct fb_event *psFBEvent = (struct fb_event *)data;
	OMAPLFB_BOOL bBlanked;

	
	if (event != FB_EVENT_BLANK)
	{
		return 0;
	}

	psDevInfo = OMAPLFBGetAnchorPtr();

	bBlanked = (*(IMG_INT *)psFBEvent->data != 0) ? OMAPLFB_TRUE: OMAPLFB_FALSE;

	psDevInfo->bBlanked = bBlanked;

	return 0;
}


OMAPLFB_ERROR OMAPLFBUnblankDisplay(OMAPLFB_DEVINFO *psDevInfo)
{
	int res;

	acquire_console_sem();
	res = fb_blank(psDevInfo->psLINFBInfo, 0);
	release_console_sem();
	if (res != 0 && res != -EINVAL)
	{
		printk(KERN_WARNING DRIVER_PREFIX
			": fb_blank failed (%d)", res);
		return (OMAPLFB_ERROR_GENERIC);
	}

	return (OMAPLFB_OK);
}

OMAPLFB_ERROR OMAPLFBEnableLFBEventNotification(OMAPLFB_DEVINFO *psDevInfo)
{
	int                res;
	OMAPLFB_ERROR         eError;

	
	memset(&psDevInfo->sLINNotifBlock, 0, sizeof(psDevInfo->sLINNotifBlock));

	psDevInfo->sLINNotifBlock.notifier_call = FrameBufferEvents;

	psDevInfo->bBlanked = OMAPLFB_FALSE;

	res = fb_register_client(&psDevInfo->sLINNotifBlock);
	if (res != 0)
	{
		printk(KERN_WARNING DRIVER_PREFIX
			": fb_register_client failed (%d)", res);

		return (OMAPLFB_ERROR_GENERIC);
	}

	eError = OMAPLFBUnblankDisplay(psDevInfo);
	if (eError != OMAPLFB_OK)
	{
		printk(KERN_WARNING DRIVER_PREFIX
			": UnblankDisplay failed (%d)", eError);
		return eError;
	}

	return (OMAPLFB_OK);
}

OMAPLFB_ERROR OMAPLFBDisableLFBEventNotification(OMAPLFB_DEVINFO *psDevInfo)
{
	int res;

	
	res = fb_unregister_client(&psDevInfo->sLINNotifBlock);
	if (res != 0)
	{
		printk(KERN_WARNING DRIVER_PREFIX
			": fb_unregister_client failed (%d)", res);
		return (OMAPLFB_ERROR_GENERIC);
	}

	psDevInfo->bBlanked = OMAPLFB_FALSE;

	return (OMAPLFB_OK);
}

static int __init OMAPLFB_Init(void)
{

	if(OMAPLFBInit() != OMAPLFB_OK)
	{
		printk(KERN_WARNING DRIVER_PREFIX ": OMAPLFB_Init: OMAPLFBInit failed\n");
		return -ENODEV;
	}

	return 0;

}

static IMG_VOID __exit OMAPLFB_Cleanup(IMG_VOID)
{    
	if(OMAPLFBDeinit() != OMAPLFB_OK)
	{
		printk(KERN_WARNING DRIVER_PREFIX ": OMAPLFB_Cleanup: OMAPLFBDeinit failed\n");
	}
}

late_initcall(OMAPLFB_Init);
module_exit(OMAPLFB_Cleanup);

