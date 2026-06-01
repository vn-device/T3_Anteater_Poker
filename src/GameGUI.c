/******************************************************************************
 * File: GameGUI.c
 * Author: Team T3
 * Date: May 31, 2026
 * 
 * * Description:
 * Implements the GTK 3.0 event-driven graphical interface. Utilizes a main 
 * GtkStack to toggle between the starting Role Selection menu, the Host/Join 
 * configuration forms, and the active Poker Table within a single unified window.
 * Integrates ranked showdown leaderboard and round timer overlays.
 *****************************************************************************/

#include <string.h>
#include <sys/socket.h>
#include <gtk/gtk.h>
#include "GameGUI.h"
#include "GameData.h"
#include "GameProtocol.h"
#include <math.h>
#include "HandEval.h"

/* External Networking Hooks defined in main.c */
extern int PerformHostConnection(const char *name, const char *password, int maxPlayers, char *outRoomCode, int *outSeat);
extern int PerformJoinConnection(const char *name, const char *password, const char *roomCode, int *outSeat);
extern void StartNetworkListener(int playerSeat);

/* Global GUI State Pointers */
static GtkWidget *pMainWindow;
static GtkWidget *pMainStack;
static GtkWidget *pLobbyStack;
static GtkWidget *pStatusLabel;
static GtkWidget *pTableArea;
static GtkWidget *pBetSpinButton;
static GtkWidget *pButtonFold  = NULL;
static GtkWidget *pButtonCheck = NULL;
static GtkWidget *pButtonCall  = NULL;
static GtkWidget *pButtonRaise = NULL;
static GtkWidget *pButtonStartGame = NULL;

/* Lobby Input Pointers */
static GtkWidget *h_name_entry, *h_pass_entry, *h_spin;
static GtkWidget *j_code_entry, *j_name_entry, *j_pass_entry;

/* Timer State */
static guint g_TimerSourceId  = 0;
static int   g_RoundElapsed   = 0;

static int g_CurrentCallAmount = 0;
static int g_CurrentMinRaise   = 0;
static int g_LocalSeat = -1;

/* Dedicated Client-Side Data Matrix */
static Table g_ClientTable;
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
#define SEAT_ELLIPSE_RX  280  
#define SEAT_ELLIPSE_RY  165
#define SEAT_BADGE_W     120
#define SEAT_BADGE_H      60
#define SEAT_CARD_W       28
#define SEAT_CARD_H       40
#define SEAT_CARD_GAP      4

//=============================================================================

static void OnLobbyStackNavClicked(GtkWidget *widget, gpointer data)
{
    GtkStack *stack = GTK_STACK(data);
    const gchar *target = (const gchar *)g_object_get_data(G_OBJECT(widget), "target_page");
    gtk_stack_set_visible_child_name(stack, target);
}

static void FormatNetworkString(char *str, const gchar *input, size_t max_len)
{
    strncpy(str, input, max_len - 1);
    str[max_len - 1] = '\0';
    for (int i = 0; str[i]; i++) {
        if (str[i] == ' ') {
            str[i] = '_';
        }
    }
}

static void ShowLobbyError(const char *message)
{
    GtkWidget *err = gtk_message_dialog_new(GTK_WINDOW(pMainWindow),
                                            GTK_DIALOG_MODAL,
                                            GTK_MESSAGE_ERROR,
                                            GTK_BUTTONS_OK,
                                            "%s",
                                            message);
    gtk_dialog_run(GTK_DIALOG(err));
    gtk_widget_destroy(err);
}

static void OnHostStartClicked(GtkWidget *widget, gpointer data)
{
    char safeName[32], safePass[32], roomCode[16] = {0};
    int seat = -1;
    
    FormatNetworkString(safeName, gtk_entry_get_text(GTK_ENTRY(h_name_entry)), sizeof(safeName));
    FormatNetworkString(safePass, gtk_entry_get_text(GTK_ENTRY(h_pass_entry)), sizeof(safePass));
    int maxPlayers = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(h_spin));

    if (safeName[0] == '\0' || safePass[0] == '\0') {
        ShowLobbyError("Username and lobby password are required.");
        return;
    }

    if (PerformHostConnection(safeName, safePass, maxPlayers, roomCode, &seat)) {
        g_LocalSeat = seat;
        char hudMsg[128];
        snprintf(hudMsg, sizeof(hudMsg), "Hosting Lobby | Code: %s", roomCode);
        UpdateTelemetryHUD(0, 1000, hudMsg);
        
        gtk_stack_set_visible_child_name(GTK_STACK(pMainStack), "page_table");
        StartNetworkListener(seat);

        if (g_LocalSeat == 0 && pButtonStartGame != NULL) {
            gtk_widget_show(pButtonStartGame);
        }
    }
    else {
        ShowLobbyError("Failed to initialize host lobby.");
    }
}

