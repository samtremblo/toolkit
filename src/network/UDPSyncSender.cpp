#include "network/UDPSyncSender.h"
#include <iostream>
#include <cstring>
#include <unistd.h>

UDPSyncSender::UDPSyncSender(uint32_t client_id, const std::string& client_name, uint16_t port)
    : socket_fd(-1), port(port), client_id(client_id), client_name(client_name) {
}

UDPSyncSender::~UDPSyncSender() {
    shutdown();
}

bool UDPSyncSender::setup_socket() {
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        std::cerr << "Failed to create UDP sender socket: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Enable broadcast
    int broadcast_enable = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable)) < 0) {
        std::cerr << "Failed to enable broadcast on sender: " << strerror(errno) << std::endl;
        cleanup_socket();
        return false;
    }
    
    return true;
}

void UDPSyncSender::cleanup_socket() {
    if (socket_fd >= 0) {
        close(socket_fd);
        socket_fd = -1;
    }
}

bool UDPSyncSender::initialize() {
    if (!setup_socket()) {
        return false;
    }
    
    std::cout << "UDP Sync Sender initialized for client " << client_name 
              << " (ID: " << client_id << ")" << std::endl;
    
    // Send initial client announcement
    send_client_announce();
    
    return true;
}

void UDPSyncSender::shutdown() {
    stop_heartbeat();
    cleanup_socket();
    
    std::cout << "UDP Sync Sender shutdown" << std::endl;
}

void UDPSyncSender::start_heartbeat() {
    if (heartbeat_running.load()) {
        return;
    }
    
    heartbeat_running.store(true);
    heartbeat_thread = std::thread(&UDPSyncSender::heartbeat_loop, this);
    
    std::cout << "Heartbeat started for client " << client_name << std::endl;
}

void UDPSyncSender::stop_heartbeat() {
    if (!heartbeat_running.load()) {
        return;
    }
    
    heartbeat_running.store(false);
    
    if (heartbeat_thread.joinable()) {
        heartbeat_thread.join();
    }
    
    std::cout << "Heartbeat stopped for client " << client_name << std::endl;
}

void UDPSyncSender::heartbeat_loop() {
    while (heartbeat_running.load()) {
        SyncMessage heartbeat = SyncProtocol::create_heartbeat(client_id, client_name);
        broadcast_message(heartbeat);
        
        // Sleep for heartbeat interval
        for (int i = 0; i < HEARTBEAT_INTERVAL_SECONDS && heartbeat_running.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

bool UDPSyncSender::send_sync_cue(uint32_t frame_number) {
    SyncMessage msg = SyncProtocol::create_sync_cue(client_id, frame_number, client_name);
    
    std::cout << "Broadcasting SYNC_CUE - Frame: " << frame_number << std::endl;
    return broadcast_message(msg);
}

bool UDPSyncSender::send_seek_cue(double position) {
    SyncMessage msg = SyncProtocol::create_seek_cue(client_id, position, client_name);
    
    std::cout << "Broadcasting SEEK_CUE - Position: " << position << "s" << std::endl;
    return broadcast_message(msg);
}

bool UDPSyncSender::send_pause_cue() {
    SyncMessage msg = SyncProtocol::create_pause_cue(client_id, client_name);
    
    std::cout << "Broadcasting PAUSE_CUE" << std::endl;
    return broadcast_message(msg);
}

bool UDPSyncSender::send_resume_cue() {
    SyncMessage msg = SyncProtocol::create_resume_cue(client_id, client_name);
    
    std::cout << "Broadcasting RESUME_CUE" << std::endl;
    return broadcast_message(msg);
}

bool UDPSyncSender::send_client_announce() {
    SyncMessage msg = SyncProtocol::create_client_announce(client_id, client_name);
    
    std::cout << "Broadcasting CLIENT_ANNOUNCE for " << client_name << std::endl;
    return broadcast_message(msg);
}

bool UDPSyncSender::send_message(const SyncMessage& msg) {
    return broadcast_message(msg);
}

bool UDPSyncSender::broadcast_message(const SyncMessage& msg) {
    if (socket_fd < 0) {
        std::cerr << "Socket not initialized" << std::endl;
        return false;
    }
    
    sockaddr_in broadcast_addr;
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(port);
    broadcast_addr.sin_addr.s_addr = INADDR_BROADCAST;
    
    return send_to_sockaddr(msg, broadcast_addr);
}

bool UDPSyncSender::send_to_address(const SyncMessage& msg, const std::string& ip_address) {
    if (socket_fd < 0) {
        std::cerr << "Socket not initialized" << std::endl;
        return false;
    }
    
    sockaddr_in target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(port);
    
    if (inet_aton(ip_address.c_str(), &target_addr.sin_addr) == 0) {
        std::cerr << "Invalid IP address: " << ip_address << std::endl;
        return false;
    }
    
    return send_to_sockaddr(msg, target_addr);
}

bool UDPSyncSender::send_to_addresses(const SyncMessage& msg, const std::vector<std::string>& addresses) {
    bool all_success = true;
    
    for (const auto& address : addresses) {
        if (!send_to_address(msg, address)) {
            all_success = false;
        }
    }
    
    return all_success;
}

bool UDPSyncSender::send_to_sockaddr(const SyncMessage& msg, const sockaddr_in& addr) {
    ssize_t sent = sendto(socket_fd, &msg, sizeof(SyncMessage), 0, 
                         (const struct sockaddr*)&addr, sizeof(addr));
    
    if (sent < 0) {
        std::cerr << "Failed to send UDP message: " << strerror(errno) << std::endl;
        return false;
    }
    
    if (sent != sizeof(SyncMessage)) {
        std::cerr << "Partial UDP message sent: " << sent << "/" << sizeof(SyncMessage) << " bytes" << std::endl;
        return false;
    }
    
    return true;
}