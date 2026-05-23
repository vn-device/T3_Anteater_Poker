/******************************************************************************
 * File: GameGUI.c
 * Author: Team T3
 * Date: May 12, 2026
 * 
 * * Description:
 * Implements the GTK 3.0 event-driven graphical interface. Constructs 
 * the window hierarchy, control boxes, and action buttons, mapping user 
 * interactions to protocol events.
 *****************************************************************************/

#include <string.h>
#include <sys/socket.h>
#include "GameGUI.h"
#include "GameData.h"
#include "GameProtocol.h"

static GtkWidget *pMainWindow;

//=============================================================================

/* CSS styling for the poker table drawing area */
#define STYLE_POKER_TABLE "drawingarea { background-color: #2E8B57; }"

/* Margin constants for consistent spacing */
#define MARGIN_BUTTON_AREA 10
#define MARGIN_BUTTON_BOTTOM 10
#define TABLE_AREA_WIDTH 800
#define TABLE_AREA_HEIGHT 450
#define WINDOW_DEFAULT_WIDTH 800
#define WINDOW_DEFAULT_HEIGHT 600

//=============================================================================

/**
 * Event handler triggered when a user clicks a gameplay action button.
 * Casts the gpointer data back to a PlayerAction integer and transmits 
 * the serialized packet to the server.
 */
static void OnActionButtonClicked(GtkWidget *widget, gpointer data)
{
    /* Pull the global socket descriptor defined in main.c */
    extern int g_client_socket;
    
    int action = GPOINTER_TO_INT(data);
    char buffer[MAX_MSG_LEN];

    /* Hardcoded seat 0 and bet amount 50 for Alpha testing purposes */
    BuildActionMessage(buffer, 0, action, 50);
    
    /* Transmit the serialized network action */
    if (g_client_socket != -1) {
        send(g_client_socket, buffer, strlen(buffer), 0);
        g_print("Sent to server: %s", buffer);
    }
    else {
        g_printerr("Error: Socket disconnected. Cannot send action.\n");
    }
}

//=============================================================================

/**
 * Creates an action button with the specified label, binds it to the action
 * handler, and associates it with a player action type.
 *
 * @param label The button label text
 * @param actionType The PLAYER_ACTION enum value
 * @return Pointer to the created button widget
 */
static GtkWidget* CreateActionButton(const char* label, int actionType)
{
    if (label == NULL) return NULL;

    GtkWidget *pButton = gtk_button_new_with_label(label);
    g_signal_connect(pButton, "clicked", G_CALLBACK(OnActionButtonClicked), 
                     GINT_TO_POINTER(actionType));
    return pButton;
}

void InitializeGUI(int argc, char *argv[])
{
    if (argv == NULL) return;

    gtk_init(&argc, &argv);

    pMainWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(pMainWindow), "Anteater Poker");
    gtk_window_set_default_size(GTK_WINDOW(pMainWindow), WINDOW_DEFAULT_WIDTH, WINDOW_DEFAULT_HEIGHT);
    gtk_window_set_position(GTK_WINDOW(pMainWindow), GTK_WIN_POS_CENTER);

    /* Bind the window destruction event to exit the GTK main control loop */
    g_signal_connect(pMainWindow, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    /* Main vertical container */
    GtkWidget *pVBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(pMainWindow), pVBox);

    /* Table representation area (placeholder for 2D pixel interface) */
    GtkWidget *pTableArea = gtk_drawing_area_new();
    gtk_widget_set_size_request(pTableArea, TABLE_AREA_WIDTH, TABLE_AREA_HEIGHT);
    
    /* Apply CSS to give the drawing area a distinct green poker table background */
    GtkCssProvider *pProvider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(pProvider, STYLE_POKER_TABLE, -1, NULL);
    GtkStyleContext *pContext = gtk_widget_get_style_context(pTableArea);
    gtk_style_context_add_provider(pContext, GTK_STYLE_PROVIDER(pProvider), 
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(pProvider);

    gtk_box_pack_start(GTK_BOX(pVBox), pTableArea, TRUE, TRUE, 0);

    /* Horizontal container for player action buttons */
    GtkWidget *pHBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(pHBox, MARGIN_BUTTON_AREA);
    gtk_widget_set_margin_end(pHBox, MARGIN_BUTTON_AREA);
    gtk_widget_set_margin_bottom(pHBox, MARGIN_BUTTON_BOTTOM);
    gtk_box_pack_start(GTK_BOX(pVBox), pHBox, FALSE, FALSE, 0);

    /* Create action buttons with consistent labeling */
    GtkWidget *pBtnFold  = CreateActionButton("FOLD", PLAYER_ACTION_FOLD);
    GtkWidget *pBtnCheck = CreateActionButton("CHECK", PLAYER_ACTION_CHECK);
    GtkWidget *pBtnCall  = CreateActionButton("CALL", PLAYER_ACTION_CALL);
    GtkWidget *pBtnRaise = CreateActionButton("RAISE", PLAYER_ACTION_RAISE);

    /* Distribute buttons evenly across the horizontal box */
    gtk_box_pack_start(GTK_BOX(pHBox), pBtnFold, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(pHBox), pBtnCheck, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(pHBox), pBtnCall, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(pHBox), pBtnRaise, TRUE, TRUE, 0);
}

//=============================================================================

void ShowMainWindow(void)
{
    if (pMainWindow == NULL) return;
    gtk_widget_show_all(pMainWindow);
}

//=============================================================================