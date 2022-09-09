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
#include <alsa/asoundlib.h>
#include <pthread.h>
#include <unistd.h>
#include <float.h>
#include <stdbool.h>

#include "KissFFT/kiss_fft.h"
#include "KissFFT/kiss_fftr.h"

#include "sound.h"
#include "led.h"

//Options for ws2812
#define LED_COUNT_0             151
#define LED_COUNT_1             143

//Range of y-axis on the spectrum analyzer (see "VOLUME SCALING" for details)
#define lowdb 30.f
#define highdb 50.f

//Sound & FFT
/*

    The sound thread reads ALSA_FRAMES samples from alsa. The effect calculation thread
    uses the last FRAME_FACTOR readings to calculate the effect, resulting in FRAMES frames.

    We use different values so that our effect thread can run often (low ALSA_FRAMES), resulting
    in more fps and higher speed on the waterfall (more aesthetic imo), but still uses a longer
    wave (high FRAMES), resulting in a more accurate spectrum.

*/
#define ALSA_FRAMES         32                      
#define FRAME_FACTOR        128                     
#define FRAMES              ALSA_FRAMES*FRAME_FACTOR
#define USED_OUTPUTS        FRAMES/2-1              //I forgot why I defined that

/*
    It's very critical that the alsa thread can read waves without a delay, otherwise we will get
    some really nasty distortion. That's why we make the buffer way larger than we need it, so
    the alsa thread never has to wait for the other thread to finish reading.
*/
#define BUFFER_OVERSIZE     8

//Asethetics
#define VOL_REL             4096                    //Volume release, determines how long a loud part ducks the brightness (see "VOLUME SCALING" for details)  
#define WATERFALL_PUSH      0.1                     //How much the waterfall color is pushed away from the middle (see "WATERFALL EFFECT" for details)     

//Needed for testing noise levels
short test = -1;
float testmax = FLT_MAX;

float* alsa_buffer;   //Buffer for alsa

//Threads
int alsa_curr_index = 0; //The index (of the als_buffer) to which alsa is currently writing
bool run = true;

//ALSA
snd_pcm_t *capture_handle;
snd_pcm_format_t format;

//Soundcard
float offset;       
float noise_level = 999999;   //Initialy set high, so it doesn't interfere with the test
short channels;
unsigned int rate = 44100;

