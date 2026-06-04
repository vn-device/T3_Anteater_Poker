/******************************************************************************
 * File: PokerServer.c
 * Author: Team T3
 * Date: May 31, 2026
 * 
 * * Description:
 * Multi-client authoritative server utilizing select() for I/O multiplexing.
 * Spawns internal embedded loopback threads for autonomous poker bots.
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <stdint.h>
#include <pthread.h>
#include "GameData.h"
#include "GameProtocol.h"
#include "PokerBot.h"

#define PORT 8003
#define MAX_PENDING 10

/* Game State Variables */
typedef enum {
    GAME_WAITING_FOR_SETUP,
    GAME_WAITING_FOR_PLAYERS,
    GAME_SPAWNING_BOTS,
    GAME_ACTIVE_BETTING
} GamePhase;

Table g_MasterTable;
int g_ConnectedPlayers = 0;
int g_IsGameConfigured = 0;
int g_MaxPlayers = MAX_PLAYERS;
int g_HostSocket = -1;
char g_LobbyPassword[MAX_MSG_LEN] = {0};

/* Game Logic State */
static GamePhase g_GamePhase = GAME_WAITING_FOR_SETUP;
static int g_GameStartTime = 0;
static int g_CurrentTurnSeat = 0;
static int g_RoundPhase = GAME_STATE_WAITING;
static int g_CurrentBet = 0;
static int g_Pot = 0;
static int g_ActionsThisPhase = 0;
static int g_LastRaiseSize = BLIND_BIG;
static int g_PendingNextHand = 0;
static time_t g_ShowdownEndTime = 0;
static unsigned char g_MustAct[MAX_PLAYERS];
static Deck g_GameDeck;

static void BroadcastGameUpdate(int client_socket);
static int CountPlayersInHand(void);
static void EnterShowdown(void);
static void InitializeGameRound(void);
static void StartNextHand(void);
static int IsGameActive(void);

static int IsGameActive(void)
{
    return g_GamePhase == GAME_ACTIVE_BETTING &&
           g_RoundPhase > GAME_STATE_WAITING &&
           g_RoundPhase < GAME_STATE_SHOWDOWN;
}

static int IsSeatInHand(int seat)
{
    if (seat < 0 || seat >= g_MaxPlayers) {
        return 0;
    }

    Player *p = &g_MasterTable.players[seat];
    return p->socket != -1 && !p->outOfGame && p->points > 0;
}

static int IsSeatActiveForTurn(int seat)
{
    if (!IsSeatInHand(seat)) {
        return 0;
    }
    if (g_MasterTable.players[seat].isFolded) {
        return 0;
    }
    /* CRITICAL FIX: All-In players (points == 0) must be skipped in turn advancement.
       They are eligible to win at showdown but cannot take any actions. */
    if (g_MasterTable.players[seat].points == 0) {
        return 0;
    }
    return 1;
}

static int GetNextInHandSeat(int fromSeat)
{
    int attempts = 0;
    int seat = fromSeat;

    do {
        seat = (seat + 1) % g_MaxPlayers;
        attempts++;
    } while (!IsSeatInHand(seat) && attempts < g_MaxPlayers);

    return IsSeatInHand(seat) ? seat : -1;
}

static int GetCallAmount(int seat)
{
    if (seat < 0 || seat >= g_MaxPlayers) {
        return 0;
    }

    int owed = g_CurrentBet - g_MasterTable.players[seat].street_investment;
    if (owed < 0) {
        owed = 0;
    }
    if (owed > g_MasterTable.players[seat].points) {
        owed = g_MasterTable.players[seat].points;
    }
    return owed;
}

static int GetMinRaiseTotal(void)
{
    if (g_CurrentBet == 0) {
        return g_LastRaiseSize > 0 ? g_LastRaiseSize : BLIND_BIG;
    }
    return g_CurrentBet + g_LastRaiseSize;
}

static void ResetLobbyState(void)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        g_MasterTable.players[i].socket = -1;
        g_MasterTable.players[i].name[0] = '\0';
        g_MasterTable.players[i].points = 0;
        g_MasterTable.players[i].isFolded = 0;
        g_MasterTable.players[i].outOfGame = 0;
        g_MasterTable.players[i].isBot = 0;
        g_MasterTable.players[i].cardsVisible = 0;
        g_MasterTable.players[i].total_hand_investment = 0;
        g_MasterTable.players[i].street_investment = 0;
    }

    g_ConnectedPlayers = 0;
    g_IsGameConfigured = 0;
    g_MaxPlayers = MAX_PLAYERS;
    g_HostSocket = -1;
    memset(g_LobbyPassword, 0, MAX_MSG_LEN);
    g_GamePhase = GAME_WAITING_FOR_SETUP;
    g_RoundPhase = GAME_STATE_WAITING;
    g_CurrentTurnSeat = 0;
    g_CurrentBet = 0;
    g_Pot = 0;
    g_ActionsThisPhase = 0;
    g_LastRaiseSize = BLIND_BIG;
    g_PendingNextHand = 0;
    g_ShowdownEndTime = 0;
    memset(g_MustAct, 0, sizeof(g_MustAct));
    memset(&g_MasterTable.community, 0, sizeof(g_MasterTable.community));
    g_MasterTable.dealerIdx = 0;
    g_MasterTable.pot = 0;
    g_MasterTable.state = GAME_STATE_WAITING;
    g_MasterTable.activeIdx = -1;
    printf("Lobby reset complete. Awaiting new host.\n");
}

//=============================================================================
// GAME LOGIC FUNCTIONS
//=============================================================================

