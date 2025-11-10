#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 5555
#define BUFFER_SIZE 1024

void* handle_client(void* arg) {
    int client_fd = *(int*)arg;
    char buffer[BUFFER_SIZE];
    int bytes_received;

    while ((bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';
        printf("recv: %s\n", buffer);
        // send(client_fd, buffer, strlen(buffer), 0); 
		// echo
    }

    close(client_fd);
    return NULL;
}

int main() {
    int server_fd, client_fd;
    char buffer[BUFFER_SIZE];
    int bytes_received;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    pthread_t thread;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket error");
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind error");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 5) == -1) {
        perror("listen error");
        close(server_fd);
        return 1;
    }

 //   while (1) {
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
    //    if (client_fd == -1) continue;
     //   pthread_create(&thread, NULL, handle_client, (void*)&client_fd);
      //  pthread_detach(thread);
	printf("client number %d\n",client_fd);
   // }



    while ((bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';
        printf("recv: %s\n", buffer);
        send(client_fd, buffer, strlen(buffer), 0); 
	}  
	printf("하핳하하하\n");
//    sleep(1)

	close(server_fd);
    return 0;
}

