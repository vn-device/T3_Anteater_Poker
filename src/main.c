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

#define SERVER_IP "127.0.0.1" 
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
    char playerName[MAX_NAME_LEN];
    char playerPassword[MAX_NAME_LEN];
    int localSeat = -1;
    int isOfflineMode = 0;

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
        
        /* Initialize the window with a mock seat assignment */
        InitializeGUI(0);
        UpdateTelemetryHUD(0, 1000, "Offline Test Mode");
        ShowMainWindow();
        
        /* Yield execution immediately to GTK without binding network listeners */
        gtk_main();
        return 0;
    }

    /* 3. Block execution to collect network credentials */
    if (!PromptLoginDetails(playerName, &localSeat, playerPassword)) {
        g_print("Login sequence aborted by user. Exiting.\n");
        return 0;
    }

    /* 4. Initialize TCP Stream Socket */
    if ((g_client_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Client socket creation error");
        exit(EXIT_FAILURE);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        perror("Invalid server IP address");
        close(g_client_socket);
        exit(EXIT_FAILURE);
    }

    /* 5. Connect to Authoritative Server */
    g_print("Connecting to %s:%d as %s (Seat %d)...\n", SERVER_IP, SERVER_PORT, playerName, localSeat);
    if (connect(g_client_socket, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection to server failed. Is the server running?");
        close(g_client_socket);
        exit(EXIT_FAILURE);
    }

    /* 6. Transmit Synchronous Alpha Handshake */
    BuildEnterMessage(outBuffer, playerName, localSeat, playerPassword);
    send(g_client_socket, outBuffer, strlen(outBuffer), 0);

    /* 7. Launch the Main Presentation Layer */
    InitializeGUI(localSeat);
    ShowMainWindow();

    /* 8. Bind the Asynchronous Network Hook */
    GIOChannel *io_channel = g_io_channel_unix_new(g_client_socket);
    g_io_channel_set_encoding(io_channel, NULL, NULL);
    g_io_channel_set_buffered(io_channel, FALSE);
    g_io_add_watch(io_channel, G_IO_IN, OnServerMessageReceived, NULL);
    g_io_channel_unref(io_channel);

    /* 9. Yield Execution to GTK */
    gtk_main();

    close(g_client_socket);
    return 0;
}