#ifndef MIMI_CONNECTION_H
#define MIMI_CONNECTION_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "ipc/ipc_socket.h"
typedef struct mimi_connection mimi_connection;
/* Successful creation consumes fd. Failure leaves it with the caller. */
mimi_connection *mimi_connection_create(int fd);
/* Acquire only while holding an existing reference or its owning slot lock. */
int mimi_connection_retain(mimi_connection *c);
void mimi_connection_release(mimi_connection *c);
void mimi_connection_stop(mimi_connection *c);
bool mimi_connection_stopped(mimi_connection *c);
/* Borrowed fd remains owned by c; caller holds a reference and must not close. */
int mimi_connection_fd(mimi_connection *c);
int mimi_connection_send(mimi_connection *c,uint32_t topic,const void *data,size_t size,uint32_t flags,unsigned timeout_ms);
/* Returns a complete frame size, zero for incomplete/busy, or -1 on fatal error. */
int mimi_connection_receive(mimi_connection *c,ipc_msg_t *out,unsigned frame_timeout_ms);
#endif
