#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include "struct.h"

Client *client_head = NULL;
ChatRoom *room_head = NULL;
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t room_mutex = PTHREAD_MUTEX_INITIALIZER;

Client *create_client(int fd, const char *ip) {
    Client *cli = malloc(sizeof(Client));
    if (!cli) return NULL;
    cli->fd = fd;
    strncpy(cli->ip, ip, sizeof(cli->ip)-1);
    cli->ip[sizeof(cli->ip)-1] = '\0';
    cli->user[0] = '\0';
    cli->chat_mode = MODE_MAIN;
    cli->p2p_target[0] = '\0'; 
    cli->next = NULL;
    return cli;
}

void insert_client(Client *cli) {
    pthread_mutex_lock(&client_mutex);
    cli->next = client_head;
    client_head = cli;
    pthread_mutex_unlock(&client_mutex);
}

Client *find_client_by_name(const char *user) {
    if (!user || user[0] == '\0') return NULL;
    pthread_mutex_lock(&client_mutex);
    Client *cur = client_head;
    while (cur) {
        if (cur->user[0] != '\0' && strcmp(cur->user, user) == 0) {
            pthread_mutex_unlock(&client_mutex);
            return cur;
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&client_mutex);
    return NULL;
}

void remove_client(Client *cli) {
    if (!cli) return;
    pthread_mutex_lock(&client_mutex);
    Client *cur = client_head;
    Client *prev = NULL;
    while (cur) {
        if (cur == cli) {
            if (prev) prev->next = cur->next;
            else client_head = cur->next;
            break;
        }
        prev = cur;
        cur = cur->next;
    }
    pthread_mutex_unlock(&client_mutex);
}

ChatRoom *create_room(const char *name) {
    ChatRoom *room = malloc(sizeof(ChatRoom));
    if (!room) return NULL;
    strncpy(room->name, name, sizeof(room->name)-1);
    room->name[sizeof(room->name)-1] = '\0';
    room->member_count = 0;
    pthread_mutex_init(&room->member_mutex, NULL);
    room->next = NULL;
    return room;
}

void insert_room(ChatRoom *room) {
    pthread_mutex_lock(&room_mutex);
    room->next = room_head;
    room_head = room;
    pthread_mutex_unlock(&room_mutex);
}

ChatRoom *find_room_by_name(const char *name) {
    if (!name) return NULL;
    pthread_mutex_lock(&room_mutex);
    ChatRoom *cur = room_head;
    while (cur) {
        if (strcmp(cur->name, name) == 0) {
            pthread_mutex_unlock(&room_mutex);
            return cur;
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&room_mutex);
    return NULL;
}

int delete_room(const char *name) {
    if (strcmp(name, "PUBLIC_CHAT") == 0) return 0; 

    pthread_mutex_lock(&room_mutex);
    ChatRoom *cur = room_head;
    ChatRoom *prev = NULL;

    while (cur) {
        if (strcmp(cur->name, name) == 0) {
            if (prev) prev->next = cur->next;
            else room_head = cur->next;
            
            pthread_mutex_destroy(&cur->member_mutex);
            free(cur);
            pthread_mutex_unlock(&room_mutex);
            return 1;
        }
        prev = cur;
        cur = cur->next;
    }
    pthread_mutex_unlock(&room_mutex);
    return 0;
}

char *get_room_list() {
    char *list = malloc(4096);
    if (!list) return NULL;
    list[0] = '\0';

    pthread_mutex_lock(&room_mutex);
    ChatRoom *cur = room_head;
    int count = 0;

    while (cur) {
        if (strcmp(cur->name, "PUBLIC_CHAT") != 0) {
            char buf[128];
            snprintf(buf, sizeof(buf), "[%s] (참여자: %d명)\n", cur->name, cur->member_count);
            strcat(list, buf);
            count++;
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&room_mutex);

    if (count == 0) strcpy(list, "생성된 채팅방이 없습니다.\n");
    return list;
}

int add_member_to_room(ChatRoom *room, Client *cli) {
    if (!room || !cli || cli->user[0] == '\0') return 0;

    pthread_mutex_lock(&room->member_mutex);
    if (room->member_count >= MAX_ROOM_MEMBERS) {
        pthread_mutex_unlock(&room->member_mutex);
        return 0; 
    }

    for (int i = 0; i < room->member_count; i++) {
        if (strcmp(room->members[i], cli->user) == 0) {
            pthread_mutex_unlock(&room->member_mutex);
            return 1; 
        }
    }

    strncpy(room->members[room->member_count], cli->user, sizeof(room->members[0]) - 1);
    room->members[room->member_count][sizeof(room->members[0]) - 1] = '\0';
    room->member_count++;
    pthread_mutex_unlock(&room->member_mutex);
    return 1;
}

int remove_member_from_room(ChatRoom *room, const char *user) {
    if (!room || !user || user[0] == '\0') return 0;
    
    pthread_mutex_lock(&room->member_mutex);
    int index = -1;
    for (int i = 0; i < room->member_count; i++) {
        if (strcmp(room->members[i], user) == 0) {
            index = i;
            break;
        }
    }

    if (index != -1) {
        for (int i = index; i < room->member_count - 1; i++) {
            strncpy(room->members[i], room->members[i+1], sizeof(room->members[0]) - 1);
        }
        room->member_count--;
        pthread_mutex_unlock(&room->member_mutex);
        return 1;
    }
    
    pthread_mutex_unlock(&room->member_mutex);
    return 0;
}

void broadcast_room_message_ex(ChatRoom *room, const char *msg, int exclude_fd) {
    if (!room || !msg) return;

    pthread_mutex_lock(&room->member_mutex);
    for (int i = 0; i < room->member_count; i++) {
        Client *cur_cli = find_client_by_name(room->members[i]);
        if (cur_cli && cur_cli->fd != exclude_fd) {
            if (cur_cli->chat_mode == MODE_ROOM_CHAT || cur_cli->chat_mode == MODE_PUBLIC_CHAT) {
                send(cur_cli->fd, msg, strlen(msg), 0);
            }
        }
    }
    pthread_mutex_unlock(&room->member_mutex);
}

char *get_room_members_list_ex(ChatRoom *room) {
    if (!room) return strdup("Invalid Room");
    char *list = malloc(2048);
    if (!list) return strdup("Memory error");
    list[0] = '\0';
    
    pthread_mutex_lock(&room->member_mutex);
    size_t len = 0;
    for (int i = 0; i < room->member_count; i++) {
        if (len > 0) len += snprintf(list + len, 2048 - len, ", ");
        len += snprintf(list + len, 2048 - len, "%s", room->members[i]);
    }
    pthread_mutex_unlock(&room->member_mutex);

    if (len == 0) strcpy(list, "No members");
    return list;
}

ChatRoom *find_room_by_member(const char *user) {
    if (!user || user[0] == '\0') return NULL;
    pthread_mutex_lock(&room_mutex);
    ChatRoom *cur = room_head;
    while (cur) {
        if (strcmp(cur->name, "PUBLIC_CHAT") != 0) { 
            pthread_mutex_lock(&cur->member_mutex);
            for (int i = 0; i < cur->member_count; i++) {
                if (strcmp(cur->members[i], user) == 0) {
                    pthread_mutex_unlock(&cur->member_mutex);
                    pthread_mutex_unlock(&room_mutex);
                    return cur;
                }
            }
            pthread_mutex_unlock(&cur->member_mutex);
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&room_mutex);
    return NULL;
}