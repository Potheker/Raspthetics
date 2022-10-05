# Raspthetics
An audio spectrum analyzer for the Raspberry Pi, using a soundcard and an addressable LED strip on the GPIO ports. Additionally a second waterfall-style effect on a second strip is included.

# Prerequisites

	sudo apt-get install libasound2-dev scons



# Preparation
1. Plug 2 strips to the GPIO pins. Not all are usable, sticking with Pins 13 and 18 is safe. Enter the length of the strips to LED_COUNT, the pin to GPIO_PIN and whether you want to reverse the order into REVERSE. Hereby the defines with "\_0" are for the spectrum and "\_1" for the waterfall. Also you may want to change the STRIP_TYPE and TARGET_FREQ. Defaults are for a generic ws2812b.

2. Plug in a soundcard and either a microphone or use an audio splitter to route the sound going to your speakers into the soundcard (use the cyan Line port in that case)

3. Determine the device and subdevice ID of your soundcard through `arecord -l`

4. Use `alsamixer -D hw:k` where k is your device ID. Make sure  "PCM Capture" is set to the correct port. Go to the capture devices and enable your chosen device by pressing space such that it says "CAPTURE". Save the settings with `sudo alsactl store`. If they revert after reboot, use [this](https://dev.to/luisabianca/fix-alsactl-store-that-does-not-save-alsamixer-settings-130i "this").

5. *Optional*. Run `sudo ./raspthetics -i` once while nothing is played back to determine the background noise level. The main code will then be able to disable the LEDs if only noise is present.

# Usage

Use `scons` to compile and run the code with `sudo ./raspthetics -c1 -d0` if 1 is your card ID and 0 is your device ID. Card 1 and device 0 are the default.
