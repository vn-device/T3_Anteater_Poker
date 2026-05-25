/******************************************************************************
 * File: PokerBot.c
 * Author: Team T3
 * Date: May 25, 2026
 * 
 * * Description:
 * Implements the automated poker bot client. Establishes a localized socket 
 * connection to the host server, parses authoritative game state updates, 
 * and executes randomized betting actions (Fold, Check, Call, Raise) via 
 * the network protocol.
 *****************************************************************************/

#include "PokerBot.h"
#include "GameProtocol.h"
#include "GameData.h"

#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <time.h>

#define sleep_ms(x) usleep((x)*1000)
#define MAX_RECV 512

static int RandomPercent()
{
    return rand() % 100;
}

static void DecideAndAct(int sock, int seat)
{
    char out[MAX_MSG_LEN];
    int p = RandomPercent();

    if (p < 10) {
        /* Fold */
        BuildActionMessage(out, seat, ACTION_TYPE_FOLD, 0);
        send(sock, out, strlen(out), 0);
        printf("[Bot] Sending FOLD\n");
    }
    else if (p < 60) {
        /* Check */
        BuildActionMessage(out, seat, ACTION_TYPE_CHECK, 0);
        send(sock, out, strlen(out), 0);
        printf("[Bot] Sending CHECK\n");
    }
    else if (p < 90) {
        /* Call (amount 0) */
        BuildActionMessage(out, seat, ACTION_TYPE_CALL, 0);
        send(sock, out, strlen(out), 0);
        printf("[Bot] Sending CALL\n");
    }
    else {
        /* Raise small random amount */
        int amt = 10 + (rand() % 91); /* 10-100 */
        BuildActionMessage(out, seat, ACTION_TYPE_RAISE, amt);
        send(sock, out, strlen(out), 0);
        printf("[Bot] Sending RAISE %d\n", amt);
    }
}

int RunPokerBot(const char *name, const char *password)
{
    struct sockaddr_in serv;
    int sock;
    char buf[MAX_RECV];
    int seat = -1;

    srand((unsigned)time(NULL));
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_port = htons(BOT_SERVER_PORT);
    if (inet_pton(AF_INET, BOT_SERVER_IP, &serv.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock); /* Enforcing POSIX close() */
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&serv, sizeof(serv)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }
    printf("Connected to server. Attempting to take an open seat...\n");

    /* Try seats 0..MAX_PLAYERS-1 until OK */
    for (int s = 0; s < MAX_PLAYERS; s++) {
        char out[MAX_MSG_LEN];
        BuildEnterMessage(out, name, s, password);
        send(sock, out, strlen(out), 0);

        /* wait briefly for response */
        fd_set readfds;
        struct timeval tv;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        int rv = select((int)(sock+1), &readfds, NULL, NULL, &tv);
        if (rv > 0 && FD_ISSET(sock, &readfds)) {
            ssize_t r = recv(sock, buf, sizeof(buf)-1, 0);
            if (r > 0) {
                buf[r] = '\0';
                ParsedMessage msg;
                if (ParseNetworkMessage(buf, &msg) == 0) {
                    if (msg.type == MSG_TYPE_OK && msg.seat == s) {
                        seat = s;
                        printf("Joined as seat %d (%s)\n", seat, name);
                        break;
                    }
                    else if (msg.type == MSG_TYPE_ERROR) {
                        printf("Seat %d refused: %s\n", s, msg.payload);
                        continue;
                    }
                }
            }
        }
    }

    if (seat < 0) {
        printf("No seat available or join failed. Exiting.\n");
        close(sock); /* Enforcing POSIX close() */
        return 1;
    }
    int isMyTurn = 0;

    /* Main loop: listen for server messages and only act when the server authorizes our turn */
    while (1) {
        fd_set readfds;
        struct timeval tv;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        tv.tv_sec = BOT_DECISION_INTERVAL_MS / 1000;
        tv.tv_usec = (BOT_DECISION_INTERVAL_MS % 1000) * 1000;

        int rv = select((int)(sock+1), &readfds, NULL, NULL, &tv);
        if (rv < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        if (rv == 0) {
            /* No server update; wait for our turn. */
            continue;
        }

        if (FD_ISSET(sock, &readfds)) {
            ssize_t r = recv(sock, buf, sizeof(buf)-1, 0);
            if (r <= 0) {
                printf("Server disconnected.\n");
                break;
            }
            buf[r] = '\0';
            printf("[Server] %s", buf);

            ParsedMessage msg;
            if (ParseNetworkMessage(buf, &msg) == 0) {
                if (msg.type == MSG_TYPE_UPDATE && msg.seat == seat) {
                    isMyTurn = 1;
                }
                else if (msg.type == MSG_TYPE_ERROR) {
                    printf("[Bot] Server error: %s\n", msg.payload);
                }
            }

            if (isMyTurn) {
                DecideAndAct(sock, seat);
                isMyTurn = 0;
            }
        }
    }
    close(sock);
    return 0;
}

int main(int argc, char *argv[])
{
    const char *name = "AutoBot";
    const char *pass = "botpass";
    if (argc > 1) name = argv[1];
    if (argc > 2) pass = argv[2];
    return RunPokerBot(name, pass);
}