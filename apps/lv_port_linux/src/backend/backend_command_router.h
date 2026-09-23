#ifndef BACKEND_COMMAND_ROUTER_H
#define BACKEND_COMMAND_ROUTER_H

/*
 * Application-layer adapter for UI commands.
 *
 * The process entry point only composes the backend. Command decoding and
 * dispatch to system services live here so transport/lifecycle concerns do
 * not accumulate in backend_main.c.
 */
int backend_command_router_register(void);

#endif /* BACKEND_COMMAND_ROUTER_H */
