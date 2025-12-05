#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "assets/lang_config.h"
#include "mcp_server.h"
#include "assets.h"
#include "settings.h"
// Wifi helper to get IP address
#include <wifi_station.h>

#include <cstring>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>
#include <font_awesome.h>
#include "mobile_connect/sse_server.h"

#define TAG "Application"


// ========================================================
//  FIXED /chat handler with CORS + OPTIONS + JSON parsing
// ========================================================
// ------------------ State strings table ------------------
static const char* const STATE_STRINGS[] = {
    "unknown",
    "starting",
    "configuring",
    "idle",
    "connecting",
    "listening",
    "speaking",
    "upgrading",
    "activating",
    "audio_testing",
    "fatal_error",
    "invalid_state"
};

// ------------------ Constructor ------------------
Application::Application() {
    event_group_ = xEventGroupCreate();

#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            xEventGroupSetBits(app->event_group_, MAIN_EVENT_CLOCK_TICK);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);
}

Application::~Application() {
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    vEventGroupDelete(event_group_);
}

// ------------ Next: CheckAssetsVersion() ---------------

void Application::CheckAssetsVersion() {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto& assets = Assets::GetInstance();

    if (!assets.partition_valid()) {
        ESP_LOGW(TAG, "Assets partition is disabled for board %s", BOARD_NAME);
        return;
    }
    
    Settings settings("assets", true);
    // Check if there is a new assets need to be downloaded
    std::string download_url = settings.GetString("download_url");

    if (!download_url.empty()) {
        settings.EraseKey("download_url");

        char message[256];
        snprintf(message, sizeof(message), Lang::Strings::FOUND_NEW_ASSETS, download_url.c_str());
        Alert(Lang::Strings::LOADING_ASSETS, message, "cloud_arrow_down", Lang::Sounds::OGG_UPGRADE);
        
        // Wait for the audio service to be idle for 3 seconds
        vTaskDelay(pdMS_TO_TICKS(3000));
        SetDeviceState(kDeviceStateUpgrading);
        board.SetPowerSaveMode(false);
        display->SetChatMessage("system", Lang::Strings::PLEASE_WAIT);

        bool success = assets.Download(download_url, [display](int progress, size_t speed) -> void {
            std::thread([display, progress, speed]() {
                char buffer[32];
                snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, (unsigned)(speed / 1024));
                display->SetChatMessage("system", buffer);
            }).detach();
        });

        board.SetPowerSaveMode(true);
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!success) {
            Alert(Lang::Strings::ERROR, Lang::Strings::DOWNLOAD_ASSETS_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
            vTaskDelay(pdMS_TO_TICKS(2000));
            return;
        }
    }

    // Apply assets
    assets.Apply();
    display->SetChatMessage("system", "");
    display->SetEmotion("microchip_ai");
}

void Application::CheckNewVersion(Ota& ota) {
    const int MAX_RETRY = 10;
    int retry_count = 0;
    int retry_delay = 10; // initial delay 10 seconds

    auto& board = Board::GetInstance();
    while (true) {
        SetDeviceState(kDeviceStateActivating);
        auto display = board.GetDisplay();
        display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

        if (!ota.CheckVersion()) {
            retry_count++;
            if (retry_count >= MAX_RETRY) {
                ESP_LOGE(TAG, "Too many retries, exiting version check");
                return;
            }

            char buffer[256];
            snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED,
                     retry_delay, ota.GetCheckVersionUrl().c_str());
            Alert(Lang::Strings::ERROR, buffer, "cloud_slash", Lang::Sounds::OGG_EXCLAMATION);

            ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)",
                     retry_delay, retry_count, MAX_RETRY);

            for (int i = 0; i < retry_delay; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (device_state_ == kDeviceStateIdle) {
                    break;
                }
            }

            retry_delay *= 2;
            continue;
        }

        retry_count = 0;
        retry_delay = 10;

        if (ota.HasNewVersion()) {
            if (UpgradeFirmware(ota)) {
                return;
            }
        }

        ota.MarkCurrentVersionValid();

        if (!ota.HasActivationCode() && !ota.HasActivationChallenge()) {
            xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
            break;
        }

        display->SetStatus(Lang::Strings::ACTIVATION);
        
        if (ota.HasActivationCode()) {
            ShowActivationCode(ota.GetActivationCode(), ota.GetActivationMessage());
        }

        for (int i = 0; i < 10; ++i) {
            ESP_LOGI(TAG, "Activating... %d/10", i + 1);

            esp_err_t err = ota.Activate();
            if (err == ESP_OK) {
                xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
                break;
            } else if (err == ESP_ERR_TIMEOUT) {
                vTaskDelay(pdMS_TO_TICKS(3000));
            } else {
                vTaskDelay(pdMS_TO_TICKS(10000));
            }

            if (device_state_ == kDeviceStateIdle) {
                break;
            }
        }
    }
}

