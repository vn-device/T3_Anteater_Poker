/******************************************************************************
 * File: PokerServer.c
 * Author: Team T3
 * Date: May 31, 2026
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
char g_LobbyPassword[MAX_MSG_LEN] = {0};

/* Game Logic State */
static GamePhase g_GamePhase = GAME_WAITING_FOR_SETUP;
static int g_GameStartTime = 0;
static int g_CurrentTurnSeat = 0;
static int g_RoundPhase = GAME_STATE_WAITING;
static int g_CurrentBet = 0;
static int g_Pot = 0;
static int g_ActionsThisPhase = 0;
static Deck g_GameDeck;

//=============================================================================
// GAME LOGIC FUNCTIONS
//=============================================================================

static void SetTurnToFirstActiveSeat(void);
static void BroadcastGameUpdate(int client_socket)
{
    char outBuffer[MAX_MSG_LEN];

    g_MasterTable.pot = g_Pot;
    g_MasterTable.state = g_RoundPhase;
    g_MasterTable.activeIdx = g_CurrentTurnSeat;
    
    /* 1. Broadcast Core Table State */
    BuildUpdateMessage(outBuffer, g_CurrentTurnSeat, g_CurrentBet, g_Pot, g_RoundPhase);
    if (client_socket != -1) {
        send(client_socket, outBuffer, strlen(outBuffer), 0);
    } else {
        for (int i = 0; i < g_MaxPlayers; i++) {
            if (g_MasterTable.players[i].socket != -1) {
                send(g_MasterTable.players[i].socket, outBuffer, strlen(outBuffer), 0);
            }
        }
    }

    /* 2. Broadcast Synchronized Player Profiles (Names/Chips/Status) */
    for (int i = 0; i < g_MaxPlayers; i++) {
        if (g_MasterTable.players[i].socket != -1 || g_MasterTable.players[i].name[0] != '\0') {
            BuildSyncMessage(outBuffer, i, g_MasterTable.players[i].points, g_MasterTable.players[i].isFolded, g_MasterTable.players[i].name);
            
            if (client_socket != -1) {
                send(client_socket, outBuffer, strlen(outBuffer), 0);
            } else {
                for (int j = 0; j < g_MaxPlayers; j++) {
                    if (g_MasterTable.players[j].socket != -1) {
                        send(g_MasterTable.players[j].socket, outBuffer, strlen(outBuffer), 0);
                    }
                }
            }
        }
    }

    /* 3. Broadcast Active Community Cards */
    for (int i = 0; i < 5; i++) {
        BuildCommunityMessage(outBuffer, i, g_MasterTable.community[i].rank, g_MasterTable.community[i].suit);
        if (client_socket != -1) {
            send(client_socket, outBuffer, strlen(outBuffer), 0);
        } else {
            for (int j = 0; j < g_MaxPlayers; j++) {
                if (g_MasterTable.players[j].socket != -1) {
                    send(g_MasterTable.players[j].socket, outBuffer, strlen(outBuffer), 0);
                }
            }
        }
    }
}

static void InitializeGameRound(void)
{
    printf("[Game] Initializing new round...\n");
    
    CreateDeck(&g_GameDeck);
    ShuffleDeck(&g_GameDeck);
    DealHoleCards(&g_MasterTable, &g_GameDeck);
    
    /* Safely Distribute Private Cards to Specific Sockets */
    for (int i = 0; i < g_MaxPlayers; i++) {
        if (g_MasterTable.players[i].socket != -1) {
            char cardBuf[MAX_MSG_LEN];
            BuildHoleCardsMessage(cardBuf, 
                g_MasterTable.players[i].hand[0].rank, g_MasterTable.players[i].hand[0].suit,
                g_MasterTable.players[i].hand[1].rank, g_MasterTable.players[i].hand[1].suit);
            send(g_MasterTable.players[i].socket, cardBuf, strlen(cardBuf), 0);
        }
    }
    
    g_RoundPhase = GAME_STATE_PRE_FLOP;
    g_CurrentBet = 0;
    g_Pot = 0;
    g_ActionsThisPhase = 0;
    g_MasterTable.dealerIdx = (g_MasterTable.dealerIdx + 1) % g_MaxPlayers;
    SetTurnToFirstActiveSeat();
}

