/******************************************************************************
 * File: PokerServer.c
 * Author: Team T3
 * Date: May 22, 2026
 * 
 * * Description:
 * Multi-client authoritative server utilizing select() for I/O multiplexing.
 * Manages connection pooling, disconnects, seat validation, and packet parsing.
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

/* Global master state tracking all connected clients and game data */
Table g_MasterTable;

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

    /* Initialize all client socket trackers and the master table to empty state */
    memset(&g_MasterTable, 0, sizeof(Table));
    for (int i = 0; i < MAX_PLAYERS; i++) {
        client_sockets[i] = 0;
        g_MasterTable.players[i].socket = -1;
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

                    /* Free the associated seat in the master table so others can join */
                    for (int j = 0; j < MAX_PLAYERS; j++) {
                        if (g_MasterTable.players[j].socket == sd) {
                            g_MasterTable.players[j].socket = -1;
                            printf("Freed Table Seat %d\n", j);
                            break;
                        }
                    }
                } 
                /* Valid data payload received */
                else {
                    ParsedMessage msg;
                    if (ParseNetworkMessage(buffer, &msg) == 0) {
                        char outBuffer[MAX_MSG_LEN];

                        /* Handle explicit login handshake requests */
                        if (msg.type == MSG_TYPE_ENTER) {
                            if (msg.seat < 0 || msg.seat >= MAX_PLAYERS) {
                                BuildErrorMessage(outBuffer, "Invalid seat index bounds.");
                                send(sd, outBuffer, strlen(outBuffer), 0);
                            }
                            else if (g_MasterTable.players[msg.seat].socket != -1) {
                                /* Memory block is occupied. Scan for free slots to assist the user. */
                                char avail[64] = {0};
                                for (int j = 0; j < MAX_PLAYERS; j++) {
                                    if (g_MasterTable.players[j].socket == -1) {
                                        char temp[8];
                                        snprintf(temp, sizeof(temp), "%d ", j);
                                        strcat(avail, temp);
                                    }
                                }
                                char errMsg[MAX_MSG_LEN];
                                snprintf(errMsg, sizeof(errMsg), "Seat %d is already occupied.\nAvailable seats: %s", msg.seat, avail);
                                
                                BuildErrorMessage(outBuffer, errMsg);
                                send(sd, outBuffer, strlen(outBuffer), 0);
                                printf("FD %d attempted to join taken seat %d.\n", sd, msg.seat);
                            } 
                            else {
                                /* Memory block is vacant. Bind the file descriptor and allocate standard points. */
                                g_MasterTable.players[msg.seat].socket = sd;
                                strncpy(g_MasterTable.players[msg.seat].name, msg.name, MAX_NAME_LEN - 1);
                                g_MasterTable.players[msg.seat].points = 1000;
                                
                                BuildOkMessage(outBuffer, msg.seat, msg.name, 1000);
                                send(sd, outBuffer, strlen(outBuffer), 0);
                                printf("FD %d successfully assigned to Seat %d as '%s'\n", sd, msg.seat, msg.name);
                            }
                        } 
                        else {
                            /* Fallback parser for standard gameplay actions (temporary echo) */
                            printf("FD %d [Seat %d] Action/Update: %d\n", sd, msg.seat, msg.type);
                            BuildOkMessage(outBuffer, msg.seat, msg.name, 1000);
                            send(sd, outBuffer, strlen(outBuffer), 0);
                        }
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