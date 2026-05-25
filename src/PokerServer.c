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
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/select.h>
#include "GameData.h"
#include "GameProtocol.h"

#define PORT 8003
#define MAX_PENDING 10
#define STARTING_POINTS 1000

typedef struct {
    int socket;
    int seat;
    char pending[MAX_MSG_LEN];
    size_t pendingLen;
} ClientConnection;

static void ResetPlayer(Player *pPlayer, int seat)
{
    if (pPlayer == NULL) return;

    memset(pPlayer, 0, sizeof(Player));
    pPlayer->seat = seat;
    pPlayer->socket = -1;
    pPlayer->points = STARTING_POINTS;
}

static void InitializeTable(Table *pTable)
{
    if (pTable == NULL) return;

    memset(pTable, 0, sizeof(Table));
    pTable->state = GAME_STATE_WAITING;
    pTable->dealerIdx = 0;
    pTable->activeIdx = 0;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        ResetPlayer(&pTable->players[i], i);
    }
}

static void InitializeClients(ClientConnection clients[])
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        clients[i].socket = -1;
        clients[i].seat = -1;
        clients[i].pending[0] = '\0';
        clients[i].pendingLen = 0;
    }
}

static int FindFreeClientSlot(const ClientConnection clients[])
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clients[i].socket < 0) {
            return i;
        }
    }

    return -1;
}

static int SendAll(int socketFd, const char *message)
{
    size_t totalSent = 0;
    size_t messageLen;

    if (message == NULL) return -1;

    messageLen = strlen(message);
    while (totalSent < messageLen) {
        ssize_t sent = send(socketFd, message + totalSent, messageLen - totalSent, 0);

        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("send");
            return -1;
        }

        if (sent == 0) {
            return -1;
        }

        totalSent += (size_t)sent;
    }

    return 0;
}

static void SendErrorToClient(int socketFd, const char *message)
{
    char outBuffer[MAX_MSG_LEN];

    BuildErrorMessage(outBuffer, message);
    SendAll(socketFd, outBuffer);
}

static void BroadcastToOtherClients(const ClientConnection clients[],
                                    int sourceSocket,
                                    const char *message)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clients[i].socket >= 0 && clients[i].socket != sourceSocket) {
            SendAll(clients[i].socket, message);
        }
    }
}

static int CreateListeningSocket(int port)
{
    int serverFd;
    int opt = 1;
    struct sockaddr_in address;

    serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) {
        perror("Socket allocation failed");
        return -1;
    }

    if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR failed");
        close(serverFd);
        return -1;
    }

#ifdef SO_REUSEPORT
    if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEPORT failed");
        close(serverFd);
        return -1;
    }
#endif

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons((uint16_t)port);

    if (bind(serverFd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        close(serverFd);
        return -1;
    }

    if (listen(serverFd, MAX_PENDING) < 0) {
        perror("Listen failed");
        close(serverFd);
        return -1;
    }

    return serverFd;
}

static const char *ActionName(int action)
{
    switch (action) {
        case ACTION_TYPE_FOLD:
            return "FOLD";
        case ACTION_TYPE_CHECK:
            return "CHECK";
        case ACTION_TYPE_CALL:
            return "CALL";
        case ACTION_TYPE_RAISE:
            return "RAISE";
        default:
            return "UNKNOWN";
    }
}

static void DisconnectClient(ClientConnection clients[], Table *pTable, int index)
{
    int socketFd;
    int seat;

    if (clients == NULL || pTable == NULL || index < 0 || index >= MAX_PLAYERS) return;

    socketFd = clients[index].socket;
    seat = clients[index].seat;
    if (socketFd < 0) return;

    if (seat >= 0 && seat < MAX_PLAYERS && pTable->players[seat].socket == socketFd) {
        printf("Seat %d (%s) disconnected.\n",
               seat,
               pTable->players[seat].name[0] ? pTable->players[seat].name : "unregistered");
        ResetPlayer(&pTable->players[seat], seat);
    }
    else {
        printf("Unregistered client FD %d disconnected.\n", socketFd);
    }

    close(socketFd);
    clients[index].socket = -1;
    clients[index].seat = -1;
    clients[index].pending[0] = '\0';
    clients[index].pendingLen = 0;
}

