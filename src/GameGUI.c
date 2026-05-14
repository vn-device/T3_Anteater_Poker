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

#include "GameGUI.h"
#include "GameData.h"

static GtkWidget *pMainWindow;

//=============================================================================

/**
 * Event handler triggered when a user clicks a gameplay action button.
 * Casts the gpointer data back to a PlayerAction integer.
 */
static void OnActionButtonClicked(GtkWidget *widget, gpointer data)
{
    int action = GPOINTER_TO_INT(data);
    g_print("Action triggered: %d\n", action);
    
    /* * Future integration point:
     * char buffer[256];
     * BuildActionMessage(buffer, clientSeat, action, currentBetAmount);
     * SendMessageToServer(buffer); 
     */
}

//=============================================================================

void InitializeGUI(int argc, char *argv[])
{
    gtk_init(&argc, &argv);

    pMainWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(pMainWindow), "Anteater Poker");
    gtk_window_set_default_size(GTK_WINDOW(pMainWindow), 800, 600);
    gtk_window_set_position(GTK_WINDOW(pMainWindow), GTK_WIN_POS_CENTER);

    /* Bind the window destruction event to exit the GTK main control loop */
    g_signal_connect(pMainWindow, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    /* Main vertical container */
    GtkWidget *pVBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(pMainWindow), pVBox);

    /* Table representation area (placeholder for 2D pixel interface) */
    GtkWidget *pTableArea = gtk_drawing_area_new();
    gtk_widget_set_size_request(pTableArea, 800, 450);
    
    /* Apply CSS to give the drawing area a distinct green poker table background */
    GtkCssProvider *pProvider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(pProvider, "drawingarea { background-color: #2E8B57; }", -1, NULL);
    GtkStyleContext *pContext = gtk_widget_get_style_context(pTableArea);
    gtk_style_context_add_provider(pContext, GTK_STYLE_PROVIDER(pProvider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(pProvider);

    gtk_box_pack_start(GTK_BOX(pVBox), pTableArea, TRUE, TRUE, 0);

    /* Horizontal container for player action buttons */
    GtkWidget *pHBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(pHBox, 10);
    gtk_widget_set_margin_end(pHBox, 10);
    gtk_widget_set_margin_bottom(pHBox, 10);
    gtk_box_pack_start(GTK_BOX(pVBox), pHBox, FALSE, FALSE, 0);

    /* Instantiate action buttons */
    GtkWidget *pBtnFold  = gtk_button_new_with_label("FOLD");
    GtkWidget *pBtnCheck = gtk_button_new_with_label("CHECK");
    GtkWidget *pBtnCall  = gtk_button_new_with_label("CALL");
    GtkWidget *pBtnRaise = gtk_button_new_with_label("RAISE");

    /* Map buttons to the action handler, passing the PlayerAction enum value */
    g_signal_connect(pBtnFold, "clicked", G_CALLBACK(OnActionButtonClicked), GINT_TO_POINTER(PLAYER_ACTION_FOLD));
    g_signal_connect(pBtnCheck, "clicked", G_CALLBACK(OnActionButtonClicked), GINT_TO_POINTER(PLAYER_ACTION_CHECK));
    g_signal_connect(pBtnCall, "clicked", G_CALLBACK(OnActionButtonClicked), GINT_TO_POINTER(PLAYER_ACTION_CALL));
    g_signal_connect(pBtnRaise, "clicked", G_CALLBACK(OnActionButtonClicked), GINT_TO_POINTER(PLAYER_ACTION_RAISE));

    /* Distribute buttons evenly across the horizontal box */
    gtk_box_pack_start(GTK_BOX(pHBox), pBtnFold, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(pHBox), pBtnCheck, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(pHBox), pBtnCall, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(pHBox), pBtnRaise, TRUE, TRUE, 0);
}

//=============================================================================

void ShowMainWindow(void)
{
    gtk_widget_show_all(pMainWindow);
}

//=============================================================================