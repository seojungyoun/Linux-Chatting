#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define IP "127.0.0.1"
#define PORT 5555
#define BUF_SIZE 1024

int sock;

// pthread_create로 생성되어 서버에서 오는 메시지를 수신하고 출력
void* recv_thread(void* arg) {
    char msg[BUF_SIZE];
    int len;
    while ((len = recv(sock, msg, BUF_SIZE, 0)) > 0) {
        msg[len] = 0;
        printf("%s\n", msg);
    }
    return NULL;
}

int main() {
    struct sockaddr_in serv_addr;
    pthread_t t;
    char msg[BUF_SIZE];

    sock = socket(AF_INET, SOCK_STREAM, 0);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, IP, &serv_addr.sin_addr);

    connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    pthread_create(&t, NULL, recv_thread, NULL);

    while (1) {
        fgets(msg, BUF_SIZE, stdin); // 사용자로부터 표준 입력을 받아 메시지를 전송
        send(sock, msg, strlen(msg), 0);
    }

    close(sock);
    return 0;
}

