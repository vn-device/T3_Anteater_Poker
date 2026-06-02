/******************************************************************************
 * File: GameProtocol.h
 * Author: Team T3
 * Date: May 31, 2026
 * 
 * * Description:
 * Defines the communication protocol API between the Anteater Poker 
 * client and server. Specifies the string formats, parsing logic, 
 * and serialization functions for TCP/IP socket messages.
 *****************************************************************************/

#ifndef GAMEPROTOCOL_H
#define GAMEPROTOCOL_H

#include "GameData.h"

#define MAX_MSG_LEN 256

/* Protocol Command Strings */
#define CMD_ENTER     "ENTER"
#define CMD_OK        "OK"
#define CMD_ERROR     "ERROR"
#define CMD_ACTION    "ACTION"
#define CMD_UPDATE    "UPDATE"
#define CMD_HOST      "HOST"
#define CMD_SETUP     "SETUP"
#define CMD_START     "START"
#define CMD_HOLECARDS "HOLECARDS"
#define CMD_COMMUNITY "COMM"
#define CMD_SYNC      "SYNC"

/* Network Action Type Constants */
#define ACTION_TYPE_FOLD  1
#define ACTION_TYPE_CHECK 2
#define ACTION_TYPE_CALL  3
#define ACTION_TYPE_RAISE 4

//=============================================================================

/**
 * @brief Categorizes incoming TCP stream messages.
 */
typedef enum {
    MSG_TYPE_UNKNOWN = -1,
    MSG_TYPE_ENTER = 0,
    MSG_TYPE_OK,
    MSG_TYPE_ERROR,
    MSG_TYPE_ACTION,
    MSG_TYPE_UPDATE,
    MSG_TYPE_HOST,
    MSG_TYPE_SETUP,
    MSG_TYPE_START,
    MSG_TYPE_HOLECARDS,
    MSG_TYPE_COMMUNITY,
    MSG_TYPE_SYNC
} MSG_TYPE;

/**
 * @brief Structured representation of a parsed network message.
 */
typedef struct {
    MSG_TYPE type;
    int seat;
    int amount;                
    int currentBet;
    char name[MAX_NAME_LEN];
    char payload[MAX_MSG_LEN]; 
} ParsedMessage;

//=============================================================================

int ParseNetworkMessage(const char* rawStr, ParsedMessage* pMsg);
void BuildEnterMessage(char* buffer, const char* name, int seat, const char* password);
void BuildOkMessage(char* buffer, int seat, const char* name, int points);
void BuildErrorMessage(char* buffer, const char* errorMsg);
void BuildActionMessage(char* buffer, int seat, int actionType, int amount);
void BuildHostMessage(char* buffer);
void BuildSetupMessage(char* buffer, int maxPlayers);
void BuildStartMessage(char* buffer);
void BuildUpdateMessage(char* buffer, int currentTurnSeat, int currentBet, int pot, int roundPhase);

/* New Data Synchronization Builders */
void BuildHoleCardsMessage(char* buffer, int r1, char s1, int r2, char s2);
void BuildCommunityMessage(char* buffer, int index, int rank, char suit);
void BuildSyncMessage(char* buffer, int seat, int points, int isFolded, const char* name);

//=============================================================================

#endif // GAMEPROTOCOL_H
