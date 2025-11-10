#include "struct.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>

Client *head = NULL;
Client *tail = NULL;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

// 새 클라이언트 생성
Client *create_client(int fd){
    Client *pt = (Client *)malloc(sizeof(Client));
    if(!pt) return NULL;
    pt->fd = fd;
    pt->user[0] = '\0';
    pt->th = 0;
    pt->next = NULL;
    return pt;
}

// 리스트에 추가
void insert_client(Client *c) {
    pthread_mutex_lock(&clients_mutex);
    if(!head) head = tail = c;
    else { tail->next = c; tail = c; }
    pthread_mutex_unlock(&clients_mutex);
}

// fd로 삭제
void remove_client_by_fd(int fd) {
    pthread_mutex_lock(&clients_mutex);
    Client *prev = NULL;
    Client *cur = head;
    while(cur) {
        if(cur->fd == fd) {
            if(prev) prev->next = cur->next;
            else head = cur->next;
            if(cur == tail) tail = prev;
            close(cur->fd);
            free(cur);
            break;
        }
        prev = cur;
        cur = cur->next;
    }
    pthread_mutex_unlock(&clients_mutex);
}

// 이름으로 찾기
Client *find_client_by_name(const char *name) {
    pthread_mutex_lock(&clients_mutex);
    Client *cur = head;
    while(cur) {
        if(cur->user[0] != '\0' && strcmp(cur->user, name) == 0) {
            pthread_mutex_unlock(&clients_mutex);
            return cur;
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&clients_mutex);
    return NULL;
}

// fd로 찾기
Client *find_client_by_fd(int fd) {
    pthread_mutex_lock(&clients_mutex);
    Client *cur = head;
    while(cur) {
        if(cur->fd == fd) {
            pthread_mutex_unlock(&clients_mutex);
            return cur;
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&clients_mutex);
    return NULL;
}

// 모든 클라이언트에 메시지 전송
void broadcast_message(const char *msg) {
    pthread_mutex_lock(&clients_mutex);
    Client *cur = head;
    while(cur) {
        if(cur->fd > 0) send(cur->fd, msg, strlen(msg), 0);
        cur = cur->next;
    }
    pthread_mutex_unlock(&clients_mutex);
}

// 특정 fd 제외하고 전송
void broadcast_system_message(const char *msg, int except_fd) {
    pthread_mutex_lock(&clients_mutex);
    Client *cur = head;
    while(cur) {
        if(cur->fd > 0 && cur->fd != except_fd)
            send(cur->fd, msg, strlen(msg), 0);
        cur = cur->next;
    }
    pthread_mutex_unlock(&clients_mutex);
}
