rgbled-fb
=========

framebuffer device driver that implements
a simple framebuffer device for:
* ws2812b - tested with adafruit-arc - 15 pixel and 32x8 panel)
* ws2812 - not tested due to lack of device
* apa102 - not tested due to lack of device

# hw-setup/level translation
Basically you have to set it up with a 74HCT125 as a level translator
* ws2812b (and ws2812):
 * SPI-CS    is connected to 1/OE
 * SPI-MOSI  is connected to 1A
 * WS2812-DI is connected to 1Y (possibly with a pulldown)
* apa102
 * SPI-CS    is connected to 1/OE and 2/OE
 * SPI-SCK   is connected to 1A
 * SPI-MOSI  is connected to 2A
 * APA102-CI is connected to 1Y (possibly with pulldowns)
 * APA102-DI is connected to 1Y (possibly with pulldowns)

# device-tree

You need to configure the framebuffer via the device-tree

Here an example for a ws2812b string that creates a 40x16 pixel framebuffer
from 2 matrix 8x32 plus 2 matrix 8x8:

```
fb2: fb@0 {
	reg = <0>;
	status = "okay";

	compatible = "worldsemi,ws2812b";
	/* spi speed needs to be 3 x the 800kHz pixel rate of the ws2812b */
	spi-max-frequency = <2400000>;
	/* limit current for the whole panel to 4A
	 * - depending on power supply */
	current-limit = <4000>;
	/*
	 * define the panels 
	 * note that if they overlap they will produce identical content
	 * for the defined portion 
	 */
	panel@0 {
		reg = <0>;
		compatible = "adafruit,neopixel,matrix,32x8";
		/* override the default height by multiplying by 2 */
		height = <16>;
	};
	panel@1 {
		reg = <1>;
		compatible = "adafruit,neopixel,matrix,8x8";
		/* set the coordinates */
		x = <32>;
		y = <0>;
		/* override the default height by multiplying by 2 */
		height = <16>;
	};
};
```

# sysfs
Lots of values are exposed in /sys/class/graphics/fbX/:
* led_count - number of LED in the "strip"
* led_max_current_blue - mAmper that a single led consumes at full power blue
* led_max_current_green - mAmper that a single led consumes at full power green
* led_max_current_red - mAmper that a single led consumes at full power red
* current - estimated mAmper that the led string consumes
* current_max - estimated maximum mAmper that the led string consumed
* current_limit - current limit in mAmper that triggers a reduction in overall brigthness to stay below this value
* brightness - overall display brightness (scaled automatically to limit current)

# Missing/todo:
* better documentation
* upsreaming to official kernel
* merge with foundation kernels
* exposing led separately via /sys/class/led on request