static void BroadcastShowdownHoleCards(void)
{
    char outBuffer[MAX_MSG_LEN];

    /* FORCE VISIBLE: Ensure all surviving (non-folded) players' cards are marked visible */
    for (int i = 0; i < g_MaxPlayers; i++) {
        if (!IsSeatInHand(i) || g_MasterTable.players[i].isFolded) {
            continue;
        }

        g_MasterTable.players[i].cardsVisible = 1;  /* Force visible=true for showdown */
        BuildShowdownCardsMessage(outBuffer, i,
            g_MasterTable.players[i].hand[0].rank, g_MasterTable.players[i].hand[0].suit,
            g_MasterTable.players[i].hand[1].rank, g_MasterTable.players[i].hand[1].suit);

        for (int j = 0; j < g_MaxPlayers; j++) {
            if (g_MasterTable.players[j].socket != -1) {
                send(g_MasterTable.players[j].socket, outBuffer, strlen(outBuffer), 0);
            }
        }
    }
}

static void CommitPlayerBet(int seat, int amount)
{
    if (seat < 0 || seat >= g_MaxPlayers || amount <= 0) {
        return;
    }

    Player *p = &g_MasterTable.players[seat];
    if (amount > p->points) {
        amount = p->points;
    }

    p->points -= amount;
    p->street_investment += amount;
    p->total_hand_investment += amount;
    g_Pot += amount;
}

static void ResetStreetInvestments(void)
{
    for (int i = 0; i < g_MaxPlayers; i++) {
        g_MasterTable.players[i].street_investment = 0;
    }
}

static void MarkAllActiveMustAct(void)
{
    memset(g_MustAct, 0, sizeof(g_MustAct));
    for (int i = 0; i < g_MaxPlayers; i++) {
        if (IsSeatActiveForTurn(i)) {
            g_MustAct[i] = 1;
        }
    }
}

static int IsBettingRoundComplete(void)
{
    for (int i = 0; i < g_MaxPlayers; i++) {
        if (!IsSeatActiveForTurn(i)) {
            continue;
        }
        if (g_MustAct[i]) {
            return 0;
        }
        /* CRITICAL FIX: Strict bet alignment check - all active players must either be all-in 
           (points == 0) OR have street_investment == g_CurrentBet. 
           The previous logic allowed skipping under-bet players if they had points > 0,
           which caused Seat 3 ($110) to be skipped when current bet was $920. */
        if (g_MasterTable.players[i].street_investment < g_CurrentBet) {
            /* Player is under-bet - round is NOT complete unless they are all-in (points == 0) */
            if (g_MasterTable.players[i].points > 0) {
                /* Player has chips but hasn't matched the bet - MUST ACT */
                return 0;
            }
            /* Player is all-in (points == 0) - they cannot act, so this is acceptable */
        }
    }
    return 1;
}

static void RotateDealer(void)
{
    int start = (g_MasterTable.dealerIdx + 1) % g_MaxPlayers;

    for (int i = 0; i < g_MaxPlayers; i++) {
        int seat = (start + i) % g_MaxPlayers;
        if (IsSeatInHand(seat)) {
            g_MasterTable.dealerIdx = seat;
            return;
        }
    }
}

static void PostBlinds(void)
{
    int sbSeat = GetNextInHandSeat(g_MasterTable.dealerIdx);
    int bbSeat = GetNextInHandSeat(sbSeat);

    if (sbSeat < 0 || bbSeat < 0) {
        return;
    }

    int sbAmount = BLIND_SMALL;
    if (sbAmount > g_MasterTable.players[sbSeat].points) {
        sbAmount = g_MasterTable.players[sbSeat].points;
    }
    CommitPlayerBet(sbSeat, sbAmount);

    int bbAmount = BLIND_BIG;
    if (bbAmount > g_MasterTable.players[bbSeat].points) {
        bbAmount = g_MasterTable.players[bbSeat].points;
    }
    CommitPlayerBet(bbSeat, bbAmount);

    g_CurrentBet = g_MasterTable.players[bbSeat].street_investment;
    g_LastRaiseSize = BLIND_BIG;
    MarkAllActiveMustAct();
}

static int CountPlayersInHand(void)
{
    int count = 0;
    for (int i = 0; i < g_MaxPlayers; i++) {
        if (IsSeatInHand(i)) {
            count++;
        }
    }
    return count;
}

static void SetPreflopFirstActor(void)
{
    if (CountPlayersInHand() == 2) {
        g_CurrentTurnSeat = g_MasterTable.dealerIdx;
        return;
    }

    int sbSeat = GetNextInHandSeat(g_MasterTable.dealerIdx);
    int bbSeat = GetNextInHandSeat(sbSeat);
    g_CurrentTurnSeat = GetNextInHandSeat(bbSeat);
    
    /* PRE-FLOP SKIP HOST FIX: Ensure first actor is not automatically incremented.
       The turn must wait for the first actor's action (bot or human) before advancing. */
    printf("[Game] Pre-flop first actor set to seat %d\n", g_CurrentTurnSeat);
}

