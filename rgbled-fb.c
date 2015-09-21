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
#include <linux/list.h>
#include <linux/list_sort.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/vmalloc.h>

#include "rgbled-fb.h"

static void rgbled_schedule(struct fb_info *info)
{
        schedule_delayed_work(&info->deferred_work, 1);
}

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

int rgbled_getPixelValue_linear(
	struct rgbled_board_info *board,
	int pixel_num,
	struct rgbled_pixel *pix)
{
	return 0;
}
EXPORT_SYMBOL_GPL(rgbled_getPixelValue_linear);

int rgbled_getPixelValue_winding(
	struct rgbled_board_info *board,
	int pixel_num,
	struct rgbled_pixel *pix)
{
	return 0;
}
EXPORT_SYMBOL_GPL(rgbled_getPixelValue_winding);

void rgbled_deferred_io(struct fb_info *fb,
			struct list_head *pagelist)
{
	struct rgbled_fb *rfb = fb->par;

	if (rfb->deferred_work)
		rfb->deferred_work(rfb);
}

static struct fb_ops rgbled_ops = {
	.fb_read	= fb_sys_read,
	.fb_write	= rgbled_write,
	.fb_fillrect	= rgbled_fillrect,
	.fb_copyarea	= rgbled_copyarea,
	.fb_imageblit	= rgbled_imageblit,
};

/* all the comented out are to get filled in */
static struct fb_fix_screeninfo fb_fix_screeninfo_default = {
	/* .id		= "ws2812b FB", */
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

static struct fb_deferred_io fb_deferred_io_default = {
	/* .delay		= HZ/10, */
	.deferred_io	= rgbled_deferred_io,
};

static inline struct rgbled_board_info *to_board_info(
	struct list_head *list)
{
	return list ? container_of(list, struct rgbled_board_info, list) :
		NULL;
}

static int rgbled_board_info_cmp(void *priv,
			   struct list_head *a,
			   struct list_head *b)
{
	struct rgbled_fb *rfb = priv;
	struct rgbled_board_info *ad = to_board_info(a);
	struct rgbled_board_info *bd = to_board_info(b);

	if (ad->id < bd->id)
		return -1;
	if (ad->id > bd->id)
		return +1;

	/* there should be no identical IDs, so mark as duplicates */
	rfb->duplicate_id = 1;
	return 0;
}

static int rgbled_probe_of_board(struct rgbled_fb *rfb,
				struct device_node *nc,
				struct rgbled_board_info *board)
{
	struct device *dev = rfb->info->device;
	struct rgbled_board_info *bi;

	bi = devm_kzalloc(dev, sizeof(*bi), GFP_KERNEL);
	if (!bi)
		return -ENOMEM;

	/* copy the default board data */
	memcpy(bi, board, sizeof(*bi));

	/* add to list */
	list_add(&bi->list, &rfb->boards);

	/* now fill in from the device tree the overrides */
	if (of_property_read_u32_index(nc, "reg",    0, &bi->id))
		return -EINVAL;
	of_property_read_u32_index(nc, "x",      0, &bi->x);
	of_property_read_u32_index(nc, "y",      0, &bi->y);
	of_property_read_u32_index(nc, "width",  0, &bi->width);
	of_property_read_u32_index(nc, "height", 0, &bi->height);

	/* calculate size of this */
	bi->pixel = bi->width * bi->height;
	rfb->pixel += bi->pixel;
	if (!bi->pixel)
		return -EINVAL;

	/* setting max coordinates */
	if (rfb->width < bi->x + bi->width)
		rfb->width = bi->x + bi->width;
	if (rfb->height < bi->x + bi->height)
		rfb->height = bi->y + bi->height;

	return 0;
}

int rgbled_scan_boards_match(struct rgbled_fb *rfb,
			     struct device_node *nc,
			     struct rgbled_board_info *boards)
{
	struct device *dev = rfb->info->device;
	const char *name[6];
	int i;
	int err;
	int idx;

	/* get the compatible property */
	for(i=0;i<6;i++)
		name[i]=NULL;
	err = of_property_read_string_helper(nc, "compatible", (const char **)&name, 6, 0);
	dev_err(dev, "Check node: %s - %i - %s\n",
		nc->full_name, err, name[0]);

