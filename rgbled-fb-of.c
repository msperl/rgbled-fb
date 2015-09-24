/*
 *  linux/drivers/video/fb/rgbled-fb.c
 *
 *  (c) Martin Sperl <kernel@martin.sperl.org>
 *
 *  generic Frame buffer code for LED strips
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/device.h>
#include <linux/fb.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/list.h>
#include <linux/module.h>

#include "rgbled-fb.h"

static int rgbled_probe_of_panel(struct rgbled_fb *rfb,
				struct device_node *nc,
				struct rgbled_panel_info *panel)
{
	struct device *dev = rfb->info->device;
	struct rgbled_panel_info *bi;
	u32 tmp;
	char *prop;
	int err;

	bi = devm_kzalloc(dev, sizeof(*bi), GFP_KERNEL);
	if (!bi)
		return -ENOMEM;

	/* copy the default panel data */
	memcpy(bi, panel, sizeof(*bi));

	/* add to list */
	list_add(&bi->list, &rfb->panels);

	/* copy the full name (including @...) */
	bi->name = nc->kobj.name;

	/* and set device node reference */
	bi->of_node = nc;

	/* now fill in from the device tree the overrides */
	if (of_property_read_u32_index(nc, "reg",    0, &bi->id))
		return -EINVAL;

	/* basic layout stuff */
	of_property_read_u32_index(nc, "x",      0, &bi->x);
	of_property_read_u32_index(nc, "y",      0, &bi->y);
	prop = "layout-y-x";
	if (of_find_property(nc, prop,0)) {
		if (bi->flags & RGBLED_FLAG_CHANGE_LAYOUT)
			bi->layout_yx = true;
		else
			goto parse_error;
	}
	prop = "inverted-x";
	if (of_find_property(nc, prop,0)) {
		if (bi->flags & RGBLED_FLAG_CHANGE_LAYOUT)
			bi->inverted_x = true;
		else
			goto parse_error;
	}
	prop = "inverted-y";
	if (of_find_property(nc, prop,0)) {
		if (bi->flags & RGBLED_FLAG_CHANGE_LAYOUT)
			bi->inverted_y = true;
		else
			goto parse_error;
	}
	prop = "meander";
	if (of_find_property(nc, prop,0)) {
		if (bi->flags & RGBLED_FLAG_CHANGE_LAYOUT)
			bi->getPixelCoords = rgbled_getPixelCoords_meander;
		else
			goto parse_error;
	}

	prop = "width";
	if (!of_property_read_u32_index(nc, prop,  0, &tmp)) {
		if (bi->flags & RGBLED_FLAG_CHANGE_WIDTH)
			bi->width = tmp;
		else
			goto parse_error;
	}
	prop = "height";
	if (!of_property_read_u32_index(nc, prop,  0, &tmp)) {
		if (bi->flags & RGBLED_FLAG_CHANGE_HEIGHT)
			bi->height = tmp;
		else
			goto parse_error;
	}
	prop = "pitch";
	if (!of_property_read_u32_index(nc, prop,  0, &tmp)) {
		if (bi->flags & RGBLED_FLAG_CHANGE_PITCH)
			bi->pitch = tmp;
		else
			goto parse_error;
	}
	prop = "current-limit";
	if (!of_property_read_u32_index(nc, prop,  0, &tmp)) {
		if (bi->current_limit)
			bi->current_limit = min(tmp, bi->current_limit);
		else
			bi->current_limit = tmp;
	}
	/* multiple towards the end */
	prop = "multiple";
	if (!of_property_read_u32_index(nc, prop,  0, &tmp)) {
		if (bi->multiple) {
			err = bi->multiple(bi, tmp);
			if (err)
				return err;
		} else {
			goto parse_error;
		}
	}

	/* finally brightness */
	if (!of_property_read_u32_index(nc, "brightness",0, &tmp))
		bi->brightness = min_t(u32, tmp, 255);
	else
		bi->brightness = 255;

	/* calculate size of panel in pixel if not set */
	if (!bi->pixel)
		bi->pixel = bi->width * bi->height;

	/* check values */
	if (!bi->width)
		return -EINVAL;
	if (!bi->height)
		return -EINVAL;
	if (!bi->pixel)
		return -EINVAL;

	/* add the number of pixel to the chain */
	rfb->pixel += bi->pixel;

	/* setting max coordinates for the framebuffer */
	if (rfb->width < bi->x + bi->width)
		rfb->width = bi->x + bi->width;
	if (rfb->height < bi->x + bi->height)
		rfb->height = bi->y + bi->height;

	return 0;

parse_error:
	fb_err(rfb->info,
		"\"%s\" property not allowed in %s\n",
		prop, bi->name);
	return -EINVAL;
}

int rgbled_scan_panels_match(struct rgbled_fb *rfb,
			     struct device_node *nc,
			     struct rgbled_panel_info *panels)
{
	struct device *dev = rfb->info->device;
	int i;
	int idx;

	/* get the compatible property */
	for(i = 0 ; panels[i].compatible ; i++) {
		idx = of_property_match_string(nc, "compatible",
					panels[i].compatible);
		if (idx >= 0) {
			return rgbled_probe_of_panel(rfb, nc,
						     &panels[i]);
		}
	}

	/* not matching */
	dev_err(dev, "Incompatible node %s found\n", nc->name);
	return -EINVAL;
}

int rgbled_scan_panels_of(struct rgbled_fb *rfb,
			  struct rgbled_panel_info *panels)
{
	struct device_node *nc;
	struct device *dev = rfb->info->device;
	int err;

	/* iterate over all entries in the device-tree */
	for_each_available_child_of_node(dev->of_node, nc) {
		err = rgbled_scan_panels_match(rfb, nc, panels) ;
		if (err) {
			of_node_put(nc);
			return err;
		}
	}

	return 0;
}

int rgbled_register_of(struct rgbled_fb *rfb)
{
	struct fb_info *fb = rfb->info;
	struct device_node *nc = fb->device->of_node;
	u32 tmp;

	/* some basics */
	rfb->of_node = nc;
	if (!rfb->name)
		rfb->name = nc->kobj.name;

	/* read brightness and current limits from device-tree */
	of_property_read_u32_index(nc, "current-limit",
				   0, &rfb->current_limit);
	of_property_read_u32_index(nc, "led-current-max-red",
				   0, &rfb->led_current_max_red);
	of_property_read_u32_index(nc, "led-current-max-green",
				   0, &rfb->led_current_max_green);
	of_property_read_u32_index(nc, "led-current-max-blue",
				   0, &rfb->led_current_max_blue);
	of_property_read_u32_index(nc, "led-current-base",
				   0, &rfb->led_current_base);

	if (!of_property_read_u32_index(nc, "brightness",0, &tmp))
		rfb->brightness = min_t(u32, tmp, 255);
	else
		rfb->brightness = 255;

	return 0;
}