void Application::ShowActivationCode(const std::string& code,
                                     const std::string& message) {
    struct digit_sound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<digit_sound, 10> digit_sounds {{
        {'0', Lang::Sounds::OGG_0},
        {'1', Lang::Sounds::OGG_1},
        {'2', Lang::Sounds::OGG_2},
        {'3', Lang::Sounds::OGG_3},
        {'4', Lang::Sounds::OGG_4},
        {'5', Lang::Sounds::OGG_5},
        {'6', Lang::Sounds::OGG_6},
        {'7', Lang::Sounds::OGG_7},
        {'8', Lang::Sounds::OGG_8},
        {'9', Lang::Sounds::OGG_9}
    }};

    Alert(Lang::Strings::ACTIVATION, message.c_str(),
          "link", Lang::Sounds::OGG_ACTIVATION);

    for (char d : code) {
        for (const auto& ds : digit_sounds) {
            if (ds.digit == d) {
                audio_service_.PlaySound(ds.sound);
            }
        }
    }
}

void Application::Alert(const char* status,
                        const char* message,
                        const char* emotion,
                        const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert [%s] %s: %s", emotion, status, message);

    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);

    if (!sound.empty()) {
        audio_service_.PlaySound(sound);
    }
}

void Application::DismissAlert() {
    if (device_state_ == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}
void Application::ToggleChatState() {
    if (device_state_ == kDeviceStateActivating) {
        
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (device_state_ == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    } else if (device_state_ == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return;
                }
            }
            SetListeningMode(aec_mode_ == kAecOff
                             ? kListeningModeAutoStop
                             : kListeningModeRealtime);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) {
        Schedule([this]() {
            protocol_->CloseAudioChannel();
        });
    }
}

void Application::StartListening() {
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (device_state_ == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return;
                }
            }
            SetListeningMode(kListeningModeManualStop);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
            SetListeningMode(kListeningModeManualStop);
        });
    }
}

