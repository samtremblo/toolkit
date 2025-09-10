#include "core/VideoPlayer.h"
#include <iostream>
#include <iomanip>
#include <thread>
#include <opencv2/opencv.hpp>
#include <SDL2/SDL.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

// Global instance for signal handling
VideoPlayer* VideoPlayer::g_player_instance = nullptr;

void signal_handler(int sig) {
    std::cout << "\nCrash detected (signal " << sig << "). Cleaning up..." << std::endl;
    
    if (VideoPlayer::g_player_instance) {
        try {
            VideoPlayer::g_player_instance->emergency_cleanup();
        } catch (...) {
            std::cout << "Emergency cleanup failed" << std::endl;
        }
    }
    
    std::cout << "Cleanup complete. Exiting safely." << std::endl;
    exit(sig);
}

VideoPlayer::VideoPlayer() 
    : window_name("Threaded Audio Video Player"),
      ffmpeg_resources(std::make_shared<FFmpegResourceManager>()),
      audio_manager(std::make_unique<AudioManager>()),
      video_manager(std::make_unique<VideoManager>()),
      video_stream_index(-1), audio_stream_index(-1) {
    
    // Register signal handlers
    signal(SIGSEGV, signal_handler);
    signal(SIGABRT, signal_handler);
    signal(SIGFPE, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    g_player_instance = this;
    
    // Initialize SDL Audio
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        std::cout << "SDL could not initialize! SDL Error: " << SDL_GetError() << std::endl;
    }
    
    std::cout << "Crash protection enabled" << std::endl;
}

VideoPlayer::~VideoPlayer() {
    audio_manager.reset();
    video_manager.reset();
    ffmpeg_resources.reset();
    SDL_Quit();
    g_player_instance = nullptr;
}

void VideoPlayer::emergency_cleanup() {
    std::cout << "Performing emergency cleanup..." << std::endl;
    try {
        is_playing.store(false);
        
        try {
            cv::destroyAllWindows();
        } catch (...) {}
        
        if (ffmpeg_resources) {
            ffmpeg_resources->cleanup();
        }
    } catch (const std::exception& e) {
        std::cout << "Exception during emergency cleanup: " << e.what() << std::endl;
    } catch (...) {
        std::cout << "Unknown exception during emergency cleanup" << std::endl;
    }
    std::cout << "Emergency cleanup completed" << std::endl;
}

bool VideoPlayer::load_video(const std::string& filename) {
    if (!init_ffmpeg_video(filename)) {
        std::cerr << "Error: Could not initialize FFmpeg for video file " << filename << std::endl;
        return false;
    }
    
    cv::namedWindow(window_name, cv::WINDOW_AUTOSIZE);
    
    // Initialize video manager
    if (!video_manager->initialize(ffmpeg_resources, video_stream_index)) {
        std::cerr << "Error: Could not initialize video manager" << std::endl;
        return false;
    }
    
    // Initialize audio manager if audio is available
    if (audio_stream_index != -1) {
        if (audio_manager->initialize(ffmpeg_resources, audio_stream_index)) {
            audio_manager->cache_audio_frames();
        }
    }
    
    // Start video caching
    video_manager->start_video_cache();
    
    // Start audio thread if audio is available
    if (audio_manager->is_initialized()) {
        audio_manager->start_audio_thread();
    }
    
    std::cout << "Video player initialized successfully. Starting playback..." << std::endl;
    return true;
}

bool VideoPlayer::init_ffmpeg_video(const std::string& filename) {
    // Open the video file
    if (avformat_open_input(&ffmpeg_resources->format_ctx, filename.c_str(), nullptr, nullptr) != 0) {
        std::cout << "Could not open video file" << std::endl;
        return false;
    }
    
    // Find stream information
    if (avformat_find_stream_info(ffmpeg_resources->format_ctx, nullptr) < 0) {
        std::cout << "Could not find stream information" << std::endl;
        return false;
    }
    
    // Find video and audio streams
    for (unsigned int i = 0; i < ffmpeg_resources->format_ctx->nb_streams; i++) {
        if (ffmpeg_resources->format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && 
            video_stream_index == -1) {
            video_stream_index = i;
        } else if (ffmpeg_resources->format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && 
                   audio_stream_index == -1) {
            audio_stream_index = i;
        }
    }
    
    if (video_stream_index == -1) {
        std::cout << "No video stream found" << std::endl;
        return false;
    }
    
    // Set FFmpeg log level to reduce spam
    av_log_set_level(AV_LOG_ERROR);
    
    return true;
}

void VideoPlayer::sync_audio_to_video_position(double position) {
    if (audio_manager->is_initialized()) {
        audio_manager->sync_to_position(position);
        master_pts.store(position);
    }
}

