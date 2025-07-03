#ifndef __NERTC_WAKE_UP_EVENT_H__
#define __NERTC_WAKE_UP_EVENT_H__


#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void * nertc_wakeup_sdk_t;
typedef void * nertc_wakeup_sdk_user_data_t;

typedef struct nertc_wakeup_sdk_callback_context {
  nertc_wakeup_sdk_t wakeup;        /**< wakeup 实例 */
  nertc_wakeup_sdk_user_data_t user_data;  /**< 用户数据 */
} nertc_wakeup_sdk_callback_context_t;

typedef struct nertc_wakeup_sdk_event_handler {
/**
  *  检测到唤醒词的回调。
  * <br>该回调方法表示 SDK 检测到了唤醒词
  * 干预或提示用户。
  * @param ctx 回调上下文
  * @param wake_word 对应的唤醒词
  * @endif
  */
void (*on_wake_word_detected)(const nertc_wakeup_sdk_callback_context_t* ctx, const char *wake_word);
} nertc_wakeup_sdk_event_handle_t;

typedef struct nertc_wakeup_sdk_config {
  nertc_wakeup_sdk_event_handle_t event_handler;
  nertc_wakeup_sdk_user_data_t user_data;
  char* appkey;
  char* deviceId;
} nertc_wakeup_sdk_config_t;

#ifdef __cplusplus
}
#endif

#endif // __NERTC_SDK_EVENT_H__