	for(i = 0 ; boards[i].compatible ; i++) {
		dev_err(dev, "Check node: %i %s\n", i, boards[i].compatible);
		idx = of_property_match_string(nc, "compatible",
					boards[i].compatible);
		if (idx >= 0) {
			dev_err(dev, "match\n");
			return rgbled_probe_of_board(rfb, nc,
						     &boards[i]);
		}
	}

	/* not matching */
	dev_err(dev, "Incompatible node %s found\n", nc->full_name);
	return -EINVAL;
}

int rgbled_scan_boards(struct rgbled_fb *rfb,
		       struct rgbled_board_info *boards)
{
	struct device *dev = rfb->info->device;
	struct device_node *nc;
	int err;

	/* iterate over all entries in the device-tree */
	for_each_available_child_of_node(dev->of_node, nc) {
		err = rgbled_scan_boards_match(rfb, nc, boards) ;
		if (err)
			return err;
	}

	/* sort list - used to shift out the data
	 * this also checks that there are no duplicate ids...
	 */
	list_sort(rfb, &rfb->boards, rgbled_board_info_cmp);

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
EXPORT_SYMBOL_GPL(rgbled_scan_boards);

static void rgbled_framebuffer_release(struct device *dev, void *res)
{
	struct rgbled_fb *rfb = *(struct rgbled_fb **)res;
	framebuffer_release(rfb->info);
	rfb->info = NULL;
}

struct rgbled_fb *rgbled_alloc(struct device *dev)
{
	struct fb_info *fb;
	struct rgbled_fb *rfb;
	struct rgbled_fb **ptr;

	/* initialize our own structure */
	rfb = devm_kzalloc(dev, sizeof(*rfb), GFP_KERNEL);

	/* first copy the defaults */
	memcpy(&rfb->deferred_io,
	       &fb_deferred_io_default,
	       sizeof(fb_deferred_io_default));

	/* now set up specific things */
	INIT_LIST_HEAD(&rfb->boards);

	/* now allocate the framebuffer_info via devres */
	ptr = devres_alloc(rgbled_framebuffer_release,
			   sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return NULL;
	/* allocate framebuffer */
	fb = framebuffer_alloc(0, dev);
	if (!fb) {
		devres_free(ptr);
		return NULL;
	}
	*ptr = rfb;
	devres_add(dev, ptr);

	/* and add a pointer back and forth */
	fb->par = rfb;
	rfb->info = fb;

	return rfb;
}
EXPORT_SYMBOL_GPL(rgbled_alloc);

static void rgbled_unregister_framebuffer(struct device *dev, void *res)
{
	struct rgbled_fb *rfb = *(struct rgbled_fb **)res;

	fb_deferred_io_cleanup(rfb->info);
	vfree(rfb->vmem);
	rfb->vmem = NULL;
	unregister_framebuffer(rfb->info);
}

int rgbled_register(struct rgbled_fb *rfb, const char *name)
{
	struct fb_info *fb = rfb->info;
	int err;
	struct rgbled_fb **ptr;

	/* prepare release */
	ptr = devres_alloc(rgbled_unregister_framebuffer,
			   sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;
	*ptr = rfb;

	/* set up basics */
	strncpy(fb->fix.id, name, sizeof(fb->fix.id));
	fb->fbops	= &rgbled_ops;
	fb->fbdefio	= &rfb->deferred_io;
	fb->fix		= fb_fix_screeninfo_default;
	fb->var		= fb_var_screeninfo_default;
	fb->flags	= FBINFO_FLAG_DEFAULT | FBINFO_VIRTFB;

	/* set up sizes */
	fb->var.xres_virtual = fb->var.xres = rfb->width;
	fb->var.yres_virtual = fb->var.yres = rfb->height;

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

	fb->screen_base = rfb->vmem;
	fb->fix.smem_start = (typeof(fb->fix.smem_start))(rfb->vmem);

	/* calculate refresh rate to use */
	if (!rfb->deferred_io.delay)
		rfb->deferred_io.delay = HZ/100;

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

	strncpy(fb->fix.id, name, sizeof(fb->fix.id));
	fb_info(fb,
		"rgbled-fb of size %ux%u with %i led of type %s - %s\n",
		rfb->width, rfb->height, rfb->pixel, fb->fix.id, name);

	return 0;
}
EXPORT_SYMBOL_GPL(rgbled_register);

MODULE_AUTHOR("Martin Sperl <kernel@martin.sperl.org>");
MODULE_DESCRIPTION("generic RGB LED FB infrastructure");
MODULE_LICENSE("GPL");
