/******************************************************************************
 * File: GameGUI.c
 * Author: Team T3
 * Date: May 23, 2026
 * 
 * * Description:
 * Implements the GTK 3.0 event-driven graphical interface. Constructs 
 * the window hierarchy, control boxes, action buttons, Cairo rendering 
 * hooks, telemetry HUD, and modal credential dialogs.
 *****************************************************************************/

#include <string.h>
#include <sys/socket.h>
#include <gtk/gtk.h>
#include "GameGUI.h"
#include "GameData.h"
#include "GameProtocol.h"

static GtkWidget *pMainWindow;
static GtkWidget *pStatusLabel;
static GtkWidget *pTableArea;
static GtkWidget *pBetSpinButton;
static GtkWidget *pButtonFold  = NULL;
static GtkWidget *pButtonCheck = NULL;
static GtkWidget *pButtonCall  = NULL;
static GtkWidget *pButtonRaise = NULL;
static int g_LocalSeat = -1;
Table *g_pTable = NULL;


//=============================================================================

#define MARGIN_BUTTON_AREA 10
#define MARGIN_BUTTON_BOTTOM 10
#define TABLE_AREA_WIDTH 800
#define TABLE_AREA_HEIGHT 450
#define WINDOW_DEFAULT_WIDTH 800
#define WINDOW_DEFAULT_HEIGHT 450
#define CARD_WIDTH   70
#define CARD_HEIGHT  100
#define CARD_SPACING 10

//=============================================================================

int PromptLoginDetails(char *outName, int *outSeat, char *outPassword, char *outIP)
{
    GtkWidget *dialog, *content_area, *grid;
    GtkWidget *ip_entry, *name_entry, *pass_entry, *seat_spin;
    GtkWidget *ip_label, *name_label, *pass_label, *seat_label;
    int response;
    int accepted = 0;

    dialog = gtk_dialog_new_with_buttons("Server Login",
                                         NULL,
                                         GTK_DIALOG_MODAL,
                                         "Connect",
                                         GTK_RESPONSE_ACCEPT,
                                         "Cancel",
                                         GTK_RESPONSE_REJECT,
                                         NULL);

    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 15);
    gtk_container_add(GTK_CONTAINER(content_area), grid);

    ip_label = gtk_label_new("Server IP:");
    ip_entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(ip_entry), 15);
    gtk_entry_set_text(GTK_ENTRY(ip_entry), "127.0.0.1");

    name_label = gtk_label_new("Username:");
    name_entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(name_entry), 31);
    gtk_entry_set_text(GTK_ENTRY(name_entry), "Player1");

    pass_label = gtk_label_new("Password:");
    pass_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(pass_entry), FALSE);
    gtk_entry_set_max_length(GTK_ENTRY(pass_entry), 31);
    gtk_entry_set_text(GTK_ENTRY(pass_entry), "AnteaterTest");

    seat_label = gtk_label_new("Seat (0-7):");
    seat_spin = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(seat_spin), 1);
    gtk_entry_set_text(GTK_ENTRY(seat_spin), "0");

    gtk_grid_attach(GTK_GRID(grid), ip_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), ip_entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), name_label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), name_entry, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), pass_label, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), pass_entry, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), seat_label, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), seat_spin, 1, 3, 1, 1);

    gtk_widget_show_all(dialog);

    /* Event Loop: Trap execution until valid data is parsed or the window closes */
    while (!accepted) {
        response = gtk_dialog_run(GTK_DIALOG(dialog));

        if (response == GTK_RESPONSE_ACCEPT) {
            const char *seatText = gtk_entry_get_text(GTK_ENTRY(seat_spin));
            int parsedSeat = -1;
            
            if (sscanf(seatText, "%d", &parsedSeat) != 1 || parsedSeat < 0 || parsedSeat > 7) {
                GtkWidget *warningDialog = gtk_message_dialog_new(GTK_WINDOW(dialog),
                                                                  GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
                                                                  GTK_MESSAGE_WARNING,
                                                                  GTK_BUTTONS_OK,
                                                                  "Invalid Seat: '%s'.\nSeat must be an integer between 0 and 7.", 
                                                                  seatText);
                gtk_window_set_title(GTK_WINDOW(warningDialog), "Validation Error");
                gtk_dialog_run(GTK_DIALOG(warningDialog));
                gtk_widget_destroy(warningDialog);
            }
            else {
                strcpy(outIP, gtk_entry_get_text(GTK_ENTRY(ip_entry)));
                strcpy(outName, gtk_entry_get_text(GTK_ENTRY(name_entry)));
                strcpy(outPassword, gtk_entry_get_text(GTK_ENTRY(pass_entry)));
                *outSeat = parsedSeat;
                
                for (int i = 0; outName[i]; i++) if (outName[i] == ' ') outName[i] = '_';
                for (int i = 0; outPassword[i]; i++) if (outPassword[i] == ' ') outPassword[i] = '_';

                accepted = 1;
            }
        }
        else {
            break;
        }
    }

    gtk_widget_destroy(dialog);
    return accepted;
}