static void EnsureBoardComplete(void)
{
    /* SAFETY NET: Check if board is complete and deal missing cards before showdown */
    int cardsDealt = 0;
    
    /* Check Flop (indices 0,1,2) */
    if (g_MasterTable.community[0].rank == 0 && g_MasterTable.community[0].suit == 0) {
        printf("[Game] SAFETY NET: Flop not dealt. Dealing 3 cards.\n");
        g_MasterTable.state = GAME_STATE_FLOP;
        DealCommunityCards(&g_MasterTable, &g_GameDeck, 3);
        printf("[Game] Flop dealt: [%d/%d, %d/%d, %d/%d]\n",
               g_MasterTable.community[0].rank, g_MasterTable.community[0].suit,
               g_MasterTable.community[1].rank, g_MasterTable.community[1].suit,
               g_MasterTable.community[2].rank, g_MasterTable.community[2].suit);
        BroadcastGameUpdate(-1);
        cardsDealt = 1;
    }
    
    /* Check Turn (index 3) */
    if (g_MasterTable.community[3].rank == 0 && g_MasterTable.community[3].suit == 0) {
        printf("[Game] SAFETY NET: Turn not dealt. Dealing 1 card.\n");
        g_MasterTable.state = GAME_STATE_TURN;
        DealCommunityCards(&g_MasterTable, &g_GameDeck, 1);
        printf("[Game] Turn dealt: [%d/%d]\n",
               g_MasterTable.community[3].rank, g_MasterTable.community[3].suit);
        BroadcastGameUpdate(-1);
        cardsDealt = 1;
    }
    
    /* Check River (index 4) */
    if (g_MasterTable.community[4].rank == 0 && g_MasterTable.community[4].suit == 0) {
        printf("[Game] SAFETY NET: River not dealt. Dealing 1 card.\n");
        g_MasterTable.state = GAME_STATE_RIVER;
        DealCommunityCards(&g_MasterTable, &g_GameDeck, 1);
        printf("[Game] River dealt: [%d/%d]\n",
               g_MasterTable.community[4].rank, g_MasterTable.community[4].suit);
        BroadcastGameUpdate(-1);
        cardsDealt = 1;
    }
    
    if (cardsDealt) {
        printf("[Game] SAFETY NET: Board run-out complete. All 5 cards now have valid rank/suit data.\n");
    }
}

static void ForceHoleCardsReveal(void)
{
    /* CRITICAL: Force all surviving players' hole cards to be visible before winner evaluation.
       This ensures the evaluator has complete visibility of all hands for accurate assessment. */
    for (int i = 0; i < g_MaxPlayers; i++) {
        if (!IsSeatInHand(i) || g_MasterTable.players[i].isFolded) {
            continue;
        }
        
        /* Force visible state */
        g_MasterTable.players[i].cardsVisible = 1;
        printf("[Game] Forced hole card reveal for seat %d: [%d/%d, %d/%d]\n",
               i, g_MasterTable.players[i].hand[0].rank, g_MasterTable.players[i].hand[0].suit,
               g_MasterTable.players[i].hand[1].rank, g_MasterTable.players[i].hand[1].suit);
    }
    
    /* Broadcast the reveal to all clients */
    BroadcastShowdownHoleCards();
    printf("[Game] All surviving hole cards forced visible and broadcasted.\n");
}

static void EnterShowdown(void)
{
    /* STEP 1: CRITICAL SAFETY NET - Ensure complete 5-card board exists before any scoring */
    EnsureBoardComplete();
    
    /* STEP 2: Force all surviving players' hole cards to be visible before winner evaluation */
    ForceHoleCardsReveal();
    
    /* STEP 3: Set showdown state and sync pot */
    g_RoundPhase = GAME_STATE_SHOWDOWN;
    g_CurrentTurnSeat = -1;
    g_MasterTable.state = g_RoundPhase;
    g_MasterTable.pot = g_Pot;
    
    /* STEP 4: Broadcast hole cards (already done in ForceHoleCardsReveal, but keep for safety) */
    BroadcastShowdownHoleCards();
    
    /* STEP 5: Evaluate winners with complete board and visible hands */
    DetermineWinner(&g_MasterTable);
    g_Pot = g_MasterTable.pot;
    g_PendingNextHand = 1;
    g_ShowdownEndTime = time(NULL) + SHOWDOWN_DELAY_S;
    BroadcastGameUpdate(-1);
}

static int CountActivePlayers(void)
{
    int active = 0;

    for (int i = 0; i < g_MaxPlayers; i++) {
        if (IsSeatActiveForTurn(i)) {
            active++;
        }
    }

    return active;
}

static void BroadcastGameUpdate(int client_socket)
{
    char outBuffer[MAX_MSG_LEN];
    int callAmount = 0;
    int minRaise = GetMinRaiseTotal();

    g_MasterTable.pot = g_Pot;
    g_MasterTable.state = g_RoundPhase;
    g_MasterTable.activeIdx = g_CurrentTurnSeat;

    if (g_CurrentTurnSeat >= 0 && g_CurrentTurnSeat < g_MaxPlayers) {
        callAmount = GetCallAmount(g_CurrentTurnSeat);
    }
    
    /* 1. Broadcast Core Table State */
    BuildUpdateMessage(outBuffer, g_CurrentTurnSeat, callAmount, g_CurrentBet, g_Pot, g_RoundPhase, minRaise, g_MasterTable.dealerIdx);
    if (client_socket != -1) {
        send(client_socket, outBuffer, strlen(outBuffer), 0);
    } else {
        for (int i = 0; i < g_MaxPlayers; i++) {
            if (g_MasterTable.players[i].socket != -1) {
                send(g_MasterTable.players[i].socket, outBuffer, strlen(outBuffer), 0);
            }
        }
    }

    /* 2. Broadcast Synchronized Player Profiles (Names/Chips/Status) */
    for (int i = 0; i < g_MaxPlayers; i++) {
        if (g_MasterTable.players[i].socket != -1 || g_MasterTable.players[i].name[0] != '\0') {
            BuildSyncMessage(outBuffer, i, g_MasterTable.players[i].points,
                             g_MasterTable.players[i].isFolded,
                             g_MasterTable.players[i].outOfGame,
                             g_MasterTable.players[i].name);
            
            if (client_socket != -1) {
                send(client_socket, outBuffer, strlen(outBuffer), 0);
            } else {
                for (int j = 0; j < g_MaxPlayers; j++) {
                    if (g_MasterTable.players[j].socket != -1) {
                        send(g_MasterTable.players[j].socket, outBuffer, strlen(outBuffer), 0);
                    }
                }
            }
        }
    }

    /* 3. Broadcast Active Community Cards */
    for (int i = 0; i < 5; i++) {
        BuildCommunityMessage(outBuffer, i, g_MasterTable.community[i].rank, g_MasterTable.community[i].suit);
        if (client_socket != -1) {
            send(client_socket, outBuffer, strlen(outBuffer), 0);
        } else {
            for (int j = 0; j < g_MaxPlayers; j++) {
                if (g_MasterTable.players[j].socket != -1) {
                    send(g_MasterTable.players[j].socket, outBuffer, strlen(outBuffer), 0);
                }
            }
        }
    }
}

