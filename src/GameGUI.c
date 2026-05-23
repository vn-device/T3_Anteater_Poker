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
 * Procedurally draws a playing card using Cairo.
 * Translates structural ranks and suits into formatted UTF-8 text, 
 * applying red/black color coding and handling custom Anteater/Joker logic.
 */
static void DrawCard(cairo_t *cr, int x, int y, int width, int height, char suit, int rank)
{
    /* 1. Draw the base card geometry */
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); // White card stock
    cairo_rectangle(cr, x, y, width, height);
    cairo_fill_preserve(cr);
    
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0); // Black border
    cairo_set_line_width(cr, 2.0);
    cairo_stroke(cr);

    /* If rank is 0, treat it as a face-down or empty placeholder */
    if (rank == 0) return;

    /* 2. Determine Font Color */
    if (suit == 'H' || suit == 'D') {
        cairo_set_source_rgb(cr, 0.8, 0.1, 0.1); // Red
    }
    else if (suit == 'N') {
        cairo_set_source_rgb(cr, 0.5, 0.0, 0.5); // Purple for wildcards
    }
    else {
        cairo_set_source_rgb(cr, 0.1, 0.1, 0.1); // Black
    }

    /* 3. Parse Rank String */
    char rankStr[8];
    if (rank >= 2 && rank <= 10) {
        snprintf(rankStr, sizeof(rankStr), "%d", rank);
    }
    else {
        switch (rank) {
            case 11: strcpy(rankStr, "J");    break;
            case 12: strcpy(rankStr, "Q");    break;
            case 13: strcpy(rankStr, "K");    break;
            case 14: strcpy(rankStr, "ANT");  break; // Custom Anteater Face Card
            case 15: strcpy(rankStr, "A");    break;
            case 16: strcpy(rankStr, "WILD"); break; // Joker
            default: strcpy(rankStr, "?");    break;
        }
    }

    /* 4. Parse Suit Symbol (UTF-8) */
    char suitStr[8];
    switch (suit) {
        case 'H': strcpy(suitStr, "♥"); break;
        case 'D': strcpy(suitStr, "♦"); break;
        case 'C': strcpy(suitStr, "♣"); break;
        case 'S': strcpy(suitStr, "♠"); break;
        default:  strcpy(suitStr, "");  break;
    }

    /* 5. Render Text to Canvas */
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 16);

    /* Draw top-left corner text */
    cairo_move_to(cr, x + 5, y + 20);
    cairo_show_text(cr, rankStr);
    
    cairo_move_to(cr, x + 5, y + 40);
    cairo_show_text(cr, suitStr);
}

//=============================================================================

/**
 * Cairo rendering pipeline. Triggered automatically by GTK whenever 
 * the window needs to be redrawn.
 */
static gboolean OnDrawTable(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    /* Paint the green poker felt background */
    cairo_set_source_rgb(cr, 0.180, 0.545, 0.341); /* SeaGreen (#2E8B57) */
    cairo_paint(cr);

    int card_width = 70;
    int card_height = 100;
    int spacing = 10;
    
    /* --- 1. Draw 5 Community Cards (Center) --- */
    int comm_total_width = (5 * card_width) + (4 * spacing);
    int comm_start_x = (TABLE_AREA_WIDTH - comm_total_width) / 2;
    int comm_start_y = (TABLE_AREA_HEIGHT - card_height) / 2 - 30; // Shifted slightly up

    for (int i = 0; i < 5; i++) {
        /* Hardcoded placeholder data for layout testing. 
           Eventually, this will pull from your global GameData Table struct. */
        char demoSuit = (i % 2 == 0) ? 'S' : 'H'; 
        int demoRank = 10 + i; 
        
        DrawCard(cr, comm_start_x + (i * (card_width + spacing)), comm_start_y, 
                 card_width, card_height, demoSuit, demoRank);
    }

    /* --- 2. Draw 2 Player Hole Cards (Bottom Center) --- */
    int hole_total_width = (2 * card_width) + spacing;
    int hole_start_x = (TABLE_AREA_WIDTH - hole_total_width) / 2;
    int hole_start_y = TABLE_AREA_HEIGHT - card_height - 20;

    /* Draw demo hole cards (Anteater of Clubs and Ace of Spades) */
    DrawCard(cr, hole_start_x, hole_start_y, card_width, card_height, 'C', 14);
    DrawCard(cr, hole_start_x + card_width + spacing, hole_start_y, card_width, card_height, 'S', 15);

    return FALSE;
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