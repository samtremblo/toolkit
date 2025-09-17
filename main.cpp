#include <opencv2/opencv.hpp>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <cmath>
#include <iomanip>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <algorithm>
#include <signal.h>
#include <memory>
#include <exception>
#include <stdexcept>
#include <atomic>
#include <deque>
#include <SDL2/SDL.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

struct AudioFrame {
    std::unique_ptr<uint8_t[]> data;
    int size;
    double pts;
    int64_t duration;
    AudioFrame(uint8_t* d, int s, double p, int64_t dur = 0) 
        : data(std::unique_ptr<uint8_t[]>(d)), size(s), pts(p), duration(dur) {}
};

struct VideoFrame {
    cv::Mat frame;
    double pts;
    VideoFrame(const cv::Mat& f, double p) : frame(f.clone()), pts(p) {}
};

// RAII wrapper for FFmpeg resources
class FFmpegResourceManager {
public:
    AVFormatContext* format_ctx = nullptr;
    AVCodecContext* video_codec_ctx = nullptr;
    AVCodecContext* audio_codec_ctx = nullptr;
    SwsContext* sws_ctx = nullptr;
    SwrContext* swr_ctx = nullptr;
    
    ~FFmpegResourceManager() {
        cleanup();
    }
    
    void cleanup() {
        if (swr_ctx) {
            swr_free(&swr_ctx);
            swr_ctx = nullptr;
        }
        if (sws_ctx) {
            sws_freeContext(sws_ctx);
            sws_ctx = nullptr;
        }
        if (video_codec_ctx) {
            avcodec_free_context(&video_codec_ctx);
            video_codec_ctx = nullptr;
        }
        if (audio_codec_ctx) {
            avcodec_free_context(&audio_codec_ctx);
            audio_codec_ctx = nullptr;
        }
        if (format_ctx) {
            avformat_close_input(&format_ctx);
            format_ctx = nullptr;
        }
    }
};

// Forward declaration for signal handling
class VideoPlayer;
static VideoPlayer* g_player_instance = nullptr;
void signal_handler(int sig);

// Forward declaration
class VideoPlayer;

// Circular buffer for audio data
class CircularAudioBuffer {
private:
    std::vector<uint8_t> buffer;
    std::atomic<size_t> write_pos{0};
    std::atomic<size_t> read_pos{0};
    size_t capacity;
    std::mutex buffer_mutex;
    
public:
    CircularAudioBuffer(size_t size) : capacity(size) {
        buffer.resize(capacity);
    }
    
    size_t write(const uint8_t* data, size_t size) {
        std::lock_guard<std::mutex> lock(buffer_mutex);
        size_t available = available_write();
        size_t to_write = std::min(size, available);
        
        size_t w_pos = write_pos.load();
        if (w_pos + to_write <= capacity) {
            memcpy(buffer.data() + w_pos, data, to_write);
        } else {
            size_t first_chunk = capacity - w_pos;
            memcpy(buffer.data() + w_pos, data, first_chunk);
            memcpy(buffer.data(), data + first_chunk, to_write - first_chunk);
        }
        
        write_pos.store((w_pos + to_write) % capacity);
        return to_write;
    }
    
    size_t read(uint8_t* data, size_t size) {
        std::lock_guard<std::mutex> lock(buffer_mutex);
        size_t available = available_read();
        size_t to_read = std::min(size, available);
        
        size_t r_pos = read_pos.load();
        if (r_pos + to_read <= capacity) {
            memcpy(data, buffer.data() + r_pos, to_read);
        } else {
            size_t first_chunk = capacity - r_pos;
            memcpy(data, buffer.data() + r_pos, first_chunk);
            memcpy(data + first_chunk, buffer.data(), to_read - first_chunk);
        }
        
        read_pos.store((r_pos + to_read) % capacity);
        return to_read;
    }
    
    size_t available_read() const {
        size_t w = write_pos.load();
        size_t r = read_pos.load();
        return (w >= r) ? (w - r) : (capacity - r + w);
    }
    
    size_t available_write() const {
        return capacity - available_read() - 1; // Leave one byte to distinguish full/empty
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(buffer_mutex);
        write_pos.store(0);
        read_pos.store(0);
    }
};

