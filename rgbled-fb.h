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

#ifndef __RGBLED_FB_H
#define __RGBLED_FB_H


#include <linux/fb.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/list_sort.h>

struct rgbled_pixel {
	u8 red;
	u8 green;
	u8 blue;
	u8 brightness;
};

struct rgbled_board_info {
	char	*compatible;
	u32	id;
	u32	x;
	u32	y;
	u32	width;
	u32	height;
	u32	pixel;

	u32	pitch;

	bool	layout_yx;
	bool	inverted_x;
	bool	inverted_y;

	u32 flags;

	int (*getPixelValue)(
		struct rgbled_board_info *board,
		int pixel_num,
		struct rgbled_pixel *pix);

	struct list_head list;
};

struct rgbled_fb {
	struct fb_info *info;
	struct fb_deferred_io deferred_io;

	struct list_head boards;
	bool duplicate_id;

	char __iomem *vmem;
	int width;
	int height;
	int vmem_size;
	int pixel;

	void (*deferred_work)(struct rgbled_fb*);
	void *par;
};

/* typical pixel handler */
extern int rgbled_getPixelValue_linear(
	struct rgbled_board_info *board,
	int pixel_num,
	struct rgbled_pixel *pix);

extern int rgbled_getPixelValue_winding(
	struct rgbled_board_info *board,
	int pixel_num,
	struct rgbled_pixel *pix);


extern struct rgbled_fb *rgbled_alloc(struct device *dev);
extern int rgbled_scan_boards(struct rgbled_fb *fb,
			      struct rgbled_board_info *boards);
extern int rgbled_register(struct rgbled_fb *fb, const char *name);

#endif /* __RGBLED_FB_H */
