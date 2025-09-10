#pragma once
#include <string>
#include <memory>
#include <chrono>
#include <atomic>
#include <signal.h>

#include "core/FFmpegResourceManager.h"
#include "audio/AudioManager.h"
#include "video/VideoManager.h"

class VideoPlayer {
private:
    std::string window_name;
    std::atomic<bool> is_playing{false};
    std::atomic<bool> is_paused{false};
    
    // Managers
    std::shared_ptr<FFmpegResourceManager> ffmpeg_resources;
    std::unique_ptr<AudioManager> audio_manager;
    std::unique_ptr<VideoManager> video_manager;
    
    // Stream indices
    int video_stream_index;
    int audio_stream_index;
    
    // Timing
    std::chrono::high_resolution_clock::time_point start_time;
    std::atomic<double> master_pts{0.0};
    
public:
    VideoPlayer();
    ~VideoPlayer();
    
    bool load_video(const std::string& filename);
    void play();
    void handle_key(char key);
    void emergency_cleanup();
    
private:
    bool init_ffmpeg_video(const std::string& filename);
    void seek_to_percentage(double percentage);
    void sync_audio_to_video_position(double position);
    
    // Static instance for signal handling
    static VideoPlayer* g_player_instance;
    friend void signal_handler(int sig);
};