// Audio synchronization helper
class AudioClock {
private:
    std::atomic<double> pts{0.0};
    std::chrono::high_resolution_clock::time_point base_time;
    std::mutex clock_mutex;
    
public:
    AudioClock() {
        base_time = std::chrono::high_resolution_clock::now();
    }
    
    void set(double timestamp) {
        std::lock_guard<std::mutex> lock(clock_mutex);
        pts.store(timestamp);
        base_time = std::chrono::high_resolution_clock::now();
    }
    
    double get() const {
        auto now = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(now - base_time).count();
        return pts.load() + elapsed;
    }
};

// SDL Audio callback function
void audio_callback(void* userdata, Uint8* stream, int len);

class VideoPlayer {
    // Make audio callback a friend so it can access private members
    friend void audio_callback(void* userdata, Uint8* stream, int len);
    
private:
    std::string window_name;
    bool is_playing;
    bool is_paused;
    double fps;
    int total_frames;
    int current_frame;
    
    // Timing and framerate
    std::chrono::high_resolution_clock::time_point start_time;
    std::chrono::high_resolution_clock::time_point last_frame_time;
    double actual_fps;
    double frame_time_ms;
    
    // FFmpeg resources - using RAII wrapper
    std::unique_ptr<FFmpegResourceManager> ffmpeg_resources;
    int video_stream_index;
    int audio_stream_index;
    double video_time_base;
    double audio_time_base;
    
    // Video caching
    std::vector<std::unique_ptr<VideoFrame>> video_cache;
    bool cache_loaded;
    std::thread cache_thread;
    std::mutex cache_mutex;
    
    // Audio threading and synchronization
    std::unique_ptr<CircularAudioBuffer> audio_buffer;
    std::thread audio_thread;
    std::atomic<bool> audio_thread_running{false};
    std::atomic<bool> audio_should_stop{false};
    std::mutex audio_sync_mutex;
    std::condition_variable audio_cv;
    
    // Audio timing
    std::unique_ptr<AudioClock> audio_clock;
    std::atomic<double> video_clock{0.0};
    std::atomic<double> master_pts{0.0};
    std::atomic<bool> seek_requested{false};
    std::atomic<double> seek_target{0.0};
    
    // Audio support
    bool has_audio;
    bool audio_muted;
    
    // SDL Audio
    SDL_AudioDeviceID audio_device;
    SDL_AudioSpec audio_spec;
    bool audio_initialized;
    int audio_sample_rate;
    int audio_channels;
    
    // Audio frame cache for threaded access
    std::deque<std::unique_ptr<AudioFrame>> audio_frame_cache;
    std::mutex audio_frame_mutex;
    size_t audio_cache_index{0};
    
    // Audio synchronization constants
    static constexpr double AUDIO_SYNC_THRESHOLD = 0.040; // 40ms threshold
    static constexpr double AUDIO_RESYNC_THRESHOLD = 1.0; // 1s for major resync
    static constexpr int AUDIO_BUFFER_SIZE = 192000; // ~1 second at 48kHz stereo
    
    // Performance monitoring
    std::vector<double> fps_history;
    std::vector<double> frame_time_history;
    std::vector<int> cache_size_history;
    std::vector<int> audio_queue_size_history;
    bool show_performance_graph;
    int graph_max_samples;
    
public:
    VideoPlayer() : window_name("Video Player"), is_playing(false), 
                   is_paused(false), fps(30.0), total_frames(0), current_frame(0),
                   actual_fps(0.0), frame_time_ms(0.0), 
                   ffmpeg_resources(std::make_unique<FFmpegResourceManager>()),
                   video_stream_index(-1), audio_stream_index(-1),
                   video_time_base(0.0), audio_time_base(0.0), cache_loaded(false),
                   audio_buffer(std::make_unique<CircularAudioBuffer>(AUDIO_BUFFER_SIZE)),
                   audio_clock(std::make_unique<AudioClock>()),
                   has_audio(false), audio_muted(false), audio_device(0),
                   audio_initialized(false), audio_sample_rate(44100), audio_channels(2),
                   show_performance_graph(true), graph_max_samples(100) {
        
        // Register signal handlers for crash protection
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
        
        
        std::cout << "🛡️ Crash protection enabled" << std::endl;
    }
    
    ~VideoPlayer() {
        stop_audio_thread();
        cleanup_audio();
        cleanup_video();
        SDL_Quit();
        g_player_instance = nullptr;
    }
    
