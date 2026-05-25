/******************************************************************************
 * File: main.c
 * Author: Team T3
 * Date: May 23, 2026
 * 
 * * Description:
 * Entry point for the Anteater Poker client application. Captures 
 * local user credentials via a modal dialog prior to establishing 
 * the TCP socket connection and spawning the asynchronous GIO loop.
 * Supports an --offline command-line flag for headless GUI testing.
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <gtk/gtk.h>
#include "GameGUI.h"
#include "GameProtocol.h"

#define SERVER_PORT 8003

int g_client_socket = -1;

//=============================================================================

static gboolean OnServerMessageReceived(GIOChannel *source, GIOCondition condition, gpointer data)
{
    char buffer[MAX_MSG_LEN];
    int fd = g_io_channel_unix_get_fd(source);
    ssize_t bytes_read = read(fd, buffer, MAX_MSG_LEN - 1);

    if (bytes_read < 0) {
        perror("Socket read error");
        return FALSE; 
    }

    if (bytes_read == 0) {
        g_print("Server disconnected. Closing client.\n");
        gtk_main_quit();
        return FALSE; 
    }

    buffer[bytes_read] = '\0';
    
    ParsedMessage msg;
    if (ParseNetworkMessage(buffer, &msg) == 0) {
        g_print("Server Broadcast -> Type: %d, Seat: %d, Payload: %s\n", 
                msg.type, msg.seat, msg.payload);
        
        switch (msg.type) {
            case MSG_TYPE_OK:
                UpdateTelemetryHUD(0, msg.amount, "Connected - Awaiting Players");
                break;
            case MSG_TYPE_ERROR:
                UpdateTelemetryHUD(0, 0, msg.payload);
                break;
            default:
                break;
        }
    }
    else {
        g_printerr("Failed to parse incoming packet: %s\n", buffer);
    }

    return TRUE; 
}

//=============================================================================

int main(int argc, char *argv[])
{
    struct sockaddr_in serv_addr;
    char outBuffer[MAX_MSG_LEN];
    char serverIP[16];
    char playerName[MAX_NAME_LEN];
    char playerPassword[MAX_NAME_LEN];
    int localSeat = -1;
    int isOfflineMode = 0;
    int loginSuccessful = 0;

    /* Scan command-line arguments for the GUI bypass flag */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--offline") == 0) {
            isOfflineMode = 1;
            break;
        }
    }

    /* 1. Initialize GTK first to enable rendering of the credential dialog */
    gtk_init(&argc, &argv);

    /* 2. Intercept execution for offline GUI testing */
    if (isOfflineMode) {
        g_print("Launching GUI in offline test mode...\n");
        InitializeGUI(0);
        UpdateTelemetryHUD(0, 1000, "Offline Test Mode");
        ShowMainWindow();
        gtk_main();
        return 0;
    }

    /* 3. Trap execution in a validation loop until the server validates the seat allocation */
    while (!loginSuccessful) {
        if (!PromptLoginDetails(playerName, &localSeat, playerPassword, serverIP)) {
            g_print("Login sequence aborted by user. Exiting.\n");
            return 0;
        }

        /* Initialize TCP Stream Socket */
        if ((g_client_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            perror("Client socket creation error");
            exit(EXIT_FAILURE);
        }

        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(SERVER_PORT);

        if (inet_pton(AF_INET, serverIP, &serv_addr.sin_addr) <= 0) {
            GtkWidget *errorDialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "Invalid IP Address format: '%s'", serverIP);
            gtk_window_set_title(GTK_WINDOW(errorDialog), "Network Error");
            gtk_dialog_run(GTK_DIALOG(errorDialog));
            gtk_widget_destroy(errorDialog);
            close(g_client_socket);
            continue;
        }

        g_print("Connecting to %s:%d as %s (Seat %d)...\n", serverIP, SERVER_PORT, playerName, localSeat);
        if (connect(g_client_socket, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            GtkWidget *errorDialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "Connection to server failed.\nIs the server active at %s?", serverIP);
            gtk_window_set_title(GTK_WINDOW(errorDialog), "Connection Error");
            gtk_dialog_run(GTK_DIALOG(errorDialog));
            gtk_widget_destroy(errorDialog);
            close(g_client_socket);
            continue;
        }

        /* Transmit Synchronous Alpha Handshake */
        BuildEnterMessage(outBuffer, playerName, localSeat, playerPassword);
        send(g_client_socket, outBuffer, strlen(outBuffer), 0);

        /* Suspend execution via blocking read to await authoritative server validation */
        char respBuffer[MAX_MSG_LEN];
        ssize_t bytes_read = read(g_client_socket, respBuffer, MAX_MSG_LEN - 1);
        
        if (bytes_read > 0) {
            respBuffer[bytes_read] = '\0';
            ParsedMessage respMsg;
            
            if (ParseNetworkMessage(respBuffer, &respMsg) == 0) {
                if (respMsg.type == MSG_TYPE_OK) {
                    /* Server successfully allocated the block; release the loop lock */
                    loginSuccessful = 1;
                } 
                else if (respMsg.type == MSG_TYPE_ERROR) {
                    /* Server rejected the handshake due to collision; terminate socket and warn user */
                    GtkWidget *warningDialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, "%s", respMsg.payload);
                    gtk_window_set_title(GTK_WINDOW(warningDialog), "Seat Unavailable");
                    gtk_dialog_run(GTK_DIALOG(warningDialog));
                    gtk_widget_destroy(warningDialog);
                    close(g_client_socket);
                }
            }
        } else {
            g_printerr("Server dropped connection during handshake validation.\n");
            close(g_client_socket);
        }
    }

    /* 4. Launch the Main Presentation Layer */
    InitializeGUI(localSeat);
    ShowMainWindow();

    /* 5. Bind the Asynchronous Network Hook for live gameplay updates */
    GIOChannel *io_channel = g_io_channel_unix_new(g_client_socket);
    g_io_channel_set_encoding(io_channel, NULL, NULL);
    g_io_channel_set_buffered(io_channel, FALSE);
    g_io_add_watch(io_channel, G_IO_IN, OnServerMessageReceived, NULL);
    g_io_channel_unref(io_channel);

    /* 6. Yield Execution to GTK */
    gtk_main();

    close(g_client_socket);
    return 0;
}