/******************************************************************************
 * File: main.c
 * Author: Team T3
 * Date: May 12, 2026
 * 
 * * Description:
 * Entry point for the Anteater Poker client application. Bootstraps 
 * the GTK environment and hands execution over to the event-based 
 * main control loop.
 *****************************************************************************/

#include <gtk/gtk.h>
#include "GameGUI.h"

int main(int argc, char *argv[])
{
    /* Initialize the hierarchical widgets and signals */
    InitializeGUI(argc, argv);
    
    /* Render the GUI to the display */
    ShowMainWindow();
    
    /* Relinquish control to GTK's event-based blocking loop */
    gtk_main();
    
    return 0;
}