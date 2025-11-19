/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#if !defined(RUUVI_TLOG_H_)
#define RUUVI_TLOG_H_

#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/kernel/thread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TLOG_ERR(fmt, ...) /* NOSONAR */ \
    do \
    { \
        if (!Z_LOG_CONST_LEVEL_CHECK(LOG_LEVEL_ERR)) \
        { \
            break; \
        } \
        struct k_thread* const current_thread  = k_current_get(); \
        const k_tid_t          thread_id       = k_current_get(); \
        const int              thread_priority = k_thread_priority_get(current_thread); \
        const char*            thread_name     = k_thread_name_get(current_thread); \
        if (NULL == thread_name) \
        { \
            thread_name = "unknown"; \
        } \
        LOG_ERR("[%s/%p/%d] " fmt, thread_name, thread_id, thread_priority __VA_OPT__(, ) __VA_ARGS__); \
    } while (0)

#define TLOG_WRN(fmt, ...) /* NOSONAR */ \
    do \
    { \
        if (!Z_LOG_CONST_LEVEL_CHECK(LOG_LEVEL_WRN)) \
        { \
            break; \
        } \
        struct k_thread* const current_thread  = k_current_get(); \
        const k_tid_t          thread_id       = k_current_get(); \
        const int              thread_priority = k_thread_priority_get(current_thread); \
        const char*            thread_name     = k_thread_name_get(current_thread); \
        if (NULL == thread_name) \
        { \
            thread_name = "unknown"; \
        } \
        LOG_WRN("[%s/%p/%d] " fmt, thread_name, thread_id, thread_priority __VA_OPT__(, ) __VA_ARGS__); \
    } while (0)

#define TLOG_INF(fmt, ...) /* NOSONAR */ \
    do \
    { \
        if (!Z_LOG_CONST_LEVEL_CHECK(LOG_LEVEL_INF)) \
        { \
            break; \
        } \
        struct k_thread* const current_thread  = k_current_get(); \
        const k_tid_t          thread_id       = k_current_get(); \
        const int              thread_priority = k_thread_priority_get(current_thread); \
        const char*            thread_name     = k_thread_name_get(current_thread); \
        if (NULL == thread_name) \
        { \
            thread_name = "unknown"; \
        } \
        LOG_INF("[%s/%p/%d] " fmt, thread_name, thread_id, thread_priority __VA_OPT__(, ) __VA_ARGS__); \
    } while (0)

#define TLOG_DBG(fmt, ...) /* NOSONAR */ \
    do \
    { \
        if (!Z_LOG_CONST_LEVEL_CHECK(LOG_LEVEL_DBG)) \
        { \
            break; \
        } \
        struct k_thread* const current_thread  = k_current_get(); \
        const k_tid_t          thread_id       = k_current_get(); \
        const int              thread_priority = k_thread_priority_get(current_thread); \
        const char*            thread_name     = k_thread_name_get(current_thread); \
        if (NULL == thread_name) \
        { \
            thread_name = "unknown"; \
        } \
        LOG_DBG("[%s/%p/%d] " fmt, thread_name, thread_id, thread_priority __VA_OPT__(, ) __VA_ARGS__); \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* RUUVI_TLOG_H_ */
