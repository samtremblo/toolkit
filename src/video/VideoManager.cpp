#include "video/VideoManager.h"
#include <iostream>
#include <iomanip>

VideoManager::VideoManager() 
    : fps(30.0), total_frames(0), frame_time_ms(0.0),
      video_stream_index(-1), video_time_base(0.0) {
}

VideoManager::~VideoManager() {
    cache_loaded.store(false);
    if (cache_thread.joinable()) {
        cache_thread.join();
    }
}

bool VideoManager::initialize(std::shared_ptr<FFmpegResourceManager> resources, int stream_index) {
    ffmpeg_resources = resources;
    video_stream_index = stream_index;
    
    if (stream_index == -1) return false;
    
    // Setup video decoder
    AVCodecParameters* video_codecpar = ffmpeg_resources->format_ctx->streams[stream_index]->codecpar;
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
    
    // Setup scaling context for BGR conversion
    ffmpeg_resources->sws_ctx = sws_getContext(
        ffmpeg_resources->video_codec_ctx->width, 
        ffmpeg_resources->video_codec_ctx->height, 
        ffmpeg_resources->video_codec_ctx->pix_fmt,
        ffmpeg_resources->video_codec_ctx->width, 
        ffmpeg_resources->video_codec_ctx->height, 
        AV_PIX_FMT_BGR24,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );
    
    if (!ffmpeg_resources->sws_ctx) {
        std::cout << "Could not initialize scaling context" << std::endl;
        return false;
    }
    
    // Get video properties
    fps = av_q2d(ffmpeg_resources->format_ctx->streams[stream_index]->r_frame_rate);
    total_frames = ffmpeg_resources->format_ctx->streams[stream_index]->nb_frames;
    if (total_frames <= 0) {
        total_frames = (int)(ffmpeg_resources->format_ctx->duration * fps / AV_TIME_BASE);
    }
    frame_time_ms = 1000.0 / fps;
    video_time_base = av_q2d(ffmpeg_resources->format_ctx->streams[stream_index]->time_base);
    current_frame.store(0);
    
    std::cout << "=== Video Information ===" << std::endl;
    std::cout << "Resolution: " << ffmpeg_resources->video_codec_ctx->width 
              << "x" << ffmpeg_resources->video_codec_ctx->height << std::endl;
    std::cout << "FPS: " << fps << std::endl;
    std::cout << "Total frames: " << total_frames << std::endl;
    
    return true;
}

void VideoManager::start_video_cache() {
    cache_thread = std::thread(&VideoManager::cache_video_frames, this);
}

void VideoManager::wait_for_cache() {
    while (!cache_loaded.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void VideoManager::cache_video_frames() {
    std::cout << "Starting video cache..." << std::endl;
    
    // Reset stream position to beginning
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
        ffmpeg_resources->video_codec_ctx->width, 
        ffmpeg_resources->video_codec_ctx->height, 1);
    uint8_t* bgr_buffer = (uint8_t*)av_malloc(bgr_buffer_size);
    
    if (!bgr_buffer) {
        av_frame_free(&frame);
        av_frame_free(&bgr_frame);
        return;
    }
    
    av_image_fill_arrays(bgr_frame->data, bgr_frame->linesize, bgr_buffer, AV_PIX_FMT_BGR24, 
        ffmpeg_resources->video_codec_ctx->width, 
        ffmpeg_resources->video_codec_ctx->height, 1);
    
    int frame_count = 0;
    int decode_errors = 0;
    
    std::cout << "Reading packets from format context..." << std::endl;
    while (av_read_frame(ffmpeg_resources->format_ctx, &packet) >= 0) {
        if (packet.stream_index == video_stream_index) {
            int ret = avcodec_send_packet(ffmpeg_resources->video_codec_ctx, &packet);
            if (ret >= 0) {
                while ((ret = avcodec_receive_frame(ffmpeg_resources->video_codec_ctx, frame)) >= 0) {
                    if (frame->width <= 0 || frame->height <= 0 || !frame->data[0]) {
                        av_frame_unref(frame);
                        continue;
                    }
                    
                    int scale_ret = sws_scale(ffmpeg_resources->sws_ctx, 
                                            frame->data, frame->linesize, 0, 
                                            ffmpeg_resources->video_codec_ctx->height, 
                                            bgr_frame->data, bgr_frame->linesize);
                    
                    if (scale_ret > 0) {
                        cv::Mat cv_frame(ffmpeg_resources->video_codec_ctx->height, 
                                       ffmpeg_resources->video_codec_ctx->width, 
                                       CV_8UC3, bgr_frame->data[0], bgr_frame->linesize[0]);
                        
                        double pts = frame->pts != AV_NOPTS_VALUE ? 
                                   frame->pts * video_time_base : frame_count / fps;
                        
                        std::lock_guard<std::mutex> lock(cache_mutex);
                        video_cache.push_back(std::make_unique<VideoFrame>(cv_frame, pts));
                        frame_count++;
                        
                        if (frame_count % 100 == 0) {
                            std::cout << "Cached " << frame_count << " video frames..." << std::endl;
                        }
                    }
                    
                    av_frame_unref(frame);
                }
            } else if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
                decode_errors++;
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
    
    cache_loaded.store(true);
    std::cout << "Video cache completed: " << frame_count << " frames cached" << std::endl;
    
    if (frame_count == 0) {
        std::cout << "Warning: No video frames were cached. Check video codec support." << std::endl;
    }
}

cv::Mat VideoManager::get_current_frame() {
    std::lock_guard<std::mutex> lock(cache_mutex);
    
    int frame_num = current_frame.load();
    if (frame_num >= 0 && frame_num < (int)video_cache.size()) {
        double current_pts = video_cache[frame_num]->pts;
        set_video_clock(current_pts);
        return video_cache[frame_num]->frame.clone();
    }
    
    return cv::Mat();
}

void VideoManager::set_current_frame(int frame) {
    if (frame >= 0 && frame < (int)video_cache.size()) {
        current_frame.store(frame);
    }
}

void VideoManager::seek_to_percentage(double percentage) {
    if (percentage < 0.0) percentage = 0.0;
    if (percentage > 100.0) percentage = 100.0;
    
    if (!cache_loaded.load()) {
        std::cout << "Seek requested but cache not loaded yet" << std::endl;
        return;
    }
    
    std::lock_guard<std::mutex> lock(cache_mutex);
    
    int max_frame = (int)video_cache.size() - 1;
    int target_frame = (int)(percentage / 100.0 * max_frame);
    
    if (target_frame >= 0 && target_frame <= max_frame) {
        current_frame.store(target_frame);
        double seek_position = video_cache[target_frame]->pts;
        set_video_clock(seek_position);
        
        std::cout << "Fast seek to " << std::fixed << std::setprecision(1) << percentage 
                  << "% (frame " << target_frame << ", PTS: " << std::setprecision(2) 
                  << seek_position << "s)" << std::endl;
    }
}

size_t VideoManager::get_cache_size() const {
    std::lock_guard<std::mutex> lock(cache_mutex);
    return video_cache.size();
}