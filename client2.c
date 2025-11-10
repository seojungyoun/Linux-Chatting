#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUF_SIZE 2048

int sock;

// 서버 메시지 수신 스레드
void *recv_thread(void *arg){
    char buf[BUF_SIZE];
    while(1){
        int n=recv(sock,buf,sizeof(buf)-1,0);
        if(n<=0) break;
        buf[n]='\0';
        printf("%s",buf);
        fflush(stdout);
    }
    return NULL;
}

int main(){
    struct sockaddr_in serv;
    sock=socket(AF_INET,SOCK_STREAM,0);
    serv.sin_family=AF_INET;
    serv.sin_port=htons(5555);
    serv.sin_addr.s_addr=inet_addr("127.0.0.1");

    // 서버 연결
    if(connect(sock,(struct sockaddr*)&serv,sizeof(serv))==-1){
        perror("connect"); return 1;
    }
    printf("서버 연결 완료\n");

    printf("\n===============================\n");
    printf(" [사용 가능한 명령어 목록]\n");
    printf("===============================\n");
    printf("회원가입: REQ_REGISTER|아이디|비밀번호\n");
    printf("로그인:   REQ_LOGIN|아이디|비밀번호\n");
    printf("공개채팅: REQ_CHAT_PUB|아이디|ALL|메시지\n");
    printf("1:1채팅:  REQ_P2P|보내는ID|받는ID|메시지\n");
    printf("친구추가: REQ_FRD_ADD|내ID|SERVER|상대ID\n");
    printf("친구검색: REQ_FRD_SEARCH|내ID|상대ID\n");
    printf("종료:     REQ_EXIT\n");
    printf("================================\n\n");

    pthread_t th;
    pthread_create(&th,NULL,recv_thread,NULL);

    // 사용자 입력 반복
    char msg[BUF_SIZE];
    while(1){
        fgets(msg,sizeof(msg),stdin);
        if(!strncmp(msg,"/exit",5)){
            send(sock,"REQ_EXIT\n",9,0);
            break;
        }
        send(sock,msg,strlen(msg),0);
    }

    close(sock);
    return 0;
}