static void InitializeGameRound(void)
{
    printf("[Game] Initializing new round...\n");

    g_PendingNextHand = 0;
    g_ShowdownEndTime = 0;
    memset(g_MustAct, 0, sizeof(g_MustAct));
    memset(g_MasterTable.community, 0, sizeof(g_MasterTable.community));
    
    CreateDeck(&g_GameDeck);
    ShuffleDeck(&g_GameDeck);
    DealHoleCards(&g_MasterTable, &g_GameDeck);
    
    /* Safely Distribute Private Cards to Specific Sockets */
    for (int i = 0; i < g_MaxPlayers; i++) {
        if (g_MasterTable.players[i].socket != -1 && IsSeatInHand(i)) {
            char cardBuf[MAX_MSG_LEN];
            BuildHoleCardsMessage(cardBuf, 
                g_MasterTable.players[i].hand[0].rank, g_MasterTable.players[i].hand[0].suit,
                g_MasterTable.players[i].hand[1].rank, g_MasterTable.players[i].hand[1].suit);
            send(g_MasterTable.players[i].socket, cardBuf, strlen(cardBuf), 0);
        }
    }
    
    g_RoundPhase = GAME_STATE_PRE_FLOP;
    g_MasterTable.state = g_RoundPhase;
    g_ActionsThisPhase = 0;
    g_Pot = 0;
    ResetStreetInvestments();
    PostBlinds();
    SetPreflopFirstActor();
}

static void StartNextHand(void)
{
    if (!g_PendingNextHand) {
        return;
    }

    g_PendingNextHand = 0;
    g_ShowdownEndTime = 0;

    /* CLEANUP: Clear community cards before next hand */
    memset(g_MasterTable.community, 0, sizeof(g_MasterTable.community));

    for (int i = 0; i < g_MaxPlayers; i++) {
        /* BANKRUPTCY: Mark players with 0 chips as OUT_OF_GAME */
        if (g_MasterTable.players[i].points <= 0 && g_MasterTable.players[i].socket != -1) {
            g_MasterTable.players[i].outOfGame = 1;
        }
        /* Reset per-hand state */
        g_MasterTable.players[i].isFolded = 0;
        g_MasterTable.players[i].cardsVisible = 0;
        g_MasterTable.players[i].total_hand_investment = 0;
        g_MasterTable.players[i].street_investment = 0;
    }

    int playersRemaining = CountPlayersInHand();
    if (playersRemaining < 2) {
        g_GamePhase = GAME_WAITING_FOR_PLAYERS;
        g_RoundPhase = GAME_STATE_WAITING;
        g_CurrentTurnSeat = -1;
        g_CurrentBet = 0;
        g_Pot = 0;
        BroadcastGameUpdate(-1);
        printf("[Game] Not enough players for next hand.\n");
        return;
    }

    /* NEXT HAND: Rotate dealer and initialize game round */
    RotateDealer();
    InitializeGameRound();
    BroadcastGameUpdate(-1);
}

static void SetTurnToFirstActiveSeat(void)
{
    int attempts = 0;

    g_CurrentTurnSeat = g_MasterTable.dealerIdx;
    do {
        g_CurrentTurnSeat = (g_CurrentTurnSeat + 1) % g_MaxPlayers;
        attempts++;
    } while (!IsSeatActiveForTurn(g_CurrentTurnSeat) && attempts < g_MaxPlayers);

    MarkAllActiveMustAct();
}

