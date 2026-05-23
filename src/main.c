/******************************************************************************
 * File: main.c
 * Author: Team T3
 * Date: May 22, 2026
 * 
 * * Description:
 * Entry point for the Anteater Poker client application. Establishes the 
 * TCP socket connection to the authoritative server, bootstraps the GTK 
 * environment, and binds the socket to the GTK event loop via GIOChannel 
 * for asynchronous, non-blocking network reads.
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <gtk/gtk.h>
#include "GameGUI.h"
#include "GameProtocol.h"

#define SERVER_IP "127.0.0.1" /* Localhost for testing; change to server IP for production */
#define SERVER_PORT 8003

/* Global socket descriptor accessible by GUI callbacks */
int g_client_socket = -1;

//=============================================================================

/**
 * Asynchronous callback triggered by the GTK event loop whenever data 
 * arrives on the connected TCP socket. Uses raw POSIX read to prevent 
 * GTK thread deadlocks.
 */
static gboolean OnServerMessageReceived(GIOChannel *source, GIOCondition condition, gpointer data)
{
    char buffer[MAX_MSG_LEN];
    
    /* Extract the raw POSIX file descriptor from the GLib channel */
    int fd = g_io_channel_unix_get_fd(source);

    /* Use standard POSIX read. It instantly returns whatever bytes are currently 
       in the OS buffer without waiting to fill MAX_MSG_LEN */
    ssize_t bytes_read = read(fd, buffer, MAX_MSG_LEN - 1);

    if (bytes_read < 0) {
        perror("Socket read error");
        return FALSE; /* Remove watch */
    }

    if (bytes_read == 0) {
        g_print("Server disconnected. Closing client.\n");
        gtk_main_quit();
        return FALSE; /* Remove watch */
    }

    /* Safely null-terminate the received data */
    buffer[bytes_read] = '\0';
    
    ParsedMessage msg;
    if (ParseNetworkMessage(buffer, &msg) == 0) {
        g_print("Server Broadcast -> Type: %d, Seat: %d, Payload: %s\n", 
                msg.type, msg.seat, msg.payload);
        
        /* We will link this to the HUD update logic later */
    } else {
        g_printerr("Failed to parse incoming packet: %s\n", buffer);
    }

    return TRUE; /* Keep the watch active in the GTK loop */
}

//=============================================================================

int main(int argc, char *argv[])
{
    struct sockaddr_in serv_addr;
    char outBuffer[MAX_MSG_LEN];

    /* 1. Initialize TCP Stream Socket */
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

    /* 2. Connect to the Authoritative Server */
    g_print("Connecting to server at %s:%d...\n", SERVER_IP, SERVER_PORT);
    if (connect(g_client_socket, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection to server failed. Is the server running?");
        close(g_client_socket);
        exit(EXIT_FAILURE);
    }
    g_print("Connected successfully.\n");

    /* 3. Transmit Alpha Handshake */
    BuildEnterMessage(outBuffer, "Player1", 0, "AlphaTestPass");
    send(g_client_socket, outBuffer, strlen(outBuffer), 0);

    /* 4. Initialize GTK GUI */
    InitializeGUI(argc, argv);
    ShowMainWindow();

    /* 5. Bind Socket to GTK Event Loop */
    GIOChannel *io_channel = g_io_channel_unix_new(g_client_socket);
    
    /* CRITICAL: Disable GLib's UTF-8 encoding checks and internal buffering 
       so it acts as a raw binary pipe for our POSIX read() calls. */
    g_io_channel_set_encoding(io_channel, NULL, NULL);
    g_io_channel_set_buffered(io_channel, FALSE);
    
    /* Instruct GLib to execute OnServerMessageReceived when G_IO_IN (read data) is pending */
    g_io_add_watch(io_channel, G_IO_IN, OnServerMessageReceived, NULL);
    
    /* Unref the channel; the event loop now owns the reference */
    g_io_channel_unref(io_channel);

    /* 6. Launch Blocking Event Loop */
    gtk_main();

    /* 7. Teardown */
    close(g_client_socket);
    return 0;
}