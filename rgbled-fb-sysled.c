/*
 *  linux/drivers/video/fb/rgbled-fb.c
 *
 *  (c) Martin Sperl <kernel@martin.sperl.org>
 *
 *  generic Frame buffer code for LED strips
 *  sysfs led implementation
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

static int rgbled_register_sysfs_boards(struct rgbled_fb *rfb)
{
	return 0;
}

#define SYSFS_HELPER_SHOW(name, field)					\
	static ssize_t name ## _show(struct device *dev,		\
				struct device_attribute *a,		\
				char *buf)				\
	{								\
		struct fb_info *fb = dev_get_drvdata(dev);		\
		struct rgbled_fb *rfb = fb->par;			\
		u32 val;						\
									\
		spin_lock(&rfb->lock);					\
		val = rfb->field;					\
		spin_unlock(&rfb->lock);				\
									\
		return sprintf(buf, "%i\n",val);			\
	}

#define SYSFS_HELPER_STORE(name, field, max)				\
	static ssize_t name ## _store(struct device *dev,		\
				struct device_attribute *a,		\
				const char *buf, size_t count)		\
	{								\
		struct fb_info *fb = dev_get_drvdata(dev);		\
		struct rgbled_fb *rfb = fb->par;			\
		char *end;						\
		u32 val = simple_strtoul(buf, &end, 0);			\
									\
		if (end == buf)						\
			return -EINVAL;					\
		if (val > max)						\
			return -EINVAL;					\
									\
		spin_lock(&rfb->lock);					\
		rfb->field = val;					\
		rfb->current_max = 0;					\
		spin_unlock(&rfb->lock);				\
									\
		rgbled_schedule(fb);					\
									\
		return count;						\
	}

#define SYSFS_HELPER_RO(name, field)					\
	SYSFS_HELPER_SHOW(name, field);					\
	static DEVICE_ATTR_RO(name)

#define SYSFS_HELPER_RW(name, field, maxv)				\
	SYSFS_HELPER_SHOW(name, field);					\
	SYSFS_HELPER_STORE(name, field, maxv);				\
	static DEVICE_ATTR_RW(name)

/* this is unfortunately needed - otherwise "current" is expanded by cpp */
#undef current

SYSFS_HELPER_RW(brightness, brightness, 255);
SYSFS_HELPER_RO(current, current_current);
SYSFS_HELPER_RO(current_max, current_max);
SYSFS_HELPER_RW(current_limit, current_limit, 100000000);
SYSFS_HELPER_RW(led_max_current_red, max_current_red, 10000);
SYSFS_HELPER_RW(led_max_current_green, max_current_green, 10000);
SYSFS_HELPER_RW(led_max_current_blue, max_current_blue, 10000);
SYSFS_HELPER_RO(led_count, pixel);
SYSFS_HELPER_RO(updates, screen_updates);

static struct device_attribute *device_attrs[] = {
	&dev_attr_brightness,
	&dev_attr_current,
	&dev_attr_current_max,
	&dev_attr_current_limit,
	&dev_attr_led_max_current_red,
	&dev_attr_led_max_current_green,
	&dev_attr_led_max_current_blue,
	&dev_attr_led_count,
	&dev_attr_updates,
};

int rgbled_register_sysfs(struct rgbled_fb *rfb)
{
	struct fb_info *fb = rfb->info;
	int i;
	int err = 0;

	/* register boards */
	err = rgbled_register_sysfs_boards(rfb);
	if (err)
		return err;

	/* register all device_attributes */
	for (i = 0; i < ARRAY_SIZE(device_attrs); i++) {
		/* create the file */
		err = device_create_file(fb->dev, device_attrs[i]);
		if (err)
			break;
	}

	if (err) {
		while (--i >= 0)
			device_remove_file(fb->dev, device_attrs[i]);
	}

	return err;
}

struct rgbled_led_data {
	struct led_classdev cdev;
	struct rgbled_fb *rfb;
	struct rgbled_pixel *pixel;
	enum rgbled_pixeltype type;
};

static void rgbled_unregister_single_led(struct device *dev, void *res)
{
	struct rgbled_led_data *led = res;

	led_classdev_unregister(&led->cdev);
}

