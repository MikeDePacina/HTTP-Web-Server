#include "portability_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_REQUEST_SIZE 2047
#define BSIZE 1024

char *get_content_type(const char* path);
//create listening socket
SOCKET create_socket(const char* host, const char* port);

//storing client info in a linked list of max size (10)
struct client_info{
    socklen_t address_length;
    struct sockaddr_storage address;
    char address_buffer[128];
    SOCKET socket;
    char request[MAX_REQUEST_SIZE+1];
    int received;
    struct client_info *next;
};

//head of linked list, we'll always add to the head of the list/prepend
static struct client_info *clients = 0;

/*
traverse linked list of max size 10 to return client info
with specified SOCKET
*/
struct client_info *get_client(SOCKET s);

//create empty client_info to be filled in later. Used in get_client if
//socket doesn't exist
struct client_info *create_client();

/*
traverse through linked list to remove from list and close 
connection with specified client info
*/
void drop_client(struct client_info *client);

char *get_client_address(struct client_info *ci);

//wait until a socket wants to send or new socket connection request
fd_set wait_on_clients(SOCKET server);

//Bad request
void send_400(struct client_info *client);

//Resource not found
void send_404(struct client_info *client);

//send resource, send resources from public folder
void serve_resource(struct client_info *client, const char *path);