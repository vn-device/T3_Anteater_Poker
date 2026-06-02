/******************************************************************************
 * File: GameProtocol.c
 * Author: Team T3
 * Date: May 31, 2026
 * 
 * * Description:
 * Implements the serialization and deserialization of the Anteater 
 * Poker network protocol. Ensures buffer safety during string construction.
 *****************************************************************************/

#include <stdio.h>
#include <string.h>
#include "GameProtocol.h"

//=============================================================================

int ParseNetworkMessage(const char* rawStr, ParsedMessage* pMsg)
{
    if (rawStr == NULL || pMsg == NULL) return -1;

    pMsg->type = MSG_TYPE_UNKNOWN;
    pMsg->seat = -1;
    pMsg->amount = 0;
    pMsg->currentBet = 0;
    memset(pMsg->name, 0, MAX_NAME_LEN);
    memset(pMsg->payload, 0, MAX_MSG_LEN);

    char cmd[32];
    if (sscanf(rawStr, "%31s", cmd) != 1) {
        return -1; 
    }

    if (strcmp(cmd, CMD_ENTER) == 0) {
        pMsg->type = MSG_TYPE_ENTER;
        if (sscanf(rawStr, "ENTER %31s SEAT %d PASSWORD %255s", pMsg->name, &pMsg->seat, pMsg->payload) < 3) {
            return -1;
        }
        return 0;
    }
    else if (strcmp(cmd, CMD_OK) == 0) {
        pMsg->type = MSG_TYPE_OK;
        if (sscanf(rawStr, "OK SEAT=%d NAME=%31[^ ] POINTS=%d", &pMsg->seat, pMsg->name, &pMsg->amount) < 3) {
            return -1;
        }
        return 0;
    }
    else if (strcmp(cmd, CMD_ERROR) == 0) {
        pMsg->type = MSG_TYPE_ERROR;
        if (strlen(rawStr) > 6) {
            strncpy(pMsg->payload, rawStr + 6, MAX_MSG_LEN - 1);
            pMsg->payload[MAX_MSG_LEN - 1] = '\0';
        }
        return 0;
    }
    else if (strcmp(cmd, CMD_ACTION) == 0) {
        pMsg->type = MSG_TYPE_ACTION;
        int actionType = 0;
        if (sscanf(rawStr, "ACTION SEAT %d TYPE %d AMOUNT %d", &pMsg->seat, &actionType, &pMsg->amount) < 3) {
            return -1;
        }
        if (actionType >= 0 && actionType <= 255) {
            pMsg->payload[0] = (char)actionType;
        }
        return 0;
    }
    else if (strcmp(cmd, CMD_HOST) == 0) {
        pMsg->type = MSG_TYPE_HOST;
        return 0;
    }
    else if (strcmp(cmd, CMD_SETUP) == 0) {
        pMsg->type = MSG_TYPE_SETUP;
        if (sscanf(rawStr, "SETUP MAXPLAYERS %d", &pMsg->seat) < 1) {
            return -1;
        }
        return 0;
    }
    else if (strcmp(cmd, CMD_START) == 0) {
        pMsg->type = MSG_TYPE_START;
        return 0;
    }
    else if (strcmp(cmd, CMD_UPDATE) == 0) {
        pMsg->type = MSG_TYPE_UPDATE;
        int currentBet = 0;
        int pot = 0;
        int roundPhase = 0;
        if (sscanf(rawStr, "UPDATE %d %d %d %d", &pMsg->seat, &currentBet, &pot, &roundPhase) < 4) {
            return -1;
        }
        pMsg->currentBet = currentBet;
        pMsg->amount = pot;
        snprintf(pMsg->payload, MAX_MSG_LEN, "%d", roundPhase);
        return 0;
    }
    else if (strcmp(cmd, CMD_HOLECARDS) == 0) {
        pMsg->type = MSG_TYPE_HOLECARDS;
        int r1, r2;
        char s1, s2;
        if (sscanf(rawStr, "HOLECARDS %d %c %d %c", &r1, &s1, &r2, &s2) == 4) {
            pMsg->seat = r1;
            pMsg->amount = r2;
            pMsg->payload[0] = s1;
            pMsg->payload[1] = s2;
            pMsg->payload[2] = '\0';
            return 0;
        }
        return -1;
    }
    else if (strcmp(cmd, CMD_COMMUNITY) == 0) {
        pMsg->type = MSG_TYPE_COMMUNITY;
        int idx, rank;
        char suit;
        if (sscanf(rawStr, "COMM %d %d %c", &idx, &rank, &suit) == 3) {
            pMsg->seat = idx;
            pMsg->amount = rank;
            pMsg->payload[0] = suit;
            pMsg->payload[1] = '\0';
            return 0;
        }
        return -1;
    }
    else if (strcmp(cmd, CMD_SYNC) == 0) {
        pMsg->type = MSG_TYPE_SYNC;
        int isFolded;
        if (sscanf(rawStr, "SYNC %d %d %d %31s", &pMsg->seat, &pMsg->amount, &isFolded, pMsg->name) == 4) {
            pMsg->payload[0] = isFolded ? '1' : '0';
            pMsg->payload[1] = '\0';
            return 0;
        }
        return -1;
    }

    return -1;
}