void SetActionButtonsSensitive(gboolean sensitive)
{
    if (pButtonFold)    gtk_widget_set_sensitive(pButtonFold,    sensitive);
    if (pButtonCheck)   gtk_widget_set_sensitive(pButtonCheck,   sensitive);
    if (pButtonCall)    gtk_widget_set_sensitive(pButtonCall,    sensitive);
    if (pButtonRaise)   gtk_widget_set_sensitive(pButtonRaise,   sensitive);
    if (pBetSpinButton) gtk_widget_set_sensitive(pBetSpinButton, sensitive);
}

//=============================================================================

static void OnActionButtonClicked(GtkWidget *widget, gpointer data)
{
    extern int g_client_socket;
    int action = GPOINTER_TO_INT(data);
    char buffer[MAX_MSG_LEN];

    /* Dynamically fetch the wager amount from the spin button */
    int wagerAmount = 0;
    if (pBetSpinButton != NULL) {
        wagerAmount = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(pBetSpinButton));
    }

    BuildActionMessage(buffer, g_LocalSeat, action, wagerAmount);
    
    if (g_client_socket != -1) {
        send(g_client_socket, buffer, strlen(buffer), 0);
        g_print("Sent to server: %s", buffer);
    }
    else {
        /* Fallback trap for --offline headless testing */
        g_print("Offline Mode: Simulated action '%d' with wager '%d'.\n", action, wagerAmount);
    }
}

//=============================================================================

static void CreateAndPackActionButtons(GtkWidget *pHBox)
{
    if (pHBox == NULL) return;

    pButtonFold  = gtk_button_new_with_label("FOLD");
    pButtonCheck = gtk_button_new_with_label("CHECK");
    pButtonCall  = gtk_button_new_with_label("CALL");
    pButtonRaise = gtk_button_new_with_label("RAISE");

    g_signal_connect(pButtonFold,  "clicked", G_CALLBACK(OnActionButtonClicked),
                     GINT_TO_POINTER(PLAYER_ACTION_FOLD));
    g_signal_connect(pButtonCheck, "clicked", G_CALLBACK(OnActionButtonClicked),
                     GINT_TO_POINTER(PLAYER_ACTION_CHECK));
    g_signal_connect(pButtonCall,  "clicked", G_CALLBACK(OnActionButtonClicked),
                     GINT_TO_POINTER(PLAYER_ACTION_CALL));
    g_signal_connect(pButtonRaise, "clicked", G_CALLBACK(OnActionButtonClicked),
                     GINT_TO_POINTER(PLAYER_ACTION_RAISE));

    gtk_box_pack_start(GTK_BOX(pHBox), pButtonFold,  TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(pHBox), pButtonCheck, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(pHBox), pButtonCall,  TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(pHBox), pButtonRaise, TRUE, TRUE, 0);

    GtkWidget *pBetLabel = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(pBetLabel), "<span weight='bold'>Wager:</span>");
    pBetSpinButton = gtk_spin_button_new_with_range(0, 10000, 10);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pBetSpinButton), 50);
    gtk_widget_set_margin_start(pBetLabel, 20);
    gtk_box_pack_start(GTK_BOX(pHBox), pBetLabel,      FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(pHBox), pBetSpinButton, FALSE, FALSE, 0);

    /* Start greyed out — enabled only when server signals your turn */
    SetActionButtonsSensitive(FALSE);
}

