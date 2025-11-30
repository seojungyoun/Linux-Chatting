#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "struct.h"

#define PORT 5555
#define BUF_SIZE 2048

pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

static void timestamp_now(char *buf, size_t len) {
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    strftime(buf, len, "%Y%m%d_%H%M%S", &tm);
}

int check_user_credentials(const char *id, const char *pw) {
    pthread_mutex_lock(&file_mutex);
    FILE *f = fopen("users.txt", "r");
    if(!f){ pthread_mutex_unlock(&file_mutex); return 0; }

    char line[256];
    int ok = 0;

    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n'); if(nl) *nl = '\0';
        char *sep = strchr(line, ':'); if(!sep) continue;
        char temp_line[64]; strncpy(temp_line, line, sep - line); temp_line[sep - line] = '\0';

        if (pw[0] == '\0') {
            if (strcmp(temp_line, id) == 0) ok = 1;
        } else {
            if(strcmp(temp_line, id)==0 && strcmp(sep+1, pw)==0) ok = 1;
        }
    }

    fclose(f);
    pthread_mutex_unlock(&file_mutex);
    return ok;
}

int register_user(const char *id, const char *pw) {
    pthread_mutex_lock(&file_mutex);
    FILE *f = fopen("users.txt", "a+");
    if(!f) f = fopen("users.txt", "w+");
    if(!f){ pthread_mutex_unlock(&file_mutex); return 0; }

    rewind(f);
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n'); if(nl)*nl = '\0';
        char *sep = strchr(line, ':'); if(!sep) continue;
        char temp_line[64]; strncpy(temp_line, line, sep - line); temp_line[sep - line] = '\0';

        if(strcmp(temp_line, id)==0){
            fclose(f);
            pthread_mutex_unlock(&file_mutex);
            return 0;
        }
    }

    fprintf(f, "%s:%s\n", id, pw);
    fclose(f);
    pthread_mutex_unlock(&file_mutex);
    return 1;
}

int add_friend(const char *user, const char *friendid) {
    char fname[128];
    snprintf(fname, sizeof(fname), "%s_friends.txt", user);

    pthread_mutex_lock(&file_mutex);
    FILE *f = fopen(fname, "a+");
    if(!f) f = fopen(fname, "w+");
    if(!f){ pthread_mutex_unlock(&file_mutex); return 0; }

    rewind(f);
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n'); if(nl) *nl = '\0';
        if(strcmp(line, friendid)==0) {
            fclose(f);
            pthread_mutex_unlock(&file_mutex);
            return 0;
        }
    }

    fprintf(f, "%s\n", friendid);
    fclose(f);
    pthread_mutex_unlock(&file_mutex);
    return 1;
}

void log_public_chat(const char *from, const char *msg) {
    char ts[64]; timestamp_now(ts, sizeof(ts));
    pthread_mutex_lock(&file_mutex);
    FILE *f = fopen("public_chat.log", "a");
    if(f){ fprintf(f, "[%s] [%s] %s\n", ts, from, msg); fclose(f); }
    pthread_mutex_unlock(&file_mutex);
}

void send_to_fd(int fd, const char *s) { if(fd>0) send(fd, s, strlen(s), 0); }

