#ifndef REQUEST_H
#define REQUEST_H
#include <pthread.h>

extern pthread_mutex_t connection_lock;
extern pthread_cond_t connection_cond;
extern int connection_count;
extern char *web_root;
void handle_request(int client_socket);

#endif // REQUEST_H
