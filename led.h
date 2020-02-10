//Options for ws2812
#define TARGET_FREQ             WS2811_TARGET_FREQ
#define GPIO_PIN_0              18
#define GPIO_PIN_1              13
#define DMA                     10
#define STRIP_TYPE              WS2811_STRIP_GBR	// WS2812/SK6812RGB integrated chip+leds

int initialize_led(int count0, int count1);
void write_color(int channel, int ledID, float value);
int render();
void led_tidy();