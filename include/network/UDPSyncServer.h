#pragma once
#include <thread>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>
#include <mutex>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "network/SyncProtocol.h"

struct ConnectedClient {
    uint32_t client_id;
    std::string name;
    std::string ip_address;
    uint16_t port;
    std::chrono::steady_clock::time_point last_heartbeat;
    
    ConnectedClient(uint32_t id, const std::string& n, const std::string& ip, uint16_t p)
        : client_id(id), name(n), ip_address(ip), port(p), 
          last_heartbeat(std::chrono::steady_clock::now()) {}
};

class UDPSyncServer {
public:
    using MessageCallback = std::function<void(const SyncMessage&, const std::string&)>;
    
private:
    int socket_fd;
    uint16_t port;
    std::atomic<bool> running{false};
    std::thread server_thread;
    std::thread heartbeat_thread;
    
    MessageCallback message_callback;
    
    // Connected clients tracking
    std::vector<std::unique_ptr<ConnectedClient>> connected_clients;
    std::mutex clients_mutex;
    
    static constexpr int CLIENT_TIMEOUT_SECONDS = 30;
    
public:
    UDPSyncServer(uint16_t port = SyncProtocol::DEFAULT_PORT);
    ~UDPSyncServer();
    
    bool start();
    void stop();
    bool is_running() const { return running.load(); }
    
    void set_message_callback(MessageCallback callback) { message_callback = callback; }
    
    // Client management
    std::vector<ConnectedClient> get_connected_clients();
    size_t get_client_count();
    
private:
    void server_loop();
    void heartbeat_loop();
    void handle_message(const SyncMessage& msg, const std::string& sender_ip, uint16_t sender_port);
    void update_client_list(const SyncMessage& msg, const std::string& sender_ip);
    void cleanup_stale_clients();
    
    bool setup_socket();
    void cleanup_socket();
};