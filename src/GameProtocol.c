/******************************************************************************
 * File: GameProtocol.c
 * Author: Team T3
 * Date: May 12, 2026
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
    /* Initialize defaults to prevent garbage data on partial parse */
    pMsg->type = MSG_TYPE_UNKNOWN;
    pMsg->seat = -1;
    pMsg->amount = 0;
    memset(pMsg->name, 0, MAX_NAME_LEN);
    memset(pMsg->payload, 0, MAX_MSG_LEN);

    char cmd[32];
    /* Extract the leading command keyword to route the parsing logic */
    if (sscanf(rawStr, "%31s", cmd) != 1) {
        return -1; 
    }

    if (strcmp(cmd, CMD_ENTER) == 0) {
        pMsg->type = MSG_TYPE_ENTER;
        /* Expected: ENTER <name> SEAT <seat> PASSWORD <pass> */
        sscanf(rawStr, "ENTER %31s SEAT %d PASSWORD %255s", pMsg->name, &pMsg->seat, pMsg->payload);
        return 0;
    }
    else if (strcmp(cmd, CMD_OK) == 0) {
        pMsg->type = MSG_TYPE_OK;
        
        /* Expected: OK SEAT=<seat> NAME=<name> POINTS=<points> */
        sscanf(rawStr, "OK SEAT=%d NAME=%31[^ ] POINTS=%d", &pMsg->seat, pMsg->name, &pMsg->amount);
        return 0;
    }
    else if (strcmp(cmd, CMD_ERROR) == 0) {
        pMsg->type = MSG_TYPE_ERROR;
        
        /* Expected: ERROR <message> */
        /* Skip the "ERROR " prefix (6 chars) to grab the rest of the string */
        if (strlen(rawStr) > 6) {
            strncpy(pMsg->payload, rawStr + 6, MAX_MSG_LEN - 1);
        }
        return 0;
    }
    else if (strcmp(cmd, CMD_ACTION) == 0) {
        pMsg->type = MSG_TYPE_ACTION;
        
        /* Expected: ACTION SEAT <seat> TYPE <actionType> AMOUNT <amount> */
        int actionType = 0;
        sscanf(rawStr, "ACTION SEAT %d TYPE %d AMOUNT %d", &pMsg->seat, &actionType, &pMsg->amount);
        
        /* Store action type in payload[0] to fit within the struct footprint safely */
        pMsg->payload[0] = (char)actionType; 
        return 0;
    }

    return -1; /* Unrecognized command */
}

//=============================================================================

void BuildEnterMessage(char* buffer, const char* name, int seat, const char* password)
{
    /* snprintf enforces MAX_MSG_LEN to prevent buffer overflow attacks via socket */
    snprintf(buffer, MAX_MSG_LEN, "ENTER %s SEAT %d PASSWORD %s\n", name, seat, password);
}

//=============================================================================

void BuildOkMessage(char* buffer, int seat, const char* name, int points)
{
    snprintf(buffer, MAX_MSG_LEN, "OK SEAT=%d NAME=%s POINTS=%d\n", seat, name, points);
}

//=============================================================================

void BuildErrorMessage(char* buffer, const char* errorMsg)
{
    snprintf(buffer, MAX_MSG_LEN, "ERROR %s\n", errorMsg);
}

//=============================================================================

void BuildActionMessage(char* buffer, int seat, int actionType, int amount)
{
    snprintf(buffer, MAX_MSG_LEN, "ACTION SEAT %d TYPE %d AMOUNT %d\n", seat, actionType, amount);
}

//=============================================================================