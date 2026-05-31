/******************************************************************************
 * File: GameGUI.h
 * Author: Team T3
 * Date: May 12, 2026
 * 
 * * Description:
 * Defines the graphical user interface API for the Anteater Poker client 
 * using GTK 3.0. Declares initialization routines, window management, 
 * and dynamic telemetry updates.
 *****************************************************************************/

#ifndef GAMEGUI_H
#define GAMEGUI_H

#include "GameData.h"
#include <gtk/gtk.h>

extern Table *g_pTable;

//=============================================================================

/**
 * Halts execution and presents a modal dialog to capture network credentials.
 * @return 1 if accepted, 0 if canceled or closed.
 */
int PromptLoginDetails(char *outName, int *outSeat, char *outPassword, char *outIP);

/**
 * Initializes the GTK environment and constructs the hierarchical widgets.
 * @param localSeat The seat index assigned to the client.
 */
void InitializeGUI(int localSeat);

/**
 * Renders the constructed main window and all child widgets to the screen.
 */
void ShowMainWindow(void);

/**
 * Dynamically updates the GTK label displaying the player's pot, points, and status.
 */
void UpdateTelemetryHUD(int pot, int points, const char *statusMsg);

/**
 * Forces the GTK drawing area to invalidate and redraw the poker table.
 */
void TriggerTableRedraw(void);

void SetActionButtonsSensitive(gboolean sensitive);

void UpdateActionContext(int callAmount, int minRaise);

void ResetRoundTimer(void);

//=============================================================================

#endif // GAMEGUI_H