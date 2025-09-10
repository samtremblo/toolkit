#include <iostream>
#include <exception>
#include "core/VideoPlayer.h"

int main(int argc, char* argv[]) {
    try {
        if (argc != 2) {
            std::cout << "Enhanced Video Player with Threaded Audio Support" << std::endl;
            std::cout << "Usage: " << argv[0] << " <video_file>" << std::endl;
            return -1;
        }
        
        std::cout << "Starting Enhanced Video Player with crash protection..." << std::endl;
        
        VideoPlayer player;
        if (player.load_video(argv[1])) {
            player.play();
        }
        
        std::cout << "Playback completed successfully" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cout << "Exception caught: " << e.what() << std::endl;
        return -1;
    } catch (...) {
        std::cout << "Unknown exception caught" << std::endl;
        return -1;
    }
}