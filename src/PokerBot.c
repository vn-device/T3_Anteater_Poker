/******************************************************************************
 * File: PokerBot.c
 * Author: Team T3
 * Date: May 30, 2026
 * 
 * * Description:
 * Implements the automated poker bot client. Establishes a localized socket 
 * connection to the parent server process, parsing authoritative state updates 
 * and executing randomized actions via the network protocol.
 *****************************************************************************/

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "PokerBot.h"
#include "GameProtocol.h"
#include "GameData.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <pthread.h>

#define sleep_ms(x) usleep((x) * 1000)
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
        BuildActionMessage(out, seat, ACTION_TYPE_FOLD, 0);
        send(sock, out, strlen(out), 0);
        printf("[EmbeddedBot %d] Executed FOLD\n", seat);
    }
    else if (p < 60) {
        BuildActionMessage(out, seat, ACTION_TYPE_CHECK, 0);
        send(sock, out, strlen(out), 0);
        printf("[EmbeddedBot %d] Executed CHECK\n", seat);
    }
    else if (p < 90) {
        BuildActionMessage(out, seat, ACTION_TYPE_CALL, 0);
        send(sock, out, strlen(out), 0);
        printf("[EmbeddedBot %d] Executed CALL\n", seat);
    }
    else {
        int amt = 10 + (rand() % 91);
        BuildActionMessage(out, seat, ACTION_TYPE_RAISE, amt);
        send(sock, out, strlen(out), 0);
        printf("[EmbeddedBot %d] Executed RAISE %d\n", seat, amt);
    }
}

void* RunPokerBotThread(void *arg)
{
    int targetSeat = (int)(intptr_t)arg;
    struct sockaddr_in serv;
    int sock;
    char buf[MAX_RECV];
    int assignedSeat = -1;
    char name[32];
    const char *password = "AnteaterTest";

    char stream_buf[4096];
    int stream_len = 0;
    memset(stream_buf, 0, sizeof(stream_buf));

    srand((unsigned)time(NULL) ^ (unsigned)pthread_self());
    snprintf(name, sizeof(name), "Bot_Seat_%d", targetSeat);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return NULL;

    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_port = htons(BOT_SERVER_PORT);

    if (inet_pton(AF_INET, BOT_SERVER_IP, &serv.sin_addr) <= 0) {
        close(sock);
        return NULL;
    }

    usleep(50000); /* Synchronization buffer to ensure parent accept() is ready */

    if (connect(sock, (struct sockaddr *)&serv, sizeof(serv)) < 0) {
        close(sock);
        return NULL;
    }

    char out[MAX_MSG_LEN];
    BuildEnterMessage(out, name, targetSeat, password);
    send(sock, out, strlen(out), 0);

    fd_set readfds;
    struct timeval tv;
    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    
    int rv = select(sock + 1, &readfds, NULL, NULL, &tv);
    if (rv > 0 && FD_ISSET(sock, &readfds)) {
        ssize_t r = recv(sock, buf, sizeof(buf) - 1, 0);
        if (r > 0) {
            buf[r] = '\0';

            ParsedMessage msg;
            if (ParseNetworkMessage(buf, &msg) == 0 &&
                msg.type == MSG_TYPE_OK &&
                msg.seat == targetSeat) {
                assignedSeat = targetSeat;
            }
        }
    }

    if (assignedSeat < 0) {
        close(sock);
        return NULL;
    }

    int isMyTurn = 0;

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        tv.tv_sec = BOT_DECISION_INTERVAL_MS / 1000;
        tv.tv_usec = (BOT_DECISION_INTERVAL_MS % 1000) * 1000;

        rv = select(sock + 1, &readfds, NULL, NULL, &tv);
        if (rv < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (rv == 0) continue;

        if (FD_ISSET(sock, &readfds)) {
            ssize_t r = recv(sock, buf, sizeof(buf) - 1, 0);
            if (r <= 0) break;

            if (stream_len + r < (int)sizeof(stream_buf)) {
                memcpy(stream_buf + stream_len, buf, r);
                stream_len += (int)r;
                stream_buf[stream_len] = '\0';
            }
            else {
                fprintf(stderr, "[EmbeddedBot %d] Stream buffer overflow. Flushing.\n", assignedSeat);
                stream_buf[0] = '\0';
                stream_len = 0;
                continue;
            }

            char *newline;
            while ((newline = strchr(stream_buf, '\n')) != NULL) {
                *newline = '\0';

                ParsedMessage msg;
                if (ParseNetworkMessage(stream_buf, &msg) == 0) {
                    if (msg.type == MSG_TYPE_UPDATE && msg.seat == assignedSeat) {
                        isMyTurn = 1;
                    }
                }

                int remaining = stream_len - (int)(newline - stream_buf) - 1;
                memmove(stream_buf, newline + 1, remaining);
                stream_len = remaining;
                stream_buf[stream_len] = '\0';
            }

            if (isMyTurn) {
                usleep(500000); /* 500ms humanization delay */
                DecideAndAct(sock, assignedSeat);
                isMyTurn = 0;
            }
        }
    }

    close(sock);
    return NULL;
}
