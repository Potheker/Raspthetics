int initialize_sound(snd_pcm_t **capture_handle, snd_pcm_format_t format, unsigned int rate);
int read_sound(snd_pcm_t *capture_handle, int *buffer, int frames);