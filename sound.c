#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <float.h>

#define TESTRUNS 200

/*
    Opens the device
*/
int initialize_sound(snd_pcm_t **capture_handle, snd_pcm_format_t format, unsigned int *rate, short* channels, char* hwid) {
  	snd_pcm_hw_params_t *hw_params;
  	int err;

    if ((err = snd_pcm_open (capture_handle, hwid, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        fprintf (stderr, "cannot open audio device (%s)\n",
                snd_strerror (err));
        return 1;
    }

    fprintf(stdout, "audio interface opened\n");
            
    if ((err = snd_pcm_hw_params_malloc (&hw_params)) < 0) {
        fprintf (stderr, "cannot allocate hardware parameter structure (%s)\n",
                snd_strerror (err));
        return 1;
    }

    fprintf(stdout, "hw_params allocated\n");
                    
    if ((err = snd_pcm_hw_params_any (*capture_handle, hw_params)) < 0) {
        fprintf (stderr, "cannot initialize hardware parameter structure (%s)\n",
                snd_strerror (err));
        return 1;
    }

    fprintf(stdout, "hw_params initialized\n");
        
    if ((err = snd_pcm_hw_params_set_access (*capture_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        fprintf (stderr, "cannot set access type (%s)\n",
                snd_strerror (err));
        return 1;
    }

    fprintf(stdout, "hw_params access setted\n");
        
    if ((err = snd_pcm_hw_params_set_format (*capture_handle, hw_params, format)) < 0) {
        fprintf (stderr, "cannot set sample format (%s)\n",
                snd_strerror (err));
        return 1;
    }

    fprintf(stdout, "hw_params format setted\n");
        
    if ((err = snd_pcm_hw_params_set_rate_near (*capture_handle, hw_params, rate, 0)) < 0) {
        fprintf (stderr, "cannot set sample rate (%s)\n",
                snd_strerror (err));
        return 1;
    }
        
    fprintf(stdout, "hw_params rate setted\n");

    if ((err = snd_pcm_hw_params_set_channels (*capture_handle, hw_params, 1)) == 0) {
        *channels = 1;
        printf("setting channels to 1\n");
    } else {
        fprintf (stderr, "cannot set channel count to 1 (%s)\n",
                snd_strerror (err));
        
        if ((err = snd_pcm_hw_params_set_channels (*capture_handle, hw_params, 2)) == 0) {
            *channels = 2;
            printf("setting channels to 2\n");
        } else {
            fprintf (stderr, "cannot set channel count to 2 (%s)\n",
                    snd_strerror (err));
            
            return 1;
        }
    }

    fprintf(stdout, "hw_params channels setted\n");
        
    if ((err = snd_pcm_hw_params (*capture_handle, hw_params)) < 0) {
        fprintf (stderr, "cannot set parameters (%s)\n",
                snd_strerror (err));
        return 1;
    }

    fprintf(stdout, "hw_params setted\n");
        
    snd_pcm_hw_params_free (hw_params);

    fprintf(stdout, "hw_params freed\n");
        
    if ((err = snd_pcm_prepare (*capture_handle)) < 0) {
        fprintf (stderr, "cannot prepare audio interface for use (%s)\n",
                snd_strerror (err));
        return 1;
    }
    return 0;
}

/*
    Read current recording from alsa
*/
int read_sound(snd_pcm_t *capture_handle, float *buffer, int frames){
    int err;
    if ((err = snd_pcm_readi (capture_handle, buffer, frames)) != frames) {
        fprintf (stderr, "read from audio interface %d failed (%s)\n", err, snd_strerror (err));
        return 1;
    }
    return 0;
}

/*
    Reads the info which is determined by running the code with "-i"
*/
int read_hardware_info(float* offset, float* noise_level){
    FILE *ptr;
    if(!(ptr = fopen("hardware.info","r")))
        return 1;  //Hardware Info hasn't been determined

    printf("hardware.info opened\n");

    if(fscanf(ptr,"%f;%f",offset, noise_level) == EOF){
        return 2;
    }

    fclose(ptr);
    return 0;
}

/*
    Tests to see if some slight form of constant voltage is at the port (mitigates some noise)
*/
int find_soundcard_parameters(float* offset, snd_pcm_t* capture_handle, float* alsa_buffer, int frames){

    printf("running test...\n");

    //Calculate average Output while (hopefully) nothing is playing
    double x = 0;
    for(int i = 0;i<TESTRUNS;i++){
        if(read_sound(capture_handle, alsa_buffer, frames) != 0){
            printf("Sound read error");
            return 1;
        }
        for(int j = 0;j<frames;j++){
            x += alsa_buffer[j];
        }
    }
    *offset = x/(frames*TESTRUNS);
    printf("First test finished, offset is %f\n", *offset);
    return 0;
}