void *handle_client_thread(void *arg) {
    Client *cli = (Client *)arg;
    int fd = cli->fd;
    char buf[BUF_SIZE];

    if (!find_room_by_name("PUBLIC_CHAT")) {
        ChatRoom *public_room = create_room("PUBLIC_CHAT");
        insert_room(public_room);
    }

    ssize_t n;
    while ((n = recv(fd, buf, sizeof(buf)-1, 0)) > 0) {
        buf[n] = '\0';
        char *nl = strchr(buf, '\n'); if(nl) *nl = '\0';
        
        char *save;
        char *cmd = strtok_r(buf, "|", &save);
        if(!cmd) continue;

        if(strcmp(cmd, "REQ_REGISTER")==0) {
            char *id = strtok_r(NULL, "|", &save);
            char *pw = strtok_r(NULL, "|", &save);
            if (!id || !pw) { send_to_fd(fd, "ERR_REGISTER_FAIL\n"); continue; }
            if(register_user(id, pw) ) send_to_fd(fd, "ACK_REGISTER_OK\n");
            else send_to_fd(fd, "ERR_ID_EXISTS\n");
        }

        else if(strcmp(cmd, "REQ_LOGIN")==0) {
            char *id = strtok_r(NULL, "|", &save);
            char *pw = strtok_r(NULL, "|", &save);
            if (!id || !pw) { send_to_fd(fd, "ERR_LOGIN_FAIL\n"); continue; }
            if(check_user_credentials(id, pw)){
                if (find_client_by_name(id)) { send_to_fd(fd, "ERR_LOGIN_FAIL\n"); continue; }
                strncpy(cli->user, id, sizeof(cli->user)-1);
                cli->user[sizeof(cli->user)-1] = '\0';
                send_to_fd(fd, "ACK_LOGIN_OK\n");
            } else send_to_fd(fd, "ERR_LOGIN_FAIL\n");
        }

        else if(strcmp(cmd, "REQ_LOGOUT")==0) { 
            ChatRoom *pub_room = find_room_by_name("PUBLIC_CHAT");
            if (pub_room && cli->user[0] != '\0' && remove_member_from_room(pub_room, cli->user)) {
                char out[256]; snprintf(out, sizeof(out), "SYSTEM|%s 님이 로그아웃했습니다.\n", cli->user);
                broadcast_room_message_ex(pub_room, out, fd);
            }
            ChatRoom *room = find_room_by_member(cli->user);
            if (room && remove_member_from_room(room, cli->user)) {
                char out[256]; snprintf(out, sizeof(out), "SYSTEM|%s 님이 로그아웃하여 방을 나갔습니다.\n", cli->user);
                broadcast_room_message_ex(room, out, fd);
            }
            
            cli->user[0] = '\0';
            cli->chat_mode = MODE_MAIN;
            cli->p2p_target[0] = '\0';
        }

        else if(strcmp(cmd, "REQ_CHAT_PUB_JOIN")==0) {
            if (cli->user[0] == '\0') { send_to_fd(fd, "SYSTEM|로그인 후 이용 가능합니다.\n"); continue; }
            ChatRoom *room = find_room_by_name("PUBLIC_CHAT");
            if (room) {
                cli->chat_mode = MODE_PUBLIC_CHAT;
                add_member_to_room(room, cli);
                char *members = get_room_members_list_ex(room);
                char out[2048]; snprintf(out, sizeof(out), "ACK_CHAT_PUB_JOIN_OK|%s\n", members);
                send_to_fd(fd, out);
                free(members);
                snprintf(out, sizeof(out), "SYSTEM|%s 님이 공개 채팅방에 입장했습니다.\n", cli->user);
                broadcast_room_message_ex(room, out, fd);
            } else send_to_fd(fd, "ERR_CHAT_PUB_JOIN_FAIL\n");
        }

        else if(strcmp(cmd, "REQ_CHAT_PUB_EXIT")==0) {
            ChatRoom *room = find_room_by_name("PUBLIC_CHAT");
            if (room && remove_member_from_room(room, cli->user)) {
                cli->chat_mode = MODE_MAIN;
                send_to_fd(fd, "ACK_CHAT_PUB_EXIT_OK\n");
                char out[256]; snprintf(out, sizeof(out), "SYSTEM|%s 님이 공개 채팅방을 나갔습니다.\n", cli->user);
                broadcast_room_message_ex(room, out, fd);
            } else send_to_fd(fd, "SYSTEM|공개 채팅방에서 나가는 데 실패했습니다.\n");
        }

        else if(strcmp(cmd, "REQ_CHAT_PUB")==0) {
            char *target = strtok_r(NULL, "|", &save); 
            char *msg = strtok_r(NULL, "\n", &save);
            if(cli->chat_mode != MODE_PUBLIC_CHAT){ send_to_fd(fd, "SYSTEM|현재 공개 채팅 모드가 아닙니다.\n"); continue; }
            ChatRoom *room = find_room_by_name("PUBLIC_CHAT");
            if (room) {
                char out[BUF_SIZE]; snprintf(out, sizeof(out), "PUB|%s|%s\n", cli->user, msg?msg:"");
                broadcast_room_message_ex(room, out, -1);
                log_public_chat(cli->user, msg?msg:"");
            }
        }

        else if(strcmp(cmd, "REQ_FRD_ADD")==0) {
            char *user = strtok_r(NULL, "|", &save);
            char *friendid = strtok_r(NULL, "\n", &save);
            if (!user || !friendid) { send_to_fd(fd, "ERR_FRD_ADD_FAIL|Bad args\n"); continue; }
            if(check_user_credentials(friendid, "")){
                if(add_friend(user, friendid)){
                    char out[128]; snprintf(out, sizeof(out), "ACK_FRD_ADD_OK|%s added %s\n", user, friendid);
                    send_to_fd(fd, out);
                } else send_to_fd(fd, "ERR_FRD_ADD_FAIL|Already friend\n");
            } else send_to_fd(fd, "ERR_FRD_ADD_FAIL|User not found\n");
        }

        else if(strcmp(cmd, "REQ_FRD_SEARCH")==0) {
            char *user_or_target = strtok_r(NULL, "|", &save);
            char *target = strtok_r(NULL, "\n", &save);
            if (!target) target = user_or_target;
            if (!target) { send_to_fd(fd, "ERR_FRD_SEARCH_FAIL|Bad args\n"); continue; }
            if(check_user_credentials(target, "")){
                char out[128]; snprintf(out, sizeof(out), "ACK_FRD_SEARCH_OK|%s exists\n", target);
                send_to_fd(fd, out);
            } else send_to_fd(fd, "ERR_FRD_SEARCH_FAIL|User not found\n");
        }

        else if(strcmp(cmd, "REQ_ROOM_START")==0) {
            char *room_name = strtok_r(NULL, "|", &save);
            char *members_str = strtok_r(NULL, "\n", &save);
            if (!room_name || !members_str) { send_to_fd(fd, "ERR_ROOM_START_FAIL|Bad args\n"); continue; }
            
            ChatRoom *room = find_room_by_name(room_name);
            if (room) {
                 send_to_fd(fd, "ERR_ROOM_START_FAIL|Room already exists\n"); 
                 continue; 
            }

            // 멤버 유효성 검사
            char temp_members[512];
            strncpy(temp_members, members_str, sizeof(temp_members)-1);
            temp_members[sizeof(temp_members)-1] = '\0';

            int all_valid = 1;
            char *tok = strtok(temp_members, ",");
            while(tok) {
                if (strcmp(tok, cli->user) != 0) {
                    if (!check_user_credentials(tok, "")) {
                        all_valid = 0;
                        break;
                    }
                }
                tok = strtok(NULL, ",");
            }

            if (!all_valid) {
                send_to_fd(fd, "ERR_ROOM_START_FAIL|Unregistered user\n");
                continue; 
            }
            
            room = create_room(room_name);
            if (!room) { send_to_fd(fd, "ERR_ROOM_START_FAIL|Server error\n"); continue; }
            insert_room(room);
            
            add_member_to_room(room, cli);
            cli->chat_mode = MODE_ROOM_CHAT;
            strncpy(cli->p2p_target, room_name, sizeof(cli->p2p_target)-1);
            
            char *member_id = strtok(members_str, ",");
            while(member_id) {
                if (strcmp(member_id, cli->user) != 0) {
                    Client *target_cli = find_client_by_name(member_id);
                    if (target_cli) {
                        char out[128]; snprintf(out, sizeof(out), "REQ_ROOM_ACCEPT|%s|%s\n", room_name, cli->user);
                        send_to_fd(target_cli->fd, out);
                    }
                }
                member_id = strtok(NULL, ",");
            }
            send_to_fd(fd, "ACK_ROOM_START_OK\n");
        }

        else if(strcmp(cmd, "ACK_ROOM_ACCEPT_OK")==0) {
            char *room_name = strtok_r(NULL, "|", &save); 
            char *sender_id = strtok_r(NULL, "|", &save); 
            if (!room_name) continue;
            
            ChatRoom *room = find_room_by_name(room_name);
            if (room) {
                cli->chat_mode = MODE_ROOM_CHAT;
                strncpy(cli->p2p_target, room_name, sizeof(cli->p2p_target)-1);
                add_member_to_room(room, cli);
                
                char *members = get_room_members_list_ex(room);
                char join_msg[2048]; snprintf(join_msg, sizeof(join_msg), "SYSTEM|%s 님이 채팅방에 입장했습니다. (현재 멤버: %s)\n", cli->user, members);
                broadcast_room_message_ex(room, join_msg, fd);
                free(members);
                
                send_to_fd(fd, "ACK_ROOM_JOIN_OK\n");
            } else {
                send_to_fd(fd, "ERR_ROOM_JOIN_FAIL|존재하지 않는 방입니다.\n");
            }
        }

        else if(strcmp(cmd, "REQ_ROOM_LIST")==0) {
            char *list = get_room_list();
            if(list) {
                char out[4096];
                snprintf(out, sizeof(out), "ACK_ROOM_LIST|%s", list);
                send_to_fd(fd, out);
                free(list);
            } else {
                send_to_fd(fd, "ACK_ROOM_LIST|목록을 불러올 수 없습니다.\n");
            }
        }
        
        else if(strcmp(cmd, "REQ_ROOM_JOIN")==0) {
            char *user = strtok_r(NULL, "|", &save);
            char *room_name = strtok_r(NULL, "\n", &save);
            
            ChatRoom *room = find_room_by_name(room_name);
            if (room) {
                 cli->chat_mode = MODE_ROOM_CHAT;
                strncpy(cli->p2p_target, room_name, sizeof(cli->p2p_target)-1);
                add_member_to_room(room, cli);
                
                char *members = get_room_members_list_ex(room);
                char join_msg[2048]; snprintf(join_msg, sizeof(join_msg), "SYSTEM|%s 님이 채팅방에 입장했습니다. (현재 멤버: %s)\n", cli->user, members);
                broadcast_room_message_ex(room, join_msg, fd);
                free(members);
                
                send_to_fd(fd, "ACK_ROOM_JOIN_OK\n");
            } else {
                send_to_fd(fd, "SYSTEM|존재하지 않는 방입니다.\n");
            }
        }

        else if(strcmp(cmd, "REQ_ROOM_DELETE")==0) {
            char *room_name = strtok_r(NULL, "\n", &save);
            ChatRoom *room = find_room_by_name(room_name);
            
            if (room) {
                char msg[256]; snprintf(msg, sizeof(msg), "SYSTEM|방장이 방을 삭제했습니다. 로비로 이동합니다.\nACK_ROOM_EXIT_OK\n");
                broadcast_room_message_ex(room, msg, -1);
                
                if (delete_room(room_name)) {
                    send_to_fd(fd, "ACK_ROOM_DELETE_OK\n");
                } else {
                    send_to_fd(fd, "ERR_ROOM_DELETE_FAIL\n");
                }
            } else {
                send_to_fd(fd, "ERR_ROOM_DELETE_FAIL|존재하지 않는 방입니다.\n");
            }
        }

        else if(strcmp(cmd, "REQ_ROOM")==0) {
            char *room_name = strtok_r(NULL, "|", &save);
            char *msg = strtok_r(NULL, "\n", &save);
            if (!room_name || cli->chat_mode != MODE_ROOM_CHAT || strcmp(cli->p2p_target, room_name) != 0) {
                send_to_fd(fd, "SYSTEM|현재 채팅방 모드가 아닙니다.\n"); continue;
            }
            
            ChatRoom *room = find_room_by_name(room_name);
            if (room) {
                char out[BUF_SIZE]; snprintf(out, sizeof(out), "ROOM|%s|%s|%s\n", room_name, cli->user, msg?msg:"");
                broadcast_room_message_ex(room, out, -1);
            } else {
                send_to_fd(fd, "SYSTEM|채팅방이 존재하지 않습니다.\n");
                cli->chat_mode = MODE_MAIN; cli->p2p_target[0] = '\0';
            }
        }

        else if(strcmp(cmd, "REQ_ROOM_EXIT")==0) {
            char *room_name = strtok_r(NULL, "|", &save);
            if (!room_name) continue;
            
            ChatRoom *room = find_room_by_name(room_name);
            if (room && remove_member_from_room(room, cli->user)) {
                cli->chat_mode = MODE_MAIN; cli->p2p_target[0] = '\0';
                send_to_fd(fd, "ACK_ROOM_EXIT_OK\n");
                
                char out[256]; snprintf(out, sizeof(out), "SYSTEM|%s 님이 채팅방을 나갔습니다.\n", cli->user);
                broadcast_room_message_ex(room, out, fd);
            } else {
                send_to_fd(fd, "ERR_ROOM_EXIT_FAIL\n");
            }
        }

        else if(strcmp(cmd, "REQ_EXIT")==0) {
            break;
        }

    }

    // 연결 종료 시 처리
    if (cli->user[0] != '\0') {
        ChatRoom *pub_room = find_room_by_name("PUBLIC_CHAT");
        if (pub_room && remove_member_from_room(pub_room, cli->user)) {
            char out[256]; snprintf(out, sizeof(out), "SYSTEM|%s 님이 접속을 종료했습니다.\n", cli->user);
            broadcast_room_message_ex(pub_room, out, fd);
        }
        
        ChatRoom *room = find_room_by_member(cli->user);
        if (room && remove_member_from_room(room, cli->user)) {
             char out[256]; snprintf(out, sizeof(out), "SYSTEM|%s 님이 접속을 종료하여 나갔습니다.\n", cli->user);
             broadcast_room_message_ex(room, out, fd);
        }
    }
    remove_client(cli);
    close(fd);
    free(cli);
    return NULL;
}

