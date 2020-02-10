#include <alsa/asoundlib.h>

int initialize_sound(snd_pcm_t **capture_handle, snd_pcm_format_t format, unsigned int rate) {
  	snd_pcm_hw_params_t *hw_params;
  	int err;

    if ((err = snd_pcm_open (capture_handle, "hw:1,0", SND_PCM_STREAM_CAPTURE, 0)) < 0) {
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
        
    if ((err = snd_pcm_hw_params_set_rate_near (*capture_handle, hw_params, &rate, 0)) < 0) {
        fprintf (stderr, "cannot set sample rate (%s)\n",
                snd_strerror (err));
        return 1;
    }
        
    fprintf(stdout, "hw_params rate setted\n");

    if ((err = snd_pcm_hw_params_set_channels (*capture_handle, hw_params, 2)) < 0) {
        fprintf (stderr, "cannot set channel count (%s)\n",
                snd_strerror (err));
        return 1;
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