    void emergency_cleanup() {
        std::cout << "🚨 Performing emergency cleanup..." << std::endl;
        try {
            cache_loaded = false;
            
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
        std::cout << "✅ Emergency cleanup completed" << std::endl;
    }
    
    // Rest of the implementation...
    // Let me continue with the essential functions
    
    bool load_video(const std::string& filename) {
        if (!init_ffmpeg_video(filename)) {
            std::cerr << "Error: Could not initialize FFmpeg for video file " << filename << std::endl;
            return false;
        }
        
        cv::namedWindow(window_name, cv::WINDOW_AUTOSIZE);
        
        // Cache audio frames first (they will be used by the audio thread)
        if (has_audio && audio_initialized) {
            cache_audio_frames();
        }
        
        // Start caching video frames
        start_video_cache();
        
        // Start audio thread after both caches are ready
        if (has_audio && audio_initialized) {
            start_audio_thread();
        }
        
        std::cout << "Video player initialized successfully. Starting playback..." << std::endl;
        return true;
    }
    
    void sync_audio_to_video_position(double video_position) {
        if (!has_audio || !audio_initialized || !audio_thread_running.load()) {
            return;
        }
        
        // Signal seek to audio thread
        {
            std::lock_guard<std::mutex> lock(audio_sync_mutex);
            seek_target.store(video_position);
            seek_requested.store(true);
        }
        audio_cv.notify_all();
        
        // Update clocks
        audio_clock->set(video_position);
        master_pts.store(video_position);
        
        std::cout << "🎵 Audio seek requested: " << std::fixed << std::setprecision(2) << video_position << "s" << std::endl;
    }
    
    void seek_to_percentage(double percentage) {
        if (percentage < 0.0) percentage = 0.0;
        if (percentage > 100.0) percentage = 100.0;
        
        if (!cache_loaded) {
            std::cout << "⏳ Seek requested but cache not loaded yet" << std::endl;
            return;
        }
        
        std::lock_guard<std::mutex> lock(cache_mutex);
        
        int max_frame = (int)video_cache.size() - 1;
        int target_frame = (int)(percentage / 100.0 * max_frame);
        
        if (target_frame >= 0 && target_frame <= max_frame) {
            current_frame = target_frame;
            double seek_position = video_cache[current_frame]->pts;
            
            std::cout << "🎯 Fast seek to " << std::fixed << std::setprecision(1) << percentage 
                      << "% (frame " << current_frame << ", PTS: " << std::setprecision(2) 
                      << seek_position << "s)" << std::endl;
            
            // Sync audio to match video position
            if (has_audio) {
                sync_audio_to_video_position(seek_position);
            }
        }
    }
    
    void handle_key(char key) {
        switch (key) {
            case 27: // ESC
            case 'q':
                is_playing = false;
                break;
            case ' ': // Space
                is_paused = !is_paused;
                if (is_paused) {
                    pause_audio_playback();
                } else {
                    start_audio_playback();
                }
                std::cout << (is_paused ? "⏸ Paused" : "▶ Playing") << std::endl;
                break;
            case 'm': // Mute audio
                toggle_audio_mute();
                break;
            case '0':
                seek_to_percentage(0.0);
                break;
            case '1':
                seek_to_percentage(10.0);
                break;
            case '2':
                seek_to_percentage(20.0);
                break;
            case '3':
                seek_to_percentage(30.0);
                break;
            case '4':
                seek_to_percentage(40.0);
                break;
            case '5':
                seek_to_percentage(50.0);
                break;
            case '6':
                seek_to_percentage(60.0);
                break;
            case '7':
                seek_to_percentage(70.0);
                break;
            case '8':
                seek_to_percentage(80.0);
                break;
            case '9':
                seek_to_percentage(90.0);
                break;
        }
    }
    
    bool init_ffmpeg_video(const std::string& filename) {
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
            if (ffmpeg_resources->format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_stream_index == -1) {
                video_stream_index = i;
            } else if (ffmpeg_resources->format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_stream_index == -1) {
                audio_stream_index = i;
            }
        }
        
        if (video_stream_index == -1) {
            std::cout << "No video stream found" << std::endl;
            return false;
        }
        
        // Setup video decoder
        AVCodecParameters* video_codecpar = ffmpeg_resources->format_ctx->streams[video_stream_index]->codecpar;
        const AVCodec* video_codec = avcodec_find_decoder(video_codecpar->codec_id);
        
        if (!video_codec) {
            std::cout << "Video codec not supported" << std::endl;
            return false;
        }
        
        ffmpeg_resources->video_codec_ctx = avcodec_alloc_context3(video_codec);
        if (!ffmpeg_resources->video_codec_ctx) {
            std::cout << "Could not allocate video codec context" << std::endl;
            return false;
        }
        
        if (avcodec_parameters_to_context(ffmpeg_resources->video_codec_ctx, video_codecpar) < 0) {
            std::cout << "Could not copy video codec parameters" << std::endl;
            return false;
        }
        
        if (avcodec_open2(ffmpeg_resources->video_codec_ctx, video_codec, nullptr) < 0) {
            std::cout << "Could not open video codec" << std::endl;
            return false;
        }
        
        // Setup scaling context for BGR conversion (for OpenCV)
        ffmpeg_resources->sws_ctx = sws_getContext(
            ffmpeg_resources->video_codec_ctx->width, ffmpeg_resources->video_codec_ctx->height, ffmpeg_resources->video_codec_ctx->pix_fmt,
            ffmpeg_resources->video_codec_ctx->width, ffmpeg_resources->video_codec_ctx->height, AV_PIX_FMT_BGR24,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );
        
        if (!ffmpeg_resources->sws_ctx) {
            std::cout << "Could not initialize scaling context" << std::endl;
            return false;
        }
        
        // Get video properties
        fps = av_q2d(ffmpeg_resources->format_ctx->streams[video_stream_index]->r_frame_rate);
        total_frames = ffmpeg_resources->format_ctx->streams[video_stream_index]->nb_frames;
        if (total_frames <= 0) {
            total_frames = (int)(ffmpeg_resources->format_ctx->duration * fps / AV_TIME_BASE);
        }
        frame_time_ms = 1000.0 / fps;
        video_time_base = av_q2d(ffmpeg_resources->format_ctx->streams[video_stream_index]->time_base);
        current_frame = 0;
        
        std::cout << "=== Video Information ===" << std::endl;
        std::cout << "Resolution: " << ffmpeg_resources->video_codec_ctx->width << "x" << ffmpeg_resources->video_codec_ctx->height << std::endl;
        std::cout << "FPS: " << fps << std::endl;
        std::cout << "Total frames: " << total_frames << std::endl;
        
        // Set FFmpeg log level to reduce spam from corrupted streams
        av_log_set_level(AV_LOG_ERROR);
        
        // Initialize audio if available
        if (audio_stream_index != -1) {
            init_audio();
        }
        
        return true;
    }
    
