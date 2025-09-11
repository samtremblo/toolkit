#pragma once
#include <cstdint>
#include <string>
#include <chrono>

enum class SyncMessageType : uint8_t {
    HEARTBEAT = 0x01,
    SYNC_CUE = 0x02,
    SEEK_CUE = 0x03,
    PAUSE_CUE = 0x04,
    RESUME_CUE = 0x05,
    CLIENT_DISCOVER = 0x06,
    CLIENT_ANNOUNCE = 0x07
};

struct SyncMessage {
    uint32_t magic = 0xDEADBEEF;  // Magic number for validation
    SyncMessageType type;
    uint32_t sender_id;
    uint64_t timestamp_us;  // Microseconds since epoch
    uint32_t frame_number;
    double seek_position;
    char client_name[32];
    uint32_t checksum;
    
    SyncMessage() : type(SyncMessageType::HEARTBEAT), sender_id(0), 
                   timestamp_us(0), frame_number(0), seek_position(0.0) {
        memset(client_name, 0, sizeof(client_name));
        checksum = 0;
    }
    
    void calculate_checksum();
    bool validate_checksum() const;
    uint64_t get_current_timestamp_us() const;
} __attribute__((packed));

class SyncProtocol {
public:
    static constexpr uint16_t DEFAULT_PORT = 9999;
    static constexpr uint32_t BROADCAST_ADDR = 0xFFFFFFFF; // 255.255.255.255
    static constexpr size_t MAX_MESSAGE_SIZE = sizeof(SyncMessage);
    
    static SyncMessage create_sync_cue(uint32_t sender_id, uint32_t frame_number, 
                                      const std::string& client_name = "");
    static SyncMessage create_seek_cue(uint32_t sender_id, double position, 
                                      const std::string& client_name = "");
    static SyncMessage create_pause_cue(uint32_t sender_id, 
                                       const std::string& client_name = "");
    static SyncMessage create_resume_cue(uint32_t sender_id, 
                                        const std::string& client_name = "");
    static SyncMessage create_heartbeat(uint32_t sender_id, 
                                       const std::string& client_name = "");
    static SyncMessage create_client_announce(uint32_t sender_id, 
                                             const std::string& client_name = "");
};