static int AdvanceRoundPhaseIfReady(void)
{
    int activePlayers = CountActivePlayers();

    if (activePlayers <= 1) {
        EnterShowdown();
        return 1;
    }

    if (!IsBettingRoundComplete()) {
        return 0;
    }

    /* STREET VARIABLE CLEAN-UP: Explicitly reset all street-level variables before transition */
    g_ActionsThisPhase = 0;
    g_CurrentBet = 0;
    g_LastRaiseSize = BLIND_BIG;
    ResetStreetInvestments();
    printf("[Game] Street transition: Reset current_bet=0, street_investment=0 for all players\n");

    if (g_RoundPhase == GAME_STATE_PRE_FLOP) {
        g_RoundPhase = GAME_STATE_FLOP;
        g_MasterTable.state = g_RoundPhase;
        DealCommunityCards(&g_MasterTable, &g_GameDeck, 3);
    }
    else if (g_RoundPhase == GAME_STATE_FLOP) {
        g_RoundPhase = GAME_STATE_TURN;
        g_MasterTable.state = g_RoundPhase;
        DealCommunityCards(&g_MasterTable, &g_GameDeck, 1);
    }
    else if (g_RoundPhase == GAME_STATE_TURN) {
        g_RoundPhase = GAME_STATE_RIVER;
        g_MasterTable.state = g_RoundPhase;
        DealCommunityCards(&g_MasterTable, &g_GameDeck, 1);
    }
    else {
        EnterShowdown();
        return 1;
    }

    /* AUTO-DEAL REMAINING CARDS: If all active players are all-in, deal remaining cards immediately */
    /* CRITICAL: Card generation must be independent of player action state */
    int allAllIn = 1;
    for (int i = 0; i < g_MaxPlayers; i++) {
        if (IsSeatActiveForTurn(i) && g_MasterTable.players[i].points > 0) {
            allAllIn = 0;
            break;
        }
    }
    
    if (allAllIn) {
        printf("[Game] All players all-in. Auto-dealing remaining community cards with full initialization.\n");
        
        /* FORCE COMPLETE CARD INITIALIZATION: Deal all remaining cards regardless of action state */
        if (g_RoundPhase == GAME_STATE_FLOP) {
            /* Deal Turn card */
            g_MasterTable.state = GAME_STATE_TURN;
            DealCommunityCards(&g_MasterTable, &g_GameDeck, 1);
            printf("[Game] Turn card dealt. Rank: %d, Suit: %d\n", 
                   g_MasterTable.community[3].rank, g_MasterTable.community[3].suit);
            /* CRITICAL: Broadcast immediately to ensure clients receive structured data before showdown */
            BroadcastGameUpdate(-1);
            
            /* Deal River card */
            g_MasterTable.state = GAME_STATE_RIVER;
            DealCommunityCards(&g_MasterTable, &g_GameDeck, 1);
            printf("[Game] River card dealt. Rank: %d, Suit: %d\n", 
                   g_MasterTable.community[4].rank, g_MasterTable.community[4].suit);
            /* CRITICAL: Broadcast immediately to ensure clients receive structured data before showdown */
            BroadcastGameUpdate(-1);
        }
        else if (g_RoundPhase == GAME_STATE_TURN) {
            /* Deal River card */
            g_MasterTable.state = GAME_STATE_RIVER;
            DealCommunityCards(&g_MasterTable, &g_GameDeck, 1);
            printf("[Game] River card dealt. Rank: %d, Suit: %d\n", 
                   g_MasterTable.community[4].rank, g_MasterTable.community[4].suit);
            /* CRITICAL: Broadcast immediately to ensure clients receive structured data before showdown */
            BroadcastGameUpdate(-1);
        }
        
        /* Only enter showdown after all cards are fully initialized and broadcast */
        EnterShowdown();
        return 1;
    }

    SetTurnToFirstActiveSeat();
    BroadcastGameUpdate(-1);
    return 1;
}

static void AdvanceTurn(void)
{
    if (AdvanceRoundPhaseIfReady()) {
        return;
    }

    int attempts = 0;
    int nextSeat = g_CurrentTurnSeat;

    do {
        nextSeat = (nextSeat + 1) % g_MaxPlayers;
        attempts++;
    } while (!IsSeatActiveForTurn(nextSeat) && attempts < g_MaxPlayers);

    if (!IsSeatActiveForTurn(nextSeat)) {
        EnterShowdown();
        return;
    }

    /* CRITICAL FIX: Ensure turn advancement is precise and never skips active under-bet players.
       The betting round completion check (IsBettingRoundComplete) now properly prevents
       skipping under-bet players, but we add logging here to verify turn flow. */
    g_CurrentTurnSeat = nextSeat;
    printf("[Game] Turn advanced to seat %d (isBot=%d, street_investment=%d, current_bet=%d)\n", 
           g_CurrentTurnSeat, g_MasterTable.players[nextSeat].isBot,
           g_MasterTable.players[nextSeat].street_investment, g_CurrentBet);
    
    /* BOT CHAIN DEADLOCK FIX: Send LIMITS and SYNC messages BEFORE broadcast to ensure
       consecutive bots receive turn signal immediately without timing issues */
    if (g_MasterTable.players[nextSeat].isBot) {
        int minAllowedRaise = GetMinRaiseTotal();
        int maxAllowedRaise = g_MasterTable.players[nextSeat].points;
        
        char limitsBuf[MAX_MSG_LEN];
        BuildLimitsMessage(limitsBuf, nextSeat, minAllowedRaise, maxAllowedRaise);
        send(g_MasterTable.players[nextSeat].socket, limitsBuf, strlen(limitsBuf), 0);
        
        char syncBuf[MAX_MSG_LEN];
        BuildSyncMessage(syncBuf, nextSeat, g_MasterTable.players[nextSeat].points,
                        g_MasterTable.players[nextSeat].isFolded,
                        g_MasterTable.players[nextSeat].outOfGame,
                        g_MasterTable.players[nextSeat].name);
        send(g_MasterTable.players[nextSeat].socket, syncBuf, strlen(syncBuf), 0);
    }
    
    /* BROADCAST: Update all clients with new active turn.
       Bots receive UPDATE with isMyTurn=1 and process immediately via ProcessBotBytes.
       Humans receive UPDATE and wait for ACTION message from UI/network. */
    BroadcastGameUpdate(-1);
}

//=============================================================================

