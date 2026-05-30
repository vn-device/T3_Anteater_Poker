/******************************************************************************
 * File: PokerBot.h
 * Author: Team T3
 * Date: May 30, 2026
 * 
 * * Description:
 * Defines the API and configuration constants for the automated poker bot.
 * Configured as an embedded thread worker.
 *****************************************************************************/

#ifndef POKERBOT_H
#define POKERBOT_H

#include "GameData.h"

#define BOT_SERVER_IP "127.0.0.1"
#define BOT_SERVER_PORT 8003
#define BOT_DECISION_INTERVAL_MS 5000

/* POSIX Thread entry point for embedded execution */
void* RunPokerBotThread(void *arg);

#endif // POKERBOT_H