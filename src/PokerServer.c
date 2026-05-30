/******************************************************************************
 * File: PokerServer.c
 * Author: Team T3
 * Date: May 30, 2026
 * 
 * * Description:
 * Multi-client authoritative server utilizing select() for I/O multiplexing.
 * Spawns internal embedded loopback threads for autonomous poker bots.
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <stdint.h>
#include <pthread.h>
#include "GameData.h"
#include "GameProtocol.h"
#include "PokerBot.h"

#define PORT 8003
#define MAX_PENDING 10

Table g_MasterTable;
int g_ConnectedPlayers = 0;
int g_IsGameConfigured = 0;
int g_MaxPlayers = MAX_PLAYERS;
int g_HostSocket = -1;

int main(void) 
{
    int server_fd, new_socket, activity, max_sd, sd;
    int client_sockets[MAX_PLAYERS];
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[MAX_MSG_LEN];
    fd_set readfds;

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

    printf("Authoritative Server active on port %d...\n", PORT);

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        max_sd = server_fd;

        for (int i = 0; i < MAX_PLAYERS; i++) {
            sd = client_sockets[i];
            if (sd > 0) FD_SET(sd, &readfds);
            if (sd > max_sd) max_sd = sd;
        }

        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

        if (activity < 0) continue;

        if (FD_ISSET(server_fd, &readfds)) {
            if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
                continue;
            }

            if (g_IsGameConfigured && g_ConnectedPlayers >= g_MaxPlayers) {
                char outBuffer[MAX_MSG_LEN];
                BuildErrorMessage(outBuffer, "Server is full.");
                send(new_socket, outBuffer, strlen(outBuffer), 0);
                close(new_socket);
            }
            else {
                for (int i = 0; i < MAX_PLAYERS; i++) {
                    if (client_sockets[i] == 0) {
                        client_sockets[i] = new_socket;
                        g_ConnectedPlayers++;
                        break;
                    }
                }
            }
        }

        for (int i = 0; i < MAX_PLAYERS; i++) {
            sd = client_sockets[i];

            if (sd > 0 && FD_ISSET(sd, &readfds)) {
                memset(buffer, 0, MAX_MSG_LEN);
                ssize_t valread = read(sd, buffer, MAX_MSG_LEN - 1);

                if (valread == 0) {
                    close(sd);
                    client_sockets[i] = 0;
                    g_ConnectedPlayers--;

                    if (sd == g_HostSocket) {
                        g_HostSocket = -1;
                        printf("Lobby Host connection terminated.\n");
                    }

                    for (int j = 0; j < MAX_PLAYERS; j++) {
                        if (g_MasterTable.players[j].socket == sd) {
                            g_MasterTable.players[j].socket = -1;
                            break;
                        }
                    }

                    if (g_ConnectedPlayers == 0) {
                        g_IsGameConfigured = 0;
                        g_MaxPlayers = MAX_PLAYERS;
                        printf("Lobby empty. Configuration state reset.\n");
                    }
                } 
                else {
                    ParsedMessage msg;
                    if (ParseNetworkMessage(buffer, &msg) == 0) {
                        char outBuffer[MAX_MSG_LEN];

                        if (msg.type == MSG_TYPE_ENTER) {
                            if (msg.seat < 0 || msg.seat >= MAX_PLAYERS) {
                                BuildErrorMessage(outBuffer, "Invalid seat bounds.");
                                send(sd, outBuffer, strlen(outBuffer), 0);
                            }
                            else if (g_MasterTable.players[msg.seat].socket != -1) {
                                char errMsg[MAX_MSG_LEN];
                                snprintf(errMsg, sizeof(errMsg), "Seat %d occupied.", msg.seat);
                                BuildErrorMessage(outBuffer, errMsg);
                                send(sd, outBuffer, strlen(outBuffer), 0);
                            } 
                            else {
                                g_MasterTable.players[msg.seat].socket = sd;
                                strncpy(g_MasterTable.players[msg.seat].name, msg.name, MAX_NAME_LEN - 1);
                                g_MasterTable.players[msg.seat].points = 1000;
                                
                                BuildOkMessage(outBuffer, msg.seat, msg.name, 1000);
                                send(sd, outBuffer, strlen(outBuffer), 0);
                                printf("Seat %d bound to FD %d ('%s')\n", msg.seat, sd, msg.name);

                                if (!g_IsGameConfigured && g_HostSocket == -1) {
                                    g_HostSocket = sd;
                                    BuildHostMessage(outBuffer);
                                    send(sd, outBuffer, strlen(outBuffer), 0);
                                    printf("FD %d designated Lobby Host.\n", sd);
                                }
                            }
                        } 
                        else if (msg.type == MSG_TYPE_SETUP && sd == g_HostSocket) {
                            g_MaxPlayers = msg.seat; 
                            g_IsGameConfigured = 1;
                            printf("Host finalized layout: %d max seats.\n", g_MaxPlayers);

                            for (int s = 0; s < g_MaxPlayers; s++) {
                                if (g_MasterTable.players[s].socket == -1) {
                                    pthread_t bot_tid;
                                    if (pthread_create(&bot_tid, NULL, RunPokerBotThread, (void*)(intptr_t)s) != 0) {
                                        perror("Thread spawn failed");
                                    }
                                    else {
                                        pthread_detach(bot_tid);
                                    }
                                }
                            }
                        }
                        else {
                            printf("FD %d [Seat %d] Action: %d\n", sd, msg.seat, msg.type);
                            BuildOkMessage(outBuffer, msg.seat, msg.name, 1000);
                            send(sd, outBuffer, strlen(outBuffer), 0);
                        }
                    }
                }
            }
        }
    }
    return 0;
}