int main(int argc, char *argv[])
{
    int server_fd, new_socket, activity, max_sd, sd;
    int client_sockets[MAX_PLAYERS];
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[MAX_MSG_LEN];
    fd_set readfds;

    if (argc > 1 && strcmp(argv[1], "--self-test") == 0) {
        Deck testDeck;
        CreateDeck(&testDeck);
        if (testDeck.topIndex != 0 || testDeck.deck[0].rank != CARD_RANK_TWO) {
            fprintf(stderr, "Server self-test failed.\n");
            return EXIT_FAILURE;
        }
        printf("Server self-test passed.\n");
        return EXIT_SUCCESS;
    }

    memset(&g_MasterTable, 0, sizeof(Table));
    for (int i = 0; i < MAX_PLAYERS; i++) {
        client_sockets[i] = 0;
        g_MasterTable.players[i].socket = -1;
    }

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket allocation failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("Setsockopt SO_REUSEADDR failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

#ifdef SO_REUSEPORT
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, MAX_PENDING) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Authoritative Server active on port %d...\n", PORT);

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        max_sd = server_fd;

        for (int i = 0; i < MAX_PLAYERS; i++) {
            sd = client_sockets[i];
            if (sd > 0) FD_SET(sd, &readfds);
            if (sd > max_sd) max_sd = sd;
        }

        struct timeval tv;
        struct timeval *timeout_ptr = NULL;

        if (g_PendingNextHand) {
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            timeout_ptr = &tv;
        }

        activity = select(max_sd + 1, &readfds, NULL, NULL, timeout_ptr);

        if (activity < 0) continue;

        if (FD_ISSET(server_fd, &readfds)) {
            if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
                continue;
            }

            if (g_IsGameConfigured && g_ConnectedPlayers >= g_MaxPlayers) {
                char outBuffer[MAX_MSG_LEN];
                BuildErrorMessage(outBuffer, "Server is full.");
                send(new_socket, outBuffer, strlen(outBuffer), 0);
                close(new_socket);
            }
            else {
                for (int i = 0; i < MAX_PLAYERS; i++) {
                    if (client_sockets[i] == 0) {
                        client_sockets[i] = new_socket;
                        g_ConnectedPlayers++;
                        break;
                    }
                }
            }
        }

        for (int i = 0; i < MAX_PLAYERS; i++) {
            sd = client_sockets[i];

            if (sd > 0 && FD_ISSET(sd, &readfds)) {
                memset(buffer, 0, MAX_MSG_LEN);
                ssize_t valread = read(sd, buffer, MAX_MSG_LEN - 1);

                if (valread == 0) {
                    close(sd);
                    client_sockets[i] = 0;
                    g_ConnectedPlayers--;

                    if (sd == g_HostSocket) {
                        g_HostSocket = -1;
                        printf("Lobby Host connection terminated.\n");
                        
                        /* HOST DISCONNECT HANDLING: If no active game, reset entire lobby state */
                        if (!IsGameActive()) {
                            for (int k = 0; k < MAX_PLAYERS; k++) {
                                if (client_sockets[k] > 0) {
                                    close(client_sockets[k]);
                                    client_sockets[k] = 0;
                                }
                            }
                            g_ConnectedPlayers = 0;
                            ResetLobbyState();
                            printf("Host disconnected during inactive phase. Lobby state reset.\n");
                        }
                    }

                    for (int j = 0; j < MAX_PLAYERS; j++) {
                        if (g_MasterTable.players[j].socket == sd) {
                            g_MasterTable.players[j].socket = -1;
                            g_MasterTable.players[j].isFolded = 1;
                            break;
                        }
                    }

                    if (g_ConnectedPlayers == 0) {
                        ResetLobbyState();
                        printf("Lobby empty. Configuration state reset.\n");
                    }
                } 
                else {
                    ParsedMessage msg;
                    if (ParseNetworkMessage(buffer, &msg) == 0) {
                        char outBuffer[MAX_MSG_LEN];

                        if (msg.type == MSG_TYPE_ENTER) {
                            char expectedBotName[MAX_NAME_LEN];
                            int isBotJoin = 0;

                            snprintf(expectedBotName, sizeof(expectedBotName), "Bot_Seat_%d", msg.seat);
                            isBotJoin = (g_GamePhase == GAME_SPAWNING_BOTS &&
                                         strncmp(msg.name, expectedBotName, MAX_NAME_LEN) == 0);

                            if (msg.seat < 0 || msg.seat >= MAX_PLAYERS) {
                                BuildErrorMessage(outBuffer, "Invalid seat bounds.");
                                send(sd, outBuffer, strlen(outBuffer), 0);
                            }
                            else if (g_IsGameConfigured && msg.seat >= g_MaxPlayers) {
                                BuildErrorMessage(outBuffer, "Seat outside configured table size.");
                                send(sd, outBuffer, strlen(outBuffer), 0);
                            }
                            else if (g_GamePhase == GAME_ACTIVE_BETTING ||
                                     (g_GamePhase == GAME_SPAWNING_BOTS && !isBotJoin)) {
                                BuildErrorMessage(outBuffer, "Game already started.");
                                send(sd, outBuffer, strlen(outBuffer), 0);
                            }
                            else if (g_HostSocket != -1 && !isBotJoin && strncmp(msg.payload, g_LobbyPassword, MAX_MSG_LEN) != 0) {
                                BuildErrorMessage(outBuffer, "Incorrect lobby password.");
                                send(sd, outBuffer, strlen(outBuffer), 0);
                            }
                            else {
                                /* Enforce Unique Usernames against all currently connected sockets */
                                int isDuplicateName = 0;
                                for (int s = 0; s < MAX_PLAYERS; s++) {
                                    if (g_MasterTable.players[s].socket != -1) {
                                        if (strncmp(g_MasterTable.players[s].name, msg.name, MAX_NAME_LEN) == 0) {
                                            isDuplicateName = 1;
                                            break;
                                        }
                                    }
                                }

                                if (isDuplicateName) {
                                    BuildErrorMessage(outBuffer, "Username already taken.");
                                    send(sd, outBuffer, strlen(outBuffer), 0);
                                }
                                else if (g_MasterTable.players[msg.seat].socket != -1) {
                                    char errMsg[MAX_MSG_LEN];
                                    snprintf(errMsg, sizeof(errMsg), "Seat %d occupied.", msg.seat);
                                    BuildErrorMessage(outBuffer, errMsg);
                                    send(sd, outBuffer, strlen(outBuffer), 0);
                                } 
                                else {
                                    g_MasterTable.players[msg.seat].socket = sd;
                                    strncpy(g_MasterTable.players[msg.seat].name, msg.name, MAX_NAME_LEN - 1);
                                    g_MasterTable.players[msg.seat].points = 1000;
                                    g_MasterTable.players[msg.seat].seat = msg.seat;
                                    g_MasterTable.players[msg.seat].isBot = isBotJoin ? 1 : 0;
                                    g_MasterTable.players[msg.seat].isFolded = 0;
                                    g_MasterTable.players[msg.seat].outOfGame = 0;
                                    
                                    BuildOkMessage(outBuffer, msg.seat, msg.name, 1000);
                                    send(sd, outBuffer, strlen(outBuffer), 0);

                                    if (!g_IsGameConfigured && g_HostSocket == -1) {
                                        g_HostSocket = sd;
                                        /* Persist the host's payload as the authoritative lobby password */
                                        strncpy(g_LobbyPassword, msg.payload, MAX_MSG_LEN - 1);
                                        BuildHostMessage(outBuffer);
                                        send(sd, outBuffer, strlen(outBuffer), 0);
                                    }

                                    BroadcastGameUpdate(-1);
                                }
                            }
                        } 
                        else if (msg.type == MSG_TYPE_SETUP && sd == g_HostSocket) {
                            if (msg.seat < 2 || msg.seat > MAX_PLAYERS) {
                                BuildErrorMessage(outBuffer, "Player count must be between 2 and 8.");
                                send(sd, outBuffer, strlen(outBuffer), 0);
                            }
                            else {
                                g_MaxPlayers = msg.seat;
                                g_IsGameConfigured = 1;
                                g_GamePhase = GAME_WAITING_FOR_PLAYERS;
                                g_RoundPhase = GAME_STATE_WAITING;
                                g_CurrentTurnSeat = 0;
                                g_CurrentBet = 0;
                                g_Pot = 0;
                                g_ActionsThisPhase = 0;
                                g_GameStartTime = time(NULL);
                                BroadcastGameUpdate(-1);
                            }
                        }
                        else if (msg.type == MSG_TYPE_START && sd == g_HostSocket) {
                            if (g_GamePhase == GAME_WAITING_FOR_PLAYERS) {
                                int seatedCount = 0;
                                for (int s = 0; s < g_MaxPlayers; s++) {
                                    if (g_MasterTable.players[s].socket != -1) seatedCount++;
                                }
                                
                                if (seatedCount < g_MaxPlayers) {
                                    for (int s = 0; s < g_MaxPlayers; s++) {
                                        if (g_MasterTable.players[s].socket == -1) {
                                            pthread_t bot_tid;
                                            if (pthread_create(&bot_tid, NULL, RunPokerBotThread, (void*)(intptr_t)s) == 0) {
                                                pthread_detach(bot_tid);
                                            }
                                        }
                                    }
                                    g_GamePhase = GAME_SPAWNING_BOTS;
                                } 
                                else {
                                    for (int s = 0; s < g_MaxPlayers; s++) {
                                        if (IsSeatInHand(s)) {
                                            g_MasterTable.dealerIdx = s;
                                            break;
                                        }
                                    }
                                    InitializeGameRound();
                                    g_GamePhase = GAME_ACTIVE_BETTING;
                                    BroadcastGameUpdate(-1);
                                }
                            }
                        }
                        else if (msg.type == MSG_TYPE_NEXTHAND && sd == g_HostSocket) {
                            if (g_RoundPhase == GAME_STATE_SHOWDOWN && g_PendingNextHand) {
                                StartNextHand();
                                if (CountPlayersInHand() >= 2) {
                                    g_GamePhase = GAME_ACTIVE_BETTING;
                                }
                            }
                        }
                        else {
                            if (msg.type == MSG_TYPE_ACTION) {
                                int actionType = (unsigned char)msg.payload[0];

                                if (g_GamePhase != GAME_ACTIVE_BETTING || g_RoundPhase == GAME_STATE_SHOWDOWN) {
                                    BuildErrorMessage(outBuffer, "Game is not active.");
                                    send(sd, outBuffer, strlen(outBuffer), 0);
                                }
                                else if (msg.seat < 0 || msg.seat >= g_MaxPlayers) {
                                    BuildErrorMessage(outBuffer, "Invalid action seat.");
                                    send(sd, outBuffer, strlen(outBuffer), 0);
                                }
                                else if (g_MasterTable.players[msg.seat].socket != sd) {
                                    BuildErrorMessage(outBuffer, "Action rejected for non-owned seat.");
                                    send(sd, outBuffer, strlen(outBuffer), 0);
                                }
                                else if (msg.seat != g_CurrentTurnSeat) {
                                    BuildErrorMessage(outBuffer, "It is not your turn.");
                                    send(sd, outBuffer, strlen(outBuffer), 0);
                                }
                                else if (g_MasterTable.players[msg.seat].isFolded) {
                                    BuildErrorMessage(outBuffer, "Folded players cannot act.");
                                    send(sd, outBuffer, strlen(outBuffer), 0);
                                }
                                else {
                                    int actionAccepted = 1;

                                    switch (actionType) {
                                        case ACTION_TYPE_FOLD:
                                            g_MasterTable.players[msg.seat].isFolded = 1;
                                            g_MustAct[msg.seat] = 0;
                                            
                                            /* EARLY WINNER DETECTION: If only 1 active player remains, end hand immediately */
                                            if (CountActivePlayers() == 1) {
                                                printf("[Game] All players folded pre-flop. One survivor remains. Ending hand.\n");
                                                if (actionAccepted) {
                                                    g_ActionsThisPhase++;
                                                }
                                                EnterShowdown();
                                                BroadcastGameUpdate(-1);
                                                /* Skip normal AdvanceTurn() and continue to next iteration */
                                                break;  /* Exit switch, skip AdvanceTurn() below */
                                            }
                                            break;
                                        case ACTION_TYPE_CHECK:
                                            if (GetCallAmount(msg.seat) > 0) {
                                                BuildErrorMessage(outBuffer, "Cannot check against an active bet.");
                                                send(sd, outBuffer, strlen(outBuffer), 0);
                                                actionAccepted = 0;
                                            }
                                            else {
                                                g_MustAct[msg.seat] = 0;
                                            }
                                            break;
                                        case ACTION_TYPE_CALL: {
                                            int callAmt = GetCallAmount(msg.seat);
                                            if (callAmt <= 0) {
                                                BuildErrorMessage(outBuffer, "Nothing to call.");
                                                send(sd, outBuffer, strlen(outBuffer), 0);
                                                actionAccepted = 0;
                                            }
                                            else {
                                                CommitPlayerBet(msg.seat, callAmt);
                                                g_MustAct[msg.seat] = 0;
                                            }
                                            break;
                                        }
                                        case ACTION_TYPE_RAISE: {
                                            Player *actor = &g_MasterTable.players[msg.seat];
                                            int targetStreetTotal = msg.amount;
                                            int addAmount = targetStreetTotal - actor->street_investment;
                                            int minRaiseTotal = GetMinRaiseTotal();
                                            int maxStreetTotal = actor->street_investment + actor->points;

                                            /* CHIP CEILING ENFORCEMENT: Reject any raise > maxStreetTotal (all-in) */
                                            if (addAmount <= 0 || targetStreetTotal > maxStreetTotal) {
                                                BuildErrorMessage(outBuffer, "Invalid raise amount: exceeds available chips.");
                                                send(sd, outBuffer, strlen(outBuffer), 0);
                                                printf("[Server] Rejected illegal RAISE from seat %d: amount=%d, max=%d\n", 
                                                       msg.seat, targetStreetTotal, maxStreetTotal);
                                                actionAccepted = 0;
                                            }
                                            else if (targetStreetTotal < minRaiseTotal &&
                                                     targetStreetTotal < maxStreetTotal) {
                                                BuildErrorMessage(outBuffer, "Raise below minimum.");
                                                send(sd, outBuffer, strlen(outBuffer), 0);
                                                actionAccepted = 0;
                                            }
                                            else if (targetStreetTotal <= g_CurrentBet &&
                                                     targetStreetTotal < maxStreetTotal) {
                                                BuildErrorMessage(outBuffer, "Raise must exceed current bet.");
                                                send(sd, outBuffer, strlen(outBuffer), 0);
                                                actionAccepted = 0;
                                            }
                                            else {
                                                if (targetStreetTotal > g_CurrentBet) {
                                                    g_LastRaiseSize = targetStreetTotal - g_CurrentBet;
                                                    g_CurrentBet = targetStreetTotal;
                                                    for (int s = 0; s < g_MaxPlayers; s++) {
                                                        if (IsSeatActiveForTurn(s) && s != msg.seat) {
                                                            g_MustAct[s] = 1;
                                                        }
                                                    }
                                                }
                                                CommitPlayerBet(msg.seat, addAmount);
                                                g_MustAct[msg.seat] = 0;
                                            }
                                            break;
                                        }
                                        default:
                                            BuildErrorMessage(outBuffer, "Unknown action type.");
                                            send(sd, outBuffer, strlen(outBuffer), 0);
                                            actionAccepted = 0;
                                            break;
                                    }

                                    if (actionAccepted) {
                                        g_ActionsThisPhase++;
                                        AdvanceTurn();
                                    }
                                }
                            } else {
                                char okBuffer[MAX_MSG_LEN];
                                BuildOkMessage(okBuffer, msg.seat, msg.name, 1000);
                                send(sd, okBuffer, strlen(okBuffer), 0);
                            }
                        }
                    }
                }
            }
        }
        
        if (g_GamePhase == GAME_SPAWNING_BOTS) {
            int seatedCount = 0;
            for (int i = 0; i < g_MaxPlayers; i++) {
                if (g_MasterTable.players[i].socket != -1) seatedCount++;
            }
            
            if (seatedCount == g_MaxPlayers) {
                for (int s = 0; s < g_MaxPlayers; s++) {
                    if (IsSeatInHand(s)) {
                        g_MasterTable.dealerIdx = s;
                        break;
                    }
                }
                InitializeGameRound();
                g_GamePhase = GAME_ACTIVE_BETTING;
                BroadcastGameUpdate(-1);
            }
        }

        if (g_PendingNextHand && time(NULL) >= g_ShowdownEndTime) {
            StartNextHand();
            if (CountPlayersInHand() >= 2) {
                g_GamePhase = GAME_ACTIVE_BETTING;
            }
        }
    }
    return 0;
}
