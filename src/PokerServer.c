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
#include <time.h>
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

/* Game State Variables */
typedef enum {
    GAME_WAITING_FOR_SETUP,
    GAME_WAITING_FOR_PLAYERS,
    GAME_SPAWNING_BOTS,
    GAME_ACTIVE_BETTING
} GamePhase;

Table g_MasterTable;
int g_ConnectedPlayers = 0;
int g_IsGameConfigured = 0;
int g_MaxPlayers = MAX_PLAYERS;
int g_HostSocket = -1;

/* Game Logic State */
static GamePhase g_GamePhase = GAME_WAITING_FOR_SETUP;
static int g_GameStartTime = 0;
static int g_CurrentTurnSeat = 0;
static int g_RoundPhase = GAME_STATE_PRE_FLOP;
static int g_CurrentBet = 0;
static int g_Pot = 0;
static Deck g_GameDeck;

//=============================================================================
// GAME LOGIC FUNCTIONS
//=============================================================================

static void InitializeGameRound(void)
{
    printf("[Game] Initializing new round...\n");
    
    /* Shuffle and prepare deck */
    CreateDeck(&g_GameDeck);
    ShuffleDeck(&g_GameDeck);
    
    /* Deal hole cards to seated players */
    DealHoleCards(&g_MasterTable, &g_GameDeck);
    
    for (int i = 0; i < g_MaxPlayers; i++) {
        if (g_MasterTable.players[i].socket != -1) {
            printf("Dealt to Seat %d (%s): %d%c, %d%c\n", i, g_MasterTable.players[i].name,
                   g_MasterTable.players[i].hand[0].rank,
                   g_MasterTable.players[i].hand[0].suit,
                   g_MasterTable.players[i].hand[1].rank,
                   g_MasterTable.players[i].hand[1].suit);
        }
    }
    
    /* Initialize betting state */
    g_RoundPhase = GAME_STATE_PRE_FLOP;
    g_CurrentBet = 0;
    g_Pot = 0;
    g_CurrentTurnSeat = 0;
    
    printf("[Game] Round initialized. Awaiting player actions.\n");
}

static void BroadcastGameUpdate(int client_socket)
{
    char outBuffer[MAX_MSG_LEN];
    
    /* Build UPDATE message with current game state */
    BuildUpdateMessage(outBuffer, g_CurrentTurnSeat, g_CurrentBet, g_Pot, g_RoundPhase);
    
    if (client_socket != -1) {
        /* Send to specific client */
        send(client_socket, outBuffer, strlen(outBuffer), 0);
    } else {
        /* Broadcast to all seated players */
        for (int i = 0; i < g_MaxPlayers; i++) {
            if (g_MasterTable.players[i].socket != -1 && !g_MasterTable.players[i].isFolded) {
                send(g_MasterTable.players[i].socket, outBuffer, strlen(outBuffer), 0);
            }
        }
    }
}

static void AdvanceTurn(void)
{
    /* Find next non-folded player */
    int attempts = 0;
    do {
        g_CurrentTurnSeat = (g_CurrentTurnSeat + 1) % g_MaxPlayers;
        attempts++;
    } while (g_MasterTable.players[g_CurrentTurnSeat].socket == -1 && 
             g_MasterTable.players[g_CurrentTurnSeat].isFolded && 
             attempts < g_MaxPlayers);
    
    printf("[Game] Turn advanced to Seat %d\n", g_CurrentTurnSeat);
    BroadcastGameUpdate(-1);  /* Broadcast to all */
}

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
                            g_GamePhase = GAME_WAITING_FOR_PLAYERS;
                            g_GameStartTime = time(NULL);
                            printf("Host finalized layout: %d max seats.\n", g_MaxPlayers);
                        }
                        else if (msg.type == MSG_TYPE_START && sd == g_HostSocket) {
                            if (g_GamePhase == GAME_WAITING_FOR_PLAYERS) {
                                printf("[Server] Host initiated manual start sequence.\n");
                                
                                int seatedCount = 0;
                                for (int s = 0; s < g_MaxPlayers; s++) {
                                    if (g_MasterTable.players[s].socket != -1) seatedCount++;
                                }
                                
                                /* If seats are empty, spawn threads */
                                if (seatedCount < g_MaxPlayers) {
                                    for (int s = 0; s < g_MaxPlayers; s++) {
                                        if (g_MasterTable.players[s].socket == -1) {
                                            pthread_t bot_tid;
                                            if (pthread_create(&bot_tid, NULL, RunPokerBotThread, (void*)(intptr_t)s) == 0) {
                                                pthread_detach(bot_tid);
                                            }
                                        }
                                    }
                                    /* Shift to intermediate state to await bot loopback handshakes */
                                    g_GamePhase = GAME_SPAWNING_BOTS;
                                } 
                                else {
                                    /* No bots needed, jump straight to active gameplay */
                                    InitializeGameRound();
                                    g_GamePhase = GAME_ACTIVE_BETTING;
                                    BroadcastGameUpdate(-1);
                                }
                            }
                        }
                        else {
                            /* Process player action */
                            if (msg.type == MSG_TYPE_ACTION) {
                                printf("FD %d [Seat %d] Action: %d (Amount: %d)\n", sd, msg.seat, msg.type, msg.amount);
                                
                                /* Validate action is from current turn */
                                if (msg.seat == g_CurrentTurnSeat) {
                                    /* Update table state based on action */
                                    switch (msg.amount) {  /* Note: amount field encodes action type in protocol */
                                        case ACTION_TYPE_FOLD:
                                            g_MasterTable.players[msg.seat].isFolded = 1;
                                            printf("Seat %d FOLDED\n", msg.seat);
                                            break;
                                        case ACTION_TYPE_CHECK:
                                            printf("Seat %d CHECKED\n", msg.seat);
                                            break;
                                        case ACTION_TYPE_CALL:
                                            g_MasterTable.players[msg.seat].points -= g_CurrentBet;
                                            g_Pot += g_CurrentBet;
                                            printf("Seat %d CALLED for %d\n", msg.seat, g_CurrentBet);
                                            break;
                                        case ACTION_TYPE_RAISE:
                                            g_Pot += msg.amount;
                                            g_MasterTable.players[msg.seat].points -= msg.amount;
                                            g_CurrentBet = msg.amount;
                                            printf("Seat %d RAISED to %d\n", msg.seat, g_CurrentBet);
                                            break;
                                    }
                                    
                                    /* Advance turn to next player */
                                    AdvanceTurn();
                                } else {
                                    printf("Out of turn action ignored (seat %d, current turn %d)\n", msg.seat, g_CurrentTurnSeat);
                                }
                            } else {
                                /* Other message types */
                                char okBuffer[MAX_MSG_LEN];
                                BuildOkMessage(okBuffer, msg.seat, msg.name, 1000);
                                send(sd, okBuffer, strlen(okBuffer), 0);
                            }
                        }
                    }
                }
            }
        }
        
        /* Main Execution Loop: Bot Connection Polling */
        if (g_GamePhase == GAME_SPAWNING_BOTS) {
            int seatedCount = 0;
            for (int i = 0; i < g_MaxPlayers; i++) {
                if (g_MasterTable.players[i].socket != -1) seatedCount++;
            }
            
            /* Once the bot pthreads complete their ENTER handshakes, deal the cards */
            if (seatedCount == g_MaxPlayers) {
                printf("[Game] All bot threads connected. Starting %d-player round.\n", seatedCount);
                InitializeGameRound();
                g_GamePhase = GAME_ACTIVE_BETTING;
                BroadcastGameUpdate(-1);
            }
        }
    }
    return 0;
}