    void init_audio() {
        if (audio_stream_index == -1) return;
        
        // Get audio codec
        AVCodecParameters* audio_codecpar = ffmpeg_resources->format_ctx->streams[audio_stream_index]->codecpar;
        const AVCodec* audio_codec = avcodec_find_decoder(audio_codecpar->codec_id);
        
        if (!audio_codec) return;
        
        // Create codec context
        ffmpeg_resources->audio_codec_ctx = avcodec_alloc_context3(audio_codec);
        if (!ffmpeg_resources->audio_codec_ctx) return;
        
        // Copy codec parameters and open codec
        if (avcodec_parameters_to_context(ffmpeg_resources->audio_codec_ctx, audio_codecpar) < 0 ||
            avcodec_open2(ffmpeg_resources->audio_codec_ctx, audio_codec, nullptr) < 0) {
            return;
        }
        
        // Set audio parameters from codec
        audio_sample_rate = ffmpeg_resources->audio_codec_ctx->sample_rate;
        audio_channels = ffmpeg_resources->audio_codec_ctx->ch_layout.nb_channels;
        
        // Simple resampler setup
        AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
        if (swr_alloc_set_opts2(&ffmpeg_resources->swr_ctx,
            &out_ch_layout, AV_SAMPLE_FMT_S16, audio_sample_rate,
            &ffmpeg_resources->audio_codec_ctx->ch_layout, ffmpeg_resources->audio_codec_ctx->sample_fmt, ffmpeg_resources->audio_codec_ctx->sample_rate,
            0, nullptr) < 0 || swr_init(ffmpeg_resources->swr_ctx) < 0) {
            return;
        }
        
        // Setup SDL Audio
        SDL_AudioSpec wanted_spec;
        SDL_zero(wanted_spec);
        wanted_spec.freq = audio_sample_rate;
        wanted_spec.format = AUDIO_S16SYS;
        wanted_spec.channels = 2; // Force stereo output
        wanted_spec.samples = 1024;
        wanted_spec.callback = audio_callback;
        wanted_spec.userdata = this;
        
        audio_device = SDL_OpenAudioDevice(nullptr, 0, &wanted_spec, &audio_spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
        
        if (audio_device == 0) {
            std::cout << "Failed to open audio device: " << SDL_GetError() << std::endl;
            return;
        }
        
        audio_time_base = av_q2d(ffmpeg_resources->format_ctx->streams[audio_stream_index]->time_base);
        has_audio = true;
        audio_initialized = true;
        
        std::cout << "SDL Audio initialized: " << audio_spec.freq << "Hz, " << (int)audio_spec.channels << " channels" << std::endl;
    }
    
    void start_video_cache() {
        cache_thread = std::thread([this]() {
            std::cout << "🎬 Starting video cache..." << std::endl;
            
            // Reset stream position to beginning for video decoding
            av_seek_frame(ffmpeg_resources->format_ctx, -1, 0, AVSEEK_FLAG_BACKWARD);
            
            AVPacket packet;
            AVFrame* frame = av_frame_alloc();
            AVFrame* bgr_frame = av_frame_alloc();
            
            if (!frame || !bgr_frame) {
                std::cout << "Failed to allocate frames for caching" << std::endl;
                return;
            }
            
            // Allocate BGR buffer
            int bgr_buffer_size = av_image_get_buffer_size(AV_PIX_FMT_BGR24, 
                ffmpeg_resources->video_codec_ctx->width, ffmpeg_resources->video_codec_ctx->height, 1);
            uint8_t* bgr_buffer = (uint8_t*)av_malloc(bgr_buffer_size);
            
            if (!bgr_buffer) {
                av_frame_free(&frame);
                av_frame_free(&bgr_frame);
                return;
            }
            
            av_image_fill_arrays(bgr_frame->data, bgr_frame->linesize, bgr_buffer, AV_PIX_FMT_BGR24, 
                ffmpeg_resources->video_codec_ctx->width, ffmpeg_resources->video_codec_ctx->height, 1);
            
            int frame_count = 0;
            
            // Cache all video frames with improved error handling
            int decode_errors = 0;
            std::cout << "Reading packets from format context..." << std::endl;
            while (av_read_frame(ffmpeg_resources->format_ctx, &packet) >= 0) {
                if (packet.stream_index == video_stream_index) {
                    int ret = avcodec_send_packet(ffmpeg_resources->video_codec_ctx, &packet);
                    if (ret >= 0) {
                        while ((ret = avcodec_receive_frame(ffmpeg_resources->video_codec_ctx, frame)) >= 0) {
                            // Skip frames with invalid data
                            if (frame->width <= 0 || frame->height <= 0 || !frame->data[0]) {
                                av_frame_unref(frame);
                                continue;
                            }
                            
                            // Convert to BGR for OpenCV
                            int scale_ret = sws_scale(ffmpeg_resources->sws_ctx, frame->data, frame->linesize, 0, 
                                                     ffmpeg_resources->video_codec_ctx->height, bgr_frame->data, bgr_frame->linesize);
                            
                            if (scale_ret > 0) {
                                // Create OpenCV Mat from BGR data
                                cv::Mat cv_frame(ffmpeg_resources->video_codec_ctx->height, 
                                               ffmpeg_resources->video_codec_ctx->width, 
                                               CV_8UC3, bgr_frame->data[0], bgr_frame->linesize[0]);
                                
                                double pts = frame->pts != AV_NOPTS_VALUE ? frame->pts * video_time_base : frame_count / fps;
                                
                                // Add to cache
                                std::lock_guard<std::mutex> lock(cache_mutex);
                                video_cache.push_back(std::make_unique<VideoFrame>(cv_frame, pts));
                                frame_count++;
                                
                                // Progress feedback every 100 frames
                                if (frame_count % 100 == 0) {
                                    std::cout << "Cached " << frame_count << " video frames..." << std::endl;
                                }
                            }
                            
                            av_frame_unref(frame);
                        }
                    } else if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
                        decode_errors++;
                        // Only log first few decode errors to avoid spam
                        if (decode_errors <= 3) {
                            std::cout << "Video decode warning (frame " << frame_count << ")" << std::endl;
                        }
                    }
                }
                av_packet_unref(&packet);
            }
            
            av_free(bgr_buffer);
            av_frame_free(&bgr_frame);
            av_frame_free(&frame);
            
            cache_loaded = true;
            std::cout << "✅ Video cache completed: " << frame_count << " frames cached" << std::endl;
            
            if (frame_count == 0) {
                std::cout << "⚠️ Warning: No video frames were cached. Check video codec support." << std::endl;
            }
        });
    }
    
