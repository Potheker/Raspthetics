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

#include "sound.h"
#include "led.h"


#define ARRAY_SIZE(stuff)       (sizeof(stuff) / sizeof(stuff[0]))

//Options for ws2812
#define LED_COUNT_0             160
#define LED_COUNT_1             143

//Sound & FFT
#define ALSA_FRAMES             32                 //ALSA_FRAMES is the amount of Frames read at once (this is lower for more FPS)
#define FRAMES                  2048                //FRAMES is the total amount of Frames saved and used for FFT (this is higher for better Resolution)
#define USED_OUTPUTS            FRAMES/2*0.8        //Half of frames are doubled (FFT Output is symmetrical) and 20% of Frequencies are too high
unsigned int rate = 44100;

#define LOW_READS_THRESHOLD 100
short low_reads = 0;        //This gets +1 for every read which is entirely below noise_level, if it reaches LOWS_THRESHOLD the effect gets disabled

float volume_factor = 0.0000000004f; //Variability not implemented yet

//Buffers for ALSA and copying ALSA output to the FFT Thread
int* copy_buffer[2];
int* alsa_buffer;

//Thread Safety
int alsa_last_written = 0;  //The last copy_buffer Index the alsa thread has written to
int alsa_curr_writing = 0;   //The copy_buffer Index currently used by alsa
int fft_curr_reading = -1;  //The bufffer Index currently used by FFT

//ALSA
snd_pcm_t *capture_handle;
snd_pcm_format_t format;

//Soundcard
float offset;       //Position in the ALSA output that represents the waveform being at 0 (which for some reason isn't 0 and different with every card)
float noise_level;  //Highest possible value while nothing is playing (NOTE This is the really high "raw" value without any division applied)
short channels;

//Calculate values from the readings and renders them
void *calculate_and_render(){
    printf("%f\n",offset);
    kiss_fft_cpx *cpx_in = malloc(FRAMES * sizeof(kiss_fft_cpx));
    kiss_fft_cpx *cpx_out = malloc(FRAMES * sizeof(kiss_fft_cpx));
    float *fft_out = malloc(USED_OUTPUTS * sizeof(int));
    kiss_fft_cfg cfg = kiss_fft_alloc( FRAMES , 0 ,0,0 );
    while(1){
        //While this is true, ALSA is still writng it's first output after FFT unlocked it's copy_buffer
        //(cause ALSA then only has 1 copy_buffer and has to rewrite that all the time)
        while(alsa_curr_writing == alsa_last_written)
            usleep(1);

        //This stays at 0 if all reads are below noise_level
        char low_read = 1;

        //DEBUG
        float maxread = 0;

        //Read the newest ALSA output from the latest written buffer
        fft_curr_reading = alsa_last_written;
        for(int j = 0;j<FRAMES;j++) {
            cpx_in[j] = (kiss_fft_cpx){.r = (copy_buffer[fft_curr_reading][j] - offset), .i = 0};

            //Fade the wave in/out at the beginning and end
            if(j < 500)
                cpx_in[j].r *= j/500.;
            if(j > FRAMES-500)
                cpx_in[j].r *= (FRAMES-j)/500.;

            if(abs(cpx_in[j].r) > noise_level)
                low_read = 0;
            //cpx_in[j].r *= volume_factor; //Has to be applied after noise_level test

            //DEBUG
            //printf("%f\n",(float)(cpx_in[j].r));
            if(abs(cpx_in[j].r) > maxread)
                maxread = abs(cpx_in[j].r);
        }
        //printf("\n");
        fft_curr_reading = -1;
        //printf("%f\n",maxread);

        if(low_read == 0){
            low_reads = 0;  //This will also enable the effect if it was disabled (low_reads = -1)
        } else if(low_reads != -1){
            low_reads++;
            if(low_reads >= LOW_READS_THRESHOLD) //Disable the Effect
                low_reads = -1;
            for(int j = 0;j<LED_COUNT_0;j++){
                write_color(0,j,0);
            }
            for(int j = 0;j<LED_COUNT_1;j++){
                write_color(1,j,0);
            }
            render();
        }

        if(low_reads == -1){
            continue;
        }
        

        //Perform FFT
        kiss_fft(cfg, cpx_in, cpx_out);

        //Put Outputs in a better Variable and divide them by the range of the Octave Spectrum they represent ( oct(hz(i+1))-oct(hz(i)) ) to weight them
        for(int j = 0;j<USED_OUTPUTS;j++) {
            float len = sqrt(pow(cpx_out[j].r,2)+pow(cpx_out[j].i,2))*volume_factor;
            //printf("%i: %f\n",j,len);
            fft_out[j] = len/( log2( ( rate*(j+2)/FRAMES )/16.35f) - log2( ( rate*(j+1)/FRAMES )/16.35f) );
        }


            /*
                    THE NEXT PART IS ENTIRELY THE CALCULATION OF THE LED BRIGHTNESS

        hz(i) = rate*(i+1)/frames, i = 0,...,frames-1         Gets the average frequency of the i-th FFT Output   (Actually rate and frames booth have to be halfed, but that shorts)
        oct(hz) = log2(hz/32.7)                               Converts the frequency to the Octave (1st Octave starts at 32.7hz) 
        LED(oct) = oct/8*LED_COUNT_0                          Converts the Octave to a position on the LED Strip
        f(i) = LED(oct(hz(i)))                                Gets the strip position of the i-th FFT Output
        f^-1(i) = 32.7*2^(i/LED_COUNT_0*8)/rate*frames - 1    Gets the start FFT Output of the i-th Output
            
        a..b is the Range of FFT Outputs for the j-th LED, so we want a=f^-1(j)  b=f^-1(j+1)
            */
        float a;
        float b = 0;

        //For Second Effect, we calculate the average by Sum_j=0^count0 (j+1)*value and dividing by the total value sum (which is waterfall_value pre division) (where value \in [0,1]), then subtracting 1
        float waterfall_hue = 0;

        //For Second Effect, we calculate the max of the spectrum values
        float waterfall_value = 0;
        
        //Self explanatory
        float value_sum = 0;

        for(int j = 0;j<LED_COUNT_0;j++) {
            a = b;                                                              //End of the last Frequency Range is start of the next Frequency Range
            b = (32.7*pow(2, (float)(j+1)/LED_COUNT_0*8) -32.7)/rate*FRAMES;    //f^-1(j) but I've added -32.7 and deleted -1 so it starts at the 0-th Output

            //Now we calculate the Integral_a^b of a hypothetical function g that Interpolates the FFT Outputs which are g(0),g(1),...,g(n)
            float value = 0;
            int fl = floor(a);
            if(fl == floor(b)) {                        //The entire Range is inbetween two FFT Outputs, we calculate the integral with this fancy formula (I came up with)
                value = 0.5f*(  fft_out[fl]*(2-a-b+2*fl)  +  fft_out[fl+1]*(a+b-2*fl)  );
            }
            else {                                      //It ranges over multiple Outputs
                for(int k = ceil(a);k<floor(b);k++) {   //All the j..j+1 Ranges here are to be fully integrated and are a trapeze which we calculate by:
                    value += 0.5f*(fft_out[k] + fft_out[k+1]);
                }
                value += 0.5f*(fft_out[fl] + (a-fl+1)*fft_out[fl+1]);                           //Integral of the a..ceil(a) Range
                value += fft_out[(int)floor(b)] + 0.5f*(b-floor(b))*fft_out[(int)ceil(b)];      //Integral of the floor(b)..b Range
            }
            value /= 6120*(b-a);   //Divide by the Range (b-a) to get the average value. Divide by 6120 to get roughly an value \in [0,1]
            if(value > 1)
                value = 1;

            write_color(0,j,value);

            //For second effect (see above)
            waterfall_hue += (j+1)*value;
            if(waterfall_value < value)
                waterfall_value = value;
            value_sum += value;
        }
        render();
        waterfall_hue /= value_sum;
        waterfall_hue -= 1;                 //See above construction
        waterfall_add(waterfall_value,waterfall_hue);
    }
    return 0;
}

