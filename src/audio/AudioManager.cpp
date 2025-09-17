#include "audio/AudioManager.h"
#include <iostream>
#include <iomanip>
#include <algorithm>

// SDL Audio callback function
void audio_callback_wrapper(void* userdata, Uint8* stream, int len) {
    AudioManager* manager = static_cast<AudioManager*>(userdata);
    
    // Clear the stream buffer
    memset(stream, 0, len);
    
    if (!manager || manager->is_muted() || !manager->get_buffer()) {
        return;
    }
    
    // Read audio data from circular buffer
    size_t bytes_read = manager->get_buffer()->read(stream, len);
    
    // Update audio clock based on how much data we consumed
    if (bytes_read > 0 && manager->get_clock()) {
        double samples_played = (double)bytes_read / 4; // 2 channels, 2 bytes per sample
        double time_played = samples_played / manager->audio_sample_rate;
        
        double current_audio_time = manager->get_clock()->get();
        manager->get_clock()->set(current_audio_time + time_played);
    }
}

AudioManager::AudioManager() 
    : audio_buffer(std::make_unique<CircularAudioBuffer>(AUDIO_BUFFER_SIZE)),
      audio_clock(std::make_unique<AudioClock>()),
      audio_device(0), audio_initialized(false), audio_muted(false),
      audio_sample_rate(44100), audio_channels(2), audio_stream_index(-1),
      audio_time_base(0.0) {
}

AudioManager::~AudioManager() {
    stop_audio_thread();
    
    if (audio_initialized && audio_device) {
        SDL_CloseAudioDevice(audio_device);
    }
}