void Application::StopListening() {
    if (device_state_ == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    const std::array<int, 3> valid_states = {
        kDeviceStateListening,
        kDeviceStateSpeaking,
        kDeviceStateIdle,
    };

    if (std::find(valid_states.begin(), valid_states.end(),
                  device_state_) == valid_states.end()) {
        return;
    }

    Schedule([this]() {
        if (device_state_ == kDeviceStateListening) {
            protocol_->SendStopListening();
            SetDeviceState(kDeviceStateIdle);
        }
    });
}
void Application::Start() {
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    /* Setup the display */
    auto display = board.GetDisplay();
    display->SetChatMessage("system", SystemInfo::GetUserAgent().c_str());

    /* Setup the audio service */
    auto codec = board.GetAudioCodec();
    audio_service_.Initialize(codec);
    audio_service_.Start();

    AudioServiceCallbacks callbacks;
    callbacks.on_send_queue_available = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
    };
    callbacks.on_wake_word_detected = [this](const std::string& wake_word) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
    };
    callbacks.on_vad_change = [this](bool speaking) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
    };
    audio_service_.SetCallbacks(callbacks);

    // Main event loop
    xTaskCreate([](void* arg) {
        ((Application*)arg)->MainEventLoop();
        vTaskDelete(NULL);
    }, "main_event_loop", 2048 * 4, this, 3, &main_event_loop_task_handle_);

    /* Start system clock */
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    /* Start WiFi */
    board.StartNetwork();

    // Log device IP
    {
        std::string ip = WifiStation::GetInstance().GetIpAddress();
        if (!ip.empty()) {
            ESP_LOGI(TAG, "Device IP: %s", ip.c_str());
        } else {
            ESP_LOGW(TAG, "Device IP not available yet");
        }
    }

    display->UpdateStatusBar(true);

    // Assets
    CheckAssetsVersion();

    // OTA or MQTT/WebSocket config
    Ota ota;
    CheckNewVersion(ota);

    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    // MCP tools
    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddCommonTools();
    mcp_server.AddUserOnlyTools();

    // Choose protocol
    if (ota.HasMqttConfig()) {
        protocol_ = std::make_unique<MqttProtocol>();
    } else if (ota.HasWebsocketConfig()) {
        protocol_ = std::make_unique<WebsocketProtocol>();
    } else {
        ESP_LOGW(TAG, "No protocol specified, defaulting to MQTT");
        protocol_ = std::make_unique<MqttProtocol>();
    }

    // Protocol callbacks
    protocol_->OnConnected([this]() {
        DismissAlert();
    });
    protocol_->OnNetworkError([this](const std::string& message) {
        last_error_message_ = message;
        xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
    });
    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        if (device_state_ == kDeviceStateSpeaking) {
            audio_service_.PushPacketToDecodeQueue(std::move(packet));
        }
    });
    protocol_->OnAudioChannelOpened([this, codec, &board]() {
        board.SetPowerSaveMode(false);
        if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
            ESP_LOGW(TAG,
                     "Server sample rate = %d, device = %d",
                     protocol_->server_sample_rate(),
                     codec->output_sample_rate());
        }
    });
    protocol_->OnAudioChannelClosed([this, &board]() {
        board.SetPowerSaveMode(true);
        Schedule([this]() {
            auto display = Board::GetInstance().GetDisplay();
            display->SetChatMessage("system", "");
            SetDeviceState(kDeviceStateIdle);
        });
    });

    // Incoming JSON (TTS, STT, MCP, System messages)
    protocol_->OnIncomingJson([this, display](const cJSON* root) {
        char* dump = cJSON_Print(root);
if (dump) {
    ESP_LOGI("INCOMING_JSON", "%s", dump);
    free(dump);
} else {
    ESP_LOGW("INCOMING_JSON", "Root JSON is NULL");
}

        auto type = cJSON_GetObjectItem(root, "type");
        if (!cJSON_IsString(type)) {
            ESP_LOGW(TAG, "Invalid JSON message");
            return;
        }

        if (strcmp(type->valuestring, "tts") == 0) {

            auto state = cJSON_GetObjectItem(root, "state");
            if (!cJSON_IsString(state)) return;

            if (strcmp(state->valuestring, "start") == 0) {
                Schedule([this]() {
                    aborted_ = false;
                    if (device_state_ == kDeviceStateIdle ||
                        device_state_ == kDeviceStateListening) {
                        SetDeviceState(kDeviceStateSpeaking);
                    }
                });
            }

            else if (strcmp(state->valuestring, "stop") == 0) {
                Schedule([this]() {
                    if (device_state_ == kDeviceStateSpeaking) {
                        if (listening_mode_ == kListeningModeManualStop) {
                            SetDeviceState(kDeviceStateIdle);
                        } else {
                            SetDeviceState(kDeviceStateListening);
                        }
                    }
                });
            }

            else if (strcmp(state->valuestring, "sentence_start") == 0) {
                auto text = cJSON_GetObjectItem(root, "text");
                if (cJSON_IsString(text)) {
                    ESP_LOGI(TAG, "<< %s", text->valuestring);
                    Schedule([display, message = std::string(text->valuestring)]() {
                        display->SetChatMessage("assistant", message.c_str());
                    });
                }
            }
        }

      else if (strcmp(type->valuestring, "stt") == 0) {
    ESP_LOGI("SSE", "STT CALLBACK TRIGGERED");
    ESP_LOGI("STT_DEBUG", "STT callback TRIGGERED!");

    auto text = cJSON_GetObjectItem(root, "text");
    if (cJSON_IsString(text)) {

        std::string stt = text->valuestring;
    ESP_LOGI("STT_DEBUG", "STT content = %s", stt.c_str());

        // ================================
        //  FILTER: CHẶN CÂU RÁC STT
        // ================================
        const char* blocked[] = {
            "Hãy subscribe cho kênh La La School",
            "hãy subscribe cho kênh la la school",
            "Để không bỏ lỡ những video hấp dẫn",
            "video hấp dẫn",
            "la la school"
            "Cảm ơn các bạn đã theo dõi và hẹn gặp lại."
        };

        for (const char* phrase : blocked) {
            if (stt.find(phrase) != std::string::npos) {
                ESP_LOGW("FILTER", "Blocked noisy STT: %s", stt.c_str());
                return;   // ⚠️ STOP — không gửi sang Puppy, không hiển thị
            }
        }

        // ================================
        //  Nếu không bị block → xử lý bình thường
        // ================================
        ESP_LOGI(TAG, ">> %s", stt.c_str());
        SseServer::GetInstance().Broadcast(std::string("stt: ") + stt);
        Schedule([display, message = stt]() {
            display->SetChatMessage("user", message.c_str());
        });
    }
}


        else if (strcmp(type->valuestring, "llm") == 0) {
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(emotion)) {
                Schedule([display, emotion_str = std::string(emotion->valuestring)]() {
                    display->SetEmotion(emotion_str.c_str());
                });
            }
        }

        else if (strcmp(type->valuestring, "mcp") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            if (cJSON_IsObject(payload)) {
                McpServer::GetInstance().ParseMessage(payload);
            }
        }

        else if (strcmp(type->valuestring, "system") == 0) {
            auto command = cJSON_GetObjectItem(root, "command");
            if (cJSON_IsString(command)) {
                ESP_LOGI(TAG, "System command: %s", command->valuestring);

                if (strcmp(command->valuestring, "reboot") == 0) {
                    Schedule([this]() { Reboot(); });
                }
            }
        }

        else if (strcmp(type->valuestring, "alert") == 0) {
            auto status = cJSON_GetObjectItem(root, "status");
            auto message = cJSON_GetObjectItem(root, "message");
            auto emotion = cJSON_GetObjectItem(root, "emotion");

            if (cJSON_IsString(status) &&
                cJSON_IsString(message) &&
                cJSON_IsString(emotion)) {

                Alert(status->valuestring,
                      message->valuestring,
                      emotion->valuestring,
                      Lang::Sounds::OGG_VIBRATION);
            }
        }

        else {
            ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
        }
    });

    // Start protocol
    bool protocol_started = protocol_->Start();


    SystemInfo::PrintHeapStats();

    // ===============================
    // START SSE SERVER HERE
    // ===============================
    SseServer::GetInstance().Start();

    SetDeviceState(kDeviceStateIdle);

    has_server_time_ = ota.HasServerTime();


    if (protocol_started) {
        std::string message = std::string(Lang::Strings::VERSION) +
                              ota.GetCurrentVersion();
        display->ShowNotification(message.c_str());
        display->SetChatMessage("system", "");

        audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
    }
}
// Add async task to Main Loop
void Application::Schedule(std::function<void()> callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

// ===============================
//      MAIN EVENT LOOP
// ===============================
void Application::MainEventLoop() {
    while (true) {
        auto bits = xEventGroupWaitBits(
            event_group_,
            MAIN_EVENT_SCHEDULE |
            MAIN_EVENT_SEND_AUDIO |
            MAIN_EVENT_WAKE_WORD_DETECTED |
            MAIN_EVENT_VAD_CHANGE |
            MAIN_EVENT_CLOCK_TICK |
            MAIN_EVENT_ERROR,
            pdTRUE,
            pdFALSE,
            portMAX_DELAY
        );

        // --------- ERROR HANDLING ----------
        if (bits & MAIN_EVENT_ERROR) {
            SetDeviceState(kDeviceStateIdle);
            Alert(Lang::Strings::ERROR,
                  last_error_message_.c_str(),
                  "circle_xmark",
                  Lang::Sounds::OGG_EXCLAMATION);
        }

        // --------- SEND AUDIO PACKETS -----------
        if (bits & MAIN_EVENT_SEND_AUDIO) {
            while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                if (protocol_ && !protocol_->SendAudio(std::move(packet))) {
                    break;
                }
            }
        }

        // --------- WAKE WORD DETECTED ----------
        if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
            OnWakeWordDetected();
        }

        // --------- VAD (voice detection) ---------
        if (bits & MAIN_EVENT_VAD_CHANGE) {
            if (device_state_ == kDeviceStateListening) {
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged();
            }
        }

        // --------- SCHEDULED TASKS ----------
        if (bits & MAIN_EVENT_SCHEDULE) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();

            for (auto& task : tasks) {
                task();
            }
        }

        // --------- CLOCK TICK 1s ----------
        if (bits & MAIN_EVENT_CLOCK_TICK) {
            clock_ticks_++;
            auto display = Board::GetInstance().GetDisplay();
            display->UpdateStatusBar();

            if (clock_ticks_ % 10 == 0) {
                SystemInfo::PrintHeapStats();
            }
        }
    }
}