void VideoPlayer::seek_to_percentage(double percentage) {
    video_manager->seek_to_percentage(percentage);
    
    if (audio_manager->is_initialized()) {
        double seek_position = video_manager->get_video_clock();
        sync_audio_to_video_position(seek_position);
    }
}

void VideoPlayer::handle_key(char key) {
    switch (key) {
        case 27: // ESC
        case 'q':
            is_playing.store(false);
            break;
        case ' ': // Space
            is_paused.store(!is_paused.load());
            if (is_paused.load()) {
                audio_manager->pause_playback();
            } else {
                audio_manager->start_playback();
            }
            std::cout << (is_paused.load() ? "Paused" : "Playing") << std::endl;
            break;
        case 'm': // Mute audio
            audio_manager->toggle_mute();
            break;
        case '0': seek_to_percentage(0.0); break;
        case '1': seek_to_percentage(10.0); break;
        case '2': seek_to_percentage(20.0); break;
        case '3': seek_to_percentage(30.0); break;
        case '4': seek_to_percentage(40.0); break;
        case '5': seek_to_percentage(50.0); break;
        case '6': seek_to_percentage(60.0); break;
        case '7': seek_to_percentage(70.0); break;
        case '8': seek_to_percentage(80.0); break;
        case '9': seek_to_percentage(90.0); break;
    }
}

void VideoPlayer::play() {
    std::cout << "Starting cached video playback..." << std::endl;
    is_playing.store(true);
    
    // Wait for video cache to load
    std::cout << "Waiting for video cache to load..." << std::endl;
    while (!video_manager->is_cache_loaded() && is_playing.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        char key = cv::waitKey(1);
        if (key != -1) {
            handle_key(key);
        }
    }
    
    if (!video_manager->is_cache_loaded()) {
        std::cout << "Video cache not loaded, exiting" << std::endl;
        return;
    }
    
    std::cout << "Video cache loaded, starting playback" << std::endl;
    
    // Start audio playback if available
    if (audio_manager->is_initialized() && audio_manager->is_running()) {
        sync_audio_to_video_position(0.0);
        audio_manager->start_playback();
    }
    
    // Calculate frame timing
    double fps = video_manager->get_fps();
    int frame_time_us = (int)(1000000.0 / fps);
    
    // Main playback loop
    while (is_playing.load()) {
        auto frame_start = std::chrono::high_resolution_clock::now();
        
        if (!is_paused.load()) {
            cv::Mat display_frame = video_manager->get_current_frame();
            
            if (!display_frame.empty()) {
                // Update audio manager with current video time
                double current_pts = video_manager->get_video_clock();
                if (audio_manager->is_initialized()) {
                    audio_manager->set_video_time(current_pts);
                }
                
                // Add overlay information
                cv::putText(display_frame, "Threaded Audio Video Player - Press 0-9 to seek, Q to quit, Space to pause, M to mute", 
                           cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.6, 
                           cv::Scalar(0, 255, 0), 2);
                
                char frame_info[256];
                int current_frame_num = video_manager->get_current_frame_number();
                size_t cache_size = video_manager->get_cache_size();
                
                snprintf(frame_info, sizeof(frame_info), 
                        "Frame %d/%zu (%.1f%%) | Video: %.2fs | Audio: %s | Buffer: %zu bytes", 
                        current_frame_num + 1, cache_size,
                        (current_frame_num + 1) * 100.0 / cache_size,
                        current_pts,
                        audio_manager->is_initialized() ? 
                            (audio_manager->is_muted() ? "MUTED" : 
                             (audio_manager->is_running() ? "THREADED" : "OFF")) : "N/A",
                        audio_manager->get_buffer_size());
                
                cv::putText(display_frame, frame_info, cv::Point(10, 60), 
                           cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);
                
                cv::imshow(window_name, display_frame);
                
                // Advance to next frame
                int next_frame = current_frame_num + 1;
                if (next_frame >= (int)cache_size) {
                    next_frame = 0;
                    if (audio_manager->is_initialized() && audio_manager->is_running()) {
                        sync_audio_to_video_position(0.0);
                    }
                }
                video_manager->set_current_frame(next_frame);
            }
        }
        
        // Handle keyboard input
        char key = cv::waitKey(1);
        if (key != -1) {
            handle_key(key);
        }
        
        // Frame timing
        if (!is_paused.load()) {
            auto frame_end = std::chrono::high_resolution_clock::now();
            auto frame_duration = std::chrono::duration_cast<std::chrono::microseconds>(frame_end - frame_start).count();
            
            int sleep_time = frame_time_us - (int)frame_duration;
            if (sleep_time > 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(sleep_time));
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    
    cv::destroyAllWindows();
    std::cout << "Cached playback finished" << std::endl;
}