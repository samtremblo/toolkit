#pragma once
#include <opencv2/opencv.hpp>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>

#include "core/Frame.h"
#include "core/FFmpegResourceManager.h"

class VideoManager {
private:
    // Video caching
    std::vector<std::unique_ptr<VideoFrame>> video_cache;
    std::atomic<bool> cache_loaded{false};
    std::thread cache_thread;
    mutable std::mutex cache_mutex;
    
    // Video properties
    double fps;
    int total_frames;
    std::atomic<int> current_frame{0};
    double frame_time_ms;
    
    // FFmpeg resources
    std::shared_ptr<FFmpegResourceManager> ffmpeg_resources;
    int video_stream_index;
    double video_time_base;
    
    // Timing
    std::atomic<double> video_clock{0.0};
    
public:
    VideoManager();
    ~VideoManager();
    
    bool initialize(std::shared_ptr<FFmpegResourceManager> resources, int stream_index);
    void start_video_cache();
    void wait_for_cache();
    
    cv::Mat get_current_frame();
    void set_current_frame(int frame);
    void seek_to_percentage(double percentage);
    
    double get_fps() const { return fps; }
    int get_total_frames() const { return total_frames; }
    int get_current_frame_number() const { return current_frame.load(); }
    bool is_cache_loaded() const { return cache_loaded.load(); }
    double get_video_clock() const { return video_clock.load(); }
    void set_video_clock(double pts) { video_clock.store(pts); }
    double get_frame_time_ms() const { return frame_time_ms; }
    
    size_t get_cache_size() const;
    
private:
    void cache_video_frames();
};