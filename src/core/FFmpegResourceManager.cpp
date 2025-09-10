#include "core/FFmpegResourceManager.h"

FFmpegResourceManager::~FFmpegResourceManager() {
    cleanup();
}

void FFmpegResourceManager::cleanup() {
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