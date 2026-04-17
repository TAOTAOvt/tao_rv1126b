#ifndef __AUDIO_UTILS_H__
#define __AUDIO_UTILS_H__

/**< @brief 音频信息 */
typedef struct
{
	float *data;
	int num_frames;
	int num_channels;
	int sample_rate;
} audio_buffer_t;


#ifdef __cplusplus
extern "C"
{
#endif

	/// 将音频文件读入缓冲区。
	int read_audio(const char *path, audio_buffer_t *audio);

	/// 将音频数据保存到WAV文件。
	int save_audio(const char *path, float *data, int num_frames, int sample_rate, int num_channels);

	/// 将音频数据重新采样到所需的采样率。
	int resample_audio(audio_buffer_t *audio, int original_sample_rate, int desired_sample_rate);

	/// 将音频数据转换为单声道（单声道）。
	int convert_channels(audio_buffer_t *audio);

#ifdef __cplusplus
} 
#endif

#endif // __AUDIO_UTILS_H__