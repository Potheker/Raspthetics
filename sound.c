#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <float.h>

#define TESTRUNS 50

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

int read_sound(snd_pcm_t *capture_handle, int *buffer, int frames){
    int err;
    if ((err = snd_pcm_readi (capture_handle, buffer, frames)) != frames) {
        fprintf (stderr, "read from audio interface %d failed (%s)\n", err, snd_strerror (err));
        return 1;
    }
    return 0;
}

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

 /*
    //Find out filesize
    fseek(ptr,0, SEEK_END);
    int fsize = ftell(ptr);
    fseek(ptr, 0, SEEK_SET);

 
    //Read the file
    char *read = malloc(fsize + 1);
    fread(read, 1, fsize, ptr);
    fclose(ptr);

    printf("hardware.info read\n");



    //Seach whether a line with our hwid exists
    for(int i = 0;i<(fsize-6);i++){
        if(strncmp(&read[i],hwid,6) == 0){
            printf("hwid found in file\n");
            //We found something, read it!
            sscanf(&read[i+9],"%f;%f",offset,noise_level);
            break;
        }
    }
    return 0;
    */
}

int find_soundcard_offset(float* offset, snd_pcm_t* capture_handle, int* alsa_buffer, int frames){

    printf("running offset test...\n");

    //Calculate average Output while (hopefully) nothing is playing
    *offset = 0;
    for(int i = 0;i<TESTRUNS;i++){
        //Read from ALSA
        if(read_sound(capture_handle, alsa_buffer, frames) != 0){
            printf("Sound read error");
            return 1;
        }
        for(int j = 0;j<frames;j++){
            *offset += alsa_buffer[j];
        }
    }
    *offset /= frames*TESTRUNS;
    printf("test finished, offset is %f\n", *offset);
    return 0;

    /*
    
    char* new;

    //Open Hardware Info file and edit it if it exists
    if(0 == access("hardware.info", 0)){
        FILE* ptr = fopen("hardware.info","r");
        printf("info file opened\n");

        //Find out filesize
        fseek(ptr,0, SEEK_END);
        int fsize = ftell(ptr);
        fseek(ptr, 0, SEEK_SET);
 
        //Read the file and get ready to make a new one (with maybe a new line and/or more numbers)
        char *read = malloc(fsize*sizeof(char));
        printf("memory for reading allocated\n");
        fread(read, sizeof(char), fsize, ptr);
        printf("info file read\n");
        //fclose(ptr);
        printf("info file closed\n");
        remove("hardware.info");
        new = malloc((fsize + 50)*sizeof(char)); //Allocate Memory for a new hardware file

        printf("searching for hwid\n");
        //Search whether a line with hwid already exists
        for(int i = 0;i<(fsize-6);i++){
            if(strncmp(&read[i],hwid,6) == 0){
                printf("hwid found!\n");
                //We found something, now we have to copy everything before and after the value and insert the value inmid
                memcpy(new, read, i + 9);                                               //Copy the content before the new value, including the hwid stuff
                int new_string_last_position = i + 9 + sprintf(&new[i+9], "%f;0", avg); //Copy the new value and a dummy 0 for the noise_level so our file isn't corrupted

                //Search for the rest to be copied
                for(int j = i+1;j<fsize;j++){
                    if(read[j] == ';'){
                        memcpy(&new[new_string_last_position], &read[j-1],fsize-j);
                        break;
                    }
                }
                break;
            }
        }

        //If we haven't done anything in the for-loop (no line with hwid exists), we have to copy the entire thing and append the new line
        if(new[0] != 'h'){
            printf("writing new hwid line after %d chars\n", fsize);
            memcpy(new, read, fsize);       //Copy all
            sprintf(&new[fsize], "%s - %f;0", hwid,avg);
        }
    }
    else {  //Else we have to make a new one
        new = malloc(30*sizeof(char));     //Allocate Memory for a new hardware file
        sprintf(new, "%s - %f;0", hwid,avg);
    }
    printf("new file structure generated\n");

    //(Re)write the file
    FILE* ptr;
    if(!(ptr = fopen("hardware.info","w"))){
        printf("Error generating file!\n");
        return 1;
    }
    printf("new file created\n");
    fputs(new,ptr);
    printf("offset written\n");

    //fclose(ptr);


    *offset = avg;

    */
    return 0;
}
/*
int write_noise_level(char* hwid, float noise_level){
    
    //Open Hardware Info file
    FILE *ptr = fopen("hardware.info","a+");

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

    //Search whether a line with hwid already exists
    for(int i = 0;i<(fsize-6);i++){
        if(strncmp(&read[i],hwid,6) == 0){
            //We found something, now he have to search for the start of the second number
            int pos = -1;
            //Search for the number seperator
            for(int j = i;j<(fsize-6);j++){
                if(read[j] == ';'){
                    pos = j;
                    break;
                }
            }
            if(pos == -1)
                return 2; //Error corrupted hardware.info

            memcpy(new, read, i + 1);                                               //Copy the content before the new value, including the hwid stuff
            int new_string_last_position = i + 1 + sprintf(&new[i+9], "%f", noise_level); //Copy the new value

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

    //If we haven't done anything in the for-loop (no line with hwid exists), then our file is corrupted (because write_noise_level should've already been executed)
    if(new[0] != 'h'){
        return 2;
    }

    //Delete and rewrite the file
    remove("hardware.info");
    ptr = fopen("hardware.info","w");
    fputs(new,ptr);

    printf("noise level written\n");

    return 0;
}
*/