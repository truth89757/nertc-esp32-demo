#include "multinet_afe_wake_word.h"
#include "application.h"

#include <esp_log.h>
#include <model_path.h>
#include <arpa/inet.h>
#include <sstream>

#define DETECTION_RUNNING_EVENT 1

#define TAG "MultinetAfeWakeWord"

void on_wake_word_detected_handle(const nertc_wakeup_sdk_callback_context_t* ctx, const char *wake_word)
{
    auto* multinet = static_cast<MultinetAfeWakeWord*>(ctx->user_data);
    //ESP_LOGI("test", "wake_word:%s", wake_word);
    std::string call_back_wake_word = std::string(wake_word);
    multinet->DoCallBack(call_back_wake_word);
}
MultinetAfeWakeWord::MultinetAfeWakeWord()
    : wake_word_pcm_(),
      wake_word_opus_() {
    event_group_ = xEventGroupCreate();
}

MultinetAfeWakeWord::~MultinetAfeWakeWord() {
    if (wake_word_encode_task_stack_ != nullptr) {
        heap_caps_free(wake_word_encode_task_stack_);
    }
    if(nertc_wake_word_)
    {
        nertc_wakeup_destory(nertc_wake_word_);
    }

    vEventGroupDelete(event_group_);
}

void MultinetAfeWakeWord::Initialize(AudioCodec* codec) {
    codec_ = codec;
    nertc_wakeup_sdk_config_t config;
    nertc_wakeup_sdk_event_handle_t handle =
    {
        .on_wake_word_detected = on_wake_word_detected_handle
    };
    config.event_handler = handle;
    config.user_data = this;
    nertc_wake_word_ = nertc_wakeup_create(&config); 
    nertc_wakeup_init(nertc_wake_word_, codec_->input_channels(), codec_->input_reference());
    xTaskCreate([](void* arg) {
        auto this_ = (MultinetAfeWakeWord*)arg;
        this_->AudioDetectionTask();
        vTaskDelete(NULL);
    }, "audio_detection", 4096, this, 3, nullptr);
}

void MultinetAfeWakeWord::OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) {
    wake_up_call_back_ = callback;
}

void MultinetAfeWakeWord::DoCallBack(std::string& wake_word)
{
    last_detected_wake_word_ = wake_word;
    wake_up_call_back_(last_detected_wake_word_);
}

void MultinetAfeWakeWord::StartDetection() {
    xEventGroupSetBits(event_group_, DETECTION_RUNNING_EVENT);
}

void MultinetAfeWakeWord::StopDetection() {
    xEventGroupClearBits(event_group_, DETECTION_RUNNING_EVENT);
    if (nertc_wake_word_ != nullptr) {
        nertc_wakeup_stop_detect(nertc_wake_word_);
    }
}

bool MultinetAfeWakeWord::IsDetectionRunning() {
    return xEventGroupGetBits(event_group_) & DETECTION_RUNNING_EVENT;
}

void MultinetAfeWakeWord::Feed(const std::vector<int16_t>& data) {
    if(nertc_wake_word_ == nullptr) {
        return;
    }
    StoreWakeWordData(data.data(), data.size());

    nertc_wakeup_feed(nertc_wake_word_, data.data(), data.size());
}

size_t MultinetAfeWakeWord::GetFeedSize() {
    if (nertc_wake_word_ == nullptr) {
        return 0;
    }
    return (size_t)nertc_wakeup_get_feed_size(nertc_wake_word_);
}

void MultinetAfeWakeWord::AudioDetectionTask() {
    auto fetch_size = nertc_wakeup_get_feed_chunk_size(nertc_wake_word_);
    auto feed_size = nertc_wakeup_get_fetch_chunk_size(nertc_wake_word_);
    ESP_LOGI(TAG, "Audio detection task started, feed size: %d fetch size: %d",
        feed_size, fetch_size);

    while (true) 
    {
        xEventGroupWaitBits(event_group_, DETECTION_RUNNING_EVENT, pdFALSE, pdTRUE, portMAX_DELAY);
        nertc_wakeup_detect(nertc_wake_word_);
    }
}

void MultinetAfeWakeWord::StoreWakeWordData(const int16_t* data, size_t samples) {
    // store audio data to wake_word_pcm_
    std::lock_guard<std::mutex> lock(wake_word_pcm_mutex_);
    wake_word_pcm_.emplace_back(std::vector<int16_t>(data, data + samples));
    // keep about 2 seconds of data, detect duration is 30ms (sample_rate == 16000, chunksize == 512)
    while (wake_word_pcm_.size() > 2000 / 30) {
        wake_word_pcm_.pop_front();
    }
}

void MultinetAfeWakeWord::EncodeWakeWordData() {
    wake_word_opus_.clear();
    if (wake_word_encode_task_stack_ == nullptr) {
        wake_word_encode_task_stack_ = (StackType_t*)heap_caps_malloc(4096 * 8, MALLOC_CAP_SPIRAM);
    }
    wake_word_encode_task_ = xTaskCreateStatic([](void* arg) {
        auto this_ = (MultinetAfeWakeWord*)arg;
        {
            auto start_time = esp_timer_get_time();
            auto encoder = std::make_unique<OpusEncoderWrapper>(16000, 1, OPUS_FRAME_DURATION_MS);
            encoder->SetComplexity(0); // 0 is the fastest

            int packets = 0;
            std::lock_guard<std::mutex> lock_(this_->wake_word_pcm_mutex_);
            for (auto& pcm: this_->wake_word_pcm_) {
                encoder->Encode(std::move(pcm), [this_](std::vector<uint8_t>&& opus) {
                    std::lock_guard<std::mutex> lock(this_->wake_word_mutex_);
                    this_->wake_word_opus_.emplace_back(std::move(opus));
                    this_->wake_word_cv_.notify_all();
                });
                packets++;
            }
            this_->wake_word_pcm_.clear();

            auto end_time = esp_timer_get_time();
            ESP_LOGI(TAG, "Encode wake word opus %d packets in %ld ms", packets, (long)((end_time - start_time) / 1000));

            std::lock_guard<std::mutex> lock(this_->wake_word_mutex_);
            this_->wake_word_opus_.push_back(std::vector<uint8_t>());
            this_->wake_word_cv_.notify_all();
        }
        vTaskDelete(NULL);
    }, "encode_detect_packets", 4096 * 8, this, 2, wake_word_encode_task_stack_, &wake_word_encode_task_buffer_);
}

bool MultinetAfeWakeWord::GetWakeWordOpus(std::vector<uint8_t>& opus) {
    std::unique_lock<std::mutex> lock(wake_word_mutex_);
    wake_word_cv_.wait(lock, [this]() {
        return !wake_word_opus_.empty();
    });
    opus.swap(wake_word_opus_.front());
    wake_word_opus_.pop_front();
    return !opus.empty();
}
