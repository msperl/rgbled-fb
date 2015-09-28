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
#include <linux/spinlock.h>

/**
 * struct rgbled_pixel - the pixel format used by rgbled
 * @red: red value
 * @green: green value
 * @blue: blue value
 * @brighness: global brightness (encoded as alpha in fb)
 *
 * the reason for separate brightness is to allow the
 * support of AP102 devices which have separate
 *   24bit RGB PWM settings
 *    4bit brightness constant current settings
 */
struct rgbled_pixel {
	u8			red;
	u8			green;
	u8			blue;
	u8			brightness;
};

/**
 * enum rgbled_pixeltype - define type of value passed
 * to some of the functions
 */
enum rgbled_pixeltype {
	rgbled_pixeltype_red,
	rgbled_pixeltype_green,
	rgbled_pixeltype_blue,
	rgbled_pixeltype_brightness
};

/**
 * struct rgbled_coordinates - defines x/y coordinates
 * @x: x coordinate
 * @y: x coordinate
 *
 * used when translating from panel/LED string ID back to
 * framebuffer coordinates
 */
struct rgbled_coordinates {
	int			x;
	int			y;
};

struct rgbled_panel_info;

/**
 * struct rgbled_fb - the main rgbled framebuffer structure
 * @info: pointer to struct fb_info
 * @deferred_io: struct deferred_io used by framebuffer
 * @panels: list of rgbled_panel_infos
 * @lock: global spinlock for this object
 * @par: driver specific data
 * @name: name of the device
 * @of_node: reference to the device_node that initialized this
 * @duplicate: flag to detect if we have duplicate board_ids
 * @vmem: allocated framebuffer
 * @width: framebuffer width
 * @height: framebuffer height
 * @pixel: pixel string length
 * @deferred_work: the deferred work function - typically default
 * @getPixelValue: get the corresponding pixelvalue of for the specific
 *                 panel coordinates
 * @setPixelValue: set the string pixel value inside the panel
 * @finish_work: for default implementation of filling the string
 *               final submit of the data to the device
 * @current_limit: current limit for the whole framebuffer
 * @current_active: active estimated current usage by the framebuffer
 * @current_tmp: temporary current estimation prior to updating the screen
 * @current_max: max estimated current usage by the framebuffer
 * @led_current_base: base current consumption in mA
 *                    by a single LED chip when black
 * @led_current_max_red: current consumed by red LED
 *                       at maximum brightness in mA
 * @led_current_max_green: current consumed by green LED
 *                         at maximum brightness in mA
 * @led_current_max_blue: current consumed by blue LED
 *                        at maximum brightness in mA
 * @brightness: global brightness for thled panel, that can be controlled
 *              but gets scaled down to limit current to preset values
 *              based on global or panel current limits
 * @screen_updates: number of screen updates executed
 */
struct rgbled_fb {
	struct fb_info		*info;
	struct fb_deferred_io	deferred_io;

	struct list_head	panels;
	spinlock_t		lock;
	void			*par;
	const char		*name;
	struct device_node	*of_node;
	bool			duplicate_id;

	struct rgbled_pixel	*vmem;
	int			width;
	int			height;
	int			vmem_size;

	int			pixel;

	void (*deferred_work)(struct rgbled_fb*);
	void (*getPixelValue)(struct rgbled_fb *rfb,
			      struct rgbled_panel_info *panel,
			      struct rgbled_coordinates *coord,
			      struct rgbled_pixel *pix);
	void (*setPixelValue)(struct rgbled_fb *rfb,
			      struct rgbled_panel_info *panel,
			      int pixel_num,
			      struct rgbled_pixel *pix);
	void (*finish_work)(struct rgbled_fb*);


	/* current estimates in mA */
	u32			current_limit;
	u32			current_active;
	u32			current_tmp;
	u32			current_max;

	/* current consumption when idle */
	u32			led_current_base;
	/* current estimates for full brightness pixel */
	u32			led_current_max_red;
	u32			led_current_max_green;
	u32			led_current_max_blue;

	/* global brightness */
	u8			brightness;

	/* count of screen updates */
	u32			screen_updates;
};

/**
 * struct rgb_panel_info - describes the individual chained panels
 * that make up the whole framebuffer
 * @list - list of panels inside a rgb_framebuffer
 * @id - the sequence number of this panel in the list of all panels
 * @compatible - compatible string of the pannel
 * @name - name of the panel (mostly for reference)
 * @x - the x start coordinate of the panel inside the framebuffer
 * @y - the x start coordinate of the panel inside the framebuffer
 * @width - the width of the panel in pixel
 * @height - the height of the panel in pixel
 * @pixel - the total number of pixel in this panel - this may be
 *          different from width * height to allow "abnormal" shapes
 *          like arcs, circles, ...
 * @pitch - number of pixel per length-unit (typically meter)
 *          this will modify the actual width/height and allows mixing
 *          of different panels in the future
 * @layout_xy: the layout is so that pixels are layed out vertically
 *             and then horizontally
 * @inverted_x: the pixel in X go from high to low (used for rotation)
 * @inverted_y: the pixel in y go from high to low (used for rotation)
 * @flags: define which of those values can get changed via the device tree
 * @multiple: modify default dimensions by setting multiple to allow
 *           for multiple such panels to be attached sequentially
 * @getPixelCoordinates: transform panel pixel number into x/y coordinates
 *                       in the framebuffer
 * @getPixelValue: allows for custom methods to get the real pixel value
 *                 to display on the LED - e.g: local or radial averaging
 * @current_limit: current limit for the whole framebuffer
 * @current_active: active estimated current usage by the framebuffer
 * @current_tmp: temporary current estimation prior to updating the screen
 * @current_max: max estimated current usage by the framebuffer
 * @brightness: control the brightness of this specific panel
 * @of_node: reference to the device_node that initialized this panel
 */
