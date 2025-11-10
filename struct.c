#include<stdio.h>
#include<unistd.h>
#include<stdlib.h>

typedef struct Client{
	int fd;
	char user[50];
	pthread_t th;
	//struct sockkar_in client_addr;
	struct Client *next;
	
}Client;
 
Client *head = NULL;
Client *tail = NULL;

Client *create_client(int fd){
	Client *pt;
	pt = (Client *)malloc(sizeof(Client));
	pt->fd=fd;
	return pt;
}

void insert_client(){}

void delete_Client(){}