int main() {
    int serv_sock, clnt_sock;
    struct sockaddr_in serv_addr, clnt_addr;
    socklen_t clnt_addr_size;

    pthread_mutex_lock(&file_mutex);
    FILE *f = fopen("users.txt", "a+"); if(f) fclose(f);
    pthread_mutex_unlock(&file_mutex);

    serv_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (serv_sock == -1) { perror("socket"); return 1; }

    int opt = 1; setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET; serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); serv_addr.sin_port = htons(PORT);

    if (bind(serv_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1) { perror("bind"); return 1; }
    if (listen(serv_sock, 5) == -1) { perror("listen"); return 1; }

    printf("Server listening on port %d\n", PORT);

    while (1) {
        clnt_addr_size = sizeof(clnt_addr);
        clnt_sock = accept(serv_sock, (struct sockaddr *)&clnt_addr, &clnt_addr_size);
        if (clnt_sock == -1) { perror("accept"); continue; }
        Client *cli = create_client(clnt_sock, inet_ntoa(clnt_addr.sin_addr));
        insert_client(cli);
        printf("New client connected: %s\n", cli->ip);
        pthread_t t; if (pthread_create(&t, NULL, handle_client_thread, (void *)cli) != 0) {
            perror("pthread_create"); remove_client(cli); close(clnt_sock); free(cli);
        } else pthread_detach(t);
    }

    close(serv_sock);
    return 0;
}