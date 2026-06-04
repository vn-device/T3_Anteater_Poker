/******************************************************************************
 * File: GameGUI.h
 * Author: Team T3
 * Date: May 31, 2026
 * 
 * * Description:
 * Defines the graphical user interface API for the Anteater Poker client 
 * using GTK 3.0. Refactored to utilize a Single-Page Application (SPA) 
 * architecture via GtkStack. Includes integrated timer hooks.
 *****************************************************************************/

#ifndef GAMEGUI_H
#define GAMEGUI_H

#include "GameData.h"
#include <gtk/gtk.h>

extern Table *g_pTable;

//=============================================================================

void InitializeGUI(int isOfflineMode);

void ShowMainWindow(void);

void UpdateTelemetryHUD(int pot, int points, const char *statusMsg);

void TriggerTableRedraw(void);

void SetActionButtonsSensitive(gboolean sensitive);

void UpdateActionContext(int callAmount, int minRaise);

void ResetRoundTimer(void);

void SyncGUIWithGameState(void);

/* Local Client Memory Hooks */
void ClientReceiveHoleCards(int r1, char s1, int r2, char s2);
void ClientReceiveCommunityCard(int index, int rank, char suit);
void ClientSyncSeat(int seat, const char* name, int points, int isFolded, int outOfGame);
void ClientReceiveShowdownCards(int seat, int r1, char s1, int r2, char s2);
void ClientApplyRoundUpdate(int activeIdx, int pot, int state, int dealerIdx, int callAmount, int minRaise);

//=============================================================================

#endif // GAMEGUI_H