static int CountActivePlayers(void)
{
    int active = 0;

    for (int i = 0; i < g_MaxPlayers; i++) {
        if (g_MasterTable.players[i].socket != -1 &&
            !g_MasterTable.players[i].isFolded) {
            active++;
        }
    }

    return active;
}

static void SetTurnToFirstActiveSeat(void)
{
    int attempts = 0;

    g_CurrentTurnSeat = g_MasterTable.dealerIdx;
    do {
        g_CurrentTurnSeat = (g_CurrentTurnSeat + 1) % g_MaxPlayers;
        attempts++;
    } while ((g_MasterTable.players[g_CurrentTurnSeat].socket == -1 ||
              g_MasterTable.players[g_CurrentTurnSeat].isFolded) &&
             attempts < g_MaxPlayers);
}

static int AdvanceRoundPhaseIfReady(void)
{
    int activePlayers = CountActivePlayers();

    if (activePlayers <= 1) {
        g_RoundPhase = GAME_STATE_SHOWDOWN;
        g_CurrentTurnSeat = -1;
        g_MasterTable.pot = g_Pot;
        DetermineWinner(&g_MasterTable);
        g_Pot = g_MasterTable.pot;
        BroadcastGameUpdate(-1);
        return 1;
    }

    if (g_ActionsThisPhase < activePlayers) {
        return 0;
    }

    g_ActionsThisPhase = 0;
    g_CurrentBet = 0;

    if (g_RoundPhase == GAME_STATE_PRE_FLOP) {
        g_RoundPhase = GAME_STATE_FLOP;
        g_MasterTable.state = g_RoundPhase;
        DealCommunityCards(&g_MasterTable, &g_GameDeck, 3);
    }
    else if (g_RoundPhase == GAME_STATE_FLOP) {
        g_RoundPhase = GAME_STATE_TURN;
        g_MasterTable.state = g_RoundPhase;
        DealCommunityCards(&g_MasterTable, &g_GameDeck, 1);
    }
    else if (g_RoundPhase == GAME_STATE_TURN) {
        g_RoundPhase = GAME_STATE_RIVER;
        g_MasterTable.state = g_RoundPhase;
        DealCommunityCards(&g_MasterTable, &g_GameDeck, 1);
    }
    else {
        g_RoundPhase = GAME_STATE_SHOWDOWN;
        g_CurrentTurnSeat = -1;
        g_MasterTable.pot = g_Pot;
        DetermineWinner(&g_MasterTable);
        g_Pot = g_MasterTable.pot;
        BroadcastGameUpdate(-1);
        return 1;
    }

    SetTurnToFirstActiveSeat();
    BroadcastGameUpdate(-1);
    return 1;
}

static void AdvanceTurn(void)
{
    int attempts = 0;

    if (AdvanceRoundPhaseIfReady()) {
        return;
    }

    do {
        g_CurrentTurnSeat = (g_CurrentTurnSeat + 1) % g_MaxPlayers;
        attempts++;
    } while ((g_MasterTable.players[g_CurrentTurnSeat].socket == -1 ||
              g_MasterTable.players[g_CurrentTurnSeat].isFolded) &&
             attempts < g_MaxPlayers);

    if (g_MasterTable.players[g_CurrentTurnSeat].socket == -1 || g_MasterTable.players[g_CurrentTurnSeat].isFolded) {
        AdvanceRoundPhaseIfReady();
        return;
    }

    BroadcastGameUpdate(-1);
}

//=============================================================================