static void HandleEnterMessage(ClientConnection clients[],
                               Table *pTable,
                               int index,
                               const ParsedMessage *pMsg)
{
    Player *pPlayer;
    int socketFd;
    int oldSeat;
    char outBuffer[MAX_MSG_LEN];

    socketFd = clients[index].socket;
    if (pMsg->seat < 0 || pMsg->seat >= MAX_PLAYERS) {
        SendErrorToClient(socketFd, "Seat must be between 0 and 7");
        return;
    }

    if (pMsg->name[0] == '\0') {
        SendErrorToClient(socketFd, "Player name is required");
        return;
    }

    if (pTable->players[pMsg->seat].socket >= 0 &&
        pTable->players[pMsg->seat].socket != socketFd) {
        SendErrorToClient(socketFd, "Requested seat is already occupied");
        return;
    }

    oldSeat = clients[index].seat;
    if (oldSeat >= 0 &&
        oldSeat < MAX_PLAYERS &&
        oldSeat != pMsg->seat &&
        pTable->players[oldSeat].socket == socketFd) {
        ResetPlayer(&pTable->players[oldSeat], oldSeat);
    }

    pPlayer = &pTable->players[pMsg->seat];
    pPlayer->socket = socketFd;
    pPlayer->seat = pMsg->seat;
    pPlayer->isBot = 0;
    pPlayer->isFolded = 0;
    snprintf(pPlayer->name, sizeof(pPlayer->name), "%s", pMsg->name);
    clients[index].seat = pMsg->seat;

    BuildOkMessage(outBuffer, pPlayer->seat, pPlayer->name, pPlayer->points);
    SendAll(socketFd, outBuffer);

    printf("Registered FD %d as seat %d (%s) with %d points.\n",
           socketFd, pPlayer->seat, pPlayer->name, pPlayer->points);
}

static void HandleActionMessage(ClientConnection clients[],
                                Table *pTable,
                                int index,
                                const ParsedMessage *pMsg,
                                int *pCurrentHighest)
{
    Player *pPlayer;
    int socketFd = clients[index].socket;
    int action = (unsigned char)pMsg->payload[0];
    char outBuffer[MAX_MSG_LEN];

    if (clients[index].seat < 0) {
        SendErrorToClient(socketFd, "Register with ENTER before sending ACTION");
        return;
    }

    if (pMsg->seat != clients[index].seat) {
        SendErrorToClient(socketFd, "ACTION seat does not match this connection");
        return;
    }

    if (action < ACTION_TYPE_FOLD || action > ACTION_TYPE_RAISE) {
        SendErrorToClient(socketFd, "Unknown ACTION type");
        return;
    }

    pPlayer = &pTable->players[pMsg->seat];
    if (pPlayer->socket != socketFd) {
        SendErrorToClient(socketFd, "Seat is not registered to this connection");
        return;
    }

    if (action == ACTION_TYPE_CHECK && pMsg->amount != 0) {
        SendErrorToClient(socketFd, "CHECK requires amount 0");
        return;
    }

    if (!IsValidAction(pPlayer, action, pMsg->amount, *pCurrentHighest)) {
        SendErrorToClient(socketFd, "Illegal wager for current point balance");
        return;
    }

    switch (action) {
        case ACTION_TYPE_FOLD:
            pPlayer->isFolded = 1;
            break;
        case ACTION_TYPE_CALL:
            pPlayer->points -= pMsg->amount;
            pTable->pot += pMsg->amount;
            break;
        case ACTION_TYPE_RAISE:
            pPlayer->points -= pMsg->amount;
            pTable->pot += pMsg->amount;
            *pCurrentHighest = pMsg->amount;
            break;
        case ACTION_TYPE_CHECK:
        default:
            break;
    }

    BuildOkMessage(outBuffer, pPlayer->seat, pPlayer->name, pPlayer->points);
    SendAll(socketFd, outBuffer);

    BuildActionMessage(outBuffer, pPlayer->seat, action, pMsg->amount);
    BroadcastToOtherClients(clients, socketFd, outBuffer);

    printf("Seat %d %s amount %d | pot=%d | points=%d\n",
           pPlayer->seat,
           ActionName(action),
           pMsg->amount,
           pTable->pot,
           pPlayer->points);
}

