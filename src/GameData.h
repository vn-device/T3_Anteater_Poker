/******************************************************************************
 * File: GameData.h
 * Author: Team T3
 * Date: May 12, 2026
 * 
 * * Description:
 * Defines core data structures for Anteater Poker, including the deck, 
 * player records, and table state. Declares the public API for 
 * game initialization and state management.
 *****************************************************************************/

#ifndef GAMEDATA_H
#define GAMEDATA_H

#define MAX_PLAYERS 8
#define DECK_SIZE   58  // 52 standard + 4 Anteater cards + 2 Jokers

//=============================================================================

typedef struct {
    char suit;  // 'H', 'D', 'C', 'S', or 'N' for None
    int rank;   // 2-10, 11(J), 12(Q), 13(K), 14(Anteater), 15(Ace), 16(Joker)
} Card;

typedef struct {
    Card deck[DECK_SIZE];
    int topIndex;
} Deck;

typedef struct {
    char name[32];
    int seat;
    int points;

    /* Network & status management */
    int socket;
    unsigned char isBot;
    unsigned char isFolded;

    Card hand[2];
} Player;

typedef struct {
    Player players[MAX_PLAYERS];
    Card community[5];
    int pot;
    int state; // 0: Waiting, 1: Pre-flop, 2: Flop, 3: Turn, 4: River, 5: Showdown

    /* To track dealer and active turn */
    int dealerIdx;
    int activeIdx;
} Table;

//=============================================================================

/**
 * Initializes the deck with 52 standard cards, 4 Anteater cards, and 2 Jokers.
 */
void CreateDeck(Deck *pDeck);

/**
 * Shuffles the deck using a randomization algorithm.
 */
void ShuffleDeck(Deck *pDeck);

/**
 * Deals two private hole cards to each active player at the table.
 */
void DealHoleCards(Table *pTable, Deck *pDeck);

/**
 * Distributes community cards based on the current GameState (Flop, Turn, River).
 */
void DealCommunityCards(Table *pTable, Deck *pDeck, int count);

/**
 * Evaluates all hands at Showdown to determine the winner and award the pot.
 */
void DetermineWinner(Table *pTable);

/**
 * Validates if a player's requested action (e.g., Raise) is legal given their points.
 */
unsigned char IsValidAction(Player *pPlayer, int action, int amount, int currentHighest);

//=============================================================================

#endif // GAMEDATA_H