static void OnJoinConnectClicked(GtkWidget *widget, gpointer data)
{
    char safeName[32], safePass[32];
    int seat = -1;
    const gchar *rawCode = gtk_entry_get_text(GTK_ENTRY(j_code_entry));
    
    FormatNetworkString(safeName, gtk_entry_get_text(GTK_ENTRY(j_name_entry)), sizeof(safeName));
    FormatNetworkString(safePass, gtk_entry_get_text(GTK_ENTRY(j_pass_entry)), sizeof(safePass));

    if (safeName[0] == '\0' || safePass[0] == '\0' || rawCode[0] == '\0') {
        ShowLobbyError("Room code, username, and password are required.");
        return;
    }

    if (PerformJoinConnection(safeName, safePass, rawCode, &seat)) {
        g_LocalSeat = seat;
        char hudMsg[128];
        snprintf(hudMsg, sizeof(hudMsg), "Joined Lobby | Code: %s", rawCode);
        UpdateTelemetryHUD(0, 1000, hudMsg);
        
        gtk_stack_set_visible_child_name(GTK_STACK(pMainStack), "page_table");
        StartNetworkListener(seat);
    }
    else {
        ShowLobbyError("Connection rejected. Check the room code, password, and username.");
    }
}

static void OnStartGameClicked(GtkWidget *widget, gpointer data)
{
    extern int g_client_socket;
    char buffer[MAX_MSG_LEN];

    BuildStartMessage(buffer);
    if (g_client_socket != -1) {
        send(g_client_socket, buffer, strlen(buffer), 0);
        g_print("Sent to server: %s", buffer);
        gtk_widget_set_sensitive(pButtonStartGame, FALSE); 
    }
}

//=============================================================================

static gboolean OnTimerTick(gpointer data)
{
    (void)data;
    g_RoundElapsed++;
    TriggerTableRedraw();
    return G_SOURCE_CONTINUE;
}

void ResetRoundTimer(void)
{
    if (g_TimerSourceId != 0) {
        g_source_remove(g_TimerSourceId);
        g_TimerSourceId = 0;
    }
    g_RoundElapsed = 0;
    g_TimerSourceId = g_timeout_add_seconds(1, OnTimerTick, NULL);
}

//=============================================================================

static void RefreshActionButtonLabels(void)
{
    if (!pButtonCall || !pButtonCheck || !pButtonRaise) return;

    char buf[64];

    if (g_CurrentCallAmount <= 0) {
        gtk_button_set_label(GTK_BUTTON(pButtonCheck), "CHECK  (C)");
        gtk_button_set_label(GTK_BUTTON(pButtonCall),  "CALL");
        gtk_widget_set_sensitive(pButtonCheck, TRUE);
        gtk_widget_set_sensitive(pButtonCall,  FALSE);
    }
    else {
        gtk_button_set_label(GTK_BUTTON(pButtonCheck), "CHECK");
        snprintf(buf, sizeof(buf), "CALL $%d  (C)", g_CurrentCallAmount);
        gtk_button_set_label(GTK_BUTTON(pButtonCall), buf);
        gtk_widget_set_sensitive(pButtonCheck, FALSE);
        gtk_widget_set_sensitive(pButtonCall,  TRUE);
    }

    snprintf(buf, sizeof(buf), "RAISE  (R)  min $%d", g_CurrentMinRaise);
    gtk_button_set_label(GTK_BUTTON(pButtonRaise), buf);
}

void UpdateActionContext(int callAmount, int minRaise)
{
    g_CurrentCallAmount = callAmount;
    g_CurrentMinRaise   = minRaise;
    RefreshActionButtonLabels();
}

void SetActionButtonsSensitive(gboolean sensitive)
{
    if (pButtonFold)    gtk_widget_set_sensitive(pButtonFold,    sensitive);
    if (pButtonCheck)   gtk_widget_set_sensitive(pButtonCheck,   sensitive);
    if (pButtonCall)    gtk_widget_set_sensitive(pButtonCall,    sensitive);
    if (pButtonRaise)   gtk_widget_set_sensitive(pButtonRaise,   sensitive);
    if (pBetSpinButton) gtk_widget_set_sensitive(pBetSpinButton, sensitive);

    if (sensitive) {
        if (pBetSpinButton) {
            gtk_spin_button_set_range(GTK_SPIN_BUTTON(pBetSpinButton), g_CurrentMinRaise, 100000);
            if (gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(pBetSpinButton)) < g_CurrentMinRaise) {
                gtk_spin_button_set_value(GTK_SPIN_BUTTON(pBetSpinButton), g_CurrentMinRaise);
            }
        }
        RefreshActionButtonLabels();
    }
}

