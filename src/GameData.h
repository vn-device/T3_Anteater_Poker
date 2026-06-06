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

#define MAX_PLAYERS  8
#define MAX_NAME_LEN 32
#define DECK_SIZE    58  // 52 standard + 4 Anteater cards + 2 Jokers

#define BLIND_SMALL       5
#define BLIND_BIG        10
#define SHOWDOWN_DELAY_S  8

/* Card Suit Characters */
#define SUIT_HEARTS   'H'
#define SUIT_DIAMONDS 'D'
#define SUIT_CLUBS    'C'
#define SUIT_SPADES   'S'
#define SUIT_NONE     'N'

//=============================================================================

typedef enum {
    CARD_RANK_TWO = 2,
    CARD_RANK_THREE,
    CARD_RANK_FOUR,
    CARD_RANK_FIVE,
    CARD_RANK_SIX,
    CARD_RANK_SEVEN,
    CARD_RANK_EIGHT,
    CARD_RANK_NINE,
    CARD_RANK_TEN,
    CARD_RANK_JACK,
    CARD_RANK_QUEEN,
    CARD_RANK_KING,
    CARD_RANK_ANTEATER,
    CARD_RANK_ACE,
    CARD_RANK_JOKER
} CARD_RANK;

typedef enum {
    GAME_STATE_WAITING = 0,
    GAME_STATE_PRE_FLOP,
    GAME_STATE_FLOP,
    GAME_STATE_TURN,
    GAME_STATE_RIVER,
    GAME_STATE_SHOWDOWN
} GAME_STATE;

typedef enum {
    COMMUNITY_START_FLOP = 0,
    COMMUNITY_START_TURN = 3,
    COMMUNITY_START_RIVER = 4
} COMMUNITY_START;

/**
 * @brief Standard player actions translated into network protocol values.
 */
typedef enum {
    PLAYER_ACTION_JOIN = 0,
    PLAYER_ACTION_FOLD,
    PLAYER_ACTION_CHECK,
    PLAYER_ACTION_CALL,
    PLAYER_ACTION_RAISE
} PLAYER_ACTION;

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
    char name[MAX_NAME_LEN];
    int seat;
    int points;

    /* Network & status management */
    int socket;
    unsigned char isBot;
    unsigned char isFolded;
    unsigned char outOfGame;
    unsigned char cardsVisible;

    int total_hand_investment;
    int street_investment;

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
 * Determines the starting dealer index for a round.
 * @param pTable Pointer to the table state
 * @return The dealer's seat index
 */
int GetDealerIndex(Table *pTable);

/**
 * Validates if a player's requested action (e.g., Raise) is legal given their points.
 */
unsigned char IsValidAction(Player *pPlayer, int action, int amount, int currentHighest);

//=============================================================================

#endif // GAMEDATA_H
