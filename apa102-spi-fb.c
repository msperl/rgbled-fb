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
struct rgbled_board_info apa102_boards[] = {
	{
		.compatible	= "shiji-led,apa102,strip,30",
		.width		= 1,
		.height		= 1,
		.pitch		= 30,
	},
	{
		.compatible	= "shiji-led,apa102,strip,60",
		.width		= 1,
		.height		= 1,
		.pitch		= 60,
	},
	{
		.compatible	= "shiji-led,apa102,strip,144",
		.width		= 1,
		.height		= 1,
		.pitch		= 144,
	},
	{
		.compatible	= "adafruit,dotstar,strip,30",
		.width		= 1,
		.height		= 1,
		.pitch		= 30,
	},
	{
		.compatible	= "adafruit,dotstar,strip,60",
		.width		= 1,
		.height		= 1,
		.pitch		= 60,
	},
	{
		.compatible	= "adafruit,dotstar,strip,144",
		.width		= 1,
		.height		= 1,
		.pitch		= 144,
	},
	{ }
};

static void apa102_deferred_work(struct rgbled_fb *rfb) {
	struct apa102_data *bs=rfb->par;
	fb_info(rfb->info, "Update led screen:\n");
}

static int apa102_probe(struct spi_device *spi)
{
	struct apa102_data *bs;
	int err;
	int len;

	bs = devm_kzalloc(&spi->dev, sizeof(*bs), GFP_KERNEL);
	if (!bs)
		return -ENOMEM;

	bs->rgbled_fb = rgbled_alloc(&spi->dev);
	if (!bs->rgbled_fb)
		return -ENOMEM;
	/* scan boards to get string size */
	err = rgbled_scan_boards(bs->rgbled_fb, apa102_boards);
	if (err)
		return err;

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

	bs->rgbled_fb->deferred_work = apa102_deferred_work;
	bs->rgbled_fb->par = bs;

	return rgbled_register(bs->rgbled_fb, DEVICE_NAME);
}

static const struct of_device_id apa102_of_match[] = {
        {
                .compatible     = "shiji-led,apa102",
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
