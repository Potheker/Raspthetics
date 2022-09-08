int initialize_sound(snd_pcm_t **capture_handle, snd_pcm_format_t format, unsigned int* rate, short* channels, char* hwid);
int read_sound(snd_pcm_t *capture_handle, float* buffer, int frames);
int read_hardware_info(float* offset, float* noise_level);
int find_soundcard_parameters(float* offset, snd_pcm_t* capture_handle, float* alsa_buffer, int frames);
//int write_noise_level(float noise_level);