static void rgbled_brightness_set(struct led_classdev *led_cdev,
				  enum led_brightness brightness)
{
	struct rgbled_led_data *led = container_of(led_cdev,
						   typeof(*led), cdev);
	/* set the value itself */
	switch (led->type) {
	case rgbled_pixeltype_red:
		led->pixel->red = brightness; break;
	case rgbled_pixeltype_green:
		led->pixel->green = brightness; break;
	case rgbled_pixeltype_blue:
		led->pixel->blue = brightness; break;
	case rgbled_pixeltype_brightness:
		led->pixel->brightness = brightness;
		break;
	}
	/* modifications to make things visible if nothing is set */
	switch (led->type) {
	case rgbled_pixeltype_red:
	case rgbled_pixeltype_green:
	case rgbled_pixeltype_blue:
		if(!led->pixel->brightness)
			led->pixel->brightness= 255;
		break;
	case rgbled_pixeltype_brightness:
		/* if the rgb values are off, so set to white */
		if ((!led->pixel->red) &&
		    (!led->pixel->green) &&
		    (!led->pixel->blue)) {
			led->pixel->red = 255;
			led->pixel->green = 255;
			led->pixel->blue = 255;
		}
		break;
	}

	rgbled_schedule(led->rfb->info);
}

static enum led_brightness rgbled_brightness_get(struct led_classdev *led_cdev)
{
	struct rgbled_led_data *led = container_of(led_cdev,
						   typeof(*led), cdev);
	switch (led->type) {
	case rgbled_pixeltype_red:
		return led->pixel->red;
	case rgbled_pixeltype_green:
		return led->pixel->green;
	case rgbled_pixeltype_blue:
		return led->pixel->blue;
	case rgbled_pixeltype_brightness:
		return led->pixel->brightness;
	}

	return 0;
}

