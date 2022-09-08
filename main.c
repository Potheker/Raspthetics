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

#include "KissFFT/kiss_fft.h"
#include "KissFFT/kiss_fftr.h"

#include "sound.h"
#include "led.h"


#define ARRAY_SIZE(stuff)       (sizeof(stuff) / sizeof(stuff[0]))

//Options for ws2812
//#define LED_COUNT_0             160
#define LED_COUNT_0             151
#define LED_COUNT_1             143

//Range of y-axis is
#define lowdb 30.f
#define highdb 50.f

//Sound & FFT
#define ALSA_FRAMES         32                      //ALSA_FRAMES is the amount of Frames read at once (this is lower for more FPS)
#define FRAME_FACTOR        128                  
#define FRAMES              ALSA_FRAMES*FRAME_FACTOR//FRAMES is the total amount of Frames saved and used for FFT (this is higher for better Resolution)
#define USED_OUTPUTS        FRAMES/2-1              //I forgot why I defined that
#define BUFFER_OVERSIZE     8                       //buffer size is FRAMES*BUFFER_OVERSIZE

//Asethetics
#define VOL_REL             4096                    //Volume release, determines how long a loud part ducks the brightness (with 0 the brightness would always be so that at least 1 pixel is around max brightness, killing the dynamics. I find that 4096 kind of assures that the brightness is relative to the almost loudest part of the track (around last 30 seconds), but still doesn't take forever to change when you lower the volume)           

unsigned int rate = 44100;

short test = -1;
float testmax = 9999999;


float* last_volumes; //Saves last 2048 amplitudes
short last_volumes_index = 0;

float* alsa_buffer;   //Buffer for alsa

//Thread Safety
int alsa_curr_index = 0; //The index (of the als_buffer) to which alsa is currently writing

//ALSA
snd_pcm_t *capture_handle;
snd_pcm_format_t format;

//Soundcard
float offset;       
float noise_level = 999999;   //Initialy set high, so it doesn't interfere with the test
short channels;