// ===============================
//     WAKE WORD HANDLER
// ===============================
void Application::OnWakeWordDetected() {
    if (!protocol_) return;

    if (device_state_ == kDeviceStateIdle) {
        audio_service_.EncodeWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                audio_service_.EnableWakeWordDetection(true);
                return;
            }
        }

        auto wake_word = audio_service_.GetLastWakeWord();
        ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());

#if CONFIG_SEND_WAKE_WORD_DATA
        while (auto packet = audio_service_.PopWakeWordPacket()) {
            protocol_->SendAudio(std::move(packet));
        }
        protocol_->SendWakeWordDetected(wake_word);
        SetListeningMode(aec_mode_ == kAecOff ?
            kListeningModeAutoStop : kListeningModeRealtime);
#else
        SetListeningMode(aec_mode_ == kAecOff ?
            kListeningModeAutoStop : kListeningModeRealtime);
        audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
#endif
    }
    else if (device_state_ == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonWakeWordDetected);
    }
    else if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
    }
}

// =================================
//       ABORT SPEAKING
// =================================
void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;

    if (protocol_) {
        protocol_->SendAbortSpeaking(reason);
    }
}

// =================================
//      SET LISTENING MODE
// =================================
void Application::SetListeningMode(ListeningMode mode) {
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

// =================================
//        DEVICE STATE MACHINE
// =================================
void Application::SetDeviceState(DeviceState state) {
    if (device_state_ == state) return;

    clock_ticks_ = 0;
    auto previous_state = device_state_;
    device_state_ = state;

    ESP_LOGI(TAG, "STATE: %s", STATE_STRINGS[state]);

    DeviceStateEventManager::GetInstance().PostStateChangeEvent(
        previous_state,
        state
    );

    auto display = Board::GetInstance().GetDisplay();
    auto led = Board::GetInstance().GetLed();
    led->OnStateChanged();

    switch (state) {
        case kDeviceStateIdle:
            display->SetStatus(Lang::Strings::STANDBY);
            display->SetEmotion("neutral");
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(true);
            break;

        case kDeviceStateConnecting:
            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");
            break;

        case kDeviceStateListening:
            display->SetStatus(Lang::Strings::LISTENING);
            display->SetEmotion("neutral");

            if (!audio_service_.IsAudioProcessorRunning()) {
                protocol_->SendStartListening(listening_mode_);
                audio_service_.EnableVoiceProcessing(true);
                audio_service_.EnableWakeWordDetection(false);
            }
            break;

        case kDeviceStateSpeaking:
            display->SetStatus(Lang::Strings::SPEAKING);

            if (listening_mode_ != kListeningModeRealtime) {
                audio_service_.EnableVoiceProcessing(false);
                audio_service_.EnableWakeWordDetection(
                    audio_service_.IsAfeWakeWord());
            }
            audio_service_.ResetDecoder();
            break;

        default:
            // other states remain unchanged
            break;
    }
}
// =====================================
//               REBOOT
// =====================================
void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }

    protocol_.reset();
    audio_service_.Stop();

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}


