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
#include <linux/kobject.h>
#include <linux/leds.h>
#include <linux/list.h>
#include <linux/list_sort.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/vmalloc.h>

#include "rgbled-fb.h"

/* exposure of component data in sysfs */
#if 0
#define SYSFS_PANEL_HELPER_SHOW(name, field)				\
	static ssize_t panel_ ## name ## _show(struct device *dev,	\
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
		return sprintf(buf, "%i\n", val);			\
	}
#define SYSFS_PANEL_HELPER_RO(name, field)				\
	SYSFS_PANEL_HELPER_SHOW(name, field)				\
	static struct panel_##name## DEVICE_ATTR_RO(name)

#undef current
SYSFS_HELPER_RO(current, current_active);
SYSFS_HELPER_RO(current_max, current_max);

struct attribute *panel_attrs[]={
	NULL,
};

struct kobj_type panel_ktype = {
	.default_attrs =  panel_attrs,
};
#endif
static int rgbled_register_sysfs_panels(struct rgbled_fb *rfb)
{
#if 0
	struct fb_info *fb = rfb->info;
	struct rgbled_panel_info *panel;
	int err;

	/* iterate over all panels */
	list_for_each_entry(panel, &rfb->panels, list) {
		/* init and register kobject */
		printk(KERN_ERR "Register : %s\n", panel->name);
		err = kobject_init_and_add(&panel->kobj,
					   &panel_ktype,
					   &fb->dev->kobj,
					   panel->name);
		if (err)
			return err;
	}
#endif
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
		return sprintf(buf, "%i\n", val);			\
	}

