/******************************************************************************
 * File: PokerServer.c
 * Author: Team T3
 * Date: May 22, 2026
 * 
 * * Description:
 * Multi-client authoritative server utilizing select() for I/O multiplexing.
 * Manages connection pooling, disconnects, and packet parsing.
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/select.h>
#include "GameData.h"
#include "GameProtocol.h"

#define PORT 8003
#define MAX_PENDING 10

int main(void) 
{
    int server_fd, new_socket, activity, max_sd, sd;
    int client_sockets[MAX_PLAYERS];
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[MAX_MSG_LEN];

    /* fd_set is a bit array mapped to system file descriptors */
    fd_set readfds;

    /* Initialize all client socket trackers to 0 (empty) */
    for (int i = 0; i < MAX_PLAYERS; i++) {
        client_sockets[i] = 0;
    }

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket allocation failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("Setsockopt failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, MAX_PENDING) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Multi-client Server listening on port %d...\n", PORT);

    /* The Main Event Loop */
    while (1) {
        /* select() modifies the fd_set directly. It must be cleared and rebuilt every loop iteration. */
        FD_ZERO(&readfds);

        /* Add the master listening socket to the set */
        FD_SET(server_fd, &readfds);
        max_sd = server_fd;

        /* Add valid child sockets to the set */
        for (int i = 0; i < MAX_PLAYERS; i++) {
            sd = client_sockets[i];

            if (sd > 0) {
                FD_SET(sd, &readfds);
            }

            /* select() needs the highest file descriptor number to bound its search array */
            if (sd > max_sd) {
                max_sd = sd;
            }
        }

        /* Block thread until activity occurs on any tracked file descriptor */
        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

        if ((activity < 0)) {
            perror("Select error");
            continue;
        }

        /* CONDITION 1: Activity on the master socket = Incoming connection request */
        if (FD_ISSET(server_fd, &readfds)) {
            if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
                perror("Accept failed");
                continue;
            }

            printf("New connection: FD %d, IP %s, PORT %d\n", 
                   new_socket, inet_ntoa(address.sin_addr), ntohs(address.sin_port));

            /* Add the new descriptor to the tracking array */
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (client_sockets[i] == 0) {
                    client_sockets[i] = new_socket;
                    printf("Assigned to internal tracking index %d\n", i);
                    break;
                }
            }
        }

        /* CONDITION 2: Activity on a child socket = Incoming network data or disconnect */
        for (int i = 0; i < MAX_PLAYERS; i++) {
            sd = client_sockets[i];

            if (FD_ISSET(sd, &readfds)) {
                memset(buffer, 0, MAX_MSG_LEN);
                ssize_t valread = read(sd, buffer, MAX_MSG_LEN - 1);

                /* Read returning 0 indicates an explicit TCP FIN (client disconnected) */
                if (valread == 0) {
                    getpeername(sd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
                    printf("Host disconnected: IP %s, PORT %d\n", 
                           inet_ntoa(address.sin_addr), ntohs(address.sin_port));

                    /* Graceful teardown: close file descriptor and release array slot */
                    close(sd);
                    client_sockets[i] = 0;
                } 
                /* Valid data payload received */
                else {
                    ParsedMessage msg;
                    if (ParseNetworkMessage(buffer, &msg) == 0) {
                        printf("FD %d [Seat %d] Payload: %d\n", sd, msg.seat, msg.type);
                        
                        /* Echo response to prove bidirectional routing works */
                        char outBuffer[MAX_MSG_LEN];
                        BuildOkMessage(outBuffer, msg.seat, msg.name, 1000);
                        send(sd, outBuffer, strlen(outBuffer), 0);
                    }
                    else {
                        printf("Malformed packet from FD %d: %s\n", sd, buffer);
                    }
                }
            }
        }
    }

    return 0;
}