static void OnActionButtonClicked(GtkWidget *widget, gpointer data)
{
    extern int g_client_socket;
    int action = GPOINTER_TO_INT(data);
    char buffer[MAX_MSG_LEN];

    int wagerAmount = 0;
    if (pBetSpinButton != NULL) {
        wagerAmount = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(pBetSpinButton));
    }

    if (action == PLAYER_ACTION_RAISE && wagerAmount < g_CurrentMinRaise) {
        GtkWidget *warn = gtk_message_dialog_new(
            GTK_WINDOW(pMainWindow),
            GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING,
            GTK_BUTTONS_OK,
            "Raise of $%d is below the minimum raise of $%d.",
            wagerAmount, g_CurrentMinRaise);
        gtk_window_set_title(GTK_WINDOW(warn), "Invalid Raise");
        gtk_dialog_run(GTK_DIALOG(warn));
        gtk_widget_destroy(warn);
        return;
    }

    BuildActionMessage(buffer, g_LocalSeat, action, wagerAmount);
    
    if (g_client_socket != -1) {
        send(g_client_socket, buffer, strlen(buffer), 0);
        g_print("Sent to server: %s", buffer);
    }
    else {
        g_print("Offline Mode: Simulated action '%d' with wager '%d'.\n", action, wagerAmount);
    }
}

static void CreateAndPackActionButtons(GtkWidget *pHBox)
{
    if (pHBox == NULL) return;

    pButtonFold  = gtk_button_new_with_label("FOLD");
    pButtonCheck = gtk_button_new_with_label("CHECK");
    pButtonCall  = gtk_button_new_with_label("CALL");
    pButtonRaise = gtk_button_new_with_label("RAISE");

    g_signal_connect(pButtonFold,  "clicked", G_CALLBACK(OnActionButtonClicked), GINT_TO_POINTER(PLAYER_ACTION_FOLD));
    g_signal_connect(pButtonCheck, "clicked", G_CALLBACK(OnActionButtonClicked), GINT_TO_POINTER(PLAYER_ACTION_CHECK));
    g_signal_connect(pButtonCall,  "clicked", G_CALLBACK(OnActionButtonClicked), GINT_TO_POINTER(PLAYER_ACTION_CALL));
    g_signal_connect(pButtonRaise, "clicked", G_CALLBACK(OnActionButtonClicked), GINT_TO_POINTER(PLAYER_ACTION_RAISE));

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

    SetActionButtonsSensitive(FALSE);
}

//=============================================================================