int main(int argc, char *argv[])
{
    int server_fd, new_socket, activity, max_sd, sd;
    int client_sockets[MAX_PLAYERS];
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[MAX_MSG_LEN];
    fd_set readfds;
    char stream_bufs[MAX_PLAYERS][4096];
    int  stream_lens[MAX_PLAYERS];
    memset(stream_bufs, 0, sizeof(stream_bufs));
    memset(stream_lens, 0, sizeof(stream_lens));

    if (argc > 1 && strcmp(argv[1], "--self-test") == 0) {
        Deck testDeck;
        CreateDeck(&testDeck);
        if (testDeck.topIndex != 0 || testDeck.deck[0].rank != CARD_RANK_TWO) {
            fprintf(stderr, "Server self-test failed.\n");
            return EXIT_FAILURE;
        }
        printf("Server self-test passed.\n");
        return EXIT_SUCCESS;
    }

    memset(&g_MasterTable, 0, sizeof(Table));
    for (int i = 0; i < MAX_PLAYERS; i++) {
        client_sockets[i] = 0;
        g_MasterTable.players[i].socket = -1;
    }

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket allocation failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("Setsockopt SO_REUSEADDR failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

#ifdef SO_REUSEPORT
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

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
                            g_MasterTable.players[j].isFolded = 1;
                            break;
                        }
                    }

                    if (g_ConnectedPlayers == 0) {
                        g_IsGameConfigured = 0;
                        g_MaxPlayers = MAX_PLAYERS;
                        memset(g_LobbyPassword, 0, MAX_MSG_LEN);
                        g_GamePhase = GAME_WAITING_FOR_SETUP;
                        g_RoundPhase = GAME_STATE_WAITING;
                        g_CurrentTurnSeat = 0;
                        g_CurrentBet = 0;
                        g_Pot = 0;
                        g_ActionsThisPhase = 0;
                        printf("Lobby empty. Configuration state reset.\n");
                    }
                } 
                else {
                    if (stream_lens[i] + valread < 4096) {
                        memcpy(stream_bufs[i] + stream_lens[i], buffer, valread);
                        stream_lens[i] += valread;
                        stream_bufs[i][stream_lens[i]] = '\0';
                    } else {
                        /* Buffer overflow — flush and discard */
                        stream_bufs[i][0] = '\0';
                        stream_lens[i] = 0;
                    }

                    char *newline;
                    while ((newline = strchr(stream_bufs[i], '\n')) != NULL) {
                        *newline = '\0';
                        ParsedMessage msg;
                        if (ParseNetworkMessage(stream_bufs[i], &msg) == 0) {
                            char outBuffer[MAX_MSG_LEN];
    
                            if (msg.type == MSG_TYPE_ENTER) {
                                char expectedBotName[MAX_NAME_LEN];
                                int isBotJoin = 0;
    
                                snprintf(expectedBotName, sizeof(expectedBotName), "Bot_Seat_%d", msg.seat);
                                isBotJoin = (g_GamePhase == GAME_SPAWNING_BOTS &&
                                             strncmp(msg.name, expectedBotName, MAX_NAME_LEN) == 0);
    
                                if (msg.seat < 0 || msg.seat >= MAX_PLAYERS) {
                                    BuildErrorMessage(outBuffer, "Invalid seat bounds.");
                                    send(sd, outBuffer, strlen(outBuffer), 0);
                                }
                                else if (g_IsGameConfigured && msg.seat >= g_MaxPlayers) {
                                    BuildErrorMessage(outBuffer, "Seat outside configured table size.");
                                    send(sd, outBuffer, strlen(outBuffer), 0);
                                }
                                else if (g_GamePhase == GAME_ACTIVE_BETTING ||
                                         (g_GamePhase == GAME_SPAWNING_BOTS && !isBotJoin)) {
                                    BuildErrorMessage(outBuffer, "Game already started.");
                                    send(sd, outBuffer, strlen(outBuffer), 0);
                                }
                                else if (g_HostSocket != -1 && !isBotJoin && strncmp(msg.payload, g_LobbyPassword, MAX_MSG_LEN) != 0) {
                                    BuildErrorMessage(outBuffer, "Incorrect lobby password.");
                                    send(sd, outBuffer, strlen(outBuffer), 0);
                                }
                                else {
                                    /* Enforce Unique Usernames against all currently connected sockets */
                                    int isDuplicateName = 0;
                                    for (int s = 0; s < MAX_PLAYERS; s++) {
                                        if (g_MasterTable.players[s].socket != -1) {
                                            if (strncmp(g_MasterTable.players[s].name, msg.name, MAX_NAME_LEN) == 0) {
                                                isDuplicateName = 1;
                                                break;
                                            }
                                        }
                                    }
    
                                    if (isDuplicateName) {
                                        BuildErrorMessage(outBuffer, "Username already taken.");
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
                                        g_MasterTable.players[msg.seat].seat = msg.seat;
                                        g_MasterTable.players[msg.seat].isBot = isBotJoin ? 1 : 0;
                                        g_MasterTable.players[msg.seat].isFolded = 0;
                                        
                                        BuildOkMessage(outBuffer, msg.seat, msg.name, 1000);
                                        send(sd, outBuffer, strlen(outBuffer), 0);
    
                                        if (!g_IsGameConfigured && g_HostSocket == -1) {
                                            g_HostSocket = sd;
                                            /* Persist the host's payload as the authoritative lobby password */
                                            strncpy(g_LobbyPassword, msg.payload, MAX_MSG_LEN - 1);
                                            BuildHostMessage(outBuffer);
                                            send(sd, outBuffer, strlen(outBuffer), 0);
                                        }
    
                                        BroadcastGameUpdate(-1);
                                    }
                                }
                            } 
                            else if (msg.type == MSG_TYPE_SETUP && sd == g_HostSocket) {
                                if (msg.seat < 2 || msg.seat > MAX_PLAYERS) {
                                    BuildErrorMessage(outBuffer, "Player count must be between 2 and 8.");
                                    send(sd, outBuffer, strlen(outBuffer), 0);
                                }
                                else {
                                    g_MaxPlayers = msg.seat;
                                    g_IsGameConfigured = 1;
                                    g_GamePhase = GAME_WAITING_FOR_PLAYERS;
                                    g_RoundPhase = GAME_STATE_WAITING;
                                    g_CurrentTurnSeat = 0;
                                    g_CurrentBet = 0;
                                    g_Pot = 0;
                                    g_ActionsThisPhase = 0;
                                    g_GameStartTime = time(NULL);
                                    BroadcastGameUpdate(-1);
                                }
                            }
                            else if (msg.type == MSG_TYPE_START && sd == g_HostSocket) {
                                if (g_GamePhase == GAME_WAITING_FOR_PLAYERS) {
                                    int seatedCount = 0;
                                    for (int s = 0; s < g_MaxPlayers; s++) {
                                        if (g_MasterTable.players[s].socket != -1) seatedCount++;
                                    }
                                    
                                    if (seatedCount < g_MaxPlayers) {
                                        for (int s = 0; s < g_MaxPlayers; s++) {
                                            if (g_MasterTable.players[s].socket == -1) {
                                                pthread_t bot_tid;
                                                if (pthread_create(&bot_tid, NULL, RunPokerBotThread, (void*)(intptr_t)s) == 0) {
                                                    pthread_detach(bot_tid);
                                                }
                                            }
                                        }
                                        g_GamePhase = GAME_SPAWNING_BOTS;
                                    } 
                                    else {
                                        InitializeGameRound();
                                        g_GamePhase = GAME_ACTIVE_BETTING;
                                        BroadcastGameUpdate(-1);
                                    }
                                }
                            }
                            else {
                                if (msg.type == MSG_TYPE_ACTION) {
                                    int actionType = (unsigned char)msg.payload[0];
    
                                    if (g_GamePhase != GAME_ACTIVE_BETTING) {
                                        BuildErrorMessage(outBuffer, "Game is not active.");
                                        send(sd, outBuffer, strlen(outBuffer), 0);
                                    }
                                    else if (msg.seat < 0 || msg.seat >= g_MaxPlayers) {
                                        BuildErrorMessage(outBuffer, "Invalid action seat.");
                                        send(sd, outBuffer, strlen(outBuffer), 0);
                                    }
                                    else if (g_MasterTable.players[msg.seat].socket != sd) {
                                        BuildErrorMessage(outBuffer, "Action rejected for non-owned seat.");
                                        send(sd, outBuffer, strlen(outBuffer), 0);
                                    }
                                    else if (msg.seat != g_CurrentTurnSeat) {
                                        BuildErrorMessage(outBuffer, "It is not your turn.");
                                        send(sd, outBuffer, strlen(outBuffer), 0);
                                    }
                                    else if (g_MasterTable.players[msg.seat].isFolded) {
                                        BuildErrorMessage(outBuffer, "Folded players cannot act.");
                                        send(sd, outBuffer, strlen(outBuffer), 0);
                                    }
                                    else {
                                        switch (actionType) {
                                            case ACTION_TYPE_FOLD:
                                                g_MasterTable.players[msg.seat].isFolded = 1;
                                                break;
                                            case ACTION_TYPE_CHECK:
                                                if (g_CurrentBet > 0) {
                                                    BuildErrorMessage(outBuffer, "Cannot check against an active bet.");
                                                    send(sd, outBuffer, strlen(outBuffer), 0);
                                                    goto next_message;
                                                }
                                                break;
                                            case ACTION_TYPE_CALL:
                                                if (g_CurrentBet > g_MasterTable.players[msg.seat].points) {
                                                    BuildErrorMessage(outBuffer, "Insufficient points to call.");
                                                    send(sd, outBuffer, strlen(outBuffer), 0);
                                                    goto next_message;
                                                }
                                                g_MasterTable.players[msg.seat].points -= g_CurrentBet;
                                                g_Pot += g_CurrentBet;
                                                break;
                                            case ACTION_TYPE_RAISE:
                                                if (msg.amount <= g_CurrentBet || msg.amount > g_MasterTable.players[msg.seat].points) {
                                                    BuildErrorMessage(outBuffer, "Invalid raise amount.");
                                                    send(sd, outBuffer, strlen(outBuffer), 0);
                                                    goto next_message;
                                                }
                                                g_CurrentBet = msg.amount;
                                                g_Pot += msg.amount;
                                                g_MasterTable.players[msg.seat].points -= msg.amount;
                                                break;
                                            default:
                                                BuildErrorMessage(outBuffer, "Unknown action type.");
                                                send(sd, outBuffer, strlen(outBuffer), 0);
                                                goto next_message;
                                        }
                                        g_ActionsThisPhase++;
                                        AdvanceTurn();
                                    }
                                } else {
                                    char okBuffer[MAX_MSG_LEN];
                                    BuildOkMessage(okBuffer, msg.seat, msg.name, 1000);
                                    send(sd, okBuffer, strlen(okBuffer), 0);
                                }
                            }
                        }
                        
                        next_message:;
                        int remaining = stream_lens[i] - (newline - stream_bufs[i]) - 1;
                        memmove(stream_bufs[i], newline + 1, remaining);
                        stream_lens[i] = remaining;
                        stream_bufs[i][stream_lens[i]] = '\0';
                    }
                }
            }
        }
        
        if (g_GamePhase == GAME_SPAWNING_BOTS) {
            int seatedCount = 0;
            for (int i = 0; i < g_MaxPlayers; i++) {
                if (g_MasterTable.players[i].socket != -1) seatedCount++;
            }
            
            if (seatedCount == g_MaxPlayers) {
                InitializeGameRound();
                g_GamePhase = GAME_ACTIVE_BETTING;
                BroadcastGameUpdate(-1);
            }
        }
    }
    return 0;
}
