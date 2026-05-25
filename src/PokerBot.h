/******************************************************************************
 * File: PokerBot.h
 * Author: Team T3
 * Date: May 25, 2026
 * 
 * * Description:
 * Defines the API and configuration constants for the automated poker bot.
 * Declares network targeting parameters, the main bot execution loop, and 
 * decision-making intervals for synchronized gameplay.
 *****************************************************************************/

#ifndef POKERBOT_H
#define POKERBOT_H

#include "GameData.h"

/* Default server connection */
#define BOT_SERVER_IP "127.0.0.1"
#define BOT_SERVER_PORT 8003

/* Timing (ms) between automated decisions */
#define BOT_DECISION_INTERVAL_MS 5000

int RunPokerBot(const char *name, const char *password);

#endif // POKERBOT_H
