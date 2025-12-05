#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <esp_http_server.h>

class SseServer {
public:
    static SseServer& GetInstance() {
        static SseServer instance;
        return instance;
    }

    void Start();
    void Broadcast(const std::string& msg);

    // Internal use
    void AddClient(httpd_req_t* req);
    void RemoveClient(httpd_req_t* req);

private:
    SseServer() = default;

    httpd_handle_t server_ = nullptr;

    struct ClientConn {
        httpd_req_t* req;
    };

    std::vector<ClientConn> clients_;
    std::mutex client_mutex_;
};