static void HandleClientMessage(ClientConnection clients[],
                                Table *pTable,
                                int index,
                                const char *rawMessage,
                                int *pCurrentHighest)
{
    ParsedMessage msg;
    int socketFd = clients[index].socket;

    if (ParseNetworkMessage(rawMessage, &msg) != 0) {
        printf("Malformed packet from FD %d: %s\n", socketFd, rawMessage);
        SendErrorToClient(socketFd, "Malformed packet");
        return;
    }

    switch (msg.type) {
        case MSG_TYPE_ENTER:
            HandleEnterMessage(clients, pTable, index, &msg);
            break;
        case MSG_TYPE_ACTION:
            HandleActionMessage(clients, pTable, index, &msg, pCurrentHighest);
            break;
        default:
            SendErrorToClient(socketFd, "Unsupported client command");
            break;
    }
}

static void ProcessReceivedBytes(ClientConnection clients[],
                                 Table *pTable,
                                 int index,
                                 const char *buffer,
                                 ssize_t length,
                                 int *pCurrentHighest)
{
    for (ssize_t i = 0; i < length; i++) {
        char ch = buffer[i];

        if (ch == '\r') {
            continue;
        }

        if (ch == '\n') {
            if (clients[index].pendingLen > 0) {
                clients[index].pending[clients[index].pendingLen] = '\0';
                HandleClientMessage(clients,
                                    pTable,
                                    index,
                                    clients[index].pending,
                                    pCurrentHighest);
                clients[index].pendingLen = 0;
                clients[index].pending[0] = '\0';
            }
            continue;
        }

        if (clients[index].pendingLen >= MAX_MSG_LEN - 1) {
            SendErrorToClient(clients[index].socket, "Message exceeds maximum length");
            clients[index].pendingLen = 0;
            clients[index].pending[0] = '\0';
            continue;
        }

        clients[index].pending[clients[index].pendingLen++] = ch;
    }
}

static int RunSelfTest(void)
{
    Table table;
    ClientConnection clients[MAX_PLAYERS];
    int currentHighest = 0;
    int listener;
    int sockets[2];
    char buffer[MAX_MSG_LEN];
    ssize_t bytesRead;

    InitializeTable(&table);
    InitializeClients(clients);

    listener = CreateListeningSocket(0);
    if (listener < 0) {
        return EXIT_FAILURE;
    }
    close(listener);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) < 0) {
        perror("socketpair self-test failed");
        return EXIT_FAILURE;
    }

    clients[0].socket = sockets[0];
    HandleClientMessage(clients,
                        &table,
                        0,
                        "ENTER SelfTest SEAT 0 PASSWORD test",
                        &currentHighest);

    bytesRead = recv(sockets[1], buffer, sizeof(buffer) - 1, 0);
    if (bytesRead <= 0) {
        perror("self-test receive failed");
        close(sockets[0]);
        close(sockets[1]);
        return EXIT_FAILURE;
    }
    buffer[bytesRead] = '\0';
    if (strstr(buffer, "OK SEAT=0 NAME=SelfTest POINTS=1000") == NULL) {
        fprintf(stderr, "self-test expected OK handshake, got: %s\n", buffer);
        close(sockets[0]);
        close(sockets[1]);
        return EXIT_FAILURE;
    }

    HandleClientMessage(clients,
                        &table,
                        0,
                        "ACTION SEAT 0 TYPE 2 AMOUNT 0",
                        &currentHighest);

    bytesRead = recv(sockets[1], buffer, sizeof(buffer) - 1, 0);
    if (bytesRead <= 0) {
        perror("self-test action receive failed");
        close(sockets[0]);
        close(sockets[1]);
        return EXIT_FAILURE;
    }
    buffer[bytesRead] = '\0';
    if (strstr(buffer, "OK SEAT=0 NAME=SelfTest POINTS=1000") == NULL) {
        fprintf(stderr, "self-test expected OK action, got: %s\n", buffer);
        close(sockets[0]);
        close(sockets[1]);
        return EXIT_FAILURE;
    }

    DisconnectClient(clients, &table, 0);
    close(sockets[1]);
    printf("Server self-test passed.\n");
    return EXIT_SUCCESS;
}