static void DrawCard(cairo_t *cr, int x, int y, int width, int height, char suit, int rank)
{
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_set_line_width(cr, 2.0);
    cairo_rectangle(cr, x, y, width, height);
    cairo_stroke(cr);

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

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_rectangle(cr, x + 1, y + 1, width - 2, height - 2);
    cairo_fill(cr);

    if (rank == 0) return;

    if (suit == 'H' || suit == 'D') {
        cairo_set_source_rgb(cr, 0.8, 0.1, 0.1);
    }
    else if (suit == 'N') {
        cairo_set_source_rgb(cr, 0.5, 0.0, 0.5);
    }
    else {
        cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
    }

    char rankStr[8];
    if (rank >= 2 && rank <= 10) {
        snprintf(rankStr, sizeof(rankStr), "%d", rank);
    }
    else {
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

static void ComputeSeatPosition(int seatIndex, int localSeat, int *outX, int *outY)
{
    const int    cx    = TABLE_AREA_WIDTH  / 2;
    const int cy = TABLE_AREA_HEIGHT / 2 - 40;
    const double rx    = SEAT_ELLIPSE_RX;
    const double ry    = SEAT_ELLIPSE_RY;

    int    relative = (seatIndex - localSeat + MAX_PLAYERS) % MAX_PLAYERS;
    double angle    = (G_PI / 2.0) + (2.0 * G_PI * relative) / MAX_PLAYERS;

    *outX = cx + (int)(rx * cos(angle));
    *outY = cy + (int)(ry * sin(angle));
}

static void DrawSeat(cairo_t *cr, int cx, int cy,
                     const Player *p, int seatIndex,
                     gboolean isLocal, gboolean showCards,
                     gboolean isTurn, gboolean isDealer)
{
    const int bw = SEAT_BADGE_W;
    const int bh = SEAT_BADGE_H;
    const int bx = cx - bw / 2;
    const int by = cy - bh / 2;

    if (p == NULL || p->name[0] == '\0') {
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.25);
        cairo_rectangle(cr, bx, by, bw, bh);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.3);
        cairo_set_line_width(cr, 1.0);
        cairo_rectangle(cr, bx, by, bw, bh);
        cairo_stroke(cr);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 10);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.4);
        char emptyLabel[16];
        snprintf(emptyLabel, sizeof(emptyLabel), "Seat %d", seatIndex);
        cairo_move_to(cr, bx + 8, by + bh / 2 + 4);
        cairo_show_text(cr, emptyLabel);
        return;
    }

    double alpha = (p->isFolded) ? 0.35 : 0.80;

    if (isLocal) {
        cairo_set_source_rgba(cr, 0.05, 0.20, 0.10, alpha);
    }
    else {
        cairo_set_source_rgba(cr, 0.05, 0.05, 0.15, alpha);
    }
    cairo_rectangle(cr, bx, by, bw, bh);
    cairo_fill(cr);

    cairo_set_line_width(cr, isLocal ? 2.5 : 1.5);
    
    if (isTurn) {
        cairo_set_source_rgb(cr, 1.0, 0.85, 0.0);
    }
    else if (isLocal) {
        cairo_set_source_rgba(cr, 0.4, 0.9, 0.5, 0.9);
    }
    else {
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.4);
    }
    cairo_rectangle(cr, bx, by, bw, bh);
    cairo_stroke(cr);

    int cardStartX = bx + 4;
    int cardStartY = by + 4;

    if (g_pTable && g_pTable->state >= GAME_STATE_PRE_FLOP && !p->isFolded) {
        if (showCards) {
            DrawCard(cr, cardStartX, cardStartY,
                     SEAT_CARD_W, SEAT_CARD_H, p->hand[0].suit, p->hand[0].rank);
            DrawCard(cr, cardStartX + SEAT_CARD_W + SEAT_CARD_GAP, cardStartY,
                     SEAT_CARD_W, SEAT_CARD_H, p->hand[1].suit, p->hand[1].rank);
        }
        else {
            DrawCard(cr, cardStartX, cardStartY,
                     SEAT_CARD_W, SEAT_CARD_H, 0, 0);
            DrawCard(cr, cardStartX + SEAT_CARD_W + SEAT_CARD_GAP, cardStartY,
                     SEAT_CARD_W, SEAT_CARD_H, 0, 0);
        }
    }

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 10);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    char nameBuf[16];
    snprintf(nameBuf, sizeof(nameBuf), "%.12s", p->name);
    
    /* Shift Name coordinates to the right of the hole cards */
    cairo_move_to(cr, bx + 66, by + 26);
    cairo_show_text(cr, nameBuf);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 9);
    cairo_set_source_rgba(cr, 0.8, 0.9, 0.8, 1.0);
    char chipBuf[24];
    snprintf(chipBuf, sizeof(chipBuf), "$%d", p->points);
    
    /* Shift Chip coordinates to the right of the hole cards */
    cairo_move_to(cr, bx + 66, by + 42);
    cairo_show_text(cr, chipBuf);

    if (p->isFolded) {
        cairo_set_source_rgba(cr, 0.7, 0.1, 0.1, 0.85);
        cairo_rectangle(cr, bx + bw - 48, by + bh - 16, 44, 13);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_set_font_size(cr, 8);
        cairo_move_to(cr, bx + bw - 46, by + bh - 6);
        cairo_show_text(cr, "FOLDED");
    }
    else if (isTurn) {
        if (isLocal) {
            cairo_set_source_rgba(cr, 0.9, 0.75, 0.0, 0.90);
            cairo_rectangle(cr, bx + bw - 60, by + bh - 16, 56, 13);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
            cairo_set_font_size(cr, 8);
            cairo_move_to(cr, bx + bw - 58, by + bh - 6);
            cairo_show_text(cr, "YOUR TURN");
        }
        else {
            cairo_set_source_rgba(cr, 0.2, 0.4, 0.8, 0.85);
            cairo_rectangle(cr, bx + bw - 64, by + bh - 16, 60, 13);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            cairo_set_font_size(cr, 8);
            cairo_move_to(cr, bx + bw - 62, by + bh - 6);
            cairo_show_text(cr, "THEIR TURN");
        }
    }

    if (isDealer) {
        int dcx = bx + bw - 9;
        int dcy = by + 9;
        cairo_set_source_rgb(cr, 0.95, 0.95, 0.95);
        cairo_arc(cr, dcx, dcy, 8, 0, 2 * G_PI);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
        cairo_set_line_width(cr, 1.0);
        cairo_arc(cr, dcx, dcy, 8, 0, 2 * G_PI);
        cairo_stroke(cr);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 9);
        cairo_move_to(cr, dcx - 4, dcy + 4);
        cairo_show_text(cr, "D");
    }
}

