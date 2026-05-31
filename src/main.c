/******************************************************************************
 * File: main.c
 * Author: Team T3
 * Date: May 30, 2026
 * 
 * * Description:
 * Entry point for the Anteater Poker client application. Isolates the 
 * networking hooks accessed by the GTK event loop to provide synchronous 
 * peer-to-peer room connections and headless testing integration.
 *****************************************************************************/

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <gtk/gtk.h>
#include "GameGUI.h"
#include "GameProtocol.h"

#define SERVER_PORT 8003

int g_client_socket = -1;
pid_t g_server_pid = -1;

/* Teardown hook to prevent background zombies if the user hosted */
void CleanupLocalServer(void)
{
    if (g_server_pid > 0) {
        kill(g_server_pid, SIGTERM);
    }
}

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
        g_print("Server Broadcast -> Type: %d, Seat: %d, Payload: %s\n", msg.type, msg.seat, msg.payload);
        
        switch (msg.type) {
            case MSG_TYPE_OK:
                UpdateTelemetryHUD(0, msg.amount, "Connected - Awaiting Game Start");
                break;
            case MSG_TYPE_ERROR:
                UpdateTelemetryHUD(0, 0, msg.payload);
                break;
            case MSG_TYPE_UPDATE:
                ResetRoundTimer();
                TriggerTableRedraw();
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
/* PUBLIC NETWORKING HOOKS CALLED BY GAMEGUI.C CALLBACKS */
//=============================================================================

void StartNetworkListener(void)
{
    if (g_client_socket == -1) return;
    
    GIOChannel *io_channel = g_io_channel_unix_new(g_client_socket);
    g_io_channel_set_encoding(io_channel, NULL, NULL);
    g_io_channel_set_buffered(io_channel, FALSE);
    g_io_add_watch(io_channel, G_IO_IN, OnServerMessageReceived, NULL);
    g_io_channel_unref(io_channel);
}

int PerformHostConnection(const char *name, const char *password, int maxPlayers, char *outRoomCode, int *outSeat)
{
    struct sockaddr_in serv_addr;
    char outBuffer[MAX_MSG_LEN];
    char hostIP[16] = "127.0.0.1";
    char hostname[256];
    
    /* 1. Environment Extraction & Cipher Generation */
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        struct hostent *host = gethostbyname(hostname);
        if (host != NULL && host->h_addr_list[0] != NULL) {
            struct in_addr **addr_list = (struct in_addr **)host->h_addr_list;
            strcpy(hostIP, inet_ntoa(*addr_list[0]));
        }
    }
    
    struct in_addr addr;
    if (inet_pton(AF_INET, hostIP, &addr) == 1) {
        snprintf(outRoomCode, 9, "%08X", ntohl(addr.s_addr));
    }
    else {
        strcpy(outRoomCode, "ERROR");
    }

    /* 2. Background Daemon Spawn */
    g_server_pid = fork();
    if (g_server_pid < 0) {
        perror("Failed to launch local server");
        return 0;
    }
    else if (g_server_pid == 0) {
        execl("./bin/poker_server", "poker_server", NULL);
        perror("Server execution failed");
        exit(EXIT_FAILURE);
    }

    /* 500ms startup threshold to ensure accept() loop binds */
    usleep(500000); 

    /* 3. Loopback TCP Establishment */
    if ((g_client_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        return 0;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    if (connect(g_client_socket, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        close(g_client_socket);
        return 0;
    }

    /* 4. Host Setup Handshake Sequence */
    BuildEnterMessage(outBuffer, name, 0, password);
    send(g_client_socket, outBuffer, strlen(outBuffer), 0);

    char respBuffer[MAX_MSG_LEN];
    ssize_t bytes_read = read(g_client_socket, respBuffer, MAX_MSG_LEN - 1);
    
    if (bytes_read > 0) {
        respBuffer[bytes_read] = '\0';
        ParsedMessage msg;
        if (ParseNetworkMessage(respBuffer, &msg) == 0 && msg.type == MSG_TYPE_OK) {
            *outSeat = 0;
            
            /* Await HOST designation packet from server */
            struct timeval tv = {1, 0}; 
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(g_client_socket, &readfds);
            
            if (select(g_client_socket + 1, &readfds, NULL, NULL, &tv) > 0) {
                bytes_read = read(g_client_socket, respBuffer, MAX_MSG_LEN - 1);
                if (bytes_read > 0) {
                    respBuffer[bytes_read] = '\0';
                    if (ParseNetworkMessage(respBuffer, &msg) == 0 && msg.type == MSG_TYPE_HOST) {
                        
                        /* Authorize server to finalize configuration and spawn bots */
                        BuildSetupMessage(outBuffer, maxPlayers);
                        send(g_client_socket, outBuffer, strlen(outBuffer), 0);
                        
                        printf("[Host] Lobby successfully established. Room Code: %s\n", outRoomCode);
                        
                        return 1;
                    }
                }
            }
        }
    }
    
    close(g_client_socket);
    return 0;
}

int PerformJoinConnection(const char *name, const char *password, const char *roomCode, int *outSeat)
{
    struct sockaddr_in serv_addr;
    char outBuffer[MAX_MSG_LEN];
    char serverIP[16];
    
    /* 1. Decoder Reversion */
    unsigned int val;
    if (sscanf(roomCode, "%08X", &val) != 1) {
        return 0;
    }
    
    struct in_addr addr;
    addr.s_addr = htonl(val);
    if (inet_ntop(AF_INET, &addr, serverIP, 16) == NULL) {
        return 0;
    }

    /* 2. TCP Establishment */
    if ((g_client_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        return 0;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, serverIP, &serv_addr.sin_addr);

    if (connect(g_client_socket, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        close(g_client_socket);
        return 0;
    }

    /* 3. Auto-Seating Loop (The Among Us Mechanic) */
    for (int s = 0; s < MAX_PLAYERS; s++) {
        BuildEnterMessage(outBuffer, name, s, password);
        send(g_client_socket, outBuffer, strlen(outBuffer), 0);

        char respBuffer[MAX_MSG_LEN];
        ssize_t bytes_read = read(g_client_socket, respBuffer, MAX_MSG_LEN - 1);
        
        if (bytes_read > 0) {
            respBuffer[bytes_read] = '\0';
            ParsedMessage respMsg;
            if (ParseNetworkMessage(respBuffer, &respMsg) == 0 && respMsg.type == MSG_TYPE_OK) {
                *outSeat = s;
                return 1;
            }
        }
    }

    /* If execution escapes the loop, all seats rejected the connection */
    close(g_client_socket);
    return 0; 
}

//=============================================================================

int main(int argc, char *argv[])
{
    int isOfflineMode = 0;
    int isTestComm = 0;

    atexit(CleanupLocalServer);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--offline") == 0) {
            isOfflineMode = 1;
        }
        else if (strcmp(argv[i], "--test-comm") == 0) {
            isTestComm = 1;
        }
    }

    /* Headless Communication Testing Bypass */
    if (isTestComm) {
        struct sockaddr_in serv_addr;
        char outBuffer[MAX_MSG_LEN];

        printf("[TestClient] Initiating headless protocol verification...\n");
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return EXIT_FAILURE;

        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(SERVER_PORT);
        inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

        printf("[TestClient] Connecting to 127.0.0.1:%d...\n", SERVER_PORT);
        if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            close(sock);
            return EXIT_FAILURE;
        }

        BuildEnterMessage(outBuffer, "IntegrationBot", 0, "AnteaterTest");
        printf("[TestClient] TX -> %s", outBuffer);
        send(sock, outBuffer, strlen(outBuffer), 0);

        char respBuffer[MAX_MSG_LEN];
        ssize_t bytes = read(sock, respBuffer, MAX_MSG_LEN - 1);
        if (bytes > 0) {
            respBuffer[bytes] = '\0';
            printf("[TestClient] RX <- %s", respBuffer);
        }

        printf("[TestClient] Handshake verified. Terminating.\n");
        close(sock);
        return EXIT_SUCCESS;
    }

    /* Master GUI Lifecycle */
    gtk_init(&argc, &argv);
    
    InitializeGUI(isOfflineMode);
    ShowMainWindow();
    
    gtk_main();

    if (g_client_socket != -1) {
        close(g_client_socket);
    }
    return 0;
}