#define SYSFS_HELPER_STORE(name, field, max)				\
	static ssize_t name ## _store(struct device *dev,		\
				struct device_attribute *a,		\
				const char *buf, size_t count)		\
	{								\
		struct fb_info *fb = dev_get_drvdata(dev);		\
		struct rgbled_fb *rfb = fb->par;			\
		int err;						\
		u32 val;						\
									\
		err = kstrtou32(buf, 0, &val);				\
		if (err)						\
			return err;					\
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
	SYSFS_HELPER_SHOW(name, field)					\
	static DEVICE_ATTR_RO(name)

#define SYSFS_HELPER_RW(name, field, maxv)				\
	SYSFS_HELPER_SHOW(name, field)					\
	SYSFS_HELPER_STORE(name, field, maxv)				\
	static DEVICE_ATTR_RW(name)

/* this is unfortunately needed - otherwise "current" is expanded by cpp */
#undef current

SYSFS_HELPER_RW(brightness, brightness, 255);
SYSFS_HELPER_RO(current, current_active);
SYSFS_HELPER_RO(current_max, current_max);
SYSFS_HELPER_RW(current_limit, current_limit, 100000000);
SYSFS_HELPER_RW(led_current_max_red, led_current_max_red, 10000);
SYSFS_HELPER_RW(led_current_max_green, led_current_max_green, 10000);
SYSFS_HELPER_RW(led_current_max_blue, led_current_max_blue, 10000);
SYSFS_HELPER_RW(led_current_base, led_current_base, 10000);
SYSFS_HELPER_RO(led_count, pixel);
SYSFS_HELPER_RO(updates, screen_updates);

static struct device_attribute *device_attrs[] = {
	&dev_attr_brightness,
	&dev_attr_current,
	&dev_attr_current_max,
	&dev_attr_current_limit,
	&dev_attr_led_current_max_red,
	&dev_attr_led_current_max_green,
	&dev_attr_led_current_max_blue,
	&dev_attr_led_current_base,
	&dev_attr_led_count,
	&dev_attr_updates,
};

int rgbled_register_sysfs(struct rgbled_fb *rfb)
{
	struct fb_info *fb = rfb->info;
	int i;
	int err = 0;

	/* register panels */
	err = rgbled_register_sysfs_panels(rfb);
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

/* sysled support */
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
		if (!led->pixel->brightness)
			led->pixel->brightness = 255;
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
				      struct rgbled_panel_info *panel,
				      const char *label,
				      struct rgbled_coordinates *coord,
				      enum rgbled_pixeltype type,
				      const char *trigger)
{
	struct rgbled_led_data *led;
	struct rgbled_pixel *vpix;
	int err;

	/* get the pixel */
	vpix = rgbled_get_raw_pixel(rfb, coord);
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

static int rgbled_register_panel_led_all(struct rgbled_fb *rfb,
					 struct rgbled_panel_info *panel)
{
	int i, err;
	char label[32];
	struct rgbled_coordinates coord;

	for (i = 0; i < panel->pixel; i++) {
		/* translate coordinates */
		rgbled_get_pixel_coords(rfb, panel, i, &coord);

		/* now register the individual led components */
		snprintf(label, sizeof(label), "%s:%i:%i:red",
			 rfb->name, coord.x, coord.y);
		err = rgbled_register_single_led(rfb, panel, label, &coord,
						 rgbled_pixeltype_red,
						 NULL);
		if (err)
			return err;

		snprintf(label, sizeof(label), "%s:%i:%i:green",
			 rfb->name, coord.x, coord.y);
		err = rgbled_register_single_led(rfb, panel, label, &coord,
						 rgbled_pixeltype_green,
						 NULL);
		if (err)
			return err;

		snprintf(label, sizeof(label), "%s:%i:%i:blue",
			 rfb->name, coord.x, coord.y);
		err = rgbled_register_single_led(rfb, panel, label, &coord,
						 rgbled_pixeltype_blue,
						 NULL);
		if (err)
			return err;

		snprintf(label, sizeof(label), "%s:%i:%i:brightness",
			 rfb->name, coord.x, coord.y);
		err = rgbled_register_single_led(rfb, panel, label, &coord,
						 rgbled_pixeltype_brightness,
						 NULL);
		if (err)
			return err;
	}

	return 0;
}

int rgbled_register_panel_sysled(struct rgbled_fb *rfb,
				 struct rgbled_panel_info *panel)
{
	int err = 0;

	/* TODO: expose specific leds with custom settings */

	/* expose all leds via led-api if requested */
	if (rfb->expose_all_led || panel->expose_all_led)
		err = rgbled_register_panel_led_all(rfb, panel);

	return err;
}

/* all the comented out are to get filled in */
static struct fb_fix_screeninfo fb_fix_screeninfo_default = {
	.id		= "rgbled-fb", /* overrridden by implementation */
	.type		= FB_TYPE_PACKED_PIXELS,
	.visual		= FB_VISUAL_TRUECOLOR,
	.xpanstep	= 0,
	.ypanstep	= 0,
	.ywrapstep	= 0,
	.accel		= FB_ACCEL_NONE,
	/* .line_length	= sizeof(ws2812b_pixel) * width */
	/* .smem_start	= 0 */
	/* .smem_len	= sizeof(ws2812b_pixel) * width * height */
	.capabilities	= 0,
};

static struct fb_var_screeninfo fb_var_screeninfo_default = {
	/*
	  .xres		= width,
	  .yres		= height,
	  .xres_virtual	= width,
	  .yres_virtual	= height,
	  .width		= width * xscale;
	  .height		= width * yscale;
	*/
	.bits_per_pixel  = 8 * sizeof(struct rgbled_pixel),
#define OFFSETS(name) {						\
		8 * offsetof(struct rgbled_pixel, name),	\
		8 * sizeof((((struct rgbled_pixel *)0)->name)),	\
		0 }
	.red		= OFFSETS(red),
	.green		= OFFSETS(green),
	.blue		= OFFSETS(blue),
	.transp		= OFFSETS(brightness),
};

static void rgbled_deferred_io(struct fb_info *fb,
			       struct list_head *pagelist);

static struct fb_deferred_io fb_deferred_io_default = {
	/* .delay		= HZ / 100, */
	.deferred_io	= rgbled_deferred_io,
};

/* framebuffer operations */

static ssize_t rgbled_write(struct fb_info *info,
			    const char __user *buf, size_t count,
			    loff_t *ppos)
{
	ssize_t res = fb_sys_write(info, buf, count, ppos);

	rgbled_schedule(info);

	return res;
}

static void rgbled_fillrect(struct fb_info *info,
			    const struct fb_fillrect *rect)
{
	sys_fillrect(info, rect);
	rgbled_schedule(info);
}

static void rgbled_copyarea(struct fb_info *info,
			    const struct fb_copyarea *area)
{
	sys_copyarea(info, area);
	rgbled_schedule(info);
}

static void rgbled_imageblit(struct fb_info *info,
			     const struct fb_image *image)
{
	sys_imageblit(info, image);
	rgbled_schedule(info);
}

static struct fb_ops rgbled_ops = {
	.fb_read	= fb_sys_read,
	.fb_write	= rgbled_write,
	.fb_fillrect	= rgbled_fillrect,
	.fb_copyarea	= rgbled_copyarea,
	.fb_imageblit	= rgbled_imageblit,
};

void rgbled_get_pixel_coords_generic(
	struct rgbled_fb *rfb,
	struct rgbled_panel_info *panel,
	int panel_pixel_num,
	struct rgbled_coordinates *coord)
{
	int x, y;

	if (panel->layout_yx) {
		y = panel_pixel_num % panel->height;
		x = panel_pixel_num / panel->height;

	} else {
		x = panel_pixel_num % panel->width;
		y = panel_pixel_num / panel->width;
	}

	if (panel->inverted_x)
		x = panel->width - 1 - x;

	if (panel->inverted_y)
		y = panel->height - 1 - y;

	coord->x = x;
	coord->y = y;
}

void rgbled_get_pixel_coords_linear(
	struct rgbled_fb *rfb,
	struct rgbled_panel_info *panel,
	int panel_pixel_num,
	struct rgbled_coordinates *coord)
{
	rgbled_get_pixel_coords_generic(rfb, panel, panel_pixel_num, coord);

	coord->x += panel->x;
	coord->y += panel->y;
}
EXPORT_SYMBOL_GPL(rgbled_get_pixel_coords_linear);

void rgbled_get_pixel_coords_meander(
	struct rgbled_fb *rfb,
	struct rgbled_panel_info *panel,
	int panel_pixel_num,
	struct rgbled_coordinates *coord)
{
	rgbled_get_pixel_coords_generic(rfb, panel, panel_pixel_num, coord);

	/* handle layout */
	if (panel->layout_yx) {
		if (coord->x & 1)
			coord->y = panel->height - 1 - coord->y;
	} else {
		if (coord->y & 1)
			coord->x = panel->width - 1 - coord->x;
		coord->y += panel->y;
	}

	coord->x += panel->x;
	coord->y += panel->y;
}
EXPORT_SYMBOL_GPL(rgbled_get_pixel_coords_meander);

static inline void rgbled_get_pixel_value_set(struct rgbled_fb *rfb,
					      struct rgbled_panel_info *panel,
					      struct rgbled_pixel *pix,
					      u8 r, u8 g, u8 b, u8 bright)
{
	pix->red = r;
	pix->green = g;
	pix->blue = b;
	pix->brightness = (u32)bright *
		rfb->brightness *
		panel->brightness /
		255 / 255;
}

static void rgbled_get_pixel_value_default(struct rgbled_fb *rfb,
					   struct rgbled_panel_info *panel,
					   struct rgbled_coordinates *coord,
					   struct rgbled_pixel *pix)
{
	struct rgbled_pixel *vpix;

	if (coord->x > rfb->width)
		return rgbled_get_pixel_value_set(rfb, panel, pix,
						  0, 0, 0, 0);
	if (coord->y > rfb->height)
		return rgbled_get_pixel_value_set(rfb, panel, pix,
						  0, 0, 0, 0);

	/* copy pixel data */
	vpix = rgbled_get_raw_pixel(rfb, coord);

	rgbled_get_pixel_value_set(rfb, panel, pix,
				   vpix->red, vpix->green, vpix->blue,
				   vpix->brightness);
}

static u8 rgbled_handle_panel(struct rgbled_fb *rfb,
			      int start_pixel,
			      struct rgbled_panel_info *panel)
{
	struct rgbled_coordinates coord;
	struct rgbled_pixel pix;
	int i;
	u64 c = 0; /* current - need 64bit temporarily because of scaling */

	/* iterate over all pixel */
	for (i = 0; i < panel->pixel; i++) {
		/* get the coordinates */
		rgbled_get_pixel_coords(rfb, panel, i, &coord);
		/* now get the corresponding value */
		rgbled_get_pixel_value(rfb, panel, &coord, &pix);

		/* here we could add gamma control if needed */

		/* and set it */
		rfb->set_pixel_value(rfb, panel, start_pixel + i, &pix);

		/* and calculate current estimate */
		c += pix.red   * pix.brightness * rfb->led_current_max_red;
		c += pix.green * pix.brightness * rfb->led_current_max_green;
		c += pix.blue  * pix.brightness * rfb->led_current_max_blue;
	}
	/* and scale down back */
	do_div(c, 255 * 255);

	/* add base panel-consumption (after scaling!)*/
	c += rfb->led_current_base * panel->pixel;

	/* and assign/add it */
	panel->current_tmp = c;
	rfb->current_tmp += c;

	/* return 255 for a "constant scale" - not rescaling */
	if (!panel->current_limit)
		return 255;
	if (panel->current_limit >= c)
		return 255;

	/* so we exceed the limit, so warn */
	fb_warn(rfb->info,
		"panel %s consumes %llu mA and exceeded current limit of %i mA\n",
		panel->name, c, panel->current_limit);

	/* and return a different scale */
	return  (u32)254 * panel->current_limit / (u32)c;
}

static u8 rgbled_handle_panels(struct rgbled_fb *rfb)
{
	struct rgbled_panel_info *panel;
	int start_pixel = 0;
	u8 rescale;

	/* reset current estimation */
	rfb->current_tmp = 0;

	/* iterate over all panels */
	list_for_each_entry(panel, &rfb->panels, list) {
		rescale = rgbled_handle_panel(rfb, start_pixel, panel);
		/* handle rescale request by propagating */
		if (rescale != 255)
			return rescale;
		start_pixel += panel->pixel;
	}

	/* check current */
	if (!rfb->current_limit)
		return 255;
	if (rfb->current_limit >= rfb->current_tmp)
		return 255;

	fb_warn(rfb->info,
		"total panel consumes %i mA and exceeded current limit of %i mA\n",
		rfb->current_tmp, rfb->current_limit);

	/* need to return with new scaling */
	return  (u32)254 * rfb->current_limit / rfb->current_tmp;
}

static void rgbled_update_stats(struct rgbled_fb *rfb)
{
	struct rgbled_panel_info *panel;

	spin_lock(&rfb->lock);

	/* commit currents */
	list_for_each_entry(panel, &rfb->panels, list) {
		panel->current_active = panel->current_tmp;
		if (panel->current_active > panel->current_max)
			panel->current_max = panel->current_active;
	}
	rfb->current_active = rfb->current_tmp;
	if (rfb->current_active > rfb->current_max)
		rfb->current_max = rfb->current_active;

	rfb->screen_updates++;

	spin_unlock(&rfb->lock);
}

static void rgbled_deferred_work_default(struct rgbled_fb *rfb)
{
	int iterations = 0;
	u8 rescale = rgbled_handle_panels(rfb);

	/* rescale the global brightness */
	while (rescale < 255) {
		/* change brightness */
		rfb->brightness = rfb->brightness * rescale / 255;
		/* and rerun the calculation */
		rescale = rgbled_handle_panels(rfb);
		/* and exit early after a few loops */
		iterations++;
		if (iterations > 256) {
			fb_warn(rfb->info,
				"could not reduce brightness enough to reach required current limit - not updating display");
			return;
		}
	}

	/* commit the calculated currents */
	rgbled_update_stats(rfb);

	/* and handle the final step */
	if (rfb->finish_work)
		rfb->finish_work(rfb);
}

static void rgbled_deferred_io(struct fb_info *fb,
			       struct list_head *pagelist)
{
	struct rgbled_fb *rfb = fb->par;

	rfb->deferred_work(rfb);
}

static inline struct rgbled_panel_info *to_panel_info(
	struct list_head *list)
{
	return list ? container_of(list, struct rgbled_panel_info, list) :
		NULL;
}

static int rgbled_panel_info_cmp(void *priv,
				 struct list_head *a,
				 struct list_head *b)
{
	struct rgbled_fb *rfb = priv;
	struct rgbled_panel_info *ad = to_panel_info(a);
	struct rgbled_panel_info *bd = to_panel_info(b);

	if (ad->id < bd->id)
		return -1;
	if (ad->id > bd->id)
		return 1;

	/* there should be no identical IDs, so mark as duplicates */
	rfb->duplicate_id = 1;

	return 0;
}

int rgbled_panel_multiple_width(struct rgbled_panel_info *panel, u32 val)
{
	panel->width *= val;
	panel->pixel *= val;

	return 0;
}
EXPORT_SYMBOL_GPL(rgbled_panel_multiple_width);

int rgbled_panel_multiple_height(struct rgbled_panel_info *panel, u32 val)
{
	panel->height *= val;
	panel->pixel *= val;

	return 0;
}
EXPORT_SYMBOL_GPL(rgbled_panel_multiple_height);

int rgbled_register_panel(struct rgbled_fb *rfb,
			  struct rgbled_panel_info *panel)
{
	/* add to list */
	list_add(&panel->list, &rfb->panels);

	/* calculate size of panel in pixel if not set */
	if (!panel->pixel)
		panel->pixel = panel->width * panel->height;

	/* check values */
	if (!panel->width)
		return -EINVAL;
	if (!panel->height)
		return -EINVAL;
	if (!panel->pixel)
		return -EINVAL;

	/* add the number of pixel to the chain */
	rfb->pixel += panel->pixel;

	/* setting max coordinates for the framebuffer */
	if (rfb->width < panel->x + panel->width)
		rfb->width = panel->x + panel->width;
	if (rfb->height < panel->x + panel->height)
		rfb->height = panel->y + panel->height;

	return 0;
}
EXPORT_SYMBOL_GPL(rgbled_register_panel);

int rgbled_scan_panels(struct rgbled_fb *rfb,
		       struct rgbled_panel_info *panels)
{
	struct device *dev = rfb->info->device;
	int err;

	/* scan for panels in device tree */
	err = rgbled_scan_panels_of(rfb, panels);
	if (err)
		return err;

	/* sort list - used to shift out the data
	 * this also checks that there are no duplicate ids...
	 */
	list_sort(rfb, &rfb->panels, rgbled_panel_info_cmp);

	/* if we got a duplicate, then fail */
	if (rfb->duplicate_id) {
			dev_err(dev, "duplicate\n");
		return -EINVAL;
	}

	/* if we got no pixel, then fail */
	if (!rfb->pixel) {
		dev_err(dev, "nopixel\n");
		return -EINVAL;
	}

	return 0;
}

static void rgbled_unregister_framebuffer(struct device *dev, void *res)
{
	struct rgbled_fb *rfb = *(struct rgbled_fb **)res;

	fb_deferred_io_cleanup(rfb->info);
	vfree(rfb->vmem);
	rfb->vmem = NULL;
	unregister_framebuffer(rfb->info);
}

static void rgbled_framebuffer_release(struct device *dev, void *res)
{
	struct rgbled_fb *rfb = *(struct rgbled_fb **)res;

	framebuffer_release(rfb->info);
	rfb->info = NULL;
}

struct rgbled_fb *rgbled_alloc(struct device *dev,
			       const char *name,
			       struct rgbled_panel_info *panels)
{
	struct fb_info *fb;
	struct rgbled_fb *rfb;
	struct rgbled_fb **ptr;
	int err;

	/* initialize our own structure */
	rfb = devm_kzalloc(dev, sizeof(*rfb), GFP_KERNEL);

	/* first copy the defaults */
	memcpy(&rfb->deferred_io,
	       &fb_deferred_io_default,
	       sizeof(fb_deferred_io_default));

	/* now set up specific things */
	INIT_LIST_HEAD(&rfb->panels);
	spin_lock_init(&rfb->lock);

	/* now allocate the framebuffer_info via devres */
	ptr = devres_alloc(rgbled_framebuffer_release,
			   sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);
	/* allocate framebuffer */
	fb = framebuffer_alloc(0, dev);
	if (!fb) {
		devres_free(ptr);
		return ERR_PTR(-ENOMEM);
	}
	*ptr = rfb;
	devres_add(dev, ptr);

	/* and add a pointer back and forth */
	fb->par = rfb;
	rfb->info = fb;

	/* set up basics */
	fb->fbops	= &rgbled_ops;
	fb->fbdefio	= &rfb->deferred_io;
	fb->fix		= fb_fix_screeninfo_default;
	fb->var		= fb_var_screeninfo_default;
	fb->flags	= FBINFO_FLAG_DEFAULT | FBINFO_VIRTFB;
	/* needs to happen agfter the assignement above */
	strncpy(fb->fix.id, name, sizeof(fb->fix.id));

	/* scan panels to get string size */
	err = rgbled_scan_panels(rfb, panels);
	if (err)
		return ERR_PTR(err);

	return rfb;
}
EXPORT_SYMBOL_GPL(rgbled_alloc);

static int rgbled_fix_up_structures(struct rgbled_fb *rfb)
{
	struct rgbled_panel_info *p;

	/* fill in those empty vectors */
	if (!rfb->deferred_work) {
		rfb->deferred_work = rgbled_deferred_work_default;
		/* if there is no custom implementation,
		 * then we need set_pixel_value
		 * finish_work is optional...
		 */
		if (!rfb->set_pixel_value) {
			fb_err(rfb->info,
			       "no set_pixel_value method configured\n");
			return -EINVAL;
		}
	}

	/* fill in those empty vectors with defaults */
	if (!rfb->get_pixel_value)
		rfb->get_pixel_value = rgbled_get_pixel_value_default;

	/* and the panels */
	list_for_each_entry(p, &rfb->panels, list) {
		if (!p->get_pixel_coords)
			p->get_pixel_coords = rgbled_get_pixel_coords_linear;
		if (!p->get_pixel_value)
			p->get_pixel_value = rfb->get_pixel_value;
	}

	return 0;
}

int rgbled_register_panels_sysled(struct rgbled_fb *rfb)
{
	struct rgbled_panel_info *panel;
	int err;

	/* register each  led in each panel - if given */
	list_for_each_entry(panel, &rfb->panels, list) {
		err = rgbled_register_panel_sysled(rfb, panel);
		if (err)
			return err;
	}

	return 0;
}

int rgbled_register(struct rgbled_fb *rfb)
{
	struct fb_info *fb = rfb->info;
	int err;
	struct rgbled_fb **ptr;

	/* fill in the default vectors */
	err = rgbled_fix_up_structures(rfb);
	if (err)
		return err;

	/* register devices via device-tree */
	err = rgbled_register_of(rfb);
	if (err)
		return err;

	/* prepare release */
	ptr = devres_alloc(rgbled_unregister_framebuffer,
			   sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;
	*ptr = rfb;

	/* set up sizes */
	fb->var.xres_virtual = rfb->width;
	fb->var.yres_virtual = rfb->height;
	fb->var.xres = rfb->width;
	fb->var.yres = rfb->height;

	fb->fix.line_length = sizeof(struct rgbled_pixel) * rfb->width;
	rfb->vmem_size = fb->fix.line_length * rfb->height;

	/* allocate memory */
	rfb->vmem = vzalloc(rfb->vmem_size);
	if (!rfb->vmem) {
		devres_free(ptr);
		return -ENOMEM;
	}

	/* set vmem data */
	fb->fix.smem_len = rfb->vmem_size;
	fb->screen_size = rfb->vmem_size;

	fb->screen_base = (typeof(fb->screen_base))rfb->vmem;
	fb->fix.smem_start = (typeof(fb->fix.smem_start))rfb->vmem;

	/* initialize deferred io as a devm device */
	fb_deferred_io_init(fb);

	/* register fb */
	err = register_framebuffer(fb);
	if (err) {
		vfree(rfb->vmem);
		devres_free(ptr);
		return err;
	}
	/* now register resource cleanup */
	devres_add(fb->device, ptr);

	/* and register additional information in sysfs */
	err = rgbled_register_sysfs(rfb);
	if (err)
		return err;

	/* and register panel leds */
	err = rgbled_register_panels_sysled(rfb);
	if (err)
		return err;

	/* calculate refresh rate to use */
	if (!rfb->deferred_io.delay)
		rfb->deferred_io.delay = HZ / 100;

	/* and start an initial update of the framebuffer to clean it */
	rfb->deferred_work(rfb);

	/* and report the status */
	fb_info(fb, "%s of size %ux%u with %i led, max refresh %luHz\n",
		fb->fix.id, rfb->width, rfb->height, rfb->pixel,
		HZ / rfb->deferred_io.delay);
	return 0;
}
EXPORT_SYMBOL_GPL(rgbled_register);

MODULE_AUTHOR("Martin Sperl <kernel@martin.sperl.org>");
MODULE_DESCRIPTION("generic RGB LED FB infrastructure");
MODULE_LICENSE("GPL");