//=============================================================================

static void DrawCard(cairo_t *cr, int x, int y, int width, int height,
                     char suit, int rank)
{
    /* Border — drawn for both face and back */
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_set_line_width(cr, 2.0);
    cairo_rectangle(cr, x, y, width, height);
    cairo_stroke(cr);

    /* ── CARD BACK: rank == 0 && suit == 0 ── */
    if (rank == 0 && suit == 0) {
        cairo_set_source_rgb(cr, 0.10, 0.18, 0.45);
        cairo_rectangle(cr, x + 1, y + 1, width - 2, height - 2);
        cairo_fill(cr);

        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.15);
        cairo_set_line_width(cr, 1.0);
        for (int dx = 0; dx < width; dx += 8) {
            cairo_move_to(cr, x + dx, y);
            cairo_line_to(cr, x + dx, y + height);
        }
        for (int dy = 0; dy < height; dy += 8) {
            cairo_move_to(cr, x,         y + dy);
            cairo_line_to(cr, x + width, y + dy);
        }
        cairo_stroke(cr);
        return;
    }

    /* ── CARD FACE ── */
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_rectangle(cr, x + 1, y + 1, width - 2, height - 2);
    cairo_fill(cr);

    if (rank == 0) return;  /* blank white face (original fallback) */

    if (suit == 'H' || suit == 'D') {
        cairo_set_source_rgb(cr, 0.8, 0.1, 0.1);
    } else if (suit == 'N') {
        cairo_set_source_rgb(cr, 0.5, 0.0, 0.5);
    } else {
        cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
    }

    char rankStr[8];
    if (rank >= 2 && rank <= 10) {
        snprintf(rankStr, sizeof(rankStr), "%d", rank);
    } else {
        switch (rank) {
            case 11: strcpy(rankStr, "J");    break;
            case 12: strcpy(rankStr, "Q");    break;
            case 13: strcpy(rankStr, "K");    break;
            case 14: strcpy(rankStr, "ANT");  break;
            case 15: strcpy(rankStr, "A");    break;
            case 16: strcpy(rankStr, "WILD"); break;
            default: strcpy(rankStr, "?");    break;
        }
    }

    char suitStr[8];
    switch (suit) {
        case 'H': strcpy(suitStr, "♥"); break;
        case 'D': strcpy(suitStr, "♦"); break;
        case 'C': strcpy(suitStr, "♣"); break;
        case 'S': strcpy(suitStr, "♠"); break;
        default:  strcpy(suitStr, "");  break;
    }

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 16);
    cairo_move_to(cr, x + 5, y + 20);
    cairo_show_text(cr, rankStr);
    cairo_move_to(cr, x + 5, y + 40);
    cairo_show_text(cr, suitStr);
}

//=============================================================================

