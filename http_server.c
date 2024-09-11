#include "portability_config.h"
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
#include "http_server.h"

#define MAX_REQUEST_SIZE 2047
#define BSIZE 1024



int main(){
    #if defined(_WIN32)
        WSADATA d;
        if (WSAStartup(MAKEWORD(2, 2), &d)) {
            fprintf(stderr, "Failed to initialize.\n");
            return 1;
        }
    #endif

    SOCKET server = create_socket("127.0.0.1", "8080");

    //server will keep running unless there's an error
    while(1){
        fd_set reads;
        reads = wait_on_clients(server);

        if (FD_ISSET(server, &reads)) {
            //returns new blank client info
            struct client_info *client = get_client(-1);

            client->socket = accept(server,
                    (struct sockaddr*) &(client->address),
                    &(client->address_length));

            if (!ISVALIDSOCKET(client->socket)) {
                fprintf(stderr, "accept() failed. (%d)\n",
                        GETSOCKETERRNO());
                return 1;
            }


            printf("New connection from %s.\n",
                    get_client_address(client));
        }

        struct client_info *client = clients;
        
        //traverse through linked list of clients, and check
        //for sending request
        while(client) {
            struct client_info *next = client->next;

            if (FD_ISSET(client->socket, &reads)) {
                if (MAX_REQUEST_SIZE == client->received) {
                    send_400(client);
                    continue;
                }

                int r = recv(client->socket,
                        client->request + client->received,
                        MAX_REQUEST_SIZE - client->received, 0);
                
                if (r < 1) {
                    printf("Unexpected disconnect from %s.\n",
                            get_client_address(client));
                    drop_client(client);  
                }else{
                    client->received += r;
                    client->request[client->received] = 0;

                    //check if HTTP header has been fully received,
                    //if so we can now parse it
                    char *q = strstr(client->request, "\r\n\r\n");
                    if (q) {
                        if (strncmp("GET /", client->request, 5)) {
                            send_400(client);
                        } else {
                            char *path = client->request + 4;
                            char *end_path = strstr(path, " ");
                            if (!end_path) {
                                send_400(client);
                            } else {
                                *end_path = 0;
                                serve_resource(client, path);
                            }
                        }
                        
                    }
                }
            }
            client = next;
        }
    }

    printf("\nClosing socket...\n");
    CLOSESOCKET(server);


    #if defined(_WIN32)
        WSACleanup();
    #endif

    printf("Finished.\n");

    return 0;
}

void serve_resource(struct client_info *client, const char *path){
    printf("serve_resource %s %s\n", get_client_address(client), path);

    //default file
    if (strcmp(path, "/") == 0) path = "/index.html";

    //path can't be longer 100 chars
    if (strlen(path) > 100) {
        send_400(client);
        return;
    }

    //forbidden to go out of public dir
    if (strstr(path, "..")) {
        send_404(client);
        return;
    }

    char full_path[128];
    sprintf(full_path, "public%s", path);


    #if defined(_WIN32)
        char *p = full_path;
        while (*p) {
            if (*p == '/') *p = '\\';
                ++p;
        }
    #endif

    FILE *fp = fopen(full_path, "rb");

    if (!fp) {
        send_404(client);
        return;
    }

    //get file size for content-length header
    fseek(fp, 0L, SEEK_END);
    size_t cl = ftell(fp);
    rewind(fp);//set pointer back to start of file

    //for content-type header
    const char *ct = get_content_type(full_path);

    //send headers
    char buffer[BSIZE];
    sprintf(buffer, "HTTP/1.1 200 OK\r\n");
    send(client->socket, buffer, strlen(buffer), 0);

    sprintf(buffer, "Connection: close\r\n");
    send(client->socket, buffer, strlen(buffer), 0);

    sprintf(buffer, "Content-Length: %lu\r\n", cl);
    send(client->socket, buffer, strlen(buffer), 0);

    sprintf(buffer, "Content-Type: %s\r\n", ct);
    send(client->socket, buffer, strlen(buffer), 0);
    
    //new line to signal end of header and start of body
    sprintf(buffer, "\r\n");
    send(client->socket, buffer, strlen(buffer), 0);

    //now we send file in chunks of buffer size
    int r = fread(buffer, 1, BSIZE, fp);
    while (r) {
        send(client->socket, buffer, r, 0);
        r = fread(buffer, 1, BSIZE, fp);
    }

    //close file when done sending, and close connection with client
    fclose(fp);
    drop_client(client);
}

void send_404(struct client_info *client){
    const char *c404 = "HTTP/1.1 404 Not Found\r\n"
        "Connection: close\r\n"
        "Content-Length: 9\r\n\r\nNot Found";
    send(client->socket, c404, strlen(c404), 0);
    drop_client(client);
}


