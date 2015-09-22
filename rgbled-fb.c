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

void rgbled_getPixelCoords_generic(
	struct rgbled_fb *rfb,
	struct rgbled_board_info *board,
	int board_pixel_num,
	struct rgbled_coordinates *coord)
{
	int x, y;

	if (board->layout_yx) {
		y = board_pixel_num % board->height;
		x = board_pixel_num / board->height;

	} else {
		x = board_pixel_num % board->width;
		y = board_pixel_num / board->width;
	}

	if (board->inverted_x)
		x = board->width - 1 - x;

	if (board->inverted_y)
		y = board->height - 1 - y;

	coord->x = x;
	coord->y = y;
}

void rgbled_getPixelCoords_linear(
	struct rgbled_fb *rfb,
	struct rgbled_board_info *board,
	int board_pixel_num,
	struct rgbled_coordinates *coord)
{
	rgbled_getPixelCoords_generic(rfb, board, board_pixel_num, coord);

	coord->x += board->x;
	coord->y += board->y;
}
EXPORT_SYMBOL_GPL(rgbled_getPixelCoords_linear);

void rgbled_getPixelCoords_winding(
	struct rgbled_fb *rfb,
	struct rgbled_board_info *board,
	int board_pixel_num,
	struct rgbled_coordinates *coord)
{
	rgbled_getPixelCoords_generic(rfb, board, board_pixel_num, coord);

	/* handle layout */
	if (board->layout_yx) {
		if (coord->x & 1)
			coord->y = board->height - 1 - coord->y;
	} else {
		if (coord->y & 1)
			coord->x = board->width - 1 - coord->x;
		coord->y += board->y;
	}

	coord->x += board->x;
	coord->y += board->y;
}
EXPORT_SYMBOL_GPL(rgbled_getPixelCoords_winding);

static inline void rgbled_getPixelValue_set(struct rgbled_fb *rfb,
					    struct rgbled_board_info *board,
					    struct rgbled_pixel *pix,
					    u8 r, u8 g, u8 b, u8 bright)
{
	pix->red = r;
	pix->green = g;
	pix->blue = b;
	pix->brightness = (u32)bright *
		rfb->brightness *
		board->brightness /
		255 / 255;
}

static void rgbled_getPixelValue(struct rgbled_fb *rfb,
				 struct rgbled_board_info *board,
				 struct rgbled_coordinates *coord,
				 struct rgbled_pixel *pix)
{
	struct rgbled_pixel *vpix_array = (struct rgbled_pixel *)rfb->vmem;
	struct rgbled_pixel *vpix;

	/* get the pixel Value */
	if (board->getPixelValue) {
		return board->getPixelValue(rfb, board, coord, pix);
	} else {
		if (rfb->getPixelValue)
			return rfb->getPixelValue(rfb, board, coord, pix);
	}

	/* the default implementation */
	if (coord->x > rfb->width)
		return 	rgbled_getPixelValue_set(rfb, board, pix, 0, 0, 0, 0);
	if (coord->y > rfb->height)
		return 	rgbled_getPixelValue_set(rfb, board, pix, 0, 0, 0, 0);

	/* copy pixel data */
	vpix = &vpix_array[coord->y * rfb->width + coord->x];

	rgbled_getPixelValue_set(rfb, board, pix,
				 vpix->red, vpix->green, vpix->blue,
				 vpix->brightness);
}

static u8 rgbled_handle_board(struct rgbled_fb *rfb,
			      int start_pixel,
			      struct rgbled_board_info *board)
{
	struct rgbled_coordinates coord;
	struct rgbled_pixel pix;
	int i;
	u32 c = 0;
	/* can't name it current because of a macro
	 * defined in include/asm-generic/current.h */

	for(i=0; i< board->pixel; i++) {
		/* get the coordinates */
		if (board->getPixelCoords)
			board->getPixelCoords(rfb, board, i, &coord);
		else
			rgbled_getPixelCoords_linear(rfb, board, i, &coord);
		/* now get the corresponding value */
		rgbled_getPixelValue(rfb, board, &coord, &pix);

		/* here we could add gamma control if needed */

		/* and set it */
		rfb->setPixelValue(rfb, board, start_pixel + i, &pix);

		/* and calculate current estimate */
		c += pix.red   * pix.brightness * rfb->max_current_red;
		c += pix.green * pix.brightness * rfb->max_current_green;
		c += pix.blue  * pix.brightness * rfb->max_current_blue;
	}
	/* and scale down */
	c /= 255*255;

	board->current_current_tmp = c;
	rfb->current_current_tmp += c;

	if (!board->current_limit)
		return 255;
	if (board->current_limit >= c)
		return 255;

	fb_warn(rfb->info,
		"board %s consumes %i mA and exceeded current limit of %i mA\n",
		board->name, c, board->current_limit);

	/* return a different scale */
	return  (u32)254 * board->current_limit / c;
}

static u8 rgbled_handle_boards(struct rgbled_fb *rfb)
{
	struct rgbled_board_info *board;
	int start_pixel = 0;
	u8 rescale;

	/* reset current estimation */
	rfb->current_current_tmp = 0;

	/* iterate over all boards */
	list_for_each_entry(board, &rfb->boards, list) {
		rescale = rgbled_handle_board(rfb, start_pixel, board);
		/* handle rescale request by propagating */
		if (rescale != 255)
			return rescale;
		start_pixel += board->pixel;
	}

	/* check current */
	if (!rfb->current_limit)
		return 255;
	if (rfb->current_limit >= rfb->current_current_tmp) {
		return 255;
	}

	fb_warn(rfb->info,
		"total panel consumes %i mA and exceeded current limit of %i mA\n",
		rfb->current_current_tmp, rfb->current_limit);

	/* need to return with new scaling */
	return  (u32)254 * rfb->current_limit / rfb->current_current_tmp;
}