static gboolean OnDrawTable(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    cairo_set_source_rgb(cr, 0.180, 0.545, 0.341);
    cairo_paint(cr);

    int comm_total_width = (5 * CARD_WIDTH) + (4 * CARD_SPACING);
    int comm_start_x = (TABLE_AREA_WIDTH  - comm_total_width) / 2;
    int comm_start_y = (TABLE_AREA_HEIGHT - CARD_HEIGHT) / 2 - 30;

    int revealedCount = 0;
    if (g_pTable != NULL) {
        switch (g_pTable->state) {
            case GAME_STATE_FLOP:     revealedCount = 3; break;
            case GAME_STATE_TURN:     revealedCount = 4; break;
            case GAME_STATE_RIVER:
            case GAME_STATE_SHOWDOWN: revealedCount = 5; break;
            default:                  revealedCount = 0; break;
        }
    }

    for (int i = 0; i < 5; i++) {
        int cx = comm_start_x + i * (CARD_WIDTH + CARD_SPACING);
        if (g_pTable != NULL && i < revealedCount) {
            Card c = g_pTable->community[i];
            DrawCard(cr, cx, comm_start_y, CARD_WIDTH, CARD_HEIGHT, c.suit, c.rank);
        } else {
            DrawCard(cr, cx, comm_start_y, CARD_WIDTH, CARD_HEIGHT, 0, 0);
        }
    }

    int hole_total_width = (2 * CARD_WIDTH) + CARD_SPACING;
    int hole_start_x = (TABLE_AREA_WIDTH - hole_total_width) / 2;
    int hole_start_y = TABLE_AREA_HEIGHT - CARD_HEIGHT - 20;

    if (g_pTable != NULL && g_LocalSeat >= 0 && g_LocalSeat < MAX_PLAYERS) {
        Player *me = &g_pTable->players[g_LocalSeat];
        if (g_pTable->state >= GAME_STATE_PRE_FLOP) {
            DrawCard(cr, hole_start_x, hole_start_y,
                     CARD_WIDTH, CARD_HEIGHT,
                     me->hand[0].suit, me->hand[0].rank);
            DrawCard(cr, hole_start_x + CARD_WIDTH + CARD_SPACING, hole_start_y,
                     CARD_WIDTH, CARD_HEIGHT,
                     me->hand[1].suit, me->hand[1].rank);
        } else {
            DrawCard(cr, hole_start_x, hole_start_y,
                     CARD_WIDTH, CARD_HEIGHT, 0, 0);
            DrawCard(cr, hole_start_x + CARD_WIDTH + CARD_SPACING, hole_start_y,
                     CARD_WIDTH, CARD_HEIGHT, 0, 0);
        }
    } else {
        DrawCard(cr, hole_start_x, hole_start_y,
                 CARD_WIDTH, CARD_HEIGHT, 0, 0);
        DrawCard(cr, hole_start_x + CARD_WIDTH + CARD_SPACING, hole_start_y,
                 CARD_WIDTH, CARD_HEIGHT, 0, 0);
    }

    return FALSE;
}

//=============================================================================

void InitializeGUI(int localSeat)
{
    g_LocalSeat = localSeat;

    pMainWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(pMainWindow), "Anteater Poker");
    gtk_window_set_default_size(GTK_WINDOW(pMainWindow), WINDOW_DEFAULT_WIDTH, WINDOW_DEFAULT_HEIGHT);
    gtk_window_set_position(GTK_WINDOW(pMainWindow), GTK_WIN_POS_CENTER);
    gtk_window_set_resizable(GTK_WINDOW(pMainWindow), FALSE);

    g_signal_connect(pMainWindow, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *pVBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(pMainWindow), pVBox);

    pStatusLabel = gtk_label_new(NULL);
    UpdateTelemetryHUD(0, 0, "Initializing Framework...");
    
    gtk_widget_set_margin_top(pStatusLabel, 10);
    gtk_box_pack_start(GTK_BOX(pVBox), pStatusLabel, FALSE, FALSE, 0);

    pTableArea = gtk_drawing_area_new();
    gtk_widget_set_size_request(pTableArea, TABLE_AREA_WIDTH, TABLE_AREA_HEIGHT);
    g_signal_connect(pTableArea, "draw", G_CALLBACK(OnDrawTable), NULL);

    gtk_box_pack_start(GTK_BOX(pVBox), pTableArea, TRUE, TRUE, 0);

    GtkWidget *pHBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(pHBox, MARGIN_BUTTON_AREA);
    gtk_widget_set_margin_end(pHBox, MARGIN_BUTTON_AREA);
    gtk_widget_set_margin_bottom(pHBox, MARGIN_BUTTON_BOTTOM);
    gtk_box_pack_start(GTK_BOX(pVBox), pHBox, FALSE, FALSE, 0);

    CreateAndPackActionButtons(pHBox);
}

//=============================================================================

void ShowMainWindow(void)
{
    if (pMainWindow == NULL) return;
    gtk_widget_show_all(pMainWindow);
}

//=============================================================================

void UpdateTelemetryHUD(int pot, int points, const char *statusMsg)
{
    if (pStatusLabel == NULL || statusMsg == NULL) return;

    char markup[512];
    snprintf(markup, sizeof(markup), 
             "<span font_desc='14' weight='bold' color='white'>"
             "Pot: %d | Your Points: %d | Status: %s"
             "</span>", 
             pot, points, statusMsg);
             
    gtk_label_set_markup(GTK_LABEL(pStatusLabel), markup);
}

//=============================================================================

void TriggerTableRedraw(void)
{
    if (pTableArea != NULL) {
        gtk_widget_queue_draw(pTableArea);
    }
}