/*

    We have 2 threads: The alsa thread reads sound and this thread calculates the effects

*/
void *calculate_and_render(){

    //Configuration for fft
    kiss_fftr_cfg cfg = kiss_fftr_alloc( FRAMES , 0,0,0);
    float *fft_in = malloc(FRAMES * sizeof(float));
    kiss_fft_cpx *cpx_out = malloc(FRAMES/2 * sizeof(kiss_fft_cpx));
    float *fft_out = malloc(USED_OUTPUTS * sizeof(int));


    //The last place in the buffer we performed fft on. (Has this default value so the code waits for the first read)
    int fft_last_index = (BUFFER_OVERSIZE-1)*FRAMES;

    //For saving volumes to scale the wave
    float last_volumes[VOL_REL] = {0};
    short last_volumes_index = 0;

    //Wait a bit for the alsa thread (otherwise we have some corrupted readings and it may initialy set the volume too high)
    usleep(300000);

    /*

        We calculate 2 effects for 2 different LED strips:

        - LED0 gets a spectrum analyzer effect, which should be self explanatory. The index
        of a LED determines the frequency, the brightness of the LED displays the loudness
        of that frequency area.

        - LED1 gets a "waterfall" effect, which is calculated from the results of the first
        effect. After every spectrum calculation we search for whether the track at that
        moment has more highs or more lows and color the first LED accordingly:
        Low hue (red) for low, high hue (blue) for high. The brightness is the amplitude of
        our wave at that moment and thus the volume of the track. All other LEDs just get
        shifted further down the strip on every step, resulting in the strip showing a time axis.

    */

    while(run){

        //While this is true alsa hasn't written a new buffer since the last iteration
        while((fft_last_index+FRAMES)%(BUFFER_OVERSIZE*FRAMES) == alsa_curr_index){
            usleep(1);
        }

        //Variables needed for the waterfall effect
        float waterfall_hue = 0;
        float waterfall_value = 0;
        float value_sum = 0;

        //Calculate the starting index of the last FRAMES written frames
        fft_last_index = (alsa_curr_index-FRAMES)%(BUFFER_OVERSIZE*FRAMES);


        //Read the newest ALSA output
        float vol = 0;
        for(int j = 0;j<FRAMES;j++) {
            fft_in[j] = (alsa_buffer[(fft_last_index+j+BUFFER_OVERSIZE*FRAMES)%(BUFFER_OVERSIZE*FRAMES)] - offset);
            if(fabs(fft_in[j]) > vol)
                vol = fabs(fft_in[j]);
        }

        /*
            VOLUME SCALING

            We take the max amplitude over the last few seconds (around 30 sec on default settings)
            and scale the wave with the inverse, resulting in the dB always to be around the
            same area, independent of the input volume.

            We use the max over the last 30 seconds, because that results in the brightness going
            down during more silent parts of the track, giving way more energy to parts where it
            becomes louder again. Sadly this results in the phenomenon of the entire effect being
            darker for 30 seconds when you lower the volume on your device. A possible solution
            would be to get a device with 2 outputs and seperate volume knobs, so you can adjust
            the volume you hear while leaving the volume going into the Microcontroller the same.

            To scale the resulting spectrum we then use the lowdb and highdb defines (see on top),
            which dictate which area of the values are displayed. Because of the amplitude scaling,
            the amount of activity in a sepcific dB area should (as mentioned) stay the same if you
            change the volume on your playback device.
        */

        //Keep track of last volumes
        last_volumes[last_volumes_index] = vol;

        //Save current amp for waterfall effect
        waterfall_value = vol;

        //If the volume is lower than the noise level (saved as an inverse), then nothing is playing and we disable the effect
        //else we look for the max volume of the last few seconds (time depending on VOL_REL) and inverse it for scaling
        if(pow(vol,-1) > noise_level){
            vol = 0;
        } else{
            last_volumes_index = (last_volumes_index+1)%VOL_REL;
            for(int i = 0;i<VOL_REL;i++){
                if(last_volumes[i] > vol)
                    vol = last_volumes[i];
            }
            vol = pow(vol,-1);
        }
        
        //Now we scale all the readings and the waterfall amp with respect to the volume
        for(int j = 0;j<FRAMES;j++) {
            fft_in[j] *= vol;
        }
        waterfall_value *= vol;

        //Perform FFT
        kiss_fftr(cfg, fft_in, cpx_out);

        /*
            FFT gives us a complex value. The amp of the band is the abs of the complex.
            
            We plug that amp into 20*log10(x) to transform it to dB.

            We perceive higher frequencies louder. According to the FF Pro-Q 3 manual, adding
            4.5dB per octave results in an accurate representation of what we hear.
            x = (j+1)*rate/FRAMES is the frequency of the band and log2(x/1000) is the octave
            relative to 1khz.
        */
        for(int j = 0;j<USED_OUTPUTS;j++) {
            fft_out[j] = 20*log10(sqrt(powf(cpx_out[j+1].r,2)+powf(cpx_out[j+1].i,2)));
            fft_out[j] += 4.5f*log2((j+1)*(float)rate*pow((float)FRAMES,-1)/1000); //We add 4.5dB per octave because high frequencies are perceived louder (-> FF Pro-Q 3 manual)
        }

        /*

            BRIGHTNESS CALCULATION FROM FFT OUTPUTS

            As the perceived height of a tone is 2-logarithmic, the Function LED_index (=k) \mapsto "frequency represented"
            has to be exponential (2^k). The middle of the first LED (k=0) should be 40hz (a bit below lowest perceivable tone)
            => 2^k * 40. Now 2^9*40 is approx. the highest perceivable tone (20khz). We want this not to be k=8, but k=LED_COUNT_0-1.
            That yields us
            
            f: R -> R, LED_index \mapsto frequency     
            k \mapsto f(k) = 2^(9*k/(LED_COUNT_0-1))*40
            
            The function fft_index (=k) \mapsto "frequency represented" is (k+1)*rate/FRAMES (this is exactly the middle of the band)
            Now we choose the cutoffs of each LED's frequency domain as f(k-0.5) to f(k+0.5) and thus in terms of fft_out, the
            cutoffs are low = f(k-0.5)*FRAMES/RATE-1 and high = f(k+0.5)*FRAMES/RATE-1.

            We search for the max between low and high. Note that low and high are floats, while fft_outs only exist for whole numbers.
            That's why we first check all fft_out[i] with i between low and high, then for the boundaries we interpolate linearly between 
            floor and ceil.

        */

        float low = 0;
        float high = pow( 2 , 9.f*(0.5f)/(LED_COUNT_0-1) )*40.f * FRAMES/rate-1;
        for(int j = 0;j<LED_COUNT_0;j++){

            //These are the fft Indexes where the band for this LED starts and ends
            low = high; //Low of the last is high of the next
            high = pow( 2 , 9.f*(j+0.5f)/(LED_COUNT_0-1) )*40.f * FRAMES/rate-1;

            //Now we find the max as described above
            float max = -9999999;

            //Check whole numbers in the interval
            for(int k = (int)ceil(low);k<=(int)floor(high);k++){
                if(fft_out[k] > max){
                    max = fft_out[k];
                }
            }

            //Check the boundaries with linear interpolation
            float x = low-floor(low);
            x = x*fft_out[(int)ceil(low)]+(1.f-x)*fft_out[(int)floor(low)];
            if(x > max)
                max = x;
            x = high-floor(high);
            x = x*fft_out[(int)ceil(high)]+(1.f-x)*fft_out[(int)floor(high)];
            if(x > max)
                max = x;


            //Now we want values in the interval [lowdb,highdb] to transformed into brightness values in [0,1]
            max = (lowdb-max)/(lowdb-highdb);

            //Cutoffs
            if(max < 0){
                max = 0;
            }
            if(max > 1){
                max = 1;
            }

            //Write the calculated brightness (color is calculated in led.c)
            write_color(0,j,max);

            //See below (WATERFALL EFFECT)
            waterfall_hue += (j+1)*max;
            value_sum += max;

        }

            
        /*

            WATERFALL EFFECT

            We essentially calculate the expected value like in probability theory, which is pretty much what we want
            in terms of a being a single value representing how high or low the current frequencies are.

            The difference is that in probability theory the total is always 1, while here the total is the value_sum
            by which we have to divide afterwards.

            Now hue is in [0,count0] and it's almost always in the middle. To mitigate that effect we first transform
            it to [-1,1] by applying x -> x*2/LED_COUNT_0-1. Then we apply the root function, which pushes numbers
            away from 0 (exactly what we want then) and reverse the transform. WATERFALL_PUSH determines which root
            we take.

            As the root doesn't work with negatives, we take the abs before and negate the number afterwards if it
            was negative.

        */

        waterfall_hue /= value_sum;
        int negative = waterfall_hue < LED_COUNT_0/2.;
        waterfall_hue = (      pow(fabs(waterfall_hue*2./LED_COUNT_0-1.) ,WATERFALL_PUSH) * (1.-negative*2.)    +1.)/2.*LED_COUNT_0;
        waterfall_add(waterfall_value,waterfall_hue);
        render();


        //This is for testing the noise level. We save the max vol while nothing is played back. If the volume ever falls below that value, the effect gets disabled.
        if(test >= 0){
            test++;
            if(vol < testmax){       
                testmax = vol;
            }
            if(test > 100){

                testmax *= 0.98;        //Some more safety
                printf("Second test finished, noise level is %f\n",testmax);

                noise_level = testmax;  //Save value
                test = 1000;            //Tell main thread to save values
                run = false;            //Tell alsa thread to exit
                return 0;
            }
        }
    }
    kiss_fftr_free(cfg);
    return 0;
}