static void rgbled_update_stats(struct rgbled_fb *rfb)
{
	struct rgbled_board_info *board;

	spin_lock(&rfb->lock);

	/* commit currents */
	list_for_each_entry(board, &rfb->boards, list) {
		board->current_current = board->current_current_tmp;
		if (board->current_current > board->current_max)
			board->current_max = board->current_current;
	}
	rfb->current_current = rfb->current_current_tmp;
	if (rfb->current_current > rfb->current_max)
		rfb->current_max = rfb->current_current;

	rfb->screen_updates++;

	spin_unlock(&rfb->lock);
}

static void rgbled_deferred_io_default(struct rgbled_fb *rfb)
{
	int iterations = 0;
	u8 rescale = rgbled_handle_boards(rfb);

	/* rescale the global brightness */
	while(rescale < 255) {
		/* change brightness */
		rfb->brightness = rfb->brightness * rescale / 255;
		/* and rerun the calculation */
		rescale = rgbled_handle_boards(rfb);
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

	if (rfb->deferred_work)
		rfb->deferred_work(rfb);
	else
		rgbled_deferred_io_default(rfb);
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
	u32 tmp;

	bi = devm_kzalloc(dev, sizeof(*bi), GFP_KERNEL);
	if (!bi)
		return -ENOMEM;

	/* copy the default board data */
	memcpy(bi, board, sizeof(*bi));

	/* add to list */
	list_add(&bi->list, &rfb->boards);

	/* copy the full name */
	bi->name = of_node_full_name(nc);

	/* now fill in from the device tree the overrides */
	if (of_property_read_u32_index(nc, "reg",    0, &bi->id))
		return -EINVAL;
	of_property_read_u32_index(nc, "x",      0, &bi->x);
	of_property_read_u32_index(nc, "y",      0, &bi->y);
	of_property_read_u32_index(nc, "width",  0, &bi->width);
	of_property_read_u32_index(nc, "height", 0, &bi->height);
	of_property_read_u32_index(nc, "pixel",  0, &bi->pixel);
	of_property_read_u32_index(nc, "pitch",  0, &bi->pitch);
	of_property_read_u32_index(nc, "current-limit",
				   0, &bi->current_limit);

	if (!of_property_read_u32_index(nc, "brightness",0, &tmp))
		bi->brightness = min_t(u32, tmp, 255);
	else
		bi->brightness = 255;


	/* some boolean settings */
	if (of_find_property(nc, "layout-y-x",0))
		bi->layout_yx = true;
	if (of_find_property(nc, "inverted-x",0))
		bi->inverted_x = true;
	if (of_find_property(nc, "inverted-y",0))
		bi->inverted_y = true;

	/* calculate size of board in pixel if not set */
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
}

int rgbled_scan_boards_match(struct rgbled_fb *rfb,
			     struct device_node *nc,
			     struct rgbled_board_info *boards)
{
	struct device *dev = rfb->info->device;
	int i;
	int idx;

	/* get the compatible property */
	for(i = 0 ; boards[i].compatible ; i++) {
		idx = of_property_match_string(nc, "compatible",
					boards[i].compatible);
		if (idx >= 0) {
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

struct rgbled_fb *rgbled_alloc(struct device *dev,
			       const char *name,
			       struct rgbled_board_info *boards)
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
	INIT_LIST_HEAD(&rfb->boards);
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

	/* scan boards to get string size */
	err = rgbled_scan_boards(rfb, boards);
	if (err)
		return ERR_PTR(err);

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

static int rgbled_register_sysfs(struct rgbled_fb *rfb)
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

int rgbled_register(struct rgbled_fb *rfb)
{
	struct fb_info *fb = rfb->info;
	struct device_node *nc = fb->device->of_node;
	int err;
	struct rgbled_fb **ptr;
	u32 tmp;

	/* read brightness and current limits from device-tree */
	of_property_read_u32_index(nc, "current-limit",
				   0, &rfb->current_limit);
	of_property_read_u32_index(nc, "max-current-red",
				   0, &rfb->max_current_red);
	of_property_read_u32_index(nc, "max-current-green",
				   0, &rfb->max_current_green);
	of_property_read_u32_index(nc, "max-current-blue",
				   0, &rfb->max_current_blue);

	if (!of_property_read_u32_index(nc, "brightness",0, &tmp))
		rfb->brightness = min_t(u32, tmp, 255);
	else
		rfb->brightness = 255;

	/* prepare release */
	ptr = devres_alloc(rgbled_unregister_framebuffer,
			   sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;
	*ptr = rfb;

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

	/* and register additional information in sysfs */
	err = rgbled_register_sysfs(rfb);
	if (err)
		return err;

	fb_info(fb,
		"rgbled-fb of size %ux%u with %i led of type %s\n",
		rfb->width, rfb->height, rfb->pixel, fb->fix.id);

	return 0;
}
EXPORT_SYMBOL_GPL(rgbled_register);

MODULE_AUTHOR("Martin Sperl <kernel@martin.sperl.org>");
MODULE_DESCRIPTION("generic RGB LED FB infrastructure");
MODULE_LICENSE("GPL");
