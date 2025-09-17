#include "network/UDPSyncServer.h"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>

UDPSyncServer::UDPSyncServer(uint16_t port) : socket_fd(-1), port(port) {
}

UDPSyncServer::~UDPSyncServer() {
    stop();
}

bool UDPSyncServer::setup_socket() {
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        std::cerr << "Failed to create UDP socket: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Enable broadcast and reuse address
    int broadcast_enable = 1;
    int reuse_addr = 1;
    
    if (setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable)) < 0) {
        std::cerr << "Failed to enable broadcast: " << strerror(errno) << std::endl;
        cleanup_socket();
        return false;
    }
    
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr)) < 0) {
        std::cerr << "Failed to set reuse address: " << strerror(errno) << std::endl;
        cleanup_socket();
        return false;
    }
    
    // Set non-blocking for timeout handling
    int flags = fcntl(socket_fd, F_GETFL, 0);
    fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);
    
    // Bind to port
    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    if (bind(socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Failed to bind UDP socket to port " << port << ": " << strerror(errno) << std::endl;
        cleanup_socket();
        return false;
    }
    
    std::cout << "UDP Sync Server listening on port " << port << std::endl;
    return true;
}

void UDPSyncServer::cleanup_socket() {
    if (socket_fd >= 0) {
        close(socket_fd);
        socket_fd = -1;
    }
}

bool UDPSyncServer::start() {
    if (running.load()) {
        return true;
    }
    
    if (!setup_socket()) {
        return false;
    }
    
    running.store(true);
    
    server_thread = std::thread(&UDPSyncServer::server_loop, this);
    heartbeat_thread = std::thread(&UDPSyncServer::heartbeat_loop, this);
    
    std::cout << "UDP Sync Server started" << std::endl;
    return true;
}

void UDPSyncServer::stop() {
    if (!running.load()) {
        return;
    }
    
    running.store(false);
    
    if (server_thread.joinable()) {
        server_thread.join();
    }
    
    if (heartbeat_thread.joinable()) {
        heartbeat_thread.join();
    }
    
    cleanup_socket();
    
    std::lock_guard<std::mutex> lock(clients_mutex);
    connected_clients.clear();
    
    std::cout << "UDP Sync Server stopped" << std::endl;
}

void UDPSyncServer::server_loop() {
    uint8_t buffer[SyncProtocol::MAX_MESSAGE_SIZE];
    sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    while (running.load()) {
        ssize_t received = recvfrom(socket_fd, buffer, sizeof(buffer), 0, 
                                   (struct sockaddr*)&client_addr, &client_len);
        
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No data available, continue
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            } else {
                std::cerr << "UDP receive error: " << strerror(errno) << std::endl;
                break;
            }
        }
        
        if (received == sizeof(SyncMessage)) {
            SyncMessage* msg = reinterpret_cast<SyncMessage*>(buffer);
            
            // Validate message
            if (msg->magic == 0xDEADBEEF && msg->validate_checksum()) {
                std::string sender_ip = inet_ntoa(client_addr.sin_addr);
                uint16_t sender_port = ntohs(client_addr.sin_port);
                handle_message(*msg, sender_ip, sender_port);
            } else {
                std::cout << "Received invalid sync message" << std::endl;
            }
        }
    }
}

void UDPSyncServer::heartbeat_loop() {
    while (running.load()) {
        cleanup_stale_clients();
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

void UDPSyncServer::handle_message(const SyncMessage& msg, const std::string& sender_ip, uint16_t sender_port) {
    // Update client list for all message types
    update_client_list(msg, sender_ip);
    
    // Call user callback if set
    if (message_callback) {
        message_callback(msg, sender_ip);
    }
    
    // Log message (optional, for debugging)
    switch (msg.type) {
        case SyncMessageType::SYNC_CUE:
            std::cout << "Received SYNC_CUE from " << msg.client_name 
                      << " (" << sender_ip << ":" << sender_port << ") - Frame: " << msg.frame_number << std::endl;
            break;
        case SyncMessageType::SEEK_CUE:
            std::cout << "Received SEEK_CUE from " << msg.client_name 
                      << " (" << sender_ip << ":" << sender_port << ") - Position: " << msg.seek_position << "s" << std::endl;
            break;
        case SyncMessageType::PAUSE_CUE:
            std::cout << "Received PAUSE_CUE from " << msg.client_name 
                      << " (" << sender_ip << ":" << sender_port << ")" << std::endl;
            break;
        case SyncMessageType::RESUME_CUE:
            std::cout << "Received RESUME_CUE from " << msg.client_name 
                      << " (" << sender_ip << ":" << sender_port << ")" << std::endl;
            break;
        case SyncMessageType::CLIENT_ANNOUNCE:
            std::cout << "Client " << msg.client_name 
                      << " (" << sender_ip << ":" << sender_port << ") announced" << std::endl;
            break;
        case SyncMessageType::HEARTBEAT:
            // Don't log heartbeats to avoid spam
            break;
    }
}

void UDPSyncServer::update_client_list(const SyncMessage& msg, const std::string& sender_ip) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    
    // Find existing client
    for (auto& client : connected_clients) {
        if (client->client_id == msg.sender_id) {
            client->last_heartbeat = std::chrono::steady_clock::now();
            client->name = std::string(msg.client_name);
            client->ip_address = sender_ip;
            return;
        }
    }
    
    // Add new client
    connected_clients.push_back(
        std::make_unique<ConnectedClient>(
            msg.sender_id, 
            std::string(msg.client_name), 
            sender_ip, 
            port
        )
    );
    
    std::cout << "New client connected: " << msg.client_name 
              << " (" << sender_ip << ") ID: " << msg.sender_id << std::endl;
}

void UDPSyncServer::cleanup_stale_clients() {
    std::lock_guard<std::mutex> lock(clients_mutex);
    
    auto now = std::chrono::steady_clock::now();
    
    connected_clients.erase(
        std::remove_if(connected_clients.begin(), connected_clients.end(),
            [now](const std::unique_ptr<ConnectedClient>& client) {
                auto time_since_heartbeat = std::chrono::duration_cast<std::chrono::seconds>(
                    now - client->last_heartbeat).count();
                
                if (time_since_heartbeat > CLIENT_TIMEOUT_SECONDS) {
                    std::cout << "Client " << client->name 
                              << " (" << client->ip_address << ") timed out" << std::endl;
                    return true;
                }
                return false;
            }),
        connected_clients.end()
    );
}

std::vector<ConnectedClient> UDPSyncServer::get_connected_clients() {
    std::lock_guard<std::mutex> lock(clients_mutex);
    
    std::vector<ConnectedClient> clients;
    for (const auto& client : connected_clients) {
        clients.emplace_back(*client);
    }
    
    return clients;
}

size_t UDPSyncServer::get_client_count() {
    std::lock_guard<std::mutex> lock(clients_mutex);
    return connected_clients.size();
}