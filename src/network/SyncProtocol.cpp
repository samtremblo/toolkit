#include "network/SyncProtocol.h"
#include <cstring>
#include <chrono>

void SyncMessage::calculate_checksum() {
    checksum = 0;
    const uint8_t* data = reinterpret_cast<const uint8_t*>(this);
    const size_t size = sizeof(SyncMessage) - sizeof(checksum);
    
    uint32_t sum = 0;
    for (size_t i = 0; i < size; ++i) {
        sum += data[i];
    }
    checksum = sum;
}

bool SyncMessage::validate_checksum() const {
    uint32_t stored_checksum = checksum;
    const_cast<SyncMessage*>(this)->checksum = 0;
    
    const uint8_t* data = reinterpret_cast<const uint8_t*>(this);
    const size_t size = sizeof(SyncMessage) - sizeof(checksum);
    
    uint32_t sum = 0;
    for (size_t i = 0; i < size; ++i) {
        sum += data[i];
    }
    
    const_cast<SyncMessage*>(this)->checksum = stored_checksum;
    return sum == stored_checksum;
}

uint64_t SyncMessage::get_current_timestamp_us() const {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

SyncMessage SyncProtocol::create_sync_cue(uint32_t sender_id, uint32_t frame_number, 
                                          const std::string& client_name) {
    SyncMessage msg;
    msg.type = SyncMessageType::SYNC_CUE;
    msg.sender_id = sender_id;
    msg.timestamp_us = msg.get_current_timestamp_us();
    msg.frame_number = frame_number;
    
    strncpy(msg.client_name, client_name.c_str(), sizeof(msg.client_name) - 1);
    msg.calculate_checksum();
    
    return msg;
}

SyncMessage SyncProtocol::create_seek_cue(uint32_t sender_id, double position, 
                                          const std::string& client_name) {
    SyncMessage msg;
    msg.type = SyncMessageType::SEEK_CUE;
    msg.sender_id = sender_id;
    msg.timestamp_us = msg.get_current_timestamp_us();
    msg.seek_position = position;
    
    strncpy(msg.client_name, client_name.c_str(), sizeof(msg.client_name) - 1);
    msg.calculate_checksum();
    
    return msg;
}

SyncMessage SyncProtocol::create_pause_cue(uint32_t sender_id, 
                                          const std::string& client_name) {
    SyncMessage msg;
    msg.type = SyncMessageType::PAUSE_CUE;
    msg.sender_id = sender_id;
    msg.timestamp_us = msg.get_current_timestamp_us();
    
    strncpy(msg.client_name, client_name.c_str(), sizeof(msg.client_name) - 1);
    msg.calculate_checksum();
    
    return msg;
}

SyncMessage SyncProtocol::create_resume_cue(uint32_t sender_id, 
                                           const std::string& client_name) {
    SyncMessage msg;
    msg.type = SyncMessageType::RESUME_CUE;
    msg.sender_id = sender_id;
    msg.timestamp_us = msg.get_current_timestamp_us();
    
    strncpy(msg.client_name, client_name.c_str(), sizeof(msg.client_name) - 1);
    msg.calculate_checksum();
    
    return msg;
}

SyncMessage SyncProtocol::create_heartbeat(uint32_t sender_id, 
                                          const std::string& client_name) {
    SyncMessage msg;
    msg.type = SyncMessageType::HEARTBEAT;
    msg.sender_id = sender_id;
    msg.timestamp_us = msg.get_current_timestamp_us();
    
    strncpy(msg.client_name, client_name.c_str(), sizeof(msg.client_name) - 1);
    msg.calculate_checksum();
    
    return msg;
}

SyncMessage SyncProtocol::create_client_announce(uint32_t sender_id, 
                                                const std::string& client_name) {
    SyncMessage msg;
    msg.type = SyncMessageType::CLIENT_ANNOUNCE;
    msg.sender_id = sender_id;
    msg.timestamp_us = msg.get_current_timestamp_us();
    
    strncpy(msg.client_name, client_name.c_str(), sizeof(msg.client_name) - 1);
    msg.calculate_checksum();
    
    return msg;
}