int main(int argc, char *argv[]) 
{
    int server_fd, new_socket, activity, max_sd, sd;
    int currentHighest = 0;
    ClientConnection clients[MAX_PLAYERS];
    Table table;
    struct sockaddr_in address;
    socklen_t addrlen;
    char buffer[MAX_MSG_LEN];

    /* fd_set is a bit array mapped to system file descriptors */
    fd_set readfds;

    signal(SIGPIPE, SIG_IGN);

    if (argc > 1 && strcmp(argv[1], "--self-test") == 0) {
        return RunSelfTest();
    }

    InitializeTable(&table);
    InitializeClients(clients);

    server_fd = CreateListeningSocket(PORT);
    if (server_fd < 0) {
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
            sd = clients[i].socket;

            if (sd >= 0) {
                FD_SET(sd, &readfds);

                /* select() needs the highest file descriptor number to bound its search array */
                if (sd > max_sd) {
                    max_sd = sd;
                }
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
            addrlen = sizeof(address);
            if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
                perror("Accept failed");
                continue;
            }

            printf("New connection: FD %d, IP %s, PORT %d\n", 
                   new_socket, inet_ntoa(address.sin_addr), ntohs(address.sin_port));

            /* Add the new descriptor to the tracking array */
            int slot = FindFreeClientSlot(clients);
            if (slot < 0) {
                SendErrorToClient(new_socket, "Server table is full");
                close(new_socket);
            }
            else {
                clients[slot].socket = new_socket;
                clients[slot].seat = -1;
                clients[slot].pending[0] = '\0';
                clients[slot].pendingLen = 0;
                printf("Assigned to internal tracking index %d\n", slot);
            }
        }

        /* CONDITION 2: Activity on a child socket = Incoming network data or disconnect */
        for (int i = 0; i < MAX_PLAYERS; i++) {
            sd = clients[i].socket;

            if (sd >= 0 && FD_ISSET(sd, &readfds)) {
                memset(buffer, 0, MAX_MSG_LEN);
                ssize_t valread = recv(sd, buffer, MAX_MSG_LEN - 1, 0);

                /* Read returning 0 indicates an explicit TCP FIN (client disconnected) */
                if (valread < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    perror("Socket read error");
                    DisconnectClient(clients, &table, i);
                }
                else if (valread == 0) {
                    addrlen = sizeof(address);
                    getpeername(sd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
                    printf("Host disconnected: IP %s, PORT %d\n", 
                           inet_ntoa(address.sin_addr), ntohs(address.sin_port));

                    DisconnectClient(clients, &table, i);
                } 
                /* Valid data payload received */
                else {
                    ProcessReceivedBytes(clients,
                                         &table,
                                         i,
                                         buffer,
                                         valread,
                                         &currentHighest);
                }
            }
        }
    }

    return 0;
}
