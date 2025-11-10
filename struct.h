#ifndef STRUCT_H
#define STRUCT_H

#include <pthread.h>

// 클라이언트 정보 구조체
typedef struct Client {
    int fd;                 // 소켓 fd
    char user[64];          // 로그인한 사용자의 ID
    pthread_t th;           // 스레드 ID
    struct Client *next;    // 다음 클라이언트를 가리키는 포인터
} Client;

extern Client *head;
extern Client *tail;

// 함수 선언
Client *create_client(int fd);
void insert_client(Client *c);
void remove_client_by_fd(int fd);
Client *find_client_by_name(const char *name);
Client *find_client_by_fd(int fd);
void broadcast_message(const char *msg);
void broadcast_system_message(const char *msg, int except_fd);

#endif