static void DrawShowdownOverlay(cairo_t *cr)
{
    if (g_pTable == NULL) return;

    typedef struct {
        int       seatIndex;
        HandValue hv;
    } SeatResult;

    SeatResult results[MAX_PLAYERS];
    int        resultCount = 0;

    for (int s = 0; s < MAX_PLAYERS; s++) {
        Player *p = &g_pTable->players[s];
        if (p->name[0] == '\0' || p->isFolded) continue;

        Card combined[7];
        combined[0] = p->hand[0];
        combined[1] = p->hand[1];
        for (int i = 0; i < 5; i++) {
            combined[2 + i] = g_pTable->community[i];
        }

        results[resultCount].seatIndex = s;
        EvaluateBestHand(combined, 7, &results[resultCount].hv);
        resultCount++;
    }

    for (int i = 1; i < resultCount; i++) {
        SeatResult key = results[i];
        int j = i - 1;
        while (j >= 0 && CompareHandValues(&results[j].hv, &key.hv) < 0) {
            results[j + 1] = results[j];
            j--;
        }
        results[j + 1] = key;
    }

    const int panelW = 340;
    const int panelH = 30 + resultCount * 26 + 10;
    const int panelX = (TABLE_AREA_WIDTH  - panelW) / 2;
    const int panelY = (TABLE_AREA_HEIGHT - panelH) / 2 - 20;

    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.78);
    cairo_rectangle(cr, panelX, panelY, panelW, panelH);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, 1.0, 0.85, 0.0, 0.9);
    cairo_set_line_width(cr, 2.0);
    cairo_rectangle(cr, panelX, panelY, panelW, panelH);
    cairo_stroke(cr);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 13);
    cairo_set_source_rgb(cr, 1.0, 0.85, 0.0);
    cairo_move_to(cr, panelX + 12, panelY + 20);
    cairo_show_text(cr, "SHOWDOWN RESULTS");

    for (int i = 0; i < resultCount; i++) {
        Player   *p    = &g_pTable->players[results[i].seatIndex];
        int       rowY = panelY + 30 + i * 26;
        gboolean  isWinner = (i == 0);

        if (isWinner) {
            cairo_set_source_rgba(cr, 1.0, 0.85, 0.0, 0.15);
            cairo_rectangle(cr, panelX + 2, rowY, panelW - 4, 22);
            cairo_fill(cr);
        }

        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 11);
        cairo_set_source_rgb(cr, isWinner ? 1.0 : 0.7,
                                 isWinner ? 0.85 : 0.7,
                                 isWinner ? 0.0 : 0.7);
        char rankStr[4];
        snprintf(rankStr, sizeof(rankStr), "#%d", i + 1);
        cairo_move_to(cr, panelX + 10, rowY + 15);
        cairo_show_text(cr, rankStr);

        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_set_font_size(cr, 11);
        char nameBuf[16];
        snprintf(nameBuf, sizeof(nameBuf), "%.12s", p->name);
        cairo_move_to(cr, panelX + 40, rowY + 15);
        cairo_show_text(cr, nameBuf);

        const char *handName = HandCategoryToString(results[i].hv.category);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_ITALIC, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_source_rgba(cr, 0.7, 0.95, 0.7, 1.0);
        cairo_move_to(cr, panelX + 155, rowY + 15);
        cairo_show_text(cr, handName);

        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_source_rgba(cr, 0.8, 0.9, 0.8, 1.0);
        char ptsBuf[16];
        snprintf(ptsBuf, sizeof(ptsBuf), "$%d", p->points);
        cairo_move_to(cr, panelX + panelW - 55, rowY + 15);
        cairo_show_text(cr, ptsBuf);
    }
}

static gboolean OnDrawTable(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    cairo_set_source_rgb(cr, 0.118, 0.420, 0.255);
    cairo_paint(cr);

    int cx = TABLE_AREA_WIDTH  / 2;
    int cy = TABLE_AREA_HEIGHT / 2;
    cairo_pattern_t *vignette = cairo_pattern_create_radial(cx, cy, 80, cx, cy, 460);
    cairo_pattern_add_color_stop_rgba(vignette, 0.0, 0.0, 0.0, 0.0, 0.0);
    cairo_pattern_add_color_stop_rgba(vignette, 1.0, 0.0, 0.0, 0.0, 0.45);
    cairo_set_source(cr, vignette);
    cairo_paint(cr);
    cairo_pattern_destroy(vignette);

    if (g_pTable != NULL && g_pTable->pot > 0) {
        char potBuf[32];
        snprintf(potBuf, sizeof(potBuf), "POT: $%d", g_pTable->pot);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 13);
        cairo_set_source_rgba(cr, 1.0, 0.95, 0.7, 0.95);
        cairo_move_to(cr, TABLE_AREA_WIDTH / 2 - 42,
                      TABLE_AREA_HEIGHT / 2 - 40 - SEAT_ELLIPSE_RY / 2 - 10);
        cairo_show_text(cr, potBuf);
    }
    
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
        int cardX = comm_start_x + i * (CARD_WIDTH + CARD_SPACING);
        if (g_pTable != NULL && i < revealedCount) {
            Card c = g_pTable->community[i];
            DrawCard(cr, cardX, comm_start_y, CARD_WIDTH, CARD_HEIGHT, c.suit, c.rank);
        }
        else {
            DrawCard(cr, cardX, comm_start_y, CARD_WIDTH, CARD_HEIGHT, 0, 0);
        }
    }

    for (int s = 0; s < MAX_PLAYERS; s++) {
        int sx, sy;
        ComputeSeatPosition(s, (g_LocalSeat >= 0 ? g_LocalSeat : 0), &sx, &sy);

        gboolean isLocal   = (s == g_LocalSeat);
        gboolean isTurn = (g_pTable != NULL && g_pTable->activeIdx == s);
        gboolean isDealer = (g_pTable != NULL && g_pTable->dealerIdx == s);
        gboolean showCards = isLocal ||
                             (g_pTable != NULL && g_pTable->state == GAME_STATE_SHOWDOWN);

        const Player *p = (g_pTable != NULL) ? &g_pTable->players[s] : NULL;

        DrawSeat(cr, sx, sy, p, s, isLocal, showCards, isTurn, isDealer);
    }

    if (g_TimerSourceId != 0) {
        char timerBuf[32];
        int  mins = g_RoundElapsed / 60;
        int  secs = g_RoundElapsed % 60;
        snprintf(timerBuf, sizeof(timerBuf), "Round: %02d:%02d", mins, secs);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 12);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.75);
        cairo_move_to(cr, TABLE_AREA_WIDTH - 115, 20);
        cairo_show_text(cr, timerBuf);
    }
    
    if (g_pTable != NULL && g_pTable->state == GAME_STATE_SHOWDOWN) {
        DrawShowdownOverlay(cr);
    }

    return FALSE;
}

