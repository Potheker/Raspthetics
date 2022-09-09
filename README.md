# Raspthetics
An audio spectrum analyzer for the Raspberry Pi, using a soundcard (preferably one with a line in port) and an addressable LED strip on the GPIO ports. Additionally another effect on a second strip is included.

# Prerequisites

`sudo apt-get install libasound2-dev
sudo apt-get install scons`


# Usage
First enter "alsamixer -D hw:1" into your terminal (hw:1 maybe has to be replaced with your soundcards id, find it with "arecord -l". In the alsamixer first make sure that under "PCM Capture" the correct source is selected (whether your sound is plugged into the microphone port or line in. Line in is better, but now available on all cards). Then press tab to go to the capture devices and make sure the selected source (mic or line) is enabled (has the red "CAPTURE" string on bottom). If it isn't, press space to enable. Also best set the dB gain to somewhere around zero. Hit your terminal with a "sudo alsactl store" to save the settings. If your settings are still reset after a reboot, follow this https://dev.to/luisabianca/fix-alsactl-store-that-does-not-save-alsamixer-settings-130i

Make sure your sound source is plugged in but nothing is played back, then run the code with the "-i" flag to determine background noise levels. If something played during that time (even just a notification sound) you'll have to redo it. Also redo it if you change something about your audio setup. Then run the code as usual and everything should work.
``
