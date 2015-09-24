/*
 *  linux/drivers/video/fb/apa102-spi-fb.c
 *
 *  (c) Martin Sperl <kernel@martin.sperl.org>
 *
 *  Frame buffer code for WS2812B LED strip/Panel using SPI
 *
 *  Typically setup via a 74HCT125 for level translation to 5V
 *  where:
 *    SPI-CS    is connected to 1/OE and 2/OE
 *    SPI-SCK   is connected to 1A
 *    SPI-MOSI  is connected to 2A
 *    APA102-CI is connected to 1Y
 *    APA102-DI is connected to 1Y
 *  (or any other combination of buffers on the 74HCT125)
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>

#include "rgbled-fb.h"

#define DEVICE_NAME "apa102-spi-fb"


/* an encoded rgb-pixel */
struct apa102_pixel {
	u8 brightness, b, g, r;
};

struct apa102_data {
	struct spi_device *spi;
	struct rgbled_fb *rgbled_fb;
	struct apa102_pixel *spi_data;
	struct spi_message spi_msg;
	struct spi_transfer spi_xfer;
};

/* define the different FB types */
struct rgbled_panel_info apa102_panels[] = {
	{
		.compatible	= "shiji-led,apa102,strip",
		.width		= 1,
		.height		= 1,
		.pitch		= 30,
		.flags		= RGBLED_FLAG_CHANGE_WHLP,
	},
	{
		.compatible	= "shiji-led,apa102,strip,30",
		.width		= 1,
		.height		= 1,
		.pitch		= 30,
		.flags		= RGBLED_FLAG_CHANGE_WHL,
	},
	{
		.compatible	= "shiji-led,apa102,strip,60",
		.width		= 1,
		.height		= 1,
		.pitch		= 60,
		.flags		= RGBLED_FLAG_CHANGE_WHL,
	},
	{
		.compatible	= "shiji-led,apa102,strip,144",
		.width		= 1,
		.height		= 1,
		.pitch		= 144,
		.flags		= RGBLED_FLAG_CHANGE_WHL,
	},
	{
		.compatible	= "adafruit,dotstar,strip,30",
		.width		= 1,
		.height		= 1,
		.pitch		= 30,
		.flags		= RGBLED_FLAG_CHANGE_WHL,
	},
	{
		.compatible	= "adafruit,dotstar,strip,60",
		.width		= 1,
		.height		= 1,
		.pitch		= 60,
		.flags		= RGBLED_FLAG_CHANGE_WHL,
	},
	{
		.compatible	= "adafruit,dotstar,strip,144",
		.width		= 1,
		.height		= 1,
		.pitch		= 144,
		.flags		= RGBLED_FLAG_CHANGE_WHL,
	},
	{ }
};

static void apa102_setPixelValue(struct rgbled_fb *rfb,
				struct rgbled_panel_info *panel,
				int pixel_num,
				struct rgbled_pixel *pix)
{
	struct apa102_data *bs=rfb->par;
	struct apa102_pixel *spix = &bs->spi_data[pixel_num+1];

	spix->brightness = 0xe0 | (pix->brightness >> 3);
	spix->r = pix->red;
	spix->g = pix->green;
	spix->b = pix->blue;
}

static void apa102_finish_work(struct rgbled_fb *rfb)
{
	struct apa102_data *bs=rfb->par;

	/* just issue spi_sync on the prepared spi message */
	spi_sync(bs->spi, &bs->spi_msg);
}

static int apa102_probe(struct spi_device *spi)
{
	struct apa102_data *bs;
	int len;

	bs = devm_kzalloc(&spi->dev, sizeof(*bs), GFP_KERNEL);
	if (!bs)
		return -ENOMEM;

	bs->rgbled_fb = rgbled_alloc(&spi->dev, DEVICE_NAME, apa102_panels);
	if (!bs->rgbled_fb)
		return -ENOMEM;
	if (IS_ERR(bs->rgbled_fb))
		return PTR_ERR(bs->rgbled_fb);

	/* set up the spi-message and buffers */
	len =
	      /* start frame + pixel data themselves */
	      + (bs->rgbled_fb->pixel + 1) * sizeof(struct apa102_pixel)
	      /* end signal - extra clocks needed for propagation*/
	      + bs->rgbled_fb->pixel / 8 + 1;
	bs->spi_data = devm_kzalloc(&spi->dev, len, GFP_KERNEL);
	if (!bs->spi_data)
		return -ENOMEM;
	/* fill in the "trailing" clocks */
	memset(&bs->spi_data[bs->rgbled_fb->pixel + 1],
	       255, bs->rgbled_fb->pixel / 8 + 1);

	bs->spi = spi;
	spi_message_init(&bs->spi_msg);
	bs->spi_xfer.len = len;
	bs->spi_xfer.tx_buf = bs->spi_data;
	spi_message_add_tail(&bs->spi_xfer, &bs->spi_msg);

	/* setting up deferred work */
	bs->rgbled_fb->setPixelValue = apa102_setPixelValue;
	bs->rgbled_fb->finish_work = apa102_finish_work;
	bs->rgbled_fb->par = bs;

	/* and register */
	return rgbled_register(bs->rgbled_fb);
}

static const struct of_device_id apa102_of_match[] = {
	{
		.compatible	= "shiji-led,apa102",
	},
	{ }
};
MODULE_DEVICE_TABLE(of, apa102_of_match);

static struct spi_driver apa102_driver = {
        .driver = {
                .name = DEVICE_NAME,
                .owner = THIS_MODULE,
                .of_match_table = apa102_of_match,
        },
        .probe = apa102_probe,
};
module_spi_driver(apa102_driver);

MODULE_AUTHOR("Martin Sperl <kernel@martin.sperl.org>");
MODULE_DESCRIPTION("apa102 RGB LED FB-driver via SPI");
MODULE_LICENSE("GPL");
