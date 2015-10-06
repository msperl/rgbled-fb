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
 */

#include <linux/device.h>
#include <linux/fb.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>

#include "rgbled-fb.h"

static int rgbled_probe_of_panel(struct rgbled_fb *rfb,
				 struct device_node *nc,
				 struct rgbled_panel_info *template)
{
	struct device *dev = rfb->info->device;
	struct rgbled_panel_info *panel;
	u32 tmp;
	char *prop;
	int err;

	panel = devm_kzalloc(dev, sizeof(*panel), GFP_KERNEL);
	if (!panel)
		return -ENOMEM;

	/* copy the default panel data */
	memcpy(panel, template, sizeof(*panel));

	/* copy the full name (including @...) */
	panel->name = nc->kobj.name;

	/* and set device node reference */
	panel->of_node = nc;

	/* now fill in from the device tree the overrides */
	if (of_property_read_u32_index(nc, "reg",    0, &panel->id))
		return -EINVAL;

	/* basic layout stuff */
	of_property_read_u32_index(nc, "x",      0, &panel->x);
	of_property_read_u32_index(nc, "y",      0, &panel->y);
	prop = "layout-y-x";
	if (of_find_property(nc, prop, 0)) {
		if (panel->flags & RGBLED_FLAG_CHANGE_LAYOUT)
			panel->layout_yx = true;
		else
			goto parse_error;
	}
	prop = "inverted-x";
	if (of_find_property(nc, prop, 0)) {
		if (panel->flags & RGBLED_FLAG_CHANGE_LAYOUT)
			panel->inverted_x = true;
		else
			goto parse_error;
	}
	prop = "inverted-y";
	if (of_find_property(nc, prop, 0)) {
		if (panel->flags & RGBLED_FLAG_CHANGE_LAYOUT)
			panel->inverted_y = true;
		else
			goto parse_error;
	}
	prop = "meander";
	if (of_find_property(nc, prop, 0)) {
		if (panel->flags & RGBLED_FLAG_CHANGE_LAYOUT)
			panel->get_pixel_coords =
				rgbled_get_pixel_coords_meander;
		else
			goto parse_error;
	}

	prop = "width";
	if (!of_property_read_u32_index(nc, prop,  0, &tmp)) {
		if (panel->flags & RGBLED_FLAG_CHANGE_WIDTH)
			panel->width = tmp;
		else
			goto parse_error;
	}
	prop = "height";
	if (!of_property_read_u32_index(nc, prop,  0, &tmp)) {
		if (panel->flags & RGBLED_FLAG_CHANGE_HEIGHT)
			panel->height = tmp;
		else
			goto parse_error;
	}
	prop = "pitch";
	if (!of_property_read_u32_index(nc, prop,  0, &tmp)) {
		if (panel->flags & RGBLED_FLAG_CHANGE_PITCH)
			panel->pitch = tmp;
		else
			goto parse_error;
	}
	prop = "current-limit";
	if (!of_property_read_u32_index(nc, prop,  0, &tmp)) {
		if (panel->current_limit)
			panel->current_limit = min(tmp, panel->current_limit);
		else
			panel->current_limit = tmp;
	}
	/* multiple towards the end */
	prop = "multiple";
	if (!of_property_read_u32_index(nc, prop,  0, &tmp)) {
		if (panel->multiple) {
			err = panel->multiple(panel, tmp);
			if (err)
				return err;
		} else {
			goto parse_error;
		}
	}

	/* finally brightness */
	if (!of_property_read_u32_index(nc, "brightness", 0, &tmp))
		panel->brightness = min_t(u32, tmp, 255);
	else
		panel->brightness = 255;

	/* expose alll leds via sysfs */
	if (of_find_property(nc, "linux,expose-all-led", NULL))
		panel->expose_all_led = true;

	/* register Device */
	return rgbled_register_panel(rfb, panel);

parse_error:
	fb_err(rfb->info,
	       "\"%s\" property not allowed in %s\n",
	       prop, panel->name);
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
	for (i = 0 ; panels[i].compatible ; i++) {
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
		err = rgbled_scan_panels_match(rfb, nc, panels);
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

	if (!of_property_read_u32_index(nc, "brightness", 0, &tmp))
		rfb->brightness = min_t(u32, tmp, 255);
	else
		rfb->brightness = 255;

	/* trigger the exposure of all leds */
	if (of_find_property(nc, "linux,expose-all-led", NULL))
		rfb->expose_all_led = true;

	return 0;
}

static int rgbled_register_panel_single_sysled(
	struct rgbled_fb *rfb,
	struct rgbled_panel_info *panel,
	struct device_node *nc)
{
	struct fb_info *fb = rfb->info;
	struct rgbled_coordinates coord;
	const char *label = NULL;
	const char *trigger = NULL;
	const char *channel_str;
	enum rgbled_pixeltype channel;
	u32 pix;
	int len;
	struct property *prop;

	/* if no property reg then return */
	prop = of_find_property(nc, "reg", &len);
	if (!prop) {
		fb_err(fb, "missing reg property in %s\n", nc->name);
		return -EINVAL;
	}
	/* channel */
	if (!of_property_read_string(nc, "channel", &channel_str)) {
		fb_err(fb, "missing channel property in %s\n", nc->name);
		return -EINVAL;
	}
	if (strcmp(channel_str, "red") == 0) {
		channel = rgbled_pixeltype_red;
	} else if (strcmp(channel_str, "green") == 0) {
		channel = rgbled_pixeltype_green;
	} else if (strcmp(channel_str, "blue") == 0) {
		channel = rgbled_pixeltype_blue;
	} else if (strcmp(channel_str, "brightness") == 0) {
		channel = rgbled_pixeltype_brightness;
	} else {
		fb_err(fb, "wrong channel property value %s in %s\n",
		       channel_str, nc->name);
		return -EINVAL;
	}

	/* check for 1d/2d */
	switch (len) {
	case 1: /* 1d approach */
		/* no error checking needed */
		of_property_read_u32_index(nc, "reg", 0, &pix);
		if (pix >= panel->pixel) {
			fb_err(fb, "reg value %i is out of range in %s\n",
			       pix, nc->name);
			return -EINVAL;
		}
		/* translate coordinates */
		rgbled_get_pixel_coords(rfb, panel, pix, &coord);

		break;
	case 2: /* need to handle the 2d case */
	default:
		fb_err(fb,
		       "unexpected number of arguments in reg(%i) in %s\n",
			len, nc->name);
		return -EINVAL;
	}
	/* now we got the coordinates, so run the rest */

	/* define the label */
	if (of_property_read_string(nc, "label", &label))
		label = nc->name;
	if (!label)
		return -EINVAL;

	/* read the trigger */
	of_property_read_string(nc, "linux,default-trigger", &trigger);

	/* and now register it for real */
	return rgbled_register_single_sysled(rfb, panel,
					     label, &coord,
					     channel, trigger);
}

#if 0
static int rgbled_register_panel_sysled_of(struct rgbled_fb *rfb,
					   struct rgbled_panel_info *panel)
{
	struct device_node *bnc = panel->of_node;
	struct device_node *nc;
	int err;

	/* iterate all given sub */
	for_each_available_child_of_node(bnc, nc) {
		err = rgbled_register_panel_single_sysled(rfb, panel, nc);
		if (err) {
			of_node_put(nc);
			return err;
		}
	}

	return 0;
}

#endif
