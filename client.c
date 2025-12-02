#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "struct.h" 

#define BUF_SIZE 2048

int sockfd = -1;
char current_user[64] = "";
volatile int login_success = 0;
volatile int current_mode = MODE_MAIN; 
char p2p_with[64] = ""; 
char incoming_p2p_request[128] = "";
pthread_mutex_t request_mutex = PTHREAD_MUTEX_INITIALIZER;

void trim_newline(char *s) {
    s[strcspn(s, "\n")] = 0;
    s[strcspn(s, "\r")] = 0;
}

void clear_stdin_buffer() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

void *recv_thread(void *arg) {
    char buf[BUF_SIZE];

    while (1) {
        int n = recv(sockfd, buf, sizeof(buf)-1, 0);
        if (n <= 0) break;

        buf[n] = '\0';
        char tmp[BUF_SIZE];
        strcpy(tmp, buf);

        if (strncmp(buf, "PUB|", 4) == 0) {
            char *from = strtok(tmp+4, "|");
            char *msg  = strtok(NULL, "\n");
            if (current_mode == MODE_PUBLIC_CHAT) {
                printf("\n[공개 채팅] %s: %s\n", from, msg);
            } else {
                printf("\n[SYSTEM] 새로운 공개 채팅 메시지가 있습니다. (%s)\n", from);
            }
            fflush(stdout);
            continue;
        }

        if (strncmp(buf, "ROOM|", 5) == 0) {
            char *room_name = strtok(tmp+5, "|");
            char *from = strtok(NULL, "|");
            char *msg  = strtok(NULL, "\n");

            if (current_mode == MODE_ROOM_CHAT && strcmp(room_name, p2p_with) == 0) {
                printf("\n[%s] %s: %s\n", room_name, from, msg);
            } else {
                printf("\n[SYSTEM] 새로운 채팅방 메시지가 있습니다. (%s/%s)\n", room_name, from);
            }
            fflush(stdout);
            continue;
        }

        if (strncmp(buf, "REQ_ROOM_ACCEPT|", 16) == 0) {
            char *room_name = strtok(tmp+16, "|");
            char *sender = strtok(NULL, "\n");
            pthread_mutex_lock(&request_mutex);
            snprintf(incoming_p2p_request, sizeof(incoming_p2p_request), "ROOM|%s|%s", room_name, sender);
            pthread_mutex_unlock(&request_mutex);
            printf("\n[SYSTEM] %s 님으로부터 채팅방 (%s) 초대 요청이 도착했습니다. 확인하려면 Enter를 두 번 누르세요.\n", sender, room_name);
            fflush(stdout);
            continue;
        }
        
        if (strstr(buf, "ACK_ROOM_START_OK") || strstr(buf, "ACK_ROOM_JOIN_OK")) {
            printf("\n[SYSTEM] 채팅방에 입장했습니다. 잠시 후 화면이 전환됩니다.(Enter)\n");
            current_mode = MODE_ROOM_CHAT;
            fflush(stdout);
            continue;
        }

        if (strstr(buf, "ERR_ROOM_START_FAIL")) {
            if (strstr(buf, "Unregistered user")) {
                printf("\n[SYSTEM] 등록되지 않은 사용자가 포함되어있습니다.\n");
            } else if (strstr(buf, "Room already exists")) {
                printf("\n[SYSTEM] 이미 존재하는 방 이름입니다.\n");
            } else {
                printf("\n[SYSTEM] 채팅방 생성에 실패했습니다.\n");
            }
            fflush(stdout);
            continue;
        }

        if (strstr(buf, "ACK_CHAT_PUB_JOIN_OK")) {
            char *info = strchr(tmp, '|');
            printf("\n[SYSTEM] 공개 채팅방에 입장했습니다.\n");
            if(info) printf("[SYSTEM] 현재 참여 멤버: %s\n", info+1);
            current_mode = MODE_PUBLIC_CHAT;
            fflush(stdout);
            continue;
        }
        
        if (strstr(buf, "ACK_CHAT_PUB_EXIT_OK")) {
            printf("\n[SYSTEM] 공개 채팅방을 나갔습니다. 메인 메뉴로 돌아갑니다.\n");
            current_mode = MODE_MAIN;
            fflush(stdout);
            continue;
        }
        
        if (strstr(buf, "ACK_ROOM_EXIT_OK")) {
            printf("\n[SYSTEM] 채팅방을 나갔습니다. 메인 메뉴로 돌아갑니다.\n");
            current_mode = MODE_MAIN;
            p2p_with[0] = '\0';
            fflush(stdout);
            continue;
        }
        
        if (strstr(buf, "ACK_ROOM_DELETE_OK")) {
            printf("\n[SYSTEM] 채팅방이 성공적으로 삭제되었습니다.\n");
            fflush(stdout);
            continue;
        }
        if (strstr(buf, "ERR_ROOM_DELETE_FAIL")) {
            printf("\n[SYSTEM] 채팅방 삭제 실패 (존재하지 않거나 권한 없음)\n");
            fflush(stdout);
            continue;
        }
        if (strncmp(buf, "ACK_ROOM_LIST|", 14) == 0) {
            printf("\n================ [ 현재 채팅방 목록 ] ================\n");
            printf("%s", buf + 14);
            printf("====================================================\n");
            printf("목록을 확인했습니다. 메뉴에서 입장 또는 삭제를 진행하세요.\n");
            fflush(stdout);
            continue;
        }
        
        if (strncmp(buf, "SYSTEM|", 7) == 0) {
            printf("\n[SYSTEM] %s\n", buf + 7);
            fflush(stdout);
            continue;
        }
        
        if (strncmp(buf, "ACK_FRD_ADD_OK", 14) == 0) {
            char a[64], b[64], name[64];
            char *info = strchr(tmp, '|');
            if (info && sscanf(info+1, "%63s added %63s", a, name) == 2)
                printf("\n[SYSTEM] %s 님을 친구로 추가했습니다.\n", name);
            fflush(stdout);
            continue;
        }
        if (strncmp(buf, "ERR_FRD_ADD_FAIL", 17) == 0) {
            printf("\n[SYSTEM] 친구추가 실패 (이미 존재하거나 오류)\n");
            fflush(stdout);
            continue;
        }
        if (strncmp(buf, "ACK_FRD_SEARCH_OK", 17) == 0) {
            char name[64];
            char *info = strchr(tmp, '|');
            if (info && sscanf(info+1, "%63s exists", name) == 1)
                printf("\n[SYSTEM] '%s' 님은 존재하는 사용자입니다.\n", name);
            fflush(stdout);
            continue;
        }
        if (strncmp(buf, "ERR_FRD_SEARCH_FAIL", 20) == 0) {
            printf("\n[SYSTEM] 해당 사용자를 찾을 수 없습니다.\n");
            fflush(stdout);
            continue;
        }
        if (strstr(buf, "ACK_LOGIN_OK")) {
            printf("\n[SYSTEM] 로그인 성공!\n");
            login_success = 1;
            fflush(stdout);
            continue;
        }
        if (strstr(buf, "ERR_LOGIN_FAIL")) {
            printf("\n[SYSTEM] 로그인 실패. ID 또는 PW를 확인하세요.\n");
            fflush(stdout);
            continue;
        }
        if (strstr(buf, "ACK_REGISTER_OK")) {
            printf("\n[SYSTEM] 회원가입 성공!\n");
            fflush(stdout);
            continue;
        }
        if (strstr(buf, "ERR_ID_EXISTS")) {
            printf("\n[SYSTEM] 회원가입 실패. ID가 이미 존재합니다.\n");
            fflush(stdout);
            continue;
        }

        printf("\n[SYSTEM] %s\n", buf);
        fflush(stdout);
    }

    printf("\n[SYSTEM] 서버 연결이 종료되었습니다. 프로그램을 종료합니다.\n");
    exit(0);
    return NULL;
}

