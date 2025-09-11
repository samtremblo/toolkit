#pragma once
#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "network/SyncProtocol.h"

class UDPSyncSender {
private:
    int socket_fd;
    uint16_t port;
    uint32_t client_id;
    std::string client_name;
    
    std::atomic<bool> heartbeat_running{false};
    std::thread heartbeat_thread;
    
    static constexpr int HEARTBEAT_INTERVAL_SECONDS = 10;
    
public:
    UDPSyncSender(uint32_t client_id, const std::string& client_name, 
                  uint16_t port = SyncProtocol::DEFAULT_PORT);
    ~UDPSyncSender();
    
    bool initialize();
    void shutdown();
    
    // Start/stop heartbeat
    void start_heartbeat();
    void stop_heartbeat();
    
    // Sync commands
    bool send_sync_cue(uint32_t frame_number);
    bool send_seek_cue(double position);
    bool send_pause_cue();
    bool send_resume_cue();
    bool send_client_announce();
    
    // Direct message sending
    bool send_message(const SyncMessage& msg);
    bool broadcast_message(const SyncMessage& msg);
    
    // Targeted sending to specific IPs
    bool send_to_address(const SyncMessage& msg, const std::string& ip_address);
    bool send_to_addresses(const SyncMessage& msg, const std::vector<std::string>& addresses);
    
    // Getters
    uint32_t get_client_id() const { return client_id; }
    const std::string& get_client_name() const { return client_name; }
    
private:
    bool setup_socket();
    void cleanup_socket();
    void heartbeat_loop();
    
    bool send_to_sockaddr(const SyncMessage& msg, const sockaddr_in& addr);
};