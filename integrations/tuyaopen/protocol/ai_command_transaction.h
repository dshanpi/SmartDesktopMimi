#ifndef MIMI_AI_COMMAND_TRANSACTION_H
#define MIMI_AI_COMMAND_TRANSACTION_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

/* The caller supplies a unique ID per attempt and monotonic milliseconds.
 * Transport delivery is not completion. Only accept() may mark success.
 * Runtime/audio ownership stays with the caller until confirmed success. */
typedef enum { MIMI_AI_TX_IDLE, MIMI_AI_TX_PENDING,
               MIMI_AI_TX_SUCCEEDED, MIMI_AI_TX_FAILED } mimi_ai_tx_state;
typedef enum { MIMI_AI_ENTER, MIMI_AI_EXIT } mimi_ai_command;
typedef struct {
    mimi_ai_tx_state state;
    mimi_ai_command command;
    char id[65];
    int64_t deadline_ms;
    int error;
} mimi_ai_transaction;

static inline bool mimi_ai_id_valid(const char *id)
{
    if (!id || !*id) return false;
    size_t n;
    for (n = 0; id[n]; ++n) {
        unsigned char c = (unsigned char)id[n];
        if (n >= 64 || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    }
    return true;
}

static inline bool mimi_ai_tx_begin(mimi_ai_transaction *tx, mimi_ai_command command,
                                   const char *id, int64_t now_ms, int64_t timeout_ms)
{
    if (!tx || tx->state == MIMI_AI_TX_PENDING || !mimi_ai_id_valid(id) ||
        (command != MIMI_AI_ENTER && command != MIMI_AI_EXIT) ||
        now_ms < 0 || timeout_ms <= 0 || now_ms > INT64_MAX - timeout_ms ||
        (tx->id[0] && strcmp(tx->id, id) == 0)) return false;
    tx->command = command;
    memcpy(tx->id, id, strlen(id) + 1);
    tx->deadline_ms = now_ms + timeout_ms;
    tx->error = 0;
    tx->state = MIMI_AI_TX_PENDING;
    return true;
}

static inline bool mimi_ai_tx_timeout(mimi_ai_transaction *tx, int64_t now_ms)
{
    if (!tx || tx->state != MIMI_AI_TX_PENDING || now_ms < tx->deadline_ms) return false;
    tx->error = -ETIMEDOUT;
    tx->state = MIMI_AI_TX_FAILED;
    return true;
}

static inline void mimi_ai_tx_send_failed(mimi_ai_transaction *tx, int error)
{
    if (!tx || tx->state != MIMI_AI_TX_PENDING) return;
    tx->error = error < 0 ? error : -EIO;
    tx->state = MIMI_AI_TX_FAILED;
}

static inline bool mimi_ai_tx_accept(mimi_ai_transaction *tx, mimi_ai_command command,
                                    const char *id, int result, int64_t now_ms)
{
    if (!tx || tx->state != MIMI_AI_TX_PENDING || now_ms < 0) return false;
    if (mimi_ai_tx_timeout(tx, now_ms)) return false;
    if (!mimi_ai_id_valid(id) || tx->command != command || strcmp(tx->id, id)) return false;
    tx->error = result;
    tx->state = result == 0 ? MIMI_AI_TX_SUCCEEDED : MIMI_AI_TX_FAILED;
    return true;
}
#endif
