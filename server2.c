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

// 현재 시각 문자열로
static void timestamp_now(char *buf, size_t len) {
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    strftime(buf, len, "%Y%m%d_%H%M%S", &tm);
}

// 아이디/비번 확인
int check_user_credentials(const char *id, const char *pw) {
    pthread_mutex_lock(&file_mutex);
    FILE *f = fopen("users.txt", "r");
    if(!f) { pthread_mutex_unlock(&file_mutex); return 0; }
    char line[256]; int ok = 0;
    while(fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n'); if(nl) *nl = '\0';
        char *sep = strchr(line, ':'); if(!sep) continue;
        *sep = '\0';
        if(strcmp(line, id)==0 && strcmp(sep+1, pw)==0) { ok=1; break; }
    }
    fclose(f);
    pthread_mutex_unlock(&file_mutex);
    return ok;
}

// 회원가입
int register_user(const char *id, const char *pw) {
    pthread_mutex_lock(&file_mutex);
    FILE *f = fopen("users.txt", "a+");
    if(!f) f = fopen("users.txt", "w+");
    if(!f) { pthread_mutex_unlock(&file_mutex); return 0; }
    rewind(f);
    char line[256];
    while(fgets(line,sizeof(line),f)) {
        char *nl=strchr(line,'\n'); if(nl)*nl='\0';
        char *sep=strchr(line,':'); if(!sep) continue;
        *sep='\0';
        if(strcmp(line,id)==0){ fclose(f); pthread_mutex_unlock(&file_mutex); return 0; }
    }
    fprintf(f,"%s:%s\n",id,pw);
    fclose(f);
    pthread_mutex_unlock(&file_mutex);
    return 1;
}

// 친구추가
int add_friend(const char *user, const char *friendid) {
    char fname[128];
    snprintf(fname,sizeof(fname),"%s_friends.txt",user);
    pthread_mutex_lock(&file_mutex);
    FILE *f = fopen(fname,"a+");
    if(!f) f=fopen(fname,"w+");
    if(!f){ pthread_mutex_unlock(&file_mutex); return 0; }
    rewind(f);
    char line[128];
    while(fgets(line,sizeof(line),f)){
        char *nl=strchr(line,'\n'); if(nl)*nl='\0';
        if(strcmp(line,friendid)==0){ fclose(f); pthread_mutex_unlock(&file_mutex); return 0; }
    }
    fprintf(f,"%s\n",friendid);
    fclose(f);
    pthread_mutex_unlock(&file_mutex);
    return 1;
}

// 공개 채팅 로그 저장
void log_public_chat(const char *from, const char *msg) {
    char ts[64]; timestamp_now(ts,sizeof(ts));
    pthread_mutex_lock(&file_mutex);
    FILE *f=fopen("public_chat.log","a");
    if(f){ fprintf(f,"[%s] [%s] %s\n",ts,from,msg); fclose(f); }
    pthread_mutex_unlock(&file_mutex);
}

// 1:1 채팅 로그
void log_p2p(const char *a,const char *b,const char *from,const char *msg){
    char n1[64],n2[64];
    if(strcmp(a,b)<=0){strcpy(n1,a);strcpy(n2,b);} else {strcpy(n1,b);strcpy(n2,a);}
    char fname[128];
    snprintf(fname,sizeof(fname),"%s_%s_p2p.log",n1,n2);
    char ts[64]; timestamp_now(ts,sizeof(ts));
    pthread_mutex_lock(&file_mutex);
    FILE *f=fopen(fname,"a");
    if(f){fprintf(f,"[%s] [%s] %s\n",ts,from,msg);fclose(f);}
    pthread_mutex_unlock(&file_mutex);
}

// 메시지 전송
void send_to_client_fd(int fd, const char *msg){ if(fd>0) send(fd,msg,strlen(msg),0); }

// 클라이언트 명령 처리
void *handle_client_thread(void *arg) {
    Client *cli=(Client*)arg;
    int fd=cli->fd; char buf[BUF_SIZE]; ssize_t n;

    while((n=recv(fd,buf,sizeof(buf)-1,0))>0){
        buf[n]='\0'; char *nl=strchr(buf,'\n'); if(nl)*nl='\0';
        char *save=NULL; char *cmd=strtok_r(buf,"|",&save);
        if(!cmd) continue;

        // 회원가입
        if(!strcmp(cmd,"REQ_REGISTER")){
            char *id=strtok_r(NULL,"|",&save);
            char *pw=strtok_r(NULL,"|",&save);
            if(id&&pw){
                if(register_user(id,pw)) send(fd,"ACK_REGISTER_OK\n",16,0);
                else send(fd,"ERR_ID_EXISTS\n",14,0);
            }
        }
        // 로그인
        else if(!strcmp(cmd,"REQ_LOGIN")){
            char *id=strtok_r(NULL,"|",&save);
            char *pw=strtok_r(NULL,"|",&save);
            if(check_user_credentials(id,pw)){
                strcpy(cli->user,id);
                send(fd,"ACK_LOGIN_OK\n",13,0);
            } else send(fd,"ERR_LOGIN_FAIL\n",15,0);
        }
        // 공개 채팅
        else if(!strcmp(cmd,"REQ_CHAT_PUB")){
            char *from=strtok_r(NULL,"|",&save);
            char *to=strtok_r(NULL,"|",&save);
            char *msg=strtok_r(NULL,"|",&save);
            if(from&&msg){
                char out[1024];
                snprintf(out,sizeof(out),"PUB|%s|%s\n",from,msg);
                broadcast_message(out);
                log_public_chat(from,msg);
            }
        }
        // 1:1 채팅
        else if(!strcmp(cmd,"REQ_P2P")){
            char *from=strtok_r(NULL,"|",&save);
            char *to=strtok_r(NULL,"|",&save);
            char *msg=strtok_r(NULL,"|",&save);
            Client *target=find_client_by_name(to);
            if(target){
                char out[1024];
                snprintf(out,sizeof(out),"P2P|%s|%s\n",from,msg);
                send(target->fd,out,strlen(out),0);
            }
            log_p2p(from,to,from,msg);
        }
        // 종료
        else if(!strcmp(cmd,"REQ_EXIT")) break;
    }

    close(fd);
    remove_client_by_fd(fd);
    return NULL;
}

int main(){
    int sfd=socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in saddr,caddr;
    saddr.sin_family=AF_INET;
    saddr.sin_addr.s_addr=INADDR_ANY;
    saddr.sin_port=htons(PORT);
    bind(sfd,(struct sockaddr*)&saddr,sizeof(saddr));
    listen(sfd,10);
    printf("서버 시작 (포트 %d)\n",PORT);

    socklen_t clen=sizeof(caddr);
    while(1){
        int cfd=accept(sfd,(struct sockaddr*)&caddr,&clen);
        Client *c=create_client(cfd);
        insert_client(c);
        pthread_create(&c->th,NULL,handle_client_thread,(void*)c);
        pthread_detach(c->th);
    }
    close(sfd);
    return 0;
}
