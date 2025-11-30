#ifndef STRUCT_H
#define STRUCT_H

#include <pthread.h>

// 채팅 모드 정의 >> 1:1, 그룹 통합으로 변경
#define MODE_MAIN 0
#define MODE_PUBLIC_CHAT 1
#define MODE_ROOM_WAIT 2
#define MODE_ROOM_CHAT 3 

#define MAX_ROOM_MEMBERS 20
#define MAX_ROOM_NAME_LEN 64

// 클라이언트 연결 정보 구조체
typedef struct Client {
    int fd;
    char ip[16];
    char user[64];
    int chat_mode;
    char p2p_target[64];
    struct Client *next;
} Client;

// 채팅방 정보 구조체
typedef struct ChatRoom {
    char name[MAX_ROOM_NAME_LEN];
    char members[MAX_ROOM_MEMBERS][64];
    int member_count;
    struct ChatRoom *next;
    pthread_mutex_t member_mutex;
} ChatRoom;

// 전역 변수
extern Client *client_head;
extern ChatRoom *room_head;
extern pthread_mutex_t client_mutex;
extern pthread_mutex_t room_mutex;

// 클라이언트 관리 함수
Client *create_client(int fd, const char *ip);
void insert_client(Client *cli);
Client *find_client_by_name(const char *user);
void remove_client(Client *cli);

// 채팅방 관리 함수
ChatRoom *create_room(const char *name);
void insert_room(ChatRoom *room);
ChatRoom *find_room_by_name(const char *name);
ChatRoom *find_room_by_member(const char *user);

int delete_room(const char *name);
char *get_room_list();

// 멤버 관리 함수
int add_member_to_room(ChatRoom *room, Client *cli);
int remove_member_from_room(ChatRoom *room, const char *user);
void broadcast_room_message_ex(ChatRoom *room, const char *msg, int exclude_fd);
char *get_room_members_list_ex(ChatRoom *room);

#endif