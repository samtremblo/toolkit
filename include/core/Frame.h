#pragma once
#include <opencv2/opencv.hpp>
#include <memory>

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