/******************************************************************************
 * File: GameGUI.c
 * Author: Team T3
 * Date: May 22, 2026
 * 
 * * Description:
 * Implements the GTK 3.0 event-driven graphical interface. Constructs 
 * the window hierarchy, control boxes, action buttons, Cairo rendering 
 * hooks, and telemetry HUD, mapping user interactions to protocol events.
 *****************************************************************************/

#include <string.h>
#include <sys/socket.h>
#include <gtk/gtk.h>
#include "GameGUI.h"
#include "GameData.h"
#include "GameProtocol.h"

static GtkWidget *pMainWindow;
static GtkWidget *pStatusLabel;

//=============================================================================

/* Margin constants for consistent spacing */
#define MARGIN_BUTTON_AREA 10
#define MARGIN_BUTTON_BOTTOM 10
#define TABLE_AREA_WIDTH 800
#define TABLE_AREA_HEIGHT 450
#define WINDOW_DEFAULT_WIDTH 800
#define WINDOW_DEFAULT_HEIGHT 450

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

//=============================================================================

/**
 * Helper function to create and pack action buttons into a horizontal container.
 */
static void CreateAndPackActionButtons(GtkWidget *pHBox)
{
    if (pHBox == NULL) return;
    
    const struct {
        const char *label;
        int actionType;
    } buttons[] = {
        {"FOLD", PLAYER_ACTION_FOLD},
        {"CHECK", PLAYER_ACTION_CHECK},
        {"CALL", PLAYER_ACTION_CALL},
        {"RAISE", PLAYER_ACTION_RAISE}
    };
    
    for (int i = 0; i < 4; i++) {
        GtkWidget *pButton = CreateActionButton(buttons[i].label, buttons[i].actionType);
        gtk_box_pack_start(GTK_BOX(pHBox), pButton, TRUE, TRUE, 0);
    }
}

//=============================================================================

/**
 * Cairo rendering pipeline. Triggered automatically by GTK whenever 
 * the window needs to be redrawn (e.g., resizing or explicit queue_draw calls).
 */
static gboolean OnDrawTable(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    /* Paint the green poker felt background universally */
    cairo_set_source_rgb(cr, 0.180, 0.545, 0.341); /* SeaGreen (#2E8B57) */
    cairo_paint(cr);

    /* Card geometry parameters */
    int card_width = 70;
    int card_height = 100;
    int spacing = 10;
    
    /* Calculate starting X to center the 5 community cards */
    int total_width = (5 * card_width) + (4 * spacing);
    int start_x = (TABLE_AREA_WIDTH - total_width) / 2;
    int start_y = (TABLE_AREA_HEIGHT - card_height) / 2;

    /* Procedurally draw 5 blank card placeholders */
    for (int i = 0; i < 5; i++) {
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); /* White fill */
        
        /* Draw card rectangle */
        cairo_rectangle(cr, start_x + (i * (card_width + spacing)), start_y, card_width, card_height);
        cairo_fill_preserve(cr); /* Fill but keep the path for the border */
        
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0); /* Black border */
        cairo_set_line_width(cr, 2.0);
        cairo_stroke(cr);
    }

    return FALSE; /* Return FALSE to allow other handlers to run if necessary */
}

//=============================================================================

void InitializeGUI(int argc, char *argv[])
{
    if (argv == NULL) return;

    gtk_init(&argc, &argv);

    pMainWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(pMainWindow), "Anteater Poker");
    gtk_window_set_default_size(GTK_WINDOW(pMainWindow), WINDOW_DEFAULT_WIDTH, WINDOW_DEFAULT_HEIGHT);
    gtk_window_set_position(GTK_WINDOW(pMainWindow), GTK_WIN_POS_CENTER);

    /* Lock the window bounds to prevent maximization and manual resizing */
    gtk_window_set_resizable(GTK_WINDOW(pMainWindow), FALSE);

    /* Bind the window destruction event to exit the GTK main control loop */
    g_signal_connect(pMainWindow, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    /* Main vertical container */
    GtkWidget *pVBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(pMainWindow), pVBox);

    /* Telemetry HUD Initialization */
    pStatusLabel = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(pStatusLabel),
        "<span font_desc='14' weight='bold' color='white'>"
        "Pot: 0 | Your Points: 0 | Status: Waiting for Server..."
        "</span>");
    
    /* Pack the label at the top of the window with some margin */
    gtk_widget_set_margin_top(pStatusLabel, 10);
    gtk_box_pack_start(GTK_BOX(pVBox), pStatusLabel, FALSE, FALSE, 0);

    /* Table representation area (placeholder for 2D pixel interface) */
    GtkWidget *pTableArea = gtk_drawing_area_new();
    gtk_widget_set_size_request(pTableArea, TABLE_AREA_WIDTH, TABLE_AREA_HEIGHT);
    
    /* Bind the Cairo rendering pipeline */
    g_signal_connect(pTableArea, "draw", G_CALLBACK(OnDrawTable), NULL);

    gtk_box_pack_start(GTK_BOX(pVBox), pTableArea, TRUE, TRUE, 0);

    /* Horizontal container for player action buttons */
    GtkWidget *pHBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(pHBox, MARGIN_BUTTON_AREA);
    gtk_widget_set_margin_end(pHBox, MARGIN_BUTTON_AREA);
    gtk_widget_set_margin_bottom(pHBox, MARGIN_BUTTON_BOTTOM);
    gtk_box_pack_start(GTK_BOX(pVBox), pHBox, FALSE, FALSE, 0);

    /* Create and pack action buttons */
    CreateAndPackActionButtons(pHBox);
}

//=============================================================================

void ShowMainWindow(void)
{
    if (pMainWindow == NULL) return;
    gtk_widget_show_all(pMainWindow);
}

//=============================================================================