#include <alsa/asoundlib.h>
#include <stdio.h>

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

    fprintf(stdout, "audio interface prepared\n");
    return 0;
}

int read_sound(snd_pcm_t *capture_handle, int *buffer, int frames){
    int err;
    if ((err = snd_pcm_readi (capture_handle, buffer, frames)) != frames) {
        fprintf (stderr, "read from audio interface %d failed (%s)\n", err, snd_strerror (err));
        return 1;
    }
    return 0;
}

int read_hardware_file(char* hwid, float *offset){
    FILE *ptr;
    if(!(ptr = fopen("hardware","r")))
        return 1;  //Hardware Info hasn't been determined

    //Find out filesize
    fseek(ptr,0, SEEK_END);
    int fsize = ftell(ptr);
    fseek(ptr, 0, SEEK_SET);
 
    //Read the file
    char *read = malloc(fsize + 1);
    fread(read, 1, fsize, ptr);
    fclose(ptr);

    //Seach whether a line with our hwid exists
    for(int i = 0;i<(fsize-6);i++){
        if(strncmp(&read[i],hwid,6) == 0){
            //We found something, read it!
            sscanf(&read[i+9],"%f",offset);
        }
    }

}

int find_soundcard_parameters(char* hwid){
    //Initialize Sound
    snd_pcm_t *capture_handle;
    unsigned int rate = 44100;
    short channels;
    initialize_sound(&capture_handle, SND_PCM_FORMAT_S16_LE, &rate, &channels, hwid);
    int* alsa_buffer = malloc(4096 * snd_pcm_format_width(SND_PCM_FORMAT_S16_LE) / 8 * channels);

    printf("sound initialized\n");

    printf("running test...\n");

    //Calculate average Output while (hopefully) nothing is playing
    float avg = 0;
    for(int i = 0;i<100;i++){
        //Read from ALSA
        if(read_sound(capture_handle, alsa_buffer, 4096) != 0){
            printf("Sound read error");
            return 1;
        }
        
        for(int j = 0;j<4096;j++){
            avg += alsa_buffer[j];
        }
    }
    avg /= 409600;
    printf("%f\n",avg);
    printf("test finished\n");

    //Open Hardware Info file
    FILE *ptr = fopen("hardware","a+");

    printf("info file opened\n");

    //Find out filesize
    fseek(ptr,0, SEEK_END);
    int fsize = ftell(ptr);
    fseek(ptr, 0, SEEK_SET);
 
    //Read the file and get ready to make a new one (with maybe a new line and/or more numbers)
    char *read = malloc(fsize + 1);
    char *new = malloc(fsize + 30);
    fread(read, 1, fsize, ptr);
    fclose(ptr);

    //Seach whether a line with hwid already exists
    for(int i = 0;i<(fsize-6);i++){
        if(strncmp(&read[i],hwid,6) == 0){
            //We found something, now we have to copy everything before and after the value and insert the value inmid
            memcpy(new, read, i + 9);                                               //Copy the content before the new value, including the hwid stuff
            int new_string_last_position = i + 9 + sprintf(&new[i+9], "%f", avg);   //Copy the new value

            //Search for whether next lines exist that have to be copied
            for(int j = i+1;j<fsize;j++){
                if(read[j] == 'h'){
                    printf("found\n");
                    memcpy(&new[new_string_last_position], &read[j-1],fsize-j);
                    break;
                }
            }
            break;
        }
    }

    //If we haven't done anything in the for-loop, we have to copy the entire thing and append the new line
    if(new[0] != 'h'){
        memcpy(new, read, fsize);   //Copy all
        sprintf(&new[fsize], "%s - %f", hwid,avg);
    }

    //Delete and rewrite the file
    remove("hardware");
    ptr = fopen("hardware","w");
    fputs(new,ptr);

    printf("new soundcard info written\n");
    return 0;
}