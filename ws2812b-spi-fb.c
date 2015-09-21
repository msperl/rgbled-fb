/*
 *  linux/drivers/video/fb/ws2812-spi-fb.c
 *
 *  (c) Martin Sperl <kernel@martin.sperl.org>
 *
 *  Frame buffer code for WS2812B LED strip/Panel using
 *  SPI-MOSI transfer only encoding clock in MOSI ignoring
 *  SPI-SCK (just used to push the bits at the correct rate).
 *
 *  Typically setup via a 74HCT125 for level translation to 5V
 *  where:
 *    SPI-CS    is connected to 1/OE
 *    SPI-MOSI  is connected to 1A
 *    WS2812-DI is connected to 1Y
 *  (or any other of the buffers on the 74HCT125)
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

#define DEVICE_NAME "ws2812b-spi-fb"

/* encoded byte
 * we present each bit as 3 bits (oversampling by a factor of 3)
 *   zero is represented as 0b100
 *   one is represented as 0b110
 * so each byte is actually represented as 3 bytes
 *
 * the following are tables to avoid repeated computations
 * and are sent via spi as high, medium, low at 3* required HZ
 */
const char byte2encoding_h[/* (value >> 5) & 0x07 */] = {
	0x92,
	0x93,
	0x9a,
	0x9b,
	0xd2,
	0xd3,
	0xda,
	0xdb
};
const char byte2encoding_m[/* (value >> 3) & 0x03 */] = {
	0x49,
	0x4d,
	0x69,
	0x6d
};
const char byte2encoding_l[/* value & 0x07 */] = {
	0x24,
	0x26,
	0x34,
	0x36,
	0xa4,
	0xa6,
	0xb4,
	0xb6
};

struct ws2812b_encoding {
	u8 h;
	u8 m;
	u8 l;
};

/* an encoded rgb-pixel */
struct ws2812b_pixel {
	struct ws2812b_encoding g, r, b;
};

struct ws2812b_data {
	struct spi_device *spi;
	struct rgbled_fb *rgbled_fb;
	struct ws2812b_pixel *spi_data;
	struct spi_message spi_msg;
	struct spi_transfer spi_xfer;
};

/* define the different FB types */
struct rgbled_board_info ws2812b_boards[] = {
	{
		.compatible	= "worldsemi,ws2812b,strip",
		.width		= 1,
		.height		= 1,
	},
	{
		.compatible	= "adafruit,neopixel,strip,30",
		.width		= 1,
		.height		= 1,
		.pitch		= 30,
	},
	{
		.compatible	= "adafruit,neopixel,strip,60",
		.width		= 1,
		.height		= 1,
		.pitch		= 60,
	},
	{
		.compatible	= "adafruit,neopixel,strip,144",
		.width		= 1,
		.height		= 1,
		.pitch		= 144,

	},
	{
		.compatible	= "adafruit,neopixel,ring,12",
		.pixel		= 12,
		.width		= 6,
		.height		= 6,
	},
	{
		.compatible	= "adafruit,neopixel,ring,16",
		.pixel		= 16,
		.width		= 8,
		.height		= 8,
	},
	{
		.compatible	= "adafruit,neopixel,ring,24",
		.pixel		= 24,
		.width		= 10,
		.height		= 10,
	},
	{
		.compatible	= "adafruit,neopixel,arc,15",
		.pixel		= 15,
		.width		= 8,
		.height		= 8,
	},
	{
		.compatible	= "adafruit,neopixel,matrix,8x8",
		.width		= 8,
		.height		= 8,
		.getPixelValue	= rgbled_getPixelValue_winding,
		.pitch		= 112,
	},
	{
		.compatible	= "adafruit,neopixel,matrix,16x16",
		.width		= 16,
		.height		= 16,
		.getPixelValue	= rgbled_getPixelValue_winding,
		.pitch		= 112,
	},
	{
		.compatible	= "adafruit,neopixel,matrix,32x8",
		.width		= 32,
		.height		= 8,
		.getPixelValue	= rgbled_getPixelValue_winding,
		.pitch		= 112,
	},
	{
		.compatible	= "adafruit,neopixel,stick,8",
		.width		= 8,
		.pitch		= 156,
	},
	{ }
};

static void ws2812b_deferred_work(struct rgbled_fb *rfb) {
	struct ws2812b_data *bs=rfb->par;
	fb_info(rfb->info, "Update led screen:\n");
}

static int ws2812b_probe(struct spi_device *spi)
{
	struct ws2812b_data *bs;
	int err;
	int len;

	bs = devm_kzalloc(&spi->dev, sizeof(*bs), GFP_KERNEL);
	if (!bs)
		return -ENOMEM;

	bs->rgbled_fb = rgbled_alloc(&spi->dev);
	if (!bs->rgbled_fb)
		return -ENOMEM;
	/* scan boards to get string size */
	err = rgbled_scan_boards(bs->rgbled_fb, ws2812b_boards);
	if (err)
		return err;

	/* set up the spi-message and buffers */
	len = bs->rgbled_fb->pixel * sizeof(struct ws2812b_pixel)
		+ 15;
	bs->spi_data = devm_kzalloc(&spi->dev, len, GFP_KERNEL);
	if (!bs->spi_data)
		return -ENOMEM;

	bs->spi = spi;
	spi_message_init(&bs->spi_msg);
	bs->spi_xfer.len = len;
	bs->spi_xfer.tx_buf = bs->spi_data;
	spi_message_add_tail(&bs->spi_xfer, &bs->spi_msg);

	bs->rgbled_fb->deferred_work = ws2812b_deferred_work;
	bs->rgbled_fb->par = bs;

	return rgbled_register(bs->rgbled_fb, DEVICE_NAME);
}

static const struct of_device_id ws2812b_of_match[] = {
        {
                .compatible     = "worldsemi,ws2812b",
        },
        { }
};
MODULE_DEVICE_TABLE(of, ws2812b_of_match);

static struct spi_driver ws2812b_driver = {
        .driver = {
                .name = DEVICE_NAME,
                .owner = THIS_MODULE,
                .of_match_table = ws2812b_of_match,
        },
        .probe = ws2812b_probe,
};
module_spi_driver(ws2812b_driver);

MODULE_AUTHOR("Martin Sperl <kernel@martin.sperl.org>");
MODULE_DESCRIPTION("WS2812B RGB LED FB-driver via SPI");
MODULE_LICENSE("GPL");
