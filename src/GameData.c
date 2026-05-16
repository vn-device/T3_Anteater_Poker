/******************************************************************************
 * File: GameData.c
 * Author: Team T3
 * Date: May 12, 2026
 *  
 * * Description:
 * Implements the core deck manipulation and distribution functions. 
 * Handles the initialization of the poker deck to the custom Anteater 
 * Poker state, shuffling algorithms, and card distribution logic.
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "GameData.h"

//=============================================================================

void CreateDeck(Deck* pDeck)
{
    char suits[] = {'H', 'D', 'C', 'S'};
    int count = 0;

    /* Create 52 standard cards */
    for (int s = 0; s < 4; s++) {
        for (int r = 2; r <= 15; r++) { // 2 through Ace
            
            /* Skip the Anteater rank (14) for the standard deck */
            if (r == 14) continue;

            pDeck->deck[count].suit = suits[s];
            pDeck->deck[count].rank = r;
            count++;
        }
    }

    /* Add 4 Anteater Face Cards (one per suit) */
    for (int s = 0; s < 4; s++) {
        pDeck->deck[count].suit = suits[s];
        pDeck->deck[count].rank = 14; 
        count++;
    }

    /* Add 2 Wild Jokers */
    for (int i = 0; i < 2; i++) {
        pDeck->deck[count].suit = 'N';
        pDeck->deck[count].rank = 16; 
        count++;
    }

    pDeck->topIndex = 0;
}

//=============================================================================

void ShuffleDeck(Deck* pDeck)
{
    srand(time(NULL));

    for (int i = DECK_SIZE - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        Card temp = pDeck->deck[i];
        pDeck->deck[i] = pDeck->deck[j];
        pDeck->deck[j] = temp;
    }

    pDeck->topIndex = 0;
}

//=============================================================================

void DealHoleCards(Table* pTable, Deck* pDeck)
{
    /* Determine the starting position: left of the dealer */
    int startIdx = (pTable->dealerIdx + 1) % MAX_PLAYERS;

    /* First pass: Deal the first card to all active seated players */
    for (int i = 0; i < MAX_PLAYERS; i++) {
        /* Circularly iterate through the table seats starting from startIdx */
        int curr = (startIdx + i) % MAX_PLAYERS;
        
        /* Proceed only if the seat is occupied by a human or bot */
        if (pTable->players[curr].socket != -1) {
            pTable->players[curr].hand[0] = pDeck->deck[pDeck->topIndex++];
            
            /* Reset the fold state for the new hand */
            pTable->players[curr].isFolded = 0; 
        }
    }

    /* Second pass: Deal the second card to all active seated players */
    for (int i = 0; i < MAX_PLAYERS; i++) {
        int curr = (startIdx + i) % MAX_PLAYERS;
        
        if (pTable->players[curr].socket != -1) {
            pTable->players[curr].hand[1] = pDeck->deck[pDeck->topIndex++];
        }
    }
}

//=============================================================================

void DealCommunityCards(Table* pTable, Deck* pDeck, int count)
{
    int start = 0;
    if (pTable->state == 2)      start = 0; // Flop
    else if (pTable->state == 3) start = 3; // Turn
    else if (pTable->state == 4) start = 4; // River

    for (int i = 0; i < count; i++) {
        pTable->community[start + i] = pDeck->deck[pDeck->topIndex++];
    }
}

//=============================================================================

unsigned char IsValidAction(Player* pPlayer, int action, int amount, int currentHighest)
{
    if (amount > pPlayer->points) return 0;
    
    if (action == 4 && amount <= currentHighest) return 0;

    return 1;
}

//=============================================================================

void DetermineWinner(Table* pTable)
{
    int eligiblePlayers[MAX_PLAYERS];
    int winnerIndexes[MAX_PLAYERS];
    int eligibleCount = 0;
    int winnerCount = 0;
    HandValue bestValue;

    if (pTable == NULL) {
        return;
    }

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (pTable->players[i].socket != -1 && !pTable->players[i].isFolded) {
            eligiblePlayers[eligibleCount++] = i;
        }
    }

    if (eligibleCount == 0) {
        return;
    }

    if (eligibleCount == 1) {
        int winner = eligiblePlayers[0];
        pTable->players[winner].points += pTable->pot;
        printf("Winner: %s wins %d points by default.\n",
               pTable->players[winner].name,
               pTable->pot);
        pTable->pot = 0;
        return;
    }

    bestValue.category = HAND_CATEGORY_INVALID;

    for (int i = 0; i < eligibleCount; i++) {
        int playerIndex = eligiblePlayers[i];
        Card cards[7];
        HandValue playerValue;

        cards[0] = pTable->players[playerIndex].hand[0];
        cards[1] = pTable->players[playerIndex].hand[1];
        for (int c = 0; c < 5; c++) {
            cards[c + 2] = pTable->community[c];
        }

        EvaluateBestHand(cards, 7, &playerValue);
        if (playerValue.category == HAND_CATEGORY_INVALID) {
            continue;
        }

        int comparison = (bestValue.category == HAND_CATEGORY_INVALID)
                         ? 1
                         : CompareHandValues(&playerValue, &bestValue);

        if (comparison > 0) {
            bestValue = playerValue;
            winnerIndexes[0] = playerIndex;
            winnerCount = 1;
        }
        else if (comparison == 0) {
            winnerIndexes[winnerCount++] = playerIndex;
        }
    }

    if (winnerCount == 0) {
        return;
    }

    int share = pTable->pot / winnerCount;
    int remainder = pTable->pot % winnerCount;

    printf("Winning hand: %s\n", HandCategoryToString(bestValue.category));

    for (int i = 0; i < winnerCount; i++) {
        int playerIndex = winnerIndexes[i];
        int award = share + (i < remainder ? 1 : 0);

        pTable->players[playerIndex].points += award;
        printf("Winner: %s wins %d points.\n",
               pTable->players[playerIndex].name,
               award);
    }

    pTable->pot = 0;
}