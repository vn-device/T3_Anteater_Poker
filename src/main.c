/******************************************************************************
 * File: main.c
 * Author: Team T3
 * Date: May 31, 2026
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
#include <stdint.h>
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
    /* Static accumulation buffer to handle TCP stream batching & fragmentation */
    static char stream_buffer[8192] = {0};
    static int stream_len = 0;
    
    char read_buf[1024];
    int fd = g_io_channel_unix_get_fd(source);
    ssize_t bytes_read = read(fd, read_buf, sizeof(read_buf) - 1);

    if (bytes_read < 0) {
        perror("Socket read error");
        return FALSE; 
    }

    if (bytes_read == 0) {
        g_print("Server disconnected. Closing client.\n");
        gtk_main_quit();
        return FALSE; 
    }

    read_buf[bytes_read] = '\0';
    
    /* Append newly arrived bytes to the master stream buffer */
    if (stream_len + bytes_read < (int)sizeof(stream_buffer)) {
        strcat(stream_buffer, read_buf);
        stream_len += bytes_read;
    } else {
        g_printerr("Stream buffer overflow. Flushing.\n");
        stream_buffer[0] = '\0';
        stream_len = 0;
        return TRUE;
    }

    /* Tokenize and process EVERY complete message in the buffer */
    char *newline_pos;
    while ((newline_pos = strchr(stream_buffer, '\n')) != NULL) {
        *newline_pos = '\0'; /* Isolate the single message */
        
        ParsedMessage msg;
        if (ParseNetworkMessage(stream_buffer, &msg) == 0) {
            
            switch (msg.type) {
                case MSG_TYPE_OK:
                    UpdateTelemetryHUD(0, msg.amount, "Connected - Awaiting Game Start");
                    break;
                case MSG_TYPE_ERROR:
                    UpdateTelemetryHUD(0, 0, msg.payload);
                    break;
                case MSG_TYPE_HOLECARDS:
                    ClientReceiveHoleCards(msg.seat, msg.payload[0], msg.amount, msg.payload[1]);
                    break;
                case MSG_TYPE_COMMUNITY:
                    ClientReceiveCommunityCard(msg.seat, msg.amount, msg.payload[0]);
                    break;
                case MSG_TYPE_SYNC:
                    ClientSyncSeat(msg.seat, msg.name, msg.amount, msg.payload[0] == '1' ? 1 : 0);
                    break;
                case MSG_TYPE_UPDATE:
                    if (g_pTable != NULL) {
                        g_pTable->activeIdx = msg.seat;
                        g_pTable->pot = msg.amount;
                        g_pTable->state = atoi(msg.payload);
                    }
                    ResetRoundTimer();
                    TriggerTableRedraw();
                    SyncGUIWithGameState();
                    break;
                default:
                    break;
            }
        }
        
        /* Shift the unparsed remainder of the stream left to the front of the buffer */
        int remaining = stream_len - (newline_pos - stream_buffer) - 1;
        memmove(stream_buffer, newline_pos + 1, remaining);
        stream_len = remaining;
        stream_buffer[stream_len] = '\0';
    }

    return TRUE; 
}

//=============================================================================

void StartNetworkListener(int playerSeat)
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

    usleep(500000); 

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

    BuildEnterMessage(outBuffer, name, 0, password);
    send(g_client_socket, outBuffer, strlen(outBuffer), 0);

    char respBuffer[MAX_MSG_LEN];
    ssize_t bytes_read = read(g_client_socket, respBuffer, MAX_MSG_LEN - 1);
    
    if (bytes_read > 0) {
        respBuffer[bytes_read] = '\0';
        ParsedMessage msg;
        if (ParseNetworkMessage(respBuffer, &msg) == 0 && msg.type == MSG_TYPE_OK) {
            *outSeat = 0;
            
            struct timeval tv = {1, 0}; 
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(g_client_socket, &readfds);
            
            if (select(g_client_socket + 1, &readfds, NULL, NULL, &tv) > 0) {
                bytes_read = read(g_client_socket, respBuffer, MAX_MSG_LEN - 1);
                if (bytes_read > 0) {
                    respBuffer[bytes_read] = '\0';
                    if (ParseNetworkMessage(respBuffer, &msg) == 0 && msg.type == MSG_TYPE_HOST) {
                        BuildSetupMessage(outBuffer, maxPlayers);
                        send(g_client_socket, outBuffer, strlen(outBuffer), 0);
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
    
    unsigned int val;
    if (sscanf(roomCode, "%08X", &val) != 1) {
        return 0;
    }
    
    struct in_addr addr;
    addr.s_addr = htonl(val);
    if (inet_ntop(AF_INET, &addr, serverIP, 16) == NULL) {
        return 0;
    }

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

    if (isTestComm) {
        struct sockaddr_in serv_addr;
        char outBuffer[MAX_MSG_LEN];

        printf("[TestClient] Initiating headless protocol verification...\n");
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return EXIT_FAILURE;

        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(SERVER_PORT);
        inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

        if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            close(sock);
            return EXIT_FAILURE;
        }

        BuildEnterMessage(outBuffer, "IntegrationBot", 0, "AnteaterTest");
        send(sock, outBuffer, strlen(outBuffer), 0);

        char respBuffer[MAX_MSG_LEN];
        ssize_t bytes = read(sock, respBuffer, MAX_MSG_LEN - 1);
        if (bytes > 0) {
            respBuffer[bytes] = '\0';
        }

        close(sock);
        return EXIT_SUCCESS;
    }

    gtk_init(&argc, &argv);
    
    InitializeGUI(isOfflineMode);
    ShowMainWindow();
    
    gtk_main();

    if (g_client_socket != -1) {
        close(g_client_socket);
    }
    return 0;
}