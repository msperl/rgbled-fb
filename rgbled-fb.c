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
        schedule_delayed_work(&info->deferred_work,
			      info->fbdefio->delay);
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
	fb_info(fb, "Update led screen: %s\n", fb->fix.id);
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
	.bits_per_pixel  = sizeof(struct rgbled_pixel),
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

struct rgbled_fb *rgbled_alloc(struct device *dev, const char *name)
{
	struct fb_info *fb;
	struct rgbled_fb *rfb;

	/* allocate framebuffer */
	fb = framebuffer_alloc(sizeof(struct rgbled_fb), dev);
	if (!fb)
		return NULL;
	rfb = fb->par;
	rfb->info = fb;

	/* first copy the defaults */
	memcpy(&rfb->deferred_io,
	       &fb_deferred_io_default,
	       sizeof(fb_deferred_io_default));
	memcpy(&fb->fix.id,
	       name,
	       sizeof(fb->fix.id)-1);

	/* now set up specific things */
	INIT_LIST_HEAD(&rfb->boards);
	fb->fbops	= &rgbled_ops;
	fb->flags	= FBINFO_FLAG_DEFAULT | FBINFO_VIRTFB;

	return rfb;
}
EXPORT_SYMBOL_GPL(rgbled_alloc);

int rgbled_scan_boards(struct rgbled_fb *rfb,
		       struct rgbled_board_info *boards)
{
	rfb->width = 32;
	rfb->height = 24;

	return 0;
}
EXPORT_SYMBOL_GPL(rgbled_scan_boards);

int rgbled_register(struct rgbled_fb *rfb)
{
	struct fb_info *fb = rfb->info;
	int err;

	/* set up basics */
	fb->fbops	= &rgbled_ops;
	fb->fbdefio	= &rfb->deferred_io;
	fb->fix		= fb_fix_screeninfo_default;
	fb->var		= fb_var_screeninfo_default;

	/* set up sizes */
	fb->var.xres_virtual = fb->var.xres = rfb->width;
	fb->var.yres_virtual = fb->var.yres = rfb->height;

	fb->fix.line_length = sizeof(struct rgbled_pixel) * rfb->width;

	rfb->pixel = rfb->width * rfb->height;
	rfb->vmem_size = sizeof(struct rgbled_pixel) * rfb->pixel;

	rfb->vmem = vzalloc(rfb->vmem_size);
	if (!rfb->vmem)
		return -ENOMEM;

	/* set vmem data */
        fb->fix.smem_len = rfb->vmem_size;
	fb->screen_size = rfb->vmem_size;

	fb->screen_base = rfb->vmem;
	fb->fix.smem_start = (typeof(fb->fix.smem_start))(rfb->vmem);

	/* calculate refresh rate to use */
	if (!rfb->deferred_io.delay)
		rfb->deferred_io.delay = HZ/10;

	/* initialize deferred io */
	fb_deferred_io_init(fb);

	/* register fb */
	err = register_framebuffer(fb);
	if (err) {
		vfree(rfb->vmem);
		rfb->vmem = NULL;
	}

	fb_info(fb, "Registered led-framebuffer of size %u x %u for %s\n",
		rfb->width, rfb->height, fb->fix.id);

	return err;
}
EXPORT_SYMBOL_GPL(rgbled_register);

int rgbled_release(struct rgbled_fb *rfb)
{
	vfree(rfb->vmem);
	rfb->vmem = NULL;

	framebuffer_release(rfb->info);

	return 0;
}
EXPORT_SYMBOL_GPL(rgbled_release);

#if 0

static inline struct ws2812b_board_data *to_board_data(
	struct list_head *list)
{
	return list ? container_of(list, struct ws2812b_board_data, list) :
		NULL;
}

static int ws2812b_probe_of_board(struct ws2812b_data *bs,
	struct spi_device *spi, struct device_node *nc)
{
	struct ws2812b_board_data *bd;