bool AudioManager::initialize(std::shared_ptr<FFmpegResourceManager> resources, int stream_index) {
    ffmpeg_resources = resources;
    audio_stream_index = stream_index;
    
    if (stream_index == -1) return false;
    
    // Get audio codec
    AVCodecParameters* audio_codecpar = ffmpeg_resources->format_ctx->streams[stream_index]->codecpar;
    const AVCodec* audio_codec = avcodec_find_decoder(audio_codecpar->codec_id);
    
    if (!audio_codec) return false;
    
    // Create codec context
    ffmpeg_resources->audio_codec_ctx = avcodec_alloc_context3(audio_codec);
    if (!ffmpeg_resources->audio_codec_ctx) return false;
    
    // Copy codec parameters and open codec
    if (avcodec_parameters_to_context(ffmpeg_resources->audio_codec_ctx, audio_codecpar) < 0 ||
        avcodec_open2(ffmpeg_resources->audio_codec_ctx, audio_codec, nullptr) < 0) {
        return false;
    }
    
    // Set audio parameters from codec
    audio_sample_rate = ffmpeg_resources->audio_codec_ctx->sample_rate;
    audio_channels = ffmpeg_resources->audio_codec_ctx->ch_layout.nb_channels;
    
    // Setup resampler
    AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    if (swr_alloc_set_opts2(&ffmpeg_resources->swr_ctx,
        &out_ch_layout, AV_SAMPLE_FMT_S16, audio_sample_rate,
        &ffmpeg_resources->audio_codec_ctx->ch_layout, 
        ffmpeg_resources->audio_codec_ctx->sample_fmt, 
        ffmpeg_resources->audio_codec_ctx->sample_rate,
        0, nullptr) < 0 || swr_init(ffmpeg_resources->swr_ctx) < 0) {
        return false;
    }
    
    // Setup SDL Audio
    SDL_AudioSpec wanted_spec;
    SDL_zero(wanted_spec);
    wanted_spec.freq = audio_sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = 2;
    wanted_spec.samples = 1024;
    wanted_spec.callback = audio_callback_wrapper;
    wanted_spec.userdata = this;
    
    audio_device = SDL_OpenAudioDevice(nullptr, 0, &wanted_spec, &audio_spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    
    if (audio_device == 0) {
        std::cout << "Failed to open audio device: " << SDL_GetError() << std::endl;
        return false;
    }
    
    audio_time_base = av_q2d(ffmpeg_resources->format_ctx->streams[stream_index]->time_base);
    audio_initialized = true;
    
    std::cout << "SDL Audio initialized: " << audio_spec.freq << "Hz, " 
              << (int)audio_spec.channels << " channels" << std::endl;
    
    return true;
}

void AudioManager::cache_audio_frames() {
    if (!audio_initialized) return;
    
    std::cout << "Caching audio frames..." << std::endl;
    
    // Reset to beginning for audio decoding
    int seek_ret = av_seek_frame(ffmpeg_resources->format_ctx, -1, 0, AVSEEK_FLAG_BACKWARD);
    if (seek_ret < 0) {
        std::cout << "Warning: Could not seek to beginning for audio caching" << std::endl;
    }
    
    AVPacket packet;
    AVFrame* frame = av_frame_alloc();
    
    if (!frame) {
        std::cout << "Failed to allocate audio frame for caching" << std::endl;
        return;
    }
    
    int frame_count = 0;
    
    while (av_read_frame(ffmpeg_resources->format_ctx, &packet) >= 0) {
        if (packet.stream_index == audio_stream_index) {
            int ret = avcodec_send_packet(ffmpeg_resources->audio_codec_ctx, &packet);
            if (ret >= 0) {
                while ((ret = avcodec_receive_frame(ffmpeg_resources->audio_codec_ctx, frame)) >= 0) {
                    int out_samples = swr_get_out_samples(ffmpeg_resources->swr_ctx, frame->nb_samples);
                    uint8_t* audio_buf = (uint8_t*)av_malloc(out_samples * 2 * 2);
                    
                    if (audio_buf) {
                        uint8_t* out[] = {audio_buf};
                        int converted = swr_convert(ffmpeg_resources->swr_ctx, out, out_samples, 
                                                   (const uint8_t**)frame->data, frame->nb_samples);
                        
                        if (converted > 0) {
                            double pts = frame->pts != AV_NOPTS_VALUE ? frame->pts * audio_time_base : 0.0;
                            int64_t duration = frame->nb_samples;
                            
                            std::lock_guard<std::mutex> lock(audio_frame_mutex);
                            audio_frame_cache.push_back(
                                std::make_unique<AudioFrame>(audio_buf, converted * 2 * 2, pts, duration));
                            frame_count++;
                        } else {
                            av_free(audio_buf);
                        }
                    }
                    
                    av_frame_unref(frame);
                }
            }
        }
        av_packet_unref(&packet);
    }
    
    av_frame_free(&frame);
    
    std::cout << "Audio frames cached: " << frame_count << " frames" << std::endl;
    
    // Reset stream position again for video caching
    av_seek_frame(ffmpeg_resources->format_ctx, -1, 0, AVSEEK_FLAG_BACKWARD);
}

void AudioManager::start_audio_thread() {
    if (audio_thread_running.load()) return;
    
    audio_thread_running.store(true);
    audio_should_stop.store(false);
    
    audio_thread = std::thread(&AudioManager::audio_thread_func, this);
    std::cout << "Audio thread started" << std::endl;
}

void AudioManager::stop_audio_thread() {
    if (!audio_thread_running.load()) return;
    
    audio_should_stop.store(true);
    audio_thread_running.store(false);
    audio_cv.notify_all();
    
    if (audio_thread.joinable()) {
        audio_thread.join();
    }
}

void AudioManager::audio_thread_func() {
    while (audio_thread_running.load() && !audio_should_stop.load()) {
        if (seek_requested.load()) {
            handle_audio_seek();
            seek_requested.store(false);
        }
        
        fill_audio_buffer();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    
    std::cout << "Audio thread stopped" << std::endl;
}

void AudioManager::handle_audio_seek() {
    double target = seek_target.load();
    
    audio_buffer->clear();
    
    std::lock_guard<std::mutex> lock(audio_frame_mutex);
    audio_cache_index = 0;
    
    for (size_t i = 0; i < audio_frame_cache.size(); ++i) {
        if (audio_frame_cache[i]->pts >= target - 0.05) {
            audio_cache_index = i;
            break;
        }
    }
    
    std::cout << "Audio seek to: " << std::fixed << std::setprecision(2) 
              << target << "s (frame " << audio_cache_index << ")" << std::endl;
}

void AudioManager::fill_audio_buffer() {
    double video_time = current_video_time.load();
    const size_t MIN_BUFFER_SIZE = AUDIO_BUFFER_SIZE / 4;
    
    if (audio_buffer->available_read() > MIN_BUFFER_SIZE) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(audio_frame_mutex);
    
    while (audio_cache_index < audio_frame_cache.size() && 
           audio_buffer->available_write() > 4096) {
        
        auto& frame = audio_frame_cache[audio_cache_index];
        double audio_time = frame->pts;
        
        if (audio_time > video_time + AUDIO_SYNC_THRESHOLD) {
            break;
        }
        
        size_t written = audio_buffer->write(frame->data.get(), frame->size);
        if (written > 0) {
            audio_cache_index++;
        } else {
            break;
        }
    }
}

void AudioManager::sync_to_position(double position) {
    if (!audio_initialized || !audio_thread_running.load()) return;
    
    {
        std::lock_guard<std::mutex> lock(audio_sync_mutex);
        seek_target.store(position);
        seek_requested.store(true);
    }
    audio_cv.notify_all();
    
    audio_clock->set(position);
    
    std::cout << "Audio seek requested: " << std::fixed 
              << std::setprecision(2) << position << "s" << std::endl;
}

void AudioManager::start_playback() {
    if (audio_initialized && !audio_muted && audio_thread_running.load()) {
        SDL_PauseAudioDevice(audio_device, 0);
        std::cout << "Audio playback started" << std::endl;
    }
}

void AudioManager::pause_playback() {
    if (audio_initialized) {
        SDL_PauseAudioDevice(audio_device, 1);
        std::cout << "Audio playback paused" << std::endl;
    }
}

void AudioManager::toggle_mute() {
    if (!audio_initialized) return;
    
    audio_muted = !audio_muted;
    if (audio_muted) {
        SDL_PauseAudioDevice(audio_device, 1);
    } else if (audio_thread_running.load()) {
        SDL_PauseAudioDevice(audio_device, 0);
    }
    
    std::cout << (audio_muted ? "Audio muted" : "Audio unmuted") << std::endl;
}

double AudioManager::get_audio_clock() const {
    return audio_clock ? audio_clock->get() : 0.0;
}

size_t AudioManager::get_buffer_size() const {
    return audio_buffer ? audio_buffer->available_read() : 0;
}