static gboolean OnKeyPress(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    (void)widget; (void)data;

    switch (event->keyval) {
        case GDK_KEY_f:
        case GDK_KEY_F:
            if (pButtonFold && gtk_widget_is_sensitive(pButtonFold)) {
                gtk_button_clicked(GTK_BUTTON(pButtonFold));
                return TRUE;
            }
            break;

        case GDK_KEY_c:
        case GDK_KEY_C:
            if (g_CurrentCallAmount <= 0) {
                if (pButtonCheck && gtk_widget_is_sensitive(pButtonCheck)) {
                    gtk_button_clicked(GTK_BUTTON(pButtonCheck));
                    return TRUE;
                }
            }
            else {
                if (pButtonCall && gtk_widget_is_sensitive(pButtonCall)) {
                    gtk_button_clicked(GTK_BUTTON(pButtonCall));
                    return TRUE;
                }
            }
            break;

        case GDK_KEY_r:
        case GDK_KEY_R:
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            if (pButtonRaise && gtk_widget_is_sensitive(pButtonRaise)) {
                gtk_button_clicked(GTK_BUTTON(pButtonRaise));
                return TRUE;
            }
            break;

        default:
            break;
    }
    return FALSE;
}

//=============================================================================
/* Modular Sub-components for GtkStack Construction */
//=============================================================================