//=============================================================================

void BuildEnterMessage(char* buffer, const char* name, int seat, const char* password)
{
    if (buffer == NULL || name == NULL || password == NULL) return;
    snprintf(buffer, MAX_MSG_LEN, "ENTER %s SEAT %d PASSWORD %s\n", name, seat, password);
}

void BuildOkMessage(char* buffer, int seat, const char* name, int points)
{
    if (buffer == NULL || name == NULL) return;
    snprintf(buffer, MAX_MSG_LEN, "OK SEAT=%d NAME=%s POINTS=%d\n", seat, name, points);
}

void BuildErrorMessage(char* buffer, const char* errorMsg)
{
    if (buffer == NULL || errorMsg == NULL) return;
    snprintf(buffer, MAX_MSG_LEN, "ERROR %s\n", errorMsg);
}

void BuildActionMessage(char* buffer, int seat, int actionType, int amount)
{
    if (buffer == NULL) return;
    snprintf(buffer, MAX_MSG_LEN, "ACTION SEAT %d TYPE %d AMOUNT %d\n", seat, actionType, amount);
}

void BuildHostMessage(char* buffer)
{
    if (buffer == NULL) return;
    snprintf(buffer, MAX_MSG_LEN, "HOST\n");
}

void BuildSetupMessage(char* buffer, int maxPlayers)
{
    if (buffer == NULL) return;
    snprintf(buffer, MAX_MSG_LEN, "SETUP MAXPLAYERS %d\n", maxPlayers);
}

void BuildStartMessage(char* buffer)
{
    if (buffer == NULL) return;
    snprintf(buffer, MAX_MSG_LEN, "START\n");
}

void BuildUpdateMessage(char* buffer, int currentTurnSeat, int currentBet, int pot, int roundPhase)
{
    if (buffer == NULL) return;
    snprintf(buffer, MAX_MSG_LEN, "UPDATE %d %d %d %d\n", currentTurnSeat, currentBet, pot, roundPhase);
}

void BuildHoleCardsMessage(char* buffer, int r1, char s1, int r2, char s2)
{
    if (buffer == NULL) return;
    snprintf(buffer, MAX_MSG_LEN, "HOLECARDS %d %c %d %c\n", r1, s1, r2, s2);
}

void BuildCommunityMessage(char* buffer, int index, int rank, char suit)
{
    if (buffer == NULL) return;
    snprintf(buffer, MAX_MSG_LEN, "COMM %d %d %c\n", index, rank, suit ? suit : SUIT_NONE);
}

void BuildSyncMessage(char* buffer, int seat, int points, int isFolded, const char* name)
{
    if (buffer == NULL || name == NULL) return;
    snprintf(buffer, MAX_MSG_LEN, "SYNC %d %d %d %s\n", seat, points, isFolded, name);
}