struct rgbled_panel_info {
	struct list_head 	list;
	u32			id;

	const char     		*compatible;
	const char		*name;

	u32			x;
	u32			y;
	u32			width;
	u32			height;

	u32			pixel;

	u32			pitch;

	bool			layout_yx;
	bool			inverted_x;
	bool			inverted_y;

	u32			flags;
#define RGBLED_FLAG_CHANGE_WIDTH	BIT(0)
#define RGBLED_FLAG_CHANGE_HEIGHT	BIT(1)
#define RGBLED_FLAG_CHANGE_PITCH	BIT(2)
#define RGBLED_FLAG_CHANGE_LAYOUT	BIT(3)
#define RGBLED_FLAG_CHANGE_WHL  	(RGBLED_FLAG_CHANGE_WIDTH |  \
					 RGBLED_FLAG_CHANGE_HEIGHT | \
					 RGBLED_FLAG_CHANGE_LAYOUT)
#define RGBLED_FLAG_CHANGE_WHLP  	(RGBLED_FLAG_CHANGE_WHL |    \
					 RGBLED_FLAG_CHANGE_PITCH)

	int (*multiple)(struct rgbled_panel_info *panel, u32 val);

	void (*getPixelCoords)(struct rgbled_fb *rfb,
			       struct rgbled_panel_info *panel,
			       int pixel_num,
			       struct rgbled_coordinates *coord);
	void (*getPixelValue)(struct rgbled_fb *rfb,
			      struct rgbled_panel_info *panel,
			      struct rgbled_coordinates * coord,
			      struct rgbled_pixel *pix);

	/* current estimates */
	u32			current_limit;
	u32			current_active;
	u32			current_tmp;
	u32			current_max;

	/* the default brightness */
	u8			brightness;

	struct device_node	*of_node;
};

/* default implementations for multiple where we have extend the panel
 * by another in sequence horizontally/vertically
*/
extern int rgbled_panel_multiple_width(struct rgbled_panel_info *panel, u32 val);
extern int rgbled_panel_multiple_height(struct rgbled_panel_info *panel, u32 val);

static inline void rgbled_getPixelValue(struct rgbled_fb *rfb,
					struct rgbled_panel_info *panel,
					struct rgbled_coordinates *coord,
					struct rgbled_pixel *pix)
{
	return panel->getPixelValue(rfb, panel, coord, pix);
}

static inline struct rgbled_pixel *rgbled_getFBPixel(
	struct rgbled_fb *rfb,
	struct rgbled_coordinates *coord)
{
	return &rfb->vmem[coord->y * rfb->width + coord->x];
}

static inline void rgbled_getPixelCoords(struct rgbled_fb *rfb,
					 struct rgbled_panel_info *panel,
					 int panel_pixel_num,
					 struct rgbled_coordinates *coord)
{
	panel->getPixelCoords(rfb, panel, panel_pixel_num, coord);
}

/* typical pixel coordinate implementation for standard types */
extern void rgbled_getPixelCoords_linear(
		struct rgbled_fb *rfb,
		struct rgbled_panel_info *panel,
		int pixel_num,
		struct rgbled_coordinates *coord);

/* every other row is inverted - depending on layout_yx */
extern void rgbled_getPixelCoords_meander(
		struct rgbled_fb *rfb,
		struct rgbled_panel_info *panel,
		int pixel_num,
		struct rgbled_coordinates *coord);

/* allocation of the rgbled_framebuffer
 * making use of devres to release the allocated resources
 */
extern struct rgbled_fb *rgbled_alloc(struct device *dev,
				      const char *name,
				      struct rgbled_panel_info *panels);
/* finally register the rgbled_framebuffer */
extern int rgbled_register(struct rgbled_fb *fb);

/* scheduling a screen update for the framebuffer */
static inline void rgbled_schedule(struct fb_info *info)
{
        schedule_delayed_work(&info->deferred_work, 1);
}

/* internal functions used in several c-files - not exported */

/* register all panels that are defined in the devicetree */
extern int rgbled_register_of(struct rgbled_fb *rfb);
extern int rgbled_scan_panels_of(struct rgbled_fb *rfb,
				 struct rgbled_panel_info *panels);
/* register the rgbled_fb in sysfs */
extern int rgbled_register_sysfs(struct rgbled_fb *rfb);
/* register the individual panels */
extern int rgbled_register_panels(struct rgbled_fb *rfb);

#endif /* __RGBLED_FB_H */
