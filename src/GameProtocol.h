/******************************************************************************
 * File: GameProtocol.h
 * Author: Team T3
 * Date: May 12, 2026
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
#define CMD_ENTER  "ENTER"
#define CMD_OK     "OK"
#define CMD_ERROR  "ERROR"
#define CMD_ACTION "ACTION"
#define CMD_UPDATE "UPDATE"
#define CMD_HOST   "HOST"
#define CMD_SETUP  "SETUP"
#define CMD_START  "START"

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
    MSG_TYPE_START
} MSG_TYPE;

/**
 * @brief Structured representation of a parsed network message.
 */
typedef struct {
    MSG_TYPE type;
    int seat;
    int amount;                // Used for bets/raises, or points
    char name[MAX_NAME_LEN];
    char payload[MAX_MSG_LEN]; // General text or error messages
} ParsedMessage;

//=============================================================================

/**
 * Parses raw string received from socket into a structured ParsedMessage.
 */
int ParseNetworkMessage(const char* rawStr, ParsedMessage* pMsg);

/**
 * Serializes a client request to join a table seat.
 */
void BuildEnterMessage(char* buffer, const char* name, int seat, const char* password);

/**
 * Serializes a server acknowledgement of a successful state change.
 */
void BuildOkMessage(char* buffer, int seat, const char* name, int points);

/**
 * Serializes a server error broadcast.
 */
void BuildErrorMessage(char* buffer, const char* errorMsg);

/**
 * Serializes a client action (FOLD, CHECK, CALL, RAISE) to the server.
 */
void BuildActionMessage(char* buffer, int seat, int actionType, int amount);

/**
 * Serializes a server message appointing a player as the Lobby Host.
 */
void BuildHostMessage(char* buffer);

/**
 * Serializes a lobby host configuration message specifying max players for the game.
 */
void BuildSetupMessage(char* buffer, int maxPlayers);

/**
 * Serializes a host override to trigger the lobby game start and bot generation.
 */
void BuildStartMessage(char* buffer);

/**
 * Serializes a server game state update broadcast.
 */
void BuildUpdateMessage(char* buffer, int currentTurnSeat, int currentBet, int pot, int roundPhase);

//=============================================================================

#endif // GAMEPROTOCOL_H