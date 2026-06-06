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

#define sleep_ms(x) usleep((x)*1000)
#define MAX_RECV 4096

static int RandomPercent(void)
{
    return rand() % 100;
}

static void ProcessBotBytes(char *stream, int *streamLen, int assignedSeat,
                            int *currentBet, int *callAmount, int *minRaise, int *isMyTurn,
                            int *myChips, int *minAllowedRaise, int *maxAllowedRaise)
{
    char *newline_pos;

    while ((newline_pos = strchr(stream, '\n')) != NULL) {
        *newline_pos = '\0';

        ParsedMessage msg;
        if (ParseNetworkMessage(stream, &msg) == 0) {
            if (msg.type == MSG_TYPE_UPDATE) {
                *currentBet = msg.currentBet;
                *callAmount = msg.amount;
                *minRaise = msg.minRaise;
                if (msg.seat == assignedSeat) {
                    *isMyTurn = 1;
                }
            }
            else if (msg.type == MSG_TYPE_SYNC && msg.seat == assignedSeat) {
                *myChips = msg.amount;
            }
            /* NEW: Extract bet range limits from LIMITS message */
            else if (msg.type == MSG_TYPE_LIMITS && msg.seat == assignedSeat) {
                *minAllowedRaise = msg.minAllowedRaise;
                *maxAllowedRaise = msg.maxRaise;
            }
        }

        int remaining = *streamLen - (int)(newline_pos - stream) - 1;
        memmove(stream, newline_pos + 1, remaining);
        *streamLen = remaining;
        stream[*streamLen] = '\0';
    }
}

static void DecideAndAct(int sock, int seat, int currentBet, int callAmount, int minRaise, int myChips, int minAllowedRaise, int maxAllowedRaise)
{
    char out[MAX_MSG_LEN];
    int p = RandomPercent();

    if (p < 10) {
        BuildActionMessage(out, seat, ACTION_TYPE_FOLD, 0);
        send(sock, out, strlen(out), 0);
        printf("[EmbeddedBot %d] Executed FOLD\n", seat);
        return;
    }

    if (callAmount <= 0) {
        BuildActionMessage(out, seat, ACTION_TYPE_CHECK, 0);
        send(sock, out, strlen(out), 0);
        printf("[EmbeddedBot %d] Executed CHECK\n", seat);
        return;
    }

    /* CHIP VALIDATION: Force FOLD if insufficient chips for CALL */
    if (callAmount > myChips) {
        BuildActionMessage(out, seat, ACTION_TYPE_FOLD, 0);
        send(sock, out, strlen(out), 0);
        printf("[EmbeddedBot %d] Insufficient chips (need %d, have %d). Forced FOLD.\n", seat, callAmount, myChips);
        return;
    }

    if (p < 80) {
        BuildActionMessage(out, seat, ACTION_TYPE_CALL, 0);
        send(sock, out, strlen(out), 0);
        printf("[EmbeddedBot %d] Executed CALL ($%d)\n", seat, callAmount);
        return;
    }

    /* INVERTED LIMIT DEADLOCK FIX: Check if min > max BEFORE attempting raise */
    if (minAllowedRaise > maxAllowedRaise) {
        /* Impossible to raise legally - player has fewer chips than minimum raise.
           Bot must degrade to CALL (all-in) or FOLD, never attempt invalid raise */
        if (callAmount <= myChips) {
            BuildActionMessage(out, seat, ACTION_TYPE_CALL, 0);
            send(sock, out, strlen(out), 0);
            printf("[EmbeddedBot %d] Limit inverted (min:%d > max:%d). CALL all-in ($%d).\n", 
                   seat, minAllowedRaise, maxAllowedRaise, callAmount);
        } else {
            BuildActionMessage(out, seat, ACTION_TYPE_FOLD, 0);
            send(sock, out, strlen(out), 0);
            printf("[EmbeddedBot %d] Limit inverted. CALL unaffordable (need %d, have %d). Forced FOLD.\n", 
                   seat, callAmount, myChips);
        }
        return;
    }

    /* CHIP VALIDATION & BET RANGE ENFORCEMENT */
    int targetTotal = minRaise;
    if (targetTotal <= currentBet) {
        targetTotal = currentBet + BLIND_BIG;
    }
    
    /* Clamp raise to legal boundaries */
    if (targetTotal < minAllowedRaise) {
        targetTotal = minAllowedRaise;
    }
    if (targetTotal > maxAllowedRaise) {
        targetTotal = maxAllowedRaise;
    }
    
    int raiseCost = targetTotal - currentBet;
    if (raiseCost > myChips) {
        if (callAmount <= myChips) {
            BuildActionMessage(out, seat, ACTION_TYPE_CALL, 0);
            send(sock, out, strlen(out), 0);
            printf("[EmbeddedBot %d] Raise unaffordable, CALL instead ($%d).\n", seat, callAmount);
        } else {
            BuildActionMessage(out, seat, ACTION_TYPE_FOLD, 0);
            send(sock, out, strlen(out), 0);
            printf("[EmbeddedBot %d] CALL also unaffordable (need %d, have %d). Forced FOLD.\n", seat, callAmount, myChips);
        }
        return;
    }
    
    BuildActionMessage(out, seat, ACTION_TYPE_RAISE, targetTotal);
    send(sock, out, strlen(out), 0);
    printf("[EmbeddedBot %d] Executed RAISE to $%d (range: %d-%d, cost: %d).\n", 
           seat, targetTotal, minAllowedRaise, maxAllowedRaise, raiseCost);
}