	bd = devm_kzalloc(&spi->dev, sizeof(*bd), GFP_KERNEL);
	if (!bd)
		return -ENOMEM;

	/* add to list */
	list_add(&bd->list, &bs->boards);

	/* set some defaults */
	bd->height = 1;

	/* now fill in from the device tree */
	of_property_read_u32_index(nc, "id",     0, &bd->id);
	of_property_read_u32_index(nc, "x",      0, &bd->x);
	of_property_read_u32_index(nc, "y",      0, &bd->y);
	of_property_read_u32_index(nc, "width",  0, &bd->width);
	of_property_read_u32_index(nc, "height", 0, &bd->height);
	of_property_read_u32_index(nc, "flags",  0, &bd->flags);
	of_property_read_string   (nc, "name",      &bd->name);

	/* some sanity checks */
	if (!bd->width)
		return -EINVAL;
	if (!bd->height)
		return -EINVAL;

	/* setting max coordinates */
	if (bs->fb.var.xres < bd->x + bd->width)
		bs->fb.var.x =
			bs->fb.var.xres_virtual =
			bd->x + bd->width;
	if (bs-fb.var.yres < bd->y + bd->height)
		bs->fb.var.yres =
			bs->fb.var.yres_virtual =
			bd->y + bd->height;

	return 0;
}

int ws2812b_board_data_cmp(void *priv,
			   struct list_head *a,
			   struct list_head *b)
{
	struct ws2812b_data *bs = priv;
	struct ws2812b_board_data *ad = to_board_data(a);
	struct ws2812b_board_data *bd = to_board_data(b);

	if (ad->id < bd->id)
		return -1;
	if (ad->id > bd->id)
		return +1;

	/* there should be no identical IDs */
	bs->duplicate = 1;
	return 0;
}

int ws2812b_probe(struct spi_device *spi)
{
	struct ws2812b_data *bs;
	struct device_node *nc;
	u32 len;
	int err;

	/* allocate private structures */
	bs = devm_kzalloc(&spi->dev, sizeof(*bs), GFP_KERNEL);
	if (!bs)
		return -ENOMEM;
	/* set the driver data */
	dev_set_drvdata(&spi->dev, bs);

	/* fill in framebuffer data with static data */
	memcpy(bs->fb,
	       ws2812b_fb_info_default,
	       sizeof(ws2812b_fb_info_default));


	/* and initialize */
	INIT_LIST_HEAD(&bs->boards);

	/* iterate over all entries in the device-tree */
	for_each_available_child_of_node(spi->dev.of_node, nc) {
		err = ws2812b_probe_of_board(bs, spi, nc);
		if (err)
			return err;
	}

	/* sort list - used to shift out the data */
	list_sort(bs, &bs->boards, ws2812b_board_data_cmp);

	/* if we got a duplicate, then fail */
	if (bs->duplicate)
		return -EINVAL;

	/* allocate spi-pixel-buffer */
	len = 3 /* colors */
		* 3 /* bytes per color - encoding */
		* bs->pixel /* pixel in loop */
		+ 15 /* Reset low for 50us @2.4MHz */;

	bs->spi_buffer = devm_kzalloc(&spi->dev, len,GFP_KERNEL);
	if (!bs->spi_buffer)
		return -ENOMEM;

	/* setup spi message */
	spi_message_init(&bs->msg);
	bs->xfer.len = len;
	bs->xfer.tx_buf = bs->spi_buffer;
	spi_message_add_tail(&bs->xfer, &bs->msg);


	/* allocate framebuffer as the final step*/
	info = framebuffer_alloc(0, &spi->dev);
	if (!info)
		return -ENOMEM;


	return err;



	/* register the led-devices */
	err = ws2812b_register_leds(bs, spi);

	return err;
}

#endif

MODULE_AUTHOR("Martin Sperl <kernel@martin.sperl.org>");
MODULE_DESCRIPTION("generic RGB LED FB infrastructure");
MODULE_LICENSE("GPL");
