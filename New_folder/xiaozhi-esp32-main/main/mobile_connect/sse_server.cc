#include "sse_server.h"
#include <esp_log.h>
#include <algorithm>
#include <cstring>

static const char* TAG = "SSE";
static SseServer* selfRef = nullptr;

// ==========================================================
//  SSE HANDLER (KHÔNG ĐƯỢC RETURN SỚM)
// ==========================================================
static esp_err_t sse_handler(httpd_req_t* req)
{
    ESP_LOGI(TAG, "Client connecting to /events");

    // Headers SSE
    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    // Sent connected event
    const char* hello =
        "event: message\n"
        "data: connected\n\n";
    httpd_resp_send_chunk(req, hello, strlen(hello));

    selfRef->AddClient(req);

    // ===========================
    //   GIỮ KẾT NỐI SỐNG
    // ===========================
    char buf[8];
    while (true)
    {
        int ret = httpd_req_recv(req, buf, sizeof(buf));
        if (ret <= 0) {
            ESP_LOGW(TAG, "Client disconnected");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    selfRef->RemoveClient(req);
    return ESP_OK;
}

// ==========================================================
// ADD CLIENT
// ==========================================================
void SseServer::AddClient(httpd_req_t* req)
{
    std::lock_guard<std::mutex> lock(client_mutex_);
    clients_.push_back({ req });

    ESP_LOGI(TAG, "SSE: client added. total=%d", clients_.size());
}

// ==========================================================
// REMOVE CLIENT
// ==========================================================
void SseServer::RemoveClient(httpd_req_t* req)
{
    std::lock_guard<std::mutex> lock(client_mutex_);

    clients_.erase(
        std::remove_if(clients_.begin(), clients_.end(),
            [&](const ClientConn& c){ return c.req == req; }),
        clients_.end()
    );

    ESP_LOGI(TAG, "SSE: client removed. total=%d", clients_.size());
}

// ==========================================================
// BROADCAST
// ==========================================================
void SseServer::Broadcast(const std::string& msg)
{
    std::lock_guard<std::mutex> lock(client_mutex_);

    std::string payload =
        "event: message\n"
        "data: " + msg + "\n\n";

    for (auto it = clients_.begin(); it != clients_.end(); )
    {
        esp_err_t err = httpd_resp_send_chunk(it->req, payload.c_str(), payload.size());

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Client failed (err=%d). Removing...", err);
            it = clients_.erase(it);
        } else {
            ++it;
        }
    }
}

// ==========================================================
// START SERVER
// ==========================================================
void SseServer::Start()
{
    selfRef = this;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8070;
    config.ctrl_port = 8071;

    if (httpd_start(&server_, &config) == ESP_OK)
    {
        httpd_uri_t uri {
            .uri      = "/events",
            .method   = HTTP_GET,
            .handler  = sse_handler,
            .user_ctx = nullptr
        };

        httpd_register_uri_handler(server_, &uri);

        ESP_LOGI(TAG, "SSE server running at http://<ip>:%d/events", config.server_port);
    }
    else {
        ESP_LOGE(TAG, "Failed to start SSE server!");
    }
}