static GtkWidget* CreateLobbyPage(void)
{
    pLobbyStack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(pLobbyStack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);

    /* --- PAGE: Role Selection --- */
    GtkWidget *box_sel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_widget_set_valign(box_sel, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(box_sel, GTK_ALIGN_CENTER);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span font_desc='18' weight='bold'>Anteater Poker Network</span>");
    
    GtkWidget *btn_go_host = gtk_button_new_with_label("Host Private Game");
    GtkWidget *btn_go_join = gtk_button_new_with_label("Join via Room Code");

    gtk_box_pack_start(GTK_BOX(box_sel), title, FALSE, FALSE, 20);
    gtk_box_pack_start(GTK_BOX(box_sel), btn_go_host, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box_sel), btn_go_join, TRUE, TRUE, 0);
    
    gtk_stack_add_named(GTK_STACK(pLobbyStack), box_sel, "page_sel");

    /* --- PAGE: Host Settings --- */
    GtkWidget *box_host = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_valign(box_host, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(box_host, GTK_ALIGN_CENTER);
    GtkWidget *grid_host = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid_host), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid_host), 10);
    
    h_name_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(h_name_entry), "Host");
    gtk_entry_set_max_length(GTK_ENTRY(h_name_entry), 31);
    
    h_pass_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(h_pass_entry), "AnteaterTest");
    gtk_entry_set_max_length(GTK_ENTRY(h_pass_entry), 31);
    
    h_spin = gtk_spin_button_new_with_range(2, 8, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(h_spin), 8);
    
    gtk_grid_attach(GTK_GRID(grid_host), gtk_label_new("Username:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid_host), h_name_entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid_host), gtk_label_new("Lobby Password:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid_host), h_pass_entry, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid_host), gtk_label_new("Max Players (2-8):"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid_host), h_spin, 1, 2, 1, 1);
    
    GtkWidget *hbox_host_btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *btn_host_back = gtk_button_new_with_label("Back");
    GtkWidget *btn_host_start = gtk_button_new_with_label("Create Lobby");
    gtk_box_pack_start(GTK_BOX(hbox_host_btns), btn_host_back, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox_host_btns), btn_host_start, TRUE, TRUE, 0);
    
    gtk_box_pack_start(GTK_BOX(box_host), grid_host, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box_host), hbox_host_btns, FALSE, FALSE, 10);
    gtk_stack_add_named(GTK_STACK(pLobbyStack), box_host, "page_host");

    /* --- PAGE: Join Settings --- */
    GtkWidget *box_join = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_valign(box_join, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(box_join, GTK_ALIGN_CENTER);
    GtkWidget *grid_join = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid_join), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid_join), 10);
    
    j_code_entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(j_code_entry), 8);
    
    j_name_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(j_name_entry), "Player");
    gtk_entry_set_max_length(GTK_ENTRY(j_name_entry), 31);
    
    j_pass_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(j_pass_entry), FALSE);
    gtk_entry_set_text(GTK_ENTRY(j_pass_entry), "AnteaterTest");
    gtk_entry_set_max_length(GTK_ENTRY(j_pass_entry), 31);

    gtk_grid_attach(GTK_GRID(grid_join), gtk_label_new("Room Code:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid_join), j_code_entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid_join), gtk_label_new("Username:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid_join), j_name_entry, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid_join), gtk_label_new("Password:"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid_join), j_pass_entry, 1, 2, 1, 1);

    GtkWidget *hbox_join_btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *btn_join_back = gtk_button_new_with_label("Back");
    GtkWidget *btn_join_conn = gtk_button_new_with_label("Connect");
    gtk_box_pack_start(GTK_BOX(hbox_join_btns), btn_join_back, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox_join_btns), btn_join_conn, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(box_join), grid_join, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box_join), hbox_join_btns, FALSE, FALSE, 10);
    gtk_stack_add_named(GTK_STACK(pLobbyStack), box_join, "page_join");

    /* Map transition signals utilizing object data strings */
    g_object_set_data(G_OBJECT(btn_go_host), "target_page", "page_host");
    g_signal_connect(btn_go_host, "clicked", G_CALLBACK(OnLobbyStackNavClicked), pLobbyStack);
    
    g_object_set_data(G_OBJECT(btn_go_join), "target_page", "page_join");
    g_signal_connect(btn_go_join, "clicked", G_CALLBACK(OnLobbyStackNavClicked), pLobbyStack);

    g_object_set_data(G_OBJECT(btn_host_back), "target_page", "page_sel");
    g_signal_connect(btn_host_back, "clicked", G_CALLBACK(OnLobbyStackNavClicked), pLobbyStack);

    g_object_set_data(G_OBJECT(btn_join_back), "target_page", "page_sel");
    g_signal_connect(btn_join_back, "clicked", G_CALLBACK(OnLobbyStackNavClicked), pLobbyStack);

    /* Bind Execute callbacks to trigger main.c networking logic */
    g_signal_connect(btn_host_start, "clicked", G_CALLBACK(OnHostStartClicked), NULL);
    g_signal_connect(btn_join_conn,  "clicked", G_CALLBACK(OnJoinConnectClicked), NULL);

    return pLobbyStack;
}

static GtkWidget* CreateTablePage(void)
{
    GtkWidget *tableVBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    
    pStatusLabel = gtk_label_new(NULL);
    UpdateTelemetryHUD(0, 0, "Initializing Framework...");
    gtk_widget_set_margin_top(pStatusLabel, 10);
    gtk_box_pack_start(GTK_BOX(tableVBox), pStatusLabel, FALSE, FALSE, 0);

    pTableArea = gtk_drawing_area_new();
    gtk_widget_set_size_request(pTableArea, TABLE_AREA_WIDTH, TABLE_AREA_HEIGHT);
    g_signal_connect(pTableArea, "draw", G_CALLBACK(OnDrawTable), NULL);
    gtk_box_pack_start(GTK_BOX(tableVBox), pTableArea, TRUE, TRUE, 0);

    /* Host Control Panel (Above the Action Buttons) */
    pButtonStartGame = gtk_button_new_with_label("START GAME");
    gtk_widget_set_margin_start(pButtonStartGame, MARGIN_BUTTON_AREA);
    gtk_widget_set_margin_end(pButtonStartGame, MARGIN_BUTTON_AREA);
    gtk_box_pack_start(GTK_BOX(tableVBox), pButtonStartGame, FALSE, FALSE, 5);
    g_signal_connect(pButtonStartGame, "clicked", G_CALLBACK(OnStartGameClicked), NULL);
    
    /* Hide by default until network sync confirms Seat 0 */
    gtk_widget_set_no_show_all(pButtonStartGame, TRUE);
    gtk_widget_hide(pButtonStartGame);

    GtkWidget *pHBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(pHBox, MARGIN_BUTTON_AREA);
    gtk_widget_set_margin_end(pHBox, MARGIN_BUTTON_AREA);
    gtk_widget_set_margin_bottom(pHBox, MARGIN_BUTTON_BOTTOM);
    gtk_box_pack_start(GTK_BOX(tableVBox), pHBox, FALSE, FALSE, 0);

    CreateAndPackActionButtons(pHBox);

    return tableVBox;
}

