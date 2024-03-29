#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>
#include <stdarg.h>
#include <getopt.h>
#include <math.h>
#include <stdbool.h>

#include "rpi_ws281x/clk.h"
#include "rpi_ws281x/gpio.h"
#include "rpi_ws281x/dma.h"
#include "rpi_ws281x/pwm.h"
#include "rpi_ws281x/ws2811.h"


//Options for ws2812
#define TARGET_FREQ             WS2811_TARGET_FREQ
#define GPIO_PIN_0              18
#define GPIO_PIN_1              13
#define DMA                     10
#define STRIP_TYPE              WS2811_STRIP_GBR	//WS2812/SK6812RGB integrated chip+leds

//Shifts LEDs on strips
#define SHIFT_0                 10 
#define SHIFT_1                 0                  

//Reverses the strips
#define REVERSE_0               false
#define REVERSE_1               true

//Color
#define saturation 1000 //Planning to make this a variable


ws2811_led_t *leds0, *leds1;
int count0, count1;

ws2811_t ledstring =
{
    .freq = TARGET_FREQ,
    .dmanum = DMA,
    .channel =
    {
        [0] =
        {
            .gpionum = GPIO_PIN_0,
            .count = 0,
            .invert = 0,
            .brightness = 255,
            .strip_type = STRIP_TYPE,
        },
        [1] =
        {
            .gpionum = GPIO_PIN_1,
            .count = 0,
            .invert = 0,
            .brightness = 255,
            .strip_type = STRIP_TYPE,
        },
    },
};

//Applys changes
int render(){
    int ret;
    if ((ret = ws2811_render(&ledstring)) != WS2811_SUCCESS){
        fprintf(stderr, "ws2811_render failed: %s\n", ws2811_get_return_t_str(ret));
        return 1;
    }
    return 0;
}

//Gets the hue of the ledID-th LED   \in[0,3600]
int led_hue_natural(int ledID){
    return (int)round(3000*ledID/(float)count0);
}

void clear(){
	for(int i = 0;i<count0;i++) {
    	ledstring.channel[0].leds[i] = 0;
	}
	for(int i = 0;i<count1;i++) {
    	ledstring.channel[1].leds[i] = 0;
	}
    render();
}

//Gets the Color of the ledID-th LED on the Rainbow (Hue 0..300) strip with brightness value
int getColor(int16_t hue, float value){
    //I copied that HSV to RGB Code lol

    if(hue < 500){
        //value *= 1.5;
    }

    char red = 0;
    char green = 0;
    char blue = 0;

    if(value >= 1)
        value = 1000;
    else
        value *= 1000;
    if (saturation == 0)
    {
        red = (uint8_t)((255 * value) / 1000);
        green = red;
        blue = red;
    }
    else
    {
        int16_t h = hue/600;
        int16_t f = ((hue%600)*1000)/600;
        int16_t p = (value*(1000-saturation))/1000;
        int16_t q = (value*(1000-((saturation*f)/1000)))/1000;
        int16_t t = (value*(1000-((saturation*(1000-f))/1000)))/1000;

        switch (h)
        {
        case 0:

            red = (uint8_t)((255 * value) / 1000);
            green = (uint8_t)((255 * t) / 1000);
            blue = (uint8_t)((255 * p) / 1000);
            break;

        case 1:

            red = (uint8_t)((255 * q) / 1000);
            green = (uint8_t)((255 * value) / 1000);
            blue = (uint8_t)((255 * p) / 1000);
            break;

        case 2:

            red = (uint8_t)((255 * p) / 1000);
            green = (uint8_t)((255 * value) / 1000);
            blue = (uint8_t)((255 * t) / 1000);
            break;

        case 3:

            red = (uint8_t)((255 * p) / 1000);
            green = (uint8_t)((255 * q) / 1000);
            blue = (uint8_t)((255 * value) / 1000);
            break;

        case 4:

            red = (uint8_t)((255 * t) / 1000);
            green = (uint8_t)((255 * p) / 1000);
            blue = (uint8_t)((255 * value) / 1000);
            break;

        case 5:

            red = (uint8_t)((255 * value) / 1000);
            green = (uint8_t)((255 * p) / 1000);
            blue = (uint8_t)((255 * q) / 1000);
            break;

        }
    }

    return red | (green << 8) | (blue << 16);
}

//Initialize global count variables and LED String
int initialize_led(int _count0, int _count1){
    count0 = _count0 + SHIFT_0;
    count1 = _count1;
    ledstring.channel[0].count = count0;
    ledstring.channel[1].count = count1;
    leds0 = malloc(sizeof(ws2811_led_t) * count0);
    leds1 = malloc(sizeof(ws2811_led_t) * count1);

    ws2811_return_t ret;
    if ((ret = ws2811_init(&ledstring)) != WS2811_SUCCESS)
    {
        fprintf(stderr, "ws2811_init failed: %s\n", ws2811_get_return_t_str(ret));
        return ret;
    }
	for(int i = 0;i<count0;i++) {
    	ledstring.channel[0].leds[i] = 0;
	}
	for(int i = 0;i<count1;i++) {
    	ledstring.channel[1].leds[i] = 0;
	}
    if ((ret = ws2811_render(&ledstring)) != WS2811_SUCCESS)
    {
        fprintf(stderr, "ws2811_render failed: %s\n", ws2811_get_return_t_str(ret));
    }

    return 0;
}

void write_color(int channel, int ledID, float value){
    ledstring.channel[channel].leds[ledID+SHIFT_0] = getColor(led_hue_natural(ledID), value);
}

void led_tidy(){
    ws2811_fini(&ledstring);
    free(leds0);
    free(leds1);
}

//Add a new color at the top of the waterfall
void waterfall_add(float value, float hue){
    if(REVERSE_1){
        for(int i = 0;i<(count1 - 1);i++){
        ledstring.channel[1].leds[i] = ledstring.channel[1].leds[i+1];
        }
        ledstring.channel[1].leds[count1-1] = getColor(led_hue_natural(hue),value);
    }
    else{
        for(int i = count1-1;i>0;i--){
        ledstring.channel[1].leds[i] = ledstring.channel[1].leds[i-1];
        }
        ledstring.channel[1].leds[0] = getColor(led_hue_natural(hue),value);
    }
}