int main(int argc, char* argv[])
{
    //Default for hwid
    char* hwid = (char*)malloc(sizeof(char)*6);
    strcpy(hwid,"hw:1,0");

    //Init Sound
    format = SND_PCM_FORMAT_S16_LE;
    if(initialize_sound(&capture_handle, format, &rate, &channels, hwid) != 0){
        return 1;
    }

    //Allocate buffers
  	copy_buffer[0] = malloc(FRAMES * snd_pcm_format_width(format) / 8*channels);
  	copy_buffer[1] = malloc(FRAMES * snd_pcm_format_width(format) / 8*channels);
    alsa_buffer = malloc(FRAMES * snd_pcm_format_width(format) / 8*channels);
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
            if(find_soundcard_parameters(&offset, &noise_level, capture_handle, copy_buffer[0], FRAMES) != 0){
                return 1;
            }

            remove("hardware.info");

            FILE *ptr;
            if(!(ptr = fopen("hardware.info","w")))
                return 1;  //Hardware Info hasn't been determined

            //fprintf(ptr,"%f;%f",offset, noise_level*1.05f);
            fprintf(ptr,"%f;%f",offset, 0.0);

            fclose(ptr);

            printf("noise level and offset written\n");

            return 0;
        }
        printf("%d\n",i);
    }

    fprintf(stdout, "arguments parsed\n");

    //Init Soundcard info
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

    fprintf(stdout, "starting main loop... (Close with Ctrl+C)\n");

    pthread_t render_thread_id;

    pthread_create(&render_thread_id, NULL, calculate_and_render, NULL);
    int u = 0;
    for(;;){
        //Shift the alsa_buffer by ALSA_FRAMES indices to make space for a new read
        for(int j = FRAMES-1;j>=ALSA_FRAMES;j--){
            alsa_buffer[j] = alsa_buffer[j-ALSA_FRAMES];
        }
        //Read from ALSA
        while(read_sound(capture_handle, alsa_buffer, ALSA_FRAMES) != 0){
            //printf("Sound read error");
            usleep(1);
        }
        if((u++)%2 == 0){
            u -= 2;
            hue_add++;
            if(hue_add > 3600)
                hue_add -= 3600;
        }

        //Find the next copy_buffer to use
        if(fft_curr_reading == -1)
            alsa_curr_writing = (alsa_last_written+1)%2;    //if FFT is not reading, just use the buffer not used last (so it switches around all times)
        else
            alsa_curr_writing = (fft_curr_reading+1)%2;     //if FFT is writing, use the one it isn't writing to

        //Copy the alsa_buffer to the copy buffer
        for(int j = 0;j<FRAMES;j++){
            copy_buffer[alsa_curr_writing][j] = alsa_buffer[j];
        }
        alsa_last_written = alsa_curr_writing;
        alsa_curr_writing = -1;
    }
        
    snd_pcm_close (capture_handle);
    fprintf(stdout, "audio interface closed\n");


    printf ("\n");

    led_tidy();

    return 0;
}