    void cache_audio_frames() {
        if (!has_audio || !ffmpeg_resources->audio_codec_ctx) {
            return;
        }
        
        std::cout << "🎵 Caching audio frames..." << std::endl;
        
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
        
        // Cache all audio frames
        while (av_read_frame(ffmpeg_resources->format_ctx, &packet) >= 0) {
            if (packet.stream_index == audio_stream_index) {
                int ret = avcodec_send_packet(ffmpeg_resources->audio_codec_ctx, &packet);
                if (ret >= 0) {
                    while ((ret = avcodec_receive_frame(ffmpeg_resources->audio_codec_ctx, frame)) >= 0) {
                        // Resample audio to S16 stereo
                        int out_samples = swr_get_out_samples(ffmpeg_resources->swr_ctx, frame->nb_samples);
                        uint8_t* audio_buf = (uint8_t*)av_malloc(out_samples * 2 * 2); // 2 channels * 2 bytes per sample
                        
                        if (audio_buf) {
                            uint8_t* out[] = {audio_buf};
                            int converted = swr_convert(ffmpeg_resources->swr_ctx, out, out_samples, 
                                                       (const uint8_t**)frame->data, frame->nb_samples);
                            
                            if (converted > 0) {
                                double pts = frame->pts != AV_NOPTS_VALUE ? frame->pts * audio_time_base : 0.0;
                                int64_t duration = frame->nb_samples;
                                
                                // Add to cache
                                std::lock_guard<std::mutex> lock(audio_frame_mutex);
                                audio_frame_cache.push_back(std::make_unique<AudioFrame>(audio_buf, converted * 2 * 2, pts, duration));
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
        
        std::cout << "✅ Audio frames cached: " << frame_count << " frames" << std::endl;
        
        // Reset stream position again for video caching
        av_seek_frame(ffmpeg_resources->format_ctx, -1, 0, AVSEEK_FLAG_BACKWARD);
    }
    
    void start_audio_thread() {
        if (audio_thread_running.load()) {
            return;
        }
        
        audio_thread_running.store(true);
        audio_should_stop.store(false);
        
        audio_thread = std::thread([this]() {
            std::cout << "🎵 Audio thread started" << std::endl;
            
            while (audio_thread_running.load() && !audio_should_stop.load()) {
                // Handle seek requests
                if (seek_requested.load()) {
                    handle_audio_seek();
                    seek_requested.store(false);
                }
                
                // Fill audio buffer with frames
                fill_audio_buffer();
                
                // Sleep briefly to avoid busy waiting
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            
            std::cout << "🎵 Audio thread stopped" << std::endl;
        });
    }
    
    void stop_audio_thread() {
        if (!audio_thread_running.load()) {
            return;
        }
        
        audio_should_stop.store(true);
        audio_thread_running.store(false);
        audio_cv.notify_all();
        
        if (audio_thread.joinable()) {
            audio_thread.join();
        }
    }
    
    void handle_audio_seek() {
        double target = seek_target.load();
        
        // Clear the circular buffer
        audio_buffer->clear();
        
        // Find the closest audio frame to the seek target
        std::lock_guard<std::mutex> lock(audio_frame_mutex);
        audio_cache_index = 0;
        
        for (size_t i = 0; i < audio_frame_cache.size(); ++i) {
            if (audio_frame_cache[i]->pts >= target - 0.05) {
                audio_cache_index = i;
                break;
            }
        }
        
        std::cout << "🎵 Audio seek to: " << std::fixed << std::setprecision(2) << target << "s (frame " << audio_cache_index << ")" << std::endl;
    }
    
    void fill_audio_buffer() {
        const size_t MIN_BUFFER_SIZE = AUDIO_BUFFER_SIZE / 4; // Keep buffer 25% full
        
        if (audio_buffer->available_read() > MIN_BUFFER_SIZE) {
            return; // Buffer has enough data
        }
        
        std::lock_guard<std::mutex> lock(audio_frame_mutex);
        
        // Fill buffer with audio frames
        while (audio_cache_index < audio_frame_cache.size() && 
               audio_buffer->available_write() > 4096) {
            
            auto& frame = audio_frame_cache[audio_cache_index];
            double video_time = video_clock.load();
            double audio_time = frame->pts;
            
            // Simple sync check - if audio is too far ahead, wait
            if (audio_time > video_time + AUDIO_SYNC_THRESHOLD) {
                break;
            }
            
            // Write frame data to circular buffer
            size_t written = audio_buffer->write(frame->data.get(), frame->size);
            if (written > 0) {
                audio_cache_index++;
            } else {
                break; // Buffer full
            }
        }
    }
    
    void cleanup_audio() {
        stop_audio_thread();
        
        if (audio_initialized && audio_device) {
            SDL_CloseAudioDevice(audio_device);
            audio_device = 0;
            audio_initialized = false;
        }
        
        // Clear audio frame cache
        {
            std::lock_guard<std::mutex> lock(audio_frame_mutex);
            audio_frame_cache.clear();
            audio_cache_index = 0;
        }
        
        if (audio_buffer) {
            audio_buffer->clear();
        }
    }
    
    void cleanup_video() {
        cache_loaded = false;
    }
    
    void play() {
        std::cout << "🎬 Starting cached video playback..." << std::endl;
        is_playing = true;
        
        // Wait for video cache to load
        std::cout << "⏳ Waiting for video cache to load..." << std::endl;
        while (!cache_loaded && is_playing) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // Handle keyboard input while waiting
            char key = cv::waitKey(1);
            if (key != -1) {
                handle_key(key);
            }
        }
        
        if (!cache_loaded) {
            std::cout << "❌ Video cache not loaded, exiting" << std::endl;
            return;
        }
        
        std::cout << "✅ Video cache loaded, starting playback" << std::endl;
        
        // Start audio playback if available
        if (has_audio && audio_initialized && audio_thread_running.load()) {
            sync_audio_to_video_position(0.0);
            start_audio_playback();
        }
        
        // Calculate frame time in microseconds for proper timing
        int frame_time_us = (int)(1000000.0 / fps);
        auto start_time = std::chrono::high_resolution_clock::now();
        
        current_frame = 0;
        
        // Main cached playback loop
        while (is_playing) {
            auto frame_start = std::chrono::high_resolution_clock::now();
            
            if (!is_paused) {
                std::lock_guard<std::mutex> lock(cache_mutex);
                
                if (current_frame < (int)video_cache.size()) {
                    cv::Mat display_frame = video_cache[current_frame]->frame.clone();
                    
                    // Update video clock for synchronization
                    double current_pts = video_cache[current_frame]->pts;
                    set_video_clock(current_pts);
                    master_pts.store(current_pts);
                    
                    // Add overlay information
                    cv::putText(display_frame, "Threaded Audio Video Player - Press 0-9 to seek, Q to quit, Space to pause, M to mute", 
                               cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.6, 
                               cv::Scalar(0, 255, 0), 2);
                    
                    char frame_info[256];
                    snprintf(frame_info, sizeof(frame_info), "Frame %d/%d (%.1f%%) | Video: %.2fs | Audio: %s | Buffer: %zu bytes", 
                            current_frame + 1, (int)video_cache.size(), 
                            (current_frame + 1) * 100.0 / video_cache.size(),
                            current_pts,
                            has_audio ? (audio_muted ? "MUTED" : (audio_thread_running.load() ? "THREADED" : "OFF")) : "N/A",
                            audio_buffer ? audio_buffer->available_read() : 0);
                    cv::putText(display_frame, frame_info, cv::Point(10, 60), 
                               cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);
                    
                    cv::imshow(window_name, display_frame);
                    current_frame++;
                    
                    // Loop back to beginning when we reach the end
                    if (current_frame >= (int)video_cache.size()) {
                        current_frame = 0;
                        if (has_audio && audio_thread_running.load()) {
                            sync_audio_to_video_position(0.0);
                        }
                    }
                }
            }
            
            // Handle keyboard input
            char key = cv::waitKey(1);
            if (key != -1) {
                handle_key(key);
            }
            
            // Precise frame timing
            if (!is_paused) {
                auto frame_end = std::chrono::high_resolution_clock::now();
                auto frame_duration = std::chrono::duration_cast<std::chrono::microseconds>(frame_end - frame_start).count();
                
                int sleep_time = frame_time_us - (int)frame_duration;
                if (sleep_time > 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(sleep_time));
                }
            } else {
                // When paused, just wait a bit
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
        
        cv::destroyAllWindows();
        std::cout << "🎬 Cached playback finished" << std::endl;
    }
    
    // Audio synchronization helper functions
    double get_audio_clock() {
        return audio_clock ? audio_clock->get() : 0.0;
    }
    
    void set_video_clock(double pts) {
        video_clock.store(pts);
    }
    
    double get_master_clock() {
        return master_pts.load(); // Use master PTS as reference
    }
    
    void start_audio_playback() {
        if (has_audio && audio_initialized && !audio_muted && audio_thread_running.load()) {
            SDL_PauseAudioDevice(audio_device, 0);
            std::cout << "🔊 Audio playback started" << std::endl;
        }
    }
    
    void pause_audio_playback() {
        if (has_audio && audio_initialized) {
            SDL_PauseAudioDevice(audio_device, 1);
            std::cout << "⏸ Audio playback paused" << std::endl;
        }
    }
    
    void toggle_audio_mute() {
        if (!has_audio || !audio_initialized) return;
        
        audio_muted = !audio_muted;
        if (audio_muted) {
            SDL_PauseAudioDevice(audio_device, 1);
        } else if (is_playing && !is_paused && audio_thread_running.load()) {
            SDL_PauseAudioDevice(audio_device, 0);
        }
        
        std::cout << (audio_muted ? "🔇 Audio muted" : "🔊 Audio unmuted") << std::endl;
    }
};

// SDL Audio callback function implementation
void audio_callback(void* userdata, Uint8* stream, int len) {
    VideoPlayer* player = static_cast<VideoPlayer*>(userdata);
    
    // Clear the stream buffer
    memset(stream, 0, len);
    
    if (!player || player->audio_muted || !player->audio_buffer) {
        return;
    }
    
    // Read audio data from circular buffer
    size_t bytes_read = player->audio_buffer->read(stream, len);
    
    // If we didn't get enough data, the rest remains silent (already cleared)
    if (bytes_read < (size_t)len) {
        // Buffer underrun - this is normal during seeks or at the end
    }
    
    // Update audio clock based on how much data we consumed
    if (bytes_read > 0) {
        double samples_played = (double)bytes_read / (2 * 2); // 2 channels, 2 bytes per sample
        double time_played = samples_played / player->audio_sample_rate;
        
        // Update the audio clock timestamp
        if (player->audio_clock) {
            double current_audio_time = player->audio_clock->get();
            player->audio_clock->set(current_audio_time + time_played);
        }
    }
}

// Signal handler implementation
void signal_handler(int sig) {
    std::cout << "\n💥 Crash detected (signal " << sig << "). Cleaning up..." << std::endl;
    
    if (g_player_instance) {
        try {
            g_player_instance->emergency_cleanup();
        } catch (...) {
            std::cout << "Emergency cleanup failed" << std::endl;
        }
    }
    
    std::cout << "Cleanup complete. Exiting safely." << std::endl;
    exit(sig);
}

int main(int argc, char* argv[]) {
    try {
        if (argc != 2) {
            std::cout << "🎬 Enhanced Video Player with Audio Support" << std::endl;
            std::cout << "Usage: " << argv[0] << " <video_file>" << std::endl;
            return -1;
        }
        
        std::cout << "🛡️ Starting Enhanced Video Player with crash protection..." << std::endl;
        
        VideoPlayer player;
        if (player.load_video(argv[1])) {
            player.play();
        }
        
        std::cout << "🎬 Playback completed successfully" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cout << "💥 Exception caught: " << e.what() << std::endl;
        return -1;
    } catch (...) {
        std::cout << "💥 Unknown exception caught" << std::endl;
        return -1;
    }
}