//=============================================================================

void InitializeGUI(int isOfflineMode)
{
    memset(&g_ClientTable, 0, sizeof(Table));
    g_pTable = &g_ClientTable;

    pMainWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(pMainWindow), "Anteater Poker");
    gtk_window_set_default_size(GTK_WINDOW(pMainWindow), WINDOW_DEFAULT_WIDTH, WINDOW_DEFAULT_HEIGHT);
    gtk_window_set_position(GTK_WINDOW(pMainWindow), GTK_WIN_POS_CENTER);
    gtk_window_set_resizable(GTK_WINDOW(pMainWindow), FALSE);

    g_signal_connect(pMainWindow, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(pMainWindow, "key-press-event", G_CALLBACK(OnKeyPress), NULL);

    pMainStack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(pMainStack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_container_add(GTK_CONTAINER(pMainWindow), pMainStack);

    GtkWidget *lobbyPage = CreateLobbyPage();
    GtkWidget *tablePage = CreateTablePage();

    gtk_stack_add_named(GTK_STACK(pMainStack), lobbyPage, "page_lobby");
    gtk_stack_add_named(GTK_STACK(pMainStack), tablePage, "page_table");

    if (isOfflineMode) {
        g_LocalSeat = 0;
        gtk_stack_set_visible_child_name(GTK_STACK(pMainStack), "page_table");
    }
    else {
        gtk_stack_set_visible_child_name(GTK_STACK(pMainStack), "page_lobby");
    }
}

//=============================================================================

void ShowMainWindow(void)
{
    if (pMainWindow == NULL) return;
    gtk_widget_show_all(pMainWindow);
}

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

void TriggerTableRedraw(void)
{
    if (pTableArea != NULL) {
        gtk_widget_queue_draw(pTableArea);
    }
}

void SyncGUIWithGameState(void)
{
    if (g_pTable == NULL) return;

    char hudMsg[128];
    if (g_pTable->state == 0) {
        strcpy(hudMsg, "Lobby Ready - Awaiting Host Start...");
    } else {
        snprintf(hudMsg, sizeof(hudMsg), "Game Active | Round Phase: %d", g_pTable->state);
    }

    UpdateTelemetryHUD(g_pTable->pot, g_pTable->players[g_LocalSeat].points, hudMsg);

    /* Host Button Visibility Toggle */
    if (g_pTable->state == 0 && g_LocalSeat == 0) {
        gtk_widget_show(pButtonStartGame);
    } else {
        gtk_widget_hide(pButtonStartGame);
    }

    if (g_pTable->state != 0 && g_pTable->activeIdx == g_LocalSeat && !g_pTable->players[g_LocalSeat].isFolded) {
        SetActionButtonsSensitive(TRUE);
    } else {
        SetActionButtonsSensitive(FALSE);
    }
}

void ClientReceiveHoleCards(int r1, char s1, int r2, char s2)
{
    if (g_pTable && g_LocalSeat >= 0 && g_LocalSeat < MAX_PLAYERS) {
        g_pTable->players[g_LocalSeat].hand[0].rank = r1;
        g_pTable->players[g_LocalSeat].hand[0].suit = s1;
        g_pTable->players[g_LocalSeat].hand[1].rank = r2;
        g_pTable->players[g_LocalSeat].hand[1].suit = s2;
        TriggerTableRedraw();
    }
}

void ClientReceiveCommunityCard(int index, int rank, char suit)
{
    if (g_pTable && index >= 0 && index < 5) {
        g_pTable->community[index].rank = rank;
        g_pTable->community[index].suit = suit;
        TriggerTableRedraw();
    }
}

void ClientSyncSeat(int seat, const char* name, int points, int isFolded)
{
    if (g_pTable && seat >= 0 && seat < MAX_PLAYERS) {
        strncpy(g_pTable->players[seat].name, name, MAX_NAME_LEN - 1);
        g_pTable->players[seat].name[MAX_NAME_LEN - 1] = '\0';
        g_pTable->players[seat].points = points;
        g_pTable->players[seat].isFolded = isFolded;
        TriggerTableRedraw();
        
        /* Force the Telemetry HUD to refresh once local points are loaded */
        if (seat == g_LocalSeat) {
            SyncGUIWithGameState();
        }
    }
}
