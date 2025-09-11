#pragma once
#include <string>
#include <memory>
#include <chrono>
#include <atomic>
#include <signal.h>

#include "core/FFmpegResourceManager.h"
#include "audio/AudioManager.h"
#include "video/VideoManager.h"
#include "network/SyncManager.h"

class VideoPlayer {
private:
    std::string window_name;
    std::atomic<bool> is_playing{false};
    std::atomic<bool> is_paused{false};
    
    // Managers
    std::shared_ptr<FFmpegResourceManager> ffmpeg_resources;
    std::unique_ptr<AudioManager> audio_manager;
    std::unique_ptr<VideoManager> video_manager;
    std::unique_ptr<SyncManager> sync_manager;
    
    // Stream indices
    int video_stream_index;
    int audio_stream_index;
    
    // Timing
    std::chrono::high_resolution_clock::time_point start_time;
    std::atomic<double> master_pts{0.0};
    
    // Sync state
    std::atomic<bool> sync_enabled{false};
    std::atomic<bool> is_sync_master{false};
    std::string config_file_path;
    
public:
    VideoPlayer();
    VideoPlayer(const std::string& config_file_path);
    ~VideoPlayer();
    
    bool load_video(const std::string& filename);
    bool load_video_with_config(const std::string& filename, const std::string& config_file_path);
    void play();
    void handle_key(char key);
    void emergency_cleanup();
    
    // Sync controls
    void enable_sync(bool enable = true);
    void set_sync_master(bool is_master = true);
    bool is_sync_enabled() const { return sync_enabled.load(); }
    bool is_master() const { return is_sync_master.load(); }
    
    // Network sync callbacks
    void on_network_sync(uint32_t frame_number);
    void on_network_seek(double position);
    void on_network_pause();
    void on_network_resume();
    
private:
    bool init_ffmpeg_video(const std::string& filename);
    void seek_to_percentage(double percentage, bool broadcast = true);
    void sync_audio_to_video_position(double position);
    void setup_sync_callbacks();
    
    // Static instance for signal handling
    static VideoPlayer* g_player_instance;
    friend void signal_handler(int sig);
};