static int rgbled_register_single_led(struct rgbled_fb *rfb,
				      struct rgbled_board_info *board,
				      const char *label,
				      struct rgbled_coordinates *coord,
				      enum rgbled_pixeltype type,
				      const char *trigger)
{
	struct rgbled_led_data *led;
	struct rgbled_pixel *vpix;
	int err;

	/* get the pixel */
	vpix = rgbled_getPixel(rfb, coord);
	if (!vpix)
		return -EINVAL;

	/* get a new led instance */
	led = devres_alloc(rgbled_unregister_single_led,
			   sizeof(*led), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	led->rfb = rfb;

	led->cdev.name = devm_kstrdup(rfb->info->dev, label, GFP_KERNEL);
	led->cdev.max_brightness = 255;

	led->cdev.brightness_set = rgbled_brightness_set;
	led->cdev.brightness_get = rgbled_brightness_get;

	led->cdev.default_trigger = trigger;
	led->pixel = vpix;
	led->type = type;

	/* register the led */
	err = led_classdev_register(rfb->info->dev, &led->cdev);
	if (err) {
		devres_free(led);
		return err;
	}

	/* add the resource to get removed */
	devres_add(rfb->info->device, led);

	return 0;
}

static int rgbled_register_board_led_single(struct rgbled_fb *rfb,
					    struct rgbled_board_info *board,
					    struct device_node *nc)
{
	struct fb_info * fb = rfb->info;
	struct rgbled_coordinates coord;
	const char *label = NULL;
	const char *trigger = NULL;
	const char *channel_str;
	enum rgbled_pixeltype channel;
	u32 pix;
	int len;
	struct property *prop = of_find_property(nc, "reg", &len);

	/* if no property then return */
	if (!prop) {
		fb_err(fb, "missing reg property in %s\n", nc->name);
		return -EINVAL;
	}
	/* channel */
	if (!of_property_read_string(nc, "channel", &channel_str)) {
		fb_err(fb, "missing channel property in %s\n", nc->name);
		return -EINVAL;
	}
	if (strcmp(channel_str, "red") == 0)
		channel = rgbled_pixeltype_red;
	else if (strcmp(channel_str, "green") == 0)
		channel = rgbled_pixeltype_green;
	else if (strcmp(channel_str, "blue") == 0)
		channel = rgbled_pixeltype_blue;
	else if (strcmp(channel_str, "brightness") == 0)
		channel = rgbled_pixeltype_brightness;
	else {
		fb_err(fb, "wrong channel property value %s in %s\n",
		       channel_str, nc->name);
		return -EINVAL;
	}


	/* check for 1d/2d */
	switch (len) {
	case 1: /* 1d approach */
		/* no error checking needed */
		of_property_read_u32_index(nc, "reg", 0, &pix);
		if (pix >= board->pixel) {
			fb_err(fb, "reg value %i is out of range in %s\n",
				pix, nc->name);
			return -EINVAL;
		}
		/* translate coordinates */
		if (board->getPixelCoords)
			board->getPixelCoords(rfb, board,
						pix, &coord);
		else
			rgbled_getPixelCoords_linear(rfb,board,
						     pix, &coord);
		break;
	case 2: /* need to handle the 2d case */
	default:
		fb_err(fb,
		       "unexpected number of arguments in reg(%i) in %s\n",
			len, nc->name);
		return -EINVAL;
		break;
	}
	/* now we got the coordinates, so run the rest */

	/* define the label */
	if (of_property_read_string(nc, "label", &label)) {
		label = nc->name;
	}
	if (!label)
		return -EINVAL;

	/* read the trigger */
	of_property_read_string(nc, "linux,default-trigger", &trigger);

	/* and now register it for real */
	return rgbled_register_single_led(rfb, board,
					  label, &coord,
					  channel, trigger);
}

static int rgbled_register_board_led_all(struct rgbled_fb *rfb,
					 struct rgbled_board_info *board)
{
	int i, err;
	char label[32];
	struct rgbled_coordinates coord;

	for(i=0; i < board->pixel; i++) {
		/* translate coordinates */
		if (board->getPixelCoords)
			board->getPixelCoords(rfb, board,
					i, &coord);
		else
			rgbled_getPixelCoords_linear(rfb,board,
						     i, &coord);
		/* now register the individual led components */
		snprintf(label, sizeof(label), "%s:%i:%i:red",
			 rfb->name, coord.x, coord.y);
		err = rgbled_register_single_led(rfb, board, label, &coord,
						 rgbled_pixeltype_red,
					         NULL);
		if (err)
			return err;

		snprintf(label, sizeof(label), "%s:%i:%i:green",
			 rfb->name, coord.x, coord.y);
		err = rgbled_register_single_led(rfb, board, label, &coord,
						 rgbled_pixeltype_green,
						 NULL);
		if (err)
			return err;

		snprintf(label, sizeof(label), "%s:%i:%i:blue",
			 rfb->name, coord.x, coord.y);
		err = rgbled_register_single_led(rfb, board, label, &coord,
						 rgbled_pixeltype_blue,
						 NULL);
		if (err)
			return err;

		snprintf(label, sizeof(label), "%s:%i:%i:brightness",
			 rfb->name, coord.x, coord.y);
		err = rgbled_register_single_led(rfb, board, label, &coord,
						 rgbled_pixeltype_brightness,
						 NULL);
		if (err)
			return err;
	}

	return 0;
}

static int rgbled_register_board_led(struct rgbled_fb *rfb,
				     struct rgbled_board_info *board)
{
	struct device_node *rnc = rfb->of_node;
	struct device_node *bnc = board->of_node;
	struct device_node *nc;
	int err = 0;

	/* iterate all defined */
	for_each_available_child_of_node(bnc, nc) {
		err = rgbled_register_board_led_single(rfb, board, nc);
		if (err) {
			of_node_put(nc);
			return err;
		}
	}

	/* if we are defined then expose all */
	if (of_find_property(rnc, "linux,expose-all-led", NULL) ||
	    of_find_property(bnc, "linux,expose-all-led", NULL))
		err = rgbled_register_board_led_all(rfb, board);

	return err;
}

int rgbled_register_boards(struct rgbled_fb *rfb)
{
	struct rgbled_board_info *board;
	int err;

	/* register each in each board - if given */
	list_for_each_entry(board, &rfb->boards, list) {
		err = rgbled_register_board_led(rfb, board);
		if (err)
			return err;
	}

	return 0;
}