void *alsa_read(){

    //Maximum amount of writes before wrapping the alsa_buffer
    int max = FRAMES*BUFFER_OVERSIZE;

    while(run){

        //Read from ALSA. The space to write is an ascending place in the alsa_buffer
        while(read_sound(capture_handle, &alsa_buffer[alsa_curr_index], ALSA_FRAMES) != 0){
            //printf("Sound read error");
            usleep(1);
        }

        //Ascend the place in the buffer, wrap if max amount is reached.
        alsa_curr_index = (alsa_curr_index+ALSA_FRAMES)%max;
    }
    return 0;
}

int main(int argc, char* argv[])
{
    //Default for hwid
    char* hwid = (char*)malloc(sizeof(char)*11);
    strcpy(hwid,"plughw:1,0");

    //Init Sound
    format = SND_PCM_FORMAT_FLOAT_LE;
    if(initialize_sound(&capture_handle, format, &rate, &channels, hwid) != 0){
        return 1;
    }
    fprintf(stdout, "audio interface prepared\n");

    //Create buffer
    alsa_buffer = malloc(snd_pcm_format_width(format) / 8*channels* FRAMES * BUFFER_OVERSIZE);  
    fprintf(stdout, "buffers allocated\n");

    //Init LED
    initialize_led(LED_COUNT_0, LED_COUNT_1);
    fprintf(stdout, "led strip initialized\n");

    //Parse Arguments
    for(int i = 1;i<argc;i++){
        if(argv[i][1] == 'c'){              //Clear LEDs
            clear();
            return 0;
        }
        if(argv[i][1] == 'c'){              //Set Soundcard ID
            hwid[7] = argv[i][2];
        } else if(argv[i][1] == 'd'){       //Set Soundcard Device ID
            i++;
            hwid[9] = argv[i][2];
        } else if(argv[i][1] == 'i'){       //Find out Soundcard Info
            //Find offset
            if(find_soundcard_parameters(&offset, capture_handle, alsa_buffer, FRAMES) != 0){
                return 1;
            }

            //Tell loop to find noise level
            test = 0;
        }
    }
    fprintf(stdout, "arguments parsed\n");

    //Init Soundcard info
    if(test == -1){
        if(read_hardware_info(&offset, &noise_level) == 0){
            printf("hardware Info read, offset is %f, noise level is %f\n", offset, noise_level);
        }
        else{
            printf("WARNING! Hardware info hasn't been determined yet, run the code once with -i for optimal behaviour.");
            offset = 0;
            noise_level = FLT_MAX;
        }
    }

    //Create threads
    fprintf(stdout, "starting main loop... (Close with Enter)\n");
    pthread_t render_thread_id;
    pthread_t alsa_thread_id;
    pthread_create(&render_thread_id, NULL, calculate_and_render, NULL);
    pthread_create(&alsa_thread_id, NULL, alsa_read, NULL);

    //Wait for user input
    if(test == -1)
        getchar();

    //Tell threads to finish
    run = false;
    pthread_join(alsa_thread_id, NULL); 
    fprintf(stdout, "alsa thread finished\n");
    pthread_join(render_thread_id, NULL);
    fprintf(stdout, "render thread finished\n");

    //Write results if testing was made
    if(test == 1000){
        FILE *ptr;
        if(!(ptr = fopen("hardware.info","w")))
            return 0;
        fprintf(ptr,"%f;%f",offset, noise_level);
        fclose(ptr);
                    
        printf("noise level and offset written\n");
    }

    //Clear LEDs
    clear();
    led_tidy();
    fprintf(stdout, "leds cleared\n");

    //Free buffers
    free(alsa_buffer); 
    fprintf(stdout, "buffer freed\n");
        
    //Close sounds
    snd_pcm_close (capture_handle);
    fprintf(stdout, "audio interface closed\n");


    return 0;
}
//select[mupfi]