void send_400(struct client_info *client){
    const char *c400 = "HTTP/1.1 400 Bad Request\r\n"
        "Connection: close\r\n"
        "Content-Length: 11\r\n\r\nBad Request";
    send(client->socket, c400, strlen(c400), 0);
    drop_client(client);
}

fd_set wait_on_clients(SOCKET server){
    fd_set reads;
    FD_ZERO(&reads);
    FD_SET(server, &reads);
    SOCKET max_socket = server;

    struct client_info *ci = clients;
    
    //client sockets to set, while updating max socket ID/number, 
    //which select needs -it tests sockets 0-max socket number, so
    //add 1 to max
    while(ci) {
        FD_SET(ci->socket, &reads);
        if (ci->socket > max_socket)
            max_socket = ci->socket;
        ci = ci->next;
    }

    if (select(max_socket+1, &reads, 0, 0, 0) < 0) {
        fprintf(stderr, "select() failed. (%d)\n", GETSOCKETERRNO());
        exit(1);
    }

    return reads;
}


char *get_client_address(struct client_info *ci){
    getnameinfo((struct sockaddr*)&ci->address,
            ci->address_length,
            ci->address_buffer, sizeof(ci->address_buffer), 0, 0,
            NI_NUMERICHOST);
    return ci->address_buffer;
}

void drop_client(struct client_info *client){
    CLOSESOCKET(client->socket);

    struct client_info **p = &clients;

    while(*p) {
        if (*p == client) {
            *p = client->next;
            free(client);
            return;
        }
        p = &(*p)->next;
    }

    fprintf(stderr, "client not found in list.\n");
    exit(1);
}

struct client_info *create_client(){
    struct client_info *node = (struct client_info*) calloc(1, sizeof(struct client_info));

    if(!node){
        fprintf(stderr,"Unable to allocate memory for new client info");
        exit(1);
    }

    return node;
};

struct client_info *get_client(SOCKET s){
    struct client_info *curr = clients;

    while(curr){
        if(curr->socket == s) break;
        curr = curr->next;
    }

    if(curr) return curr;

    //else we return newly prepended client info, and update head to it
    struct client_info *new_node = create_client();
    new_node->address_length = sizeof(new_node->address);
    new_node->next = clients;
    clients = new_node;

    return new_node;

}

SOCKET create_socket(const char* host, const char* port){
    printf("Configuring local address...\n");
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; //IPv4
    hints.ai_socktype = SOCK_STREAM; //TCP
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo *bind_address;
    getaddrinfo(host, port, &hints, &bind_address);

    printf("Creating socket...\n");
    SOCKET socket_listen;
    socket_listen = socket(bind_address->ai_family,
            bind_address->ai_socktype, bind_address->ai_protocol);
    if (!ISVALIDSOCKET(socket_listen)) {
        fprintf(stderr, "socket() failed. (%d)\n", GETSOCKETERRNO());
        exit(1);
    }

    printf("Binding socket to local address...\n");
    if (bind(socket_listen,
                bind_address->ai_addr, bind_address->ai_addrlen)) {
        fprintf(stderr, "bind() failed. (%d)\n", GETSOCKETERRNO());
        exit(1);
    }
    freeaddrinfo(bind_address);

    printf("Listening...\n");
    //only accept up to 10 connections at a time
    if (listen(socket_listen, 10) < 0) {
        fprintf(stderr, "listen() failed. (%d)\n", GETSOCKETERRNO());
        exit(1);
    }

    return socket_listen;
}


char *get_content_type(const char* path){
    //strrchar finds last occurrence of char
    const char *last_dot = strrchr(path, '.');

    if (last_dot){
        if (strcmp(last_dot, ".css") == 0) return "text/css";
        if (strcmp(last_dot, ".csv") == 0) return "text/csv";
        if (strcmp(last_dot, ".gif") == 0) return "image/gif";
        if (strcmp(last_dot, ".htm") == 0) return "text/html";
        if (strcmp(last_dot, ".html") == 0) return "text/html";
        if (strcmp(last_dot, ".ico") == 0) return "image/x-icon";
        if (strcmp(last_dot, ".jpeg") == 0) return "image/jpeg";
        if (strcmp(last_dot, ".jpg") == 0) return "image/jpeg";
        if (strcmp(last_dot, ".js") == 0) return "application/javascript";
        if (strcmp(last_dot, ".json") == 0) return "application/json";
        if (strcmp(last_dot, ".png") == 0) return "image/png";
        if (strcmp(last_dot, ".pdf") == 0) return "application/pdf";
        if (strcmp(last_dot, ".svg") == 0) return "image/svg+xml";
        if (strcmp(last_dot, ".txt") == 0) return "text/plain";
        if(strcmp(last_dot, ".c") == 0) return "text/x-c";
    }

    return "application/octet-stream";
}