int main() {
    struct sockaddr_in serv;
    
    // serv 구조체 초기화
    serv.sin_family = AF_INET;
    serv.sin_port = htons(5555);
    serv.sin_addr.s_addr = inet_addr("124.56.5.77");

    // 초기 연결
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(sockfd, (struct sockaddr *)&serv, sizeof(serv)) == -1) {
        perror("connect");
        return 1;
    }

    printf("서버 연결 완료\n");

    pthread_t th;
    pthread_create(&th, NULL, recv_thread, NULL);
    pthread_detach(th);

    int mode = 0;
    char input_line[256];
    int cmd;
    char id[64], pw[64];
    char buf[BUF_SIZE];
    char target[64], message[256];
    
    while (mode == 0) { // 로그인 메뉴 (mode 0)
        if (sockfd == -1) {
            sockfd = socket(AF_INET, SOCK_STREAM, 0);
            if (connect(sockfd, (struct sockaddr *)&serv, sizeof(serv)) == -1) {
                printf("\n[SYSTEM] 서버 재연결 실패. 종료합니다.\n");
                return 1;
            }
            printf("\n[SYSTEM] 서버 재연결 성공.\n");
            pthread_create(&th, NULL, recv_thread, NULL);
            pthread_detach(th);
        }
        
        sleep(1);
        system("clear");

        printf("+------------------------------+\n");
        printf("|          Login Menu          |\n");
        printf("+------------------------------+\n");
        printf("| 1. 로 그 인\n| 2. 회원가입\n| 3. 종료\n");
        printf("+------------------------------+\n선 택 : ");
        
        if (fgets(input_line, sizeof(input_line), stdin) == NULL) {
            send(sockfd, "REQ_EXIT\n", 9, 0);
            close(sockfd);
            return 0;
        } else {
            trim_newline(input_line);
            cmd = atoi(input_line);
        }
        
        login_success = 0;

        if (cmd == 1) {
            printf("ID: "); 
            if (fgets(id, sizeof(id), stdin) == NULL) continue; 
            trim_newline(id);

            printf("PW: "); 
            if (fgets(pw, sizeof(pw), stdin) == NULL) continue; 
            trim_newline(pw);
            
            snprintf(buf, sizeof(buf), "REQ_LOGIN|%s|%s\n", id, pw); 
            send(sockfd, buf, strlen(buf), 0);
            sleep(1);
            
            if (login_success) { 
                mode = 1; 
                current_mode = MODE_MAIN; 
                strncpy(current_user, id, sizeof(current_user)-1); 
                current_user[sizeof(current_user)-1]='\0'; 
            } else { 
                current_user[0] = '\0'; 
                printf("로그인 실패\n"); 
                sleep(1); 
            }
        }
        else if (cmd == 2) {
            printf("ID: "); 
            if (fgets(id, sizeof(id), stdin) == NULL) continue; 
            trim_newline(id);

            printf("PW: "); 
            if (fgets(pw, sizeof(pw), stdin) == NULL) continue; 
            trim_newline(pw);
            
            snprintf(buf, sizeof(buf), "REQ_REGISTER|%s|%s\n", id, pw); 
            send(sockfd, buf, strlen(buf), 0); 
            sleep(1);
        }
        else if (cmd == 3) {
            send(sockfd, "REQ_EXIT\n", 9, 0);
            close(sockfd);
            return 0;
        }
        else {
            printf("잘못된 입력입니다.\n");
            sleep(1);
        }
    }

    while (mode == 1) { // 메인 메뉴 루프 (mode 1)
        
        // 초대 요청 수락 처리
        pthread_mutex_lock(&request_mutex);
        if (incoming_p2p_request[0] != '\0') {
            char room_name[64], sender[64];
            char choice_buf[8]; 
            
            sscanf(incoming_p2p_request + 5, "%63[^|]|%63s", room_name, sender);
            incoming_p2p_request[0] = '\0'; 
            pthread_mutex_unlock(&request_mutex);
            
            clear_stdin_buffer();
            system("clear");
            
            printf("%s 님으로부터 채팅방 (%s) 초대 요청이 왔습니다. (Y/N): ", sender, room_name);
            fflush(stdout); 
            
            if (!fgets(choice_buf, sizeof(choice_buf), stdin)) { 
                send(sockfd, "REQ_EXIT\n",9,0); 
                close(sockfd); 
                return 0; 
            }
            trim_newline(choice_buf);
            
            if (choice_buf[0]=='Y' || choice_buf[0]=='y') {
                snprintf(buf, sizeof(buf), "ACK_ROOM_ACCEPT_OK|%s|%s\n", room_name, current_user);
                send(sockfd, buf, strlen(buf), 0);
                strncpy(p2p_with, room_name, sizeof(p2p_with)-1); 
                p2p_with[sizeof(p2p_with)-1]='\0'; 
                current_mode = MODE_ROOM_CHAT;
            } else {
                printf("[SYSTEM] 초대 요청을 거절했습니다.\n");
            }
            continue;
        }
        pthread_mutex_unlock(&request_mutex);

        // Command Menu 루프
        while (current_mode == MODE_MAIN) {
            sleep(1); 
            system("clear");
            printf("+-------------------------------+\n");
            printf("|          Command Menu         |\n");
            printf("+-------------------------------+\n");
            printf("| 사용자 : %s\n", current_user);
            printf("| 1. 채팅방 만들기 (1:1/그룹)\n| 2. 채팅방 목록 및 관리\n| 3. 공개채팅\n| 4. 친구추가\n| 5. 친구검색\n| 0. 로그아웃\n");
            printf("+-------------------------------+\n선 택 : ");

            if (!fgets(input_line, sizeof(input_line), stdin)) { send(sockfd, "REQ_EXIT\n",9,0); close(sockfd); return 0; }
            trim_newline(input_line); 
            
            if (strlen(input_line) == 0) { 
                break; 
            }
            
            cmd = atoi(input_line);

            if (cmd == 1) { // 채팅방 생성
                char room_name[64], members_str[256];
                printf("생성할 채팅방 이름: "); 
                if (!fgets(room_name, sizeof(room_name), stdin)) break;
                trim_newline(room_name);

                printf("초대할 ID (쉼표로 구분, 예: user1,user2): "); 
                if (!fgets(members_str, sizeof(members_str), stdin)) break;
                trim_newline(members_str);
                
                snprintf(buf, sizeof(buf), "REQ_ROOM_START|%s|%s\n", room_name, members_str); 
                send(sockfd, buf, strlen(buf), 0);
                
                strncpy(p2p_with, room_name, sizeof(p2p_with)-1); 
                p2p_with[sizeof(p2p_with)-1]='\0';
                sleep(2);
            }
            else if (cmd == 2) { // 목록 및 관리
                send(sockfd, "REQ_ROOM_LIST\n", 14, 0);
                sleep(1); 
                
                printf("\n--- 관리 메뉴 ---\n");
                printf("| 1. 입장하기\n| 2. 삭제하기\n| 0. 취소\n");
                printf("선택: ");
                
                char sub[8];
                if(fgets(sub, sizeof(sub), stdin)) {
                    int sc = atoi(sub);
                    if (sc == 1) {
                        printf("입장할 방 이름: ");
                        if(fgets(target, sizeof(target), stdin)) {
                            trim_newline(target);
                            snprintf(buf, sizeof(buf), "REQ_ROOM_JOIN|%s|%s\n", current_user, target);
                            send(sockfd, buf, strlen(buf), 0);
                            strncpy(p2p_with, target, sizeof(p2p_with)-1);
                            p2p_with[sizeof(p2p_with)-1]='\0';
                            sleep(1);
                        }
                    } else if (sc == 2) {
                        printf("삭제할 방 이름: ");
                        if(fgets(target, sizeof(target), stdin)) {
                            trim_newline(target);
                            snprintf(buf, sizeof(buf), "REQ_ROOM_DELETE|%s\n", target);
                            send(sockfd, buf, strlen(buf), 0);
                            sleep(1);
                        }
                    }
                }
            }
            else if (cmd == 3) {
                snprintf(buf, sizeof(buf), "REQ_CHAT_PUB_JOIN|%s\n", current_user); 
                send(sockfd, buf, strlen(buf), 0); 
                sleep(1);
            }
            else if (cmd == 4) {
                printf("추가할 ID: "); 
                if (!fgets(target, sizeof(target), stdin)) { send(sockfd, "REQ_EXIT\n",9,0); close(sockfd); return 0; }
                trim_newline(target);
                snprintf(buf, sizeof(buf), "REQ_FRD_ADD|%s|%s\n", current_user, target);
                send(sockfd, buf, strlen(buf), 0); 
                sleep(2);
            }
            else if (cmd == 5) {
                printf("검색할 ID: "); 
                if (!fgets(target, sizeof(target), stdin)) { send(sockfd, "REQ_EXIT\n",9,0); close(sockfd); return 0; }
                trim_newline(target);
                snprintf(buf, sizeof(buf), "REQ_FRD_SEARCH|%s\n", target); 
                send(sockfd, buf, strlen(buf), 0); 
                sleep(2);
            }
            else if (cmd == 0) {
                send(sockfd, "REQ_EXIT\n", 9, 0); 
                close(sockfd); 
                sockfd = -1; 
                current_user[0] = '\0'; 
                login_success = 0; 
                mode = 0; 
                break;
            }
            else { 
                printf("잘못된 입력입니다.\n"); 
                sleep(1); 
                break;
            }
        }

        if (current_mode == MODE_ROOM_CHAT) {
            system("clear"); 
            printf("+---------------------------------------+\n"); 
            printf("|         채팅방: %s                    |\n", p2p_with);
            printf("+---------------------------------------+\n| 1. 나가기 (메시지를 입력하여 채팅)\n+---------------------------------------+\n메시지 또는 명령어(1): ");
        }

        while (current_mode == MODE_ROOM_CHAT) {
            if (!fgets(message, sizeof(message), stdin)) { 
                snprintf(buf, sizeof(buf), "REQ_ROOM_EXIT|%s\n", p2p_with); 
                send(sockfd, buf, strlen(buf), 0); 
                close(sockfd); return 0;
            }
            
            trim_newline(message);

            if (strcmp(message, "1") == 0) { 
                snprintf(buf, sizeof(buf), "REQ_ROOM_EXIT|%s\n", p2p_with); 
                send(sockfd, buf, strlen(buf), 0); 
                sleep(1); 
                current_mode = MODE_MAIN; 
            }
            else if (strlen(message) > 0) { 
                snprintf(buf, sizeof(buf), "REQ_ROOM|%s|%s\n", p2p_with, message); 
                send(sockfd, buf, strlen(buf), 0); 
            }
        }
        
        if (current_mode == MODE_PUBLIC_CHAT) {
            system("clear"); 
            printf("+---------------------------------------+\n"); 
            printf("|             PUBLIC CHAT               |\n");
            printf("+---------------------------------------+\n");
            printf("| 1. 나가기 (메시지를 입력하여 채팅)    |\n");
            printf("+---------------------------------------+\n");
            printf("메시지 또는 명령어(1): ");
            fflush(stdout);
        }

        while (current_mode == MODE_PUBLIC_CHAT) {
            if (!fgets(message, sizeof(message), stdin)) { 
                snprintf(buf, sizeof(buf), "REQ_CHAT_PUB_EXIT|%s\n", current_user); 
                send(sockfd, buf, strlen(buf), 0); 
                close(sockfd); return 0; 
            }

            trim_newline(message);

            if (strcmp(message, "1") == 0) { 
                snprintf(buf, sizeof(buf), "REQ_CHAT_PUB_EXIT|%s\n", current_user); 
                send(sockfd, buf, strlen(buf), 0); 
                sleep(1); 
                current_mode = MODE_MAIN; 
            }
            else if (strlen(message) > 0) { 
                snprintf(buf, sizeof(buf), "REQ_CHAT_PUB|%s|%s\n", current_user, message); 
                send(sockfd, buf, strlen(buf), 0); 
            }
        }
    }

    close(sockfd);
    return 0;
}