//Calculate values from the readings and renders them
void *calculate_and_render(){
    printf("%f\n",offset);

    kiss_fftr_cfg cfg = kiss_fftr_alloc( FRAMES , 0,0,0);
    float *fft_in = malloc(FRAMES * sizeof(float));
    kiss_fft_cpx *cpx_out = malloc(FRAMES/2 * sizeof(kiss_fft_cpx));

    float *fft_out = malloc(USED_OUTPUTS * sizeof(int));

    //The last place in the buffer we performed fft on. (Has this default value so the code waits for the first read)
    int fft_last_index = (BUFFER_OVERSIZE-1)*FRAMES;

    //Wait a bit for the alsa thread (otherwise we have some corrupted readings and it may initialy set the volume too high)
    usleep(300000);

    while(1){
        //While this is true alsa hasn't written a new buffer since the last iteration
        while((fft_last_index+FRAMES)%(BUFFER_OVERSIZE*FRAMES) == alsa_curr_index){
            usleep(1);
        }

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
            same area, independent of the input volume

            To scale the resulting spectrum we then use the lowdb and highdb defines (see on top),
            which dictate which area of the values are displayed. Because of the amplitude scaling
            the amount of activity in that dB area should (as mentioned) stay the same if you
            change the volume on your playback device.
        */

        //Keep track of last volumes
        last_volumes[last_volumes_index] = vol;

        //If the volume is lower than the noise level (saved as an inverse), then nothing is playing and we disable the effect
        //else we look for the max volume of the last few seconds (time depending on VOL_REL)
        if(pow(vol,-1) > noise_level){
            vol = 0;
        } else{
            last_volumes_index = (last_volumes_index+1)%VOL_REL;
            for(int i = 0;i<VOL_REL;i++){
                if(last_volumes[i] > vol)
                    vol = last_volumes[i];
            }
        }
        
        //Invert volume for scaling
        vol = pow(vol,-1);

        //Now we use this vol to scale all our values
        for(int j = 0;j<FRAMES;j++) {
            fft_in[j] *= vol;
            //printf("%f\n",fft_out[j]);
        }

        //Perform FFT
        kiss_fftr(cfg, fft_in, cpx_out);

        //Put Outputs in a better Variable, transform into dB and find max dB (vol)
        for(int j = 0;j<USED_OUTPUTS;j++) {
            //float len = sqrt(powf(cpx_out[j+1].r,2)+powf(cpx_out[j+1].i,2))*volume_factor;
            float len = sqrt(powf(cpx_out[j+1].r,2)+powf(cpx_out[j+1].i,2));

            fft_out[j] = 20*log10(len); //len is the amplitude, fft_out[j] is the dB
            fft_out[j] += 4.5f*log2((j+1)*(float)rate*pow((float)FRAMES,-1)/1000); //We add 4.5dB per octave because high frequencies are perceived louder (-> FF Pro-Q 3 manual)
            //printf("%f\n",fft_out[j]);
        }

        /*
        printf("4: %f\n",fft_out[4]);
        printf("22: %f\n",fft_out[22]);
        printf("47: %f\n",fft_out[47]);
        printf("95: %f\n",fft_out[95]);
        printf("191: %f\n",fft_out[191]);
        printf("1799: %f\n",fft_out[1799]);
        */

        /*

            BRIGHTNESS CALCULATION FROM FFT OUTPUTS

            As the perceived height of a tone is 2-logarithmic, the Function LED_index (=k) \mapsto "frequency represented" has to be exponential (2^k)
            The middle of the first band (k=0) should be 40hz (well below lowest perceivable tone) => 2^k * 40
            Now 2^9*40 is approx. the highest perceivable tone (20khz). We want this not to be k=8 but k=LED_COUNT_0, so our final function:
            
            f: R -> R, LED_index \mapsto frequency     
            k \mapsto f(k) = 2^(9*k/LED_COUNT_0)*40

            Also higher notes are perceived louder, so we'll subtract 4.5dB per Octave (which results in a natural spectrum, according to FF Pro-Q 3)


            
            Now we choose the cutoffs of each LED's frequency domain as f(k-0.5) to f(k+0.5)
            The function fft_index (=k) \mapsto "frequency represented" is (k+1)*rate/FRAMES (this is exactly the middle of the band)
            Now we want 

        */

        //This will save the calculated brightness of the LEDs
        float value[LED_COUNT_0] = {0};
        for(int j = 0;j<LED_COUNT_0;j++){
            //These are the fft Indexes where the band for this LED starts and ends (float because we interpolate)
            float low = pow( 2 , 9.f*(j-0.5f)/LED_COUNT_0 )*40.f * FRAMES/rate;
            float high = pow( 2 , 9.f*(j+0.5f)/LED_COUNT_0 )*40.f * FRAMES/rate;

            //printf("%i: %f, %f\n",j,low,high);

            //Now we find the max of the band over a linear interpolation between the fft_outs.
            float max = -9999999;

            //The max is either one of the fft_outs which are in the (low,high) interval (extrema of the interpolation function)...
            for(int k = (int)ceil(low);k<=(int)floor(high);k++){
                if(fft_out[k] > max){
                    max = fft_out[k];
                }
            }

            //...or the margins (interpolated values exactly at low or high), so we check them
            //(this is especially important because due to the logarithmic nature it is possible that f.e. low=3.4 and high=3.5,
            // would we not interpolate in that case, then that LED could have the same value as the next, if that has low=3.5 and high=3.7,
            // resulting in an unpleasant static effect)
            float x = low-floor(low);
            x = x*fft_out[(int)ceil(low)]+(1.f-x)*fft_out[(int)floor(low)];
            if(x > max)
                max = x;
            x = high-floor(high);
            x = x*fft_out[(int)ceil(high)]+(1.f-x)*fft_out[(int)floor(high)];
            if(x > max)
                max = x;

            //Now max is below 0 and we wanna cut everything below lowdb, so we scale such that value[j] is in the interval [0,1]

            value[j] = (lowdb-max)/(lowdb-highdb);
            //printf("%f: %f\n",max,value[j]);
            if(value[j] < 0){
                value[j] = 0;
            }
            if(value[j] > 1){
                value[j] = 1;
            }
            //printf("%f\n",max+40.f*(float)j/LED_COUNT_0);

        }

        //printf("%f",value[curr_LED]);

        for(int j = 0;j<LED_COUNT_0;j++){
            //value[j] += 4.5*8*j/LED_COUNT_0;
            //printf("%i: %f\n",j,value[j]);
        }



        //For Second Effect, we calculate the average by Sum_j=0^count0 (j+1)*value and dividing by the total value sum (which is waterfall_value pre division) (where value \in [0,1]), then subtracting 1
        float waterfall_hue = 0;

        //For Second Effect, we calculate the max of the spectrum values
        float waterfall_value = 0;
        
        //Self explanatory
        float value_sum = 0;

        /*
        if(value[40] > 1){
            
            FILE *ptr;
            ptr = fopen("sees.txt","w");

            printf("hardware.info opened\n");

            for(int j = 0;j<USED_OUTPUTS;j++) {
                fprintf("%f\n",valu[j]);
            }

            for(int j = 0;j<FRAMES;j++) {
                //printf("%i ",(int)fft_in[j]);
            }

            fclose(ptr);

        }
        */
        for(int j = 0;j<LED_COUNT_0;j++){
            if(value[j] > 1){
                value[j] = 1;
            }
            if(value[j] < 0){
                value[j] = 0;
            }
            //printf("%i: %f\n",j,value[j]);

//TEMP
//value[j] = 0;

            write_color(0,j,value[j]);

            //For second effect (see above)
            waterfall_hue += (j+1)*value[j];
            if(waterfall_value < value[j])
                waterfall_value = value[j];
            value_sum += value[j];
        }

            
        render();
        waterfall_hue /= value_sum;
        waterfall_hue -= 1;                 //See above construction
        waterfall_add(waterfall_value,waterfall_hue);
        //printf("\n\n\n");

        //This is for testing the noise level. We save the max vol while nothing is played back. If the volume ever falls below that value, the effect gets disabled.
        if(test >= 0){
            test++;
            //printf("%i: %f\n",test,vol);
            if(vol < testmax){       
                testmax = vol;
            }
            if(test > 100){
                testmax *= 0.98; //Some more safety
                printf("Second test finished, noise level is %f\n",testmax);
                //Open file, write values, close file

                noise_level = testmax;
                test = 1000;
                return 0;
            }
        }
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

    last_volumes = malloc(sizeof(float)*VOL_REL);
    for(int i = 0;i<VOL_REL;i++){
        last_volumes[i] = -99999;
    }

    //We make the alsa_buffer way larger so that alsa can continuously write to it and our logic can continously read
    //With this size alsa can write to the same buffer FRAME_FACTOR*BUFFER_OVERSIZE times
    alsa_buffer = malloc(snd_pcm_format_width(format) / 8*channels* FRAMES * BUFFER_OVERSIZE);   


    fprintf(stdout, "buffers allocated\n");
    fprintf(stdout, "audio interface prepared\n");

    initialize_led(LED_COUNT_0, LED_COUNT_1);

    fprintf(stdout, "led strip initialized\n");

    //Parse Arguments
    for(int i = 1;i<argc;i++){
        if(argv[i][1] == 'e'){              //Set Soundcard ID
            clear();
            return 0;
        }
        if(argv[i][1] == 'c'){              //Set Soundcard ID
            i++;
            hwid[3] = argv[i][0];
        } else if(argv[i][1] == 'd'){       //Set Soundcard Device ID
            i++;
            hwid[5] = argv[i][0];
        } else if(argv[i][1] == 'i'){       //Find out Soundcard Info
            //Find offset
            if(find_soundcard_parameters(&offset, capture_handle, alsa_buffer, FRAMES) != 0){
                return 1;
            }

            //Tell loop to find noise level
            test = 0;
        }
        printf("%d\n",i);
    }

    fprintf(stdout, "arguments parsed\n");

    //Init Soundcard info
    if(test == -1){
        switch(read_hardware_info(&offset, &noise_level)){
            case 1:
                printf("Info about soundcard hasn't been determined yet, please run the programm with \"-i -c *cardID* -d *device*\" with your sound plugged in but nothing playing (!) \n ");
                printf("IMPORTANT NOTE: You'll probably have to redo this if you switch to another soundcard (but the app won't show an error if you won't, so remember to do it)\n ");
                printf("Sideinfo: I have no clue why I have to do this, but all soundcards seem to have an offset in the data read from alsa, if you know more about this please text me\n");
                return 1;
                break;
            case 2:
                printf("hardware.info file is corrupted. Please delete that file and then run the program with \"-i -c *cardID* -d *device*\" with your sound plugged in but nothing playing (!)\n To find your cardID and device use arecord -l in Terminal.");
                printf("IMPORTANT NOTE: You'll probably have to redo this if you switch to another soundcard (but the app won't show an error if you won't, so remember to do it)\n ");
                break;
            default:
                printf("hardware Info read, offset is %f, noise level is %f\n", offset, noise_level);
        }
    }

    fprintf(stdout, "starting main loop... (Close with Ctrl+C)\n");

    pthread_t render_thread_id;

    pthread_create(&render_thread_id, NULL, calculate_and_render, NULL);
    
    //Maximum amount of writes before wrapping the alsa_buffer
    int max = FRAMES*BUFFER_OVERSIZE;

    //This is our alsa thread:
    //We need the test<1000 condition only if we test via the -i flag, otherwise this is just while(true)
    while(test<1000){
        //Read from ALSA, as the space to write we give an ascending place in the alsa_buffer
        while(read_sound(capture_handle, &alsa_buffer[alsa_curr_index], ALSA_FRAMES) != 0){
            //printf("Sound read error");
            usleep(1);
        }
        /*
        for(int i = 0;i<ALSA_FRAMES;i++){
            printf("%i: %i\n",alsa_curr_index+i, alsa_buffer[alsa_curr_index+i]);
        }
        */

        //Ascend the place in the buffer, wrap if max amount is reached.
        alsa_curr_index = (alsa_curr_index+ALSA_FRAMES)%max;
    }
        
    snd_pcm_close (capture_handle);
    fprintf(stdout, "audio interface closed\n");

    FILE *ptr;
    if(!(ptr = fopen("hardware.info","w")))
        return 0;
    fprintf(ptr,"%f;%f",offset, noise_level);
    fclose(ptr);
                
    printf("noise level and offset written\n");

    led_tidy();

    return 0;
}