// =====================================
//         FIRMWARE UPGRADE LOGIC
// =====================================
bool Application::UpgradeFirmware(Ota& ota, const std::string& url) {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();

    std::string upgrade_url =
        url.empty() ? ota.GetFirmwareUrl() : url;
    std::string version_info =
        url.empty() ? ota.GetFirmwareVersion() : "(Manual upgrade)";

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        ESP_LOGI(TAG, "Closing audio channel before firmware upgrade");
        protocol_->CloseAudioChannel();
    }

    ESP_LOGI(TAG, "Starting firmware upgrade from: %s",
             upgrade_url.c_str());

    Alert(Lang::Strings::OTA_UPGRADE,
          Lang::Strings::UPGRADING,
          "download",
          Lang::Sounds::OGG_UPGRADE);

    vTaskDelay(pdMS_TO_TICKS(3000));
    SetDeviceState(kDeviceStateUpgrading);

    display->SetChatMessage("system",
        (std::string(Lang::Strings::NEW_VERSION) + version_info).c_str());

    board.SetPowerSaveMode(false);
    audio_service_.Stop();
    vTaskDelay(pdMS_TO_TICKS(1000));

    bool upgrade_success =
        ota.StartUpgradeFromUrl(upgrade_url,
            [display](int progress, size_t speed) {
                std::thread([display, progress, speed]() {
                    char buffer[32];
                    snprintf(buffer, sizeof(buffer),
                             "%d%% %uKB/s", progress,
                             speed / 1024);
                    display->SetChatMessage("system", buffer);
                }).detach();
            });

    if (!upgrade_success) {
        ESP_LOGE(TAG,
            "Firmware upgrade failed, restarting audio service...");

        audio_service_.Start();
        board.SetPowerSaveMode(true);

        Alert(Lang::Strings::ERROR,
              Lang::Strings::UPGRADE_FAILED,
              "circle_xmark",
              Lang::Sounds::OGG_EXCLAMATION);

        vTaskDelay(pdMS_TO_TICKS(3000));
        return false;
    }

    ESP_LOGI(TAG, "Firmware upgrade successful, rebooting...");
    display->SetChatMessage("system",
                            "Upgrade successful, rebooting...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    Reboot();
    return true;
}


