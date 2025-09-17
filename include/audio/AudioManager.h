#pragma once
#include <SDL2/SDL.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <memory>

#include "core/Frame.h"
#include "core/FFmpegResourceManager.h"
#include "utils/CircularBuffer.h"
#include "audio/AudioClock.h"

class AudioManager {
private:
    // Audio threading and synchronization
    std::unique_ptr<CircularAudioBuffer> audio_buffer;
    std::thread audio_thread;
    std::atomic<bool> audio_thread_running{false};
    std::atomic<bool> audio_should_stop{false};
    std::mutex audio_sync_mutex;
    std::condition_variable audio_cv;
    
    // Audio timing
    std::unique_ptr<AudioClock> audio_clock;
    std::atomic<bool> seek_requested{false};
    std::atomic<double> seek_target{0.0};
    std::atomic<double> current_video_time{0.0};
    
    // SDL Audio
    SDL_AudioDeviceID audio_device;
    SDL_AudioSpec audio_spec;
    bool audio_initialized;
    bool audio_muted;
    int audio_sample_rate;
    int audio_channels;
    
    // Audio frame cache
    std::deque<std::unique_ptr<AudioFrame>> audio_frame_cache;
    std::mutex audio_frame_mutex;
    size_t audio_cache_index{0};
    
    // FFmpeg resources reference
    std::shared_ptr<FFmpegResourceManager> ffmpeg_resources;
    int audio_stream_index;
    double audio_time_base;
    
    // Audio synchronization constants
    static constexpr double AUDIO_SYNC_THRESHOLD = 0.040;
    static constexpr double AUDIO_RESYNC_THRESHOLD = 1.0;
    static constexpr int AUDIO_BUFFER_SIZE = 192000;
    
public:
    AudioManager();
    ~AudioManager();
    
    bool initialize(std::shared_ptr<FFmpegResourceManager> resources, int stream_index);
    void cache_audio_frames();
    void start_audio_thread();
    void stop_audio_thread();
    
    void sync_to_position(double position);
    void start_playback();
    void pause_playback();
    void toggle_mute();
    
    double get_audio_clock() const;
    bool is_initialized() const { return audio_initialized; }
    bool is_muted() const { return audio_muted; }
    bool is_running() const { return audio_thread_running.load(); }
    size_t get_buffer_size() const;
    void set_video_time(double time) { current_video_time.store(time); }
    
    // SDL callback access
    CircularAudioBuffer* get_buffer() { return audio_buffer.get(); }
    AudioClock* get_clock() { return audio_clock.get(); }
    
private:
    void audio_thread_func();
    void handle_audio_seek();
    void fill_audio_buffer();
    
    friend void audio_callback_wrapper(void* userdata, Uint8* stream, int len);
};