void* RunPokerBotThread(void *arg)
{
    int targetSeat = (int)(intptr_t)arg;
    struct sockaddr_in serv;
    int sock;
    char recvBuf[MAX_RECV];
    char stream[MAX_RECV];
    int streamLen = 0;
    int assignedSeat = -1;
    char name[32];
    const char *password = "AnteaterTest";
    int currentBet = 0;
    int callAmount = 0;
    int minRaise = BLIND_BIG;
    int isMyTurn = 0;
    int myChips = 1000;
    int minAllowedRaise = BLIND_BIG;   /* NEW: Min allowed raise from server */
    int maxAllowedRaise = 1000;        /* NEW: Max allowed raise from server (= player.chips) */

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

    usleep(50000);

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
        ssize_t r = recv(sock, recvBuf, sizeof(recvBuf) - 1, 0);
        if (r > 0) {
            recvBuf[r] = '\0';
            ParsedMessage msg;
            char *line = recvBuf;
            char *nextLine;
            while (line != NULL && assignedSeat < 0) {
                nextLine = strchr(line, '\n');
                if (nextLine != NULL) {
                    *nextLine = '\0';
                }
                if (ParseNetworkMessage(line, &msg) == 0 &&
                    msg.type == MSG_TYPE_OK &&
                    msg.seat == targetSeat) {
                    assignedSeat = targetSeat;
                }
                line = nextLine != NULL ? nextLine + 1 : NULL;
            }
        }
    }

    if (assignedSeat < 0) {
        close(sock);
        return NULL;
    }

    isMyTurn = 0;

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
            ssize_t r = recv(sock, recvBuf, sizeof(recvBuf) - 1, 0);
            if (r <= 0) break;

            if (streamLen + r >= (int)sizeof(stream)) {
                streamLen = 0;
                stream[0] = '\0';
            }
            memcpy(stream + streamLen, recvBuf, r);
            streamLen += (int)r;
            stream[streamLen] = '\0';

            int turnBefore = isMyTurn;
            ProcessBotBytes(stream, &streamLen, assignedSeat, &currentBet, &callAmount, &minRaise, &isMyTurn, &myChips, &minAllowedRaise, &maxAllowedRaise);

            if (!turnBefore && isMyTurn) {
                usleep(500000);
                DecideAndAct(sock, assignedSeat, currentBet, callAmount, minRaise, myChips, minAllowedRaise, maxAllowedRaise);
                isMyTurn = 0;
            }
        }
    }

    close(sock);
    return NULL;
}
