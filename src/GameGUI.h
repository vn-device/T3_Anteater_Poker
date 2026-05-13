/******************************************************************************
 * File: GameGUI.h
 * Author: Team T3
 * Date: May 12, 2026
 * 
 * * Description:
 * Defines the graphical user interface API for the Anteater Poker client 
 * using GTK 3.0. Declares initialization routines and window management.
 *****************************************************************************/

#ifndef GAMEGUI_H
#define GAMEGUI_H

#include <gtk/gtk.h>

//=============================================================================

/**
 * Initializes the GTK environment, constructs the hierarchical widgets 
 * (main window, layouts, buttons), and maps signal handlers.
 */
void InitializeGUI(int argc, char *argv[]);

/**
 * Renders the constructed main window and all child widgets to the screen.
 */
void ShowMainWindow(void);

//=============================================================================

#endif // GAMEGUI_H