// =====================================
//     TRIGGER WAKE WORD PROGRAMMATICALLY
// =====================================
void Application::WakeWordInvoke(const std::string& wake_word) {
    if (!protocol_) return;

    if (device_state_ == kDeviceStateIdle) {

        audio_service_.EncodeWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                audio_service_.EnableWakeWordDetection(true);
                return;
            }
        }

        ESP_LOGI(TAG, "Wake word invoked: %s", wake_word.c_str());

#if CONFIG_USE_AFE_WAKE_WORD || CONFIG_USE_CUSTOM_WAKE_WORD
        while (auto packet = audio_service_.PopWakeWordPacket()) {
            protocol_->SendAudio(std::move(packet));
        }
        protocol_->SendWakeWordDetected(wake_word);
        SetListeningMode(
            aec_mode_ == kAecOff
            ? kListeningModeAutoStop
            : kListeningModeRealtime);
#else
        SetListeningMode(
            aec_mode_ == kAecOff
            ? kListeningModeAutoStop
            : kListeningModeRealtime);
        audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
#endif
    }
    else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() { AbortSpeaking(kAbortReasonNone); });
    }
    else if (device_state_ == kDeviceStateListening) {
        Schedule([this]() {
            if (protocol_) protocol_->CloseAudioChannel();
        });
    }
}


// =====================================
//       CAN ENTER SLEEP MODE?
// =====================================
bool Application::CanEnterSleepMode() {
    if (device_state_ != kDeviceStateIdle) return false;

    if (protocol_ && protocol_->IsAudioChannelOpened()) return false;

    if (!audio_service_.IsIdle()) return false;

    return true;
}


// =====================================
//       SEND MCP MESSAGE
// =====================================
void Application::SendMcpMessage(const std::string& payload) {
    if (!protocol_) return;

    if (xTaskGetCurrentTaskHandle() == main_event_loop_task_handle_) {
        protocol_->SendMcpMessage(payload);
    }
    else {
        Schedule([this, payload]() {
            protocol_->SendMcpMessage(payload);
        });
    }
}


// =====================================
//       SEND TEXT TO SERVER (STT)
//    (Mobile app POST /chat calls this)
// =====================================
void Application::SendTextToServer(const std::string& text) {
    if (xTaskGetCurrentTaskHandle() == main_event_loop_task_handle_) {

        if (!protocol_) {
            ESP_LOGW(TAG, "SendTextToServer: protocol not initialized");
            return;
        }

        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "session_id",
                                protocol_->session_id().c_str());
        cJSON_AddStringToObject(root, "type", "stt");
        cJSON_AddStringToObject(root, "text", text.c_str());

        char* payload = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);

        if (payload) {
            protocol_->SendRawText(payload);
            free(payload);
        }
    }
    else {
        Schedule([text]() {
            Application::GetInstance().SendTextToServer(text);
        });
    }
}


// =====================================
//       AEC MODE CHANGE
// =====================================
void Application::SetAecMode(AecMode mode) {
    aec_mode_ = mode;

    Schedule([this]() {
        auto display = Board::GetInstance().GetDisplay();

        switch (aec_mode_) {
            case kAecOff:
                audio_service_.EnableDeviceAec(false);
                display->ShowNotification(
                    Lang::Strings::RTC_MODE_OFF);
                break;

            case kAecOnServerSide:
                audio_service_.EnableDeviceAec(false);
                display->ShowNotification(
                    Lang::Strings::RTC_MODE_ON);
                break;

            case kAecOnDeviceSide:
                audio_service_.EnableDeviceAec(true);
                display->ShowNotification(
                    Lang::Strings::RTC_MODE_ON);
                break;
        }

        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
    });
}


// =====================================
//              PLAY SOUND
// =====================================
void Application::PlaySound(const std::string_view& sound) {
    audio_service_.PlaySound(sound);
}

