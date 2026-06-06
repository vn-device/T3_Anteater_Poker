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
#include <unistd.h>
#include "GameData.h"
#include "HandEval.h"

//=============================================================================

void CreateDeck(Deck* pDeck)
{
    char suits[] = {SUIT_HEARTS, SUIT_DIAMONDS, SUIT_CLUBS, SUIT_SPADES};
    int count = 0;

    if (pDeck == NULL) return;

    /* Create 52 standard cards (2 through Ace, skipping Anteater rank) */
    for (int s = 0; s < 4; s++) {
        for (int r = CARD_RANK_TWO; r <= CARD_RANK_ACE; r++) {
            
            /* Skip the Anteater rank (14) for the standard deck */
            if (r == CARD_RANK_ANTEATER) continue;

            pDeck->deck[count].suit = suits[s];
            pDeck->deck[count].rank = r;
            count++;
        }
    }

    /* Add 4 Anteater Face Cards (one per suit) */
    for (int s = 0; s < 4; s++) {
        pDeck->deck[count].suit = suits[s];
        pDeck->deck[count].rank = CARD_RANK_ANTEATER; 
        count++;
    }

    /* Add 2 Wild Jokers */
    for (int i = 0; i < 2; i++) {
        pDeck->deck[count].suit = SUIT_NONE;
        pDeck->deck[count].rank = CARD_RANK_JOKER; 
        count++;
    }

    pDeck->topIndex = 0;
}

//=============================================================================

void ShuffleDeck(Deck* pDeck)
{
    if (pDeck == NULL) return;

    srand(time(NULL) ^ (unsigned)getpid());

    for (int i = DECK_SIZE - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        Card temp = pDeck->deck[i];
        pDeck->deck[i] = pDeck->deck[j];
        pDeck->deck[j] = temp;
    }

    pDeck->topIndex = 0;
}

//=============================================================================

static int IsPlayerInHand(const Table *pTable, int seat)
{
    if (pTable == NULL || seat < 0 || seat >= MAX_PLAYERS) {
        return 0;
    }

    const Player *p = &pTable->players[seat];
    return p->socket != -1 && !p->outOfGame && p->points > 0;
}

void DealHoleCards(Table* pTable, Deck* pDeck)
{
    if (pTable == NULL || pDeck == NULL) return;

    /* Determine the starting position: left of the dealer */
    int startIdx = (pTable->dealerIdx + 1) % MAX_PLAYERS;

    /* First pass: Deal the first card to all active seated players */
    for (int i = 0; i < MAX_PLAYERS; i++) {
        int curr = (startIdx + i) % MAX_PLAYERS;
        
        if (IsPlayerInHand(pTable, curr)) {
            pTable->players[curr].hand[0] = pDeck->deck[pDeck->topIndex++];
            pTable->players[curr].isFolded = 0;
            pTable->players[curr].cardsVisible = 0;
            pTable->players[curr].total_hand_investment = 0;
            pTable->players[curr].street_investment = 0;
        }
    }

    /* Second pass: Deal the second card to all active seated players */
    for (int i = 0; i < MAX_PLAYERS; i++) {
        int curr = (startIdx + i) % MAX_PLAYERS;
        
        if (IsPlayerInHand(pTable, curr)) {
            pTable->players[curr].hand[1] = pDeck->deck[pDeck->topIndex++];
        }
    }
}

//=============================================================================

void DealCommunityCards(Table* pTable, Deck* pDeck, int count)
{
    if (pTable == NULL || pDeck == NULL) return;
    if (count <= 0 || pTable->state < GAME_STATE_FLOP || pTable->state > GAME_STATE_RIVER) return;

    int start = 0;
    
    /* Determine the starting index based on the game state */
    switch (pTable->state) {
        case GAME_STATE_FLOP:
            start = COMMUNITY_START_FLOP;  /* 0 */
            break;
        case GAME_STATE_TURN:
            start = COMMUNITY_START_TURN;  /* 3 */
            break;
        case GAME_STATE_RIVER:
            start = COMMUNITY_START_RIVER; /* 4 */
            break;
        default:
            return;
    }

    for (int i = 0; i < count; i++) {
        pTable->community[start + i] = pDeck->deck[pDeck->topIndex++];
    }
}

//=============================================================================

unsigned char IsValidAction(Player* pPlayer, int action, int amount, int currentHighest)
{
    if (pPlayer == NULL) return 0;
    if (amount < 0) return 0;
    if (amount > pPlayer->points) return 0;
    
    /* Validate RAISE action: must be higher than current bet */
    if (action == PLAYER_ACTION_RAISE && amount <= currentHighest) return 0;

    return 1;
}

//=============================================================================

int GetDealerIndex(Table* pTable)
{
    if (pTable == NULL) return -1;
    return pTable->dealerIdx;
}

//=============================================================================

static void AwardPotLayer(Table *pTable, int potAmount, const int *eligible, int eligibleCount)
{
    int winnerIndexes[MAX_PLAYERS];
    int winnerCount = 0;
    HandValue bestValue;

    if (pTable == NULL || potAmount <= 0 || eligibleCount <= 0) {
        return;
    }

    if (eligibleCount == 1) {
        pTable->players[eligible[0]].points += potAmount;
        printf("Winner: %s wins side pot %d points uncontested.\n",
               pTable->players[eligible[0]].name,
               potAmount);
        return;
    }

    bestValue.category = HAND_CATEGORY_INVALID;

    for (int i = 0; i < eligibleCount; i++) {
        int playerIndex = eligible[i];
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

    int share = potAmount / winnerCount;
    int remainder = potAmount % winnerCount;

    printf("Winning hand: %s\n", HandCategoryToString(bestValue.category));

    for (int i = 0; i < winnerCount; i++) {
        int playerIndex = winnerIndexes[i];
        int award = share + (i < remainder ? 1 : 0);

        pTable->players[playerIndex].points += award;
        printf("Winner: %s wins %d points.\n",
               pTable->players[playerIndex].name,
               award);
    }
}

void DetermineWinner(Table* pTable)
{
    int tiers[MAX_PLAYERS];
    int tierCount = 0;
    int eligible[MAX_PLAYERS];
    int eligibleCount = 0;

    if (pTable == NULL) {
        return;
    }

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (pTable->players[i].socket != -1 &&
            !pTable->players[i].isFolded &&
            !pTable->players[i].outOfGame) {
            eligible[eligibleCount++] = i;
        }
    }

    if (eligibleCount == 0) {
        pTable->pot = 0;
        return;
    }

    if (eligibleCount == 1) {
        int winner = eligible[0];
        pTable->players[winner].points += pTable->pot;
        printf("Winner: %s wins %d points by default.\n",
               pTable->players[winner].name,
               pTable->pot);
        pTable->pot = 0;
        return;
    }

    for (int i = 0; i < MAX_PLAYERS; i++) {
        int investment = pTable->players[i].total_hand_investment;
        if (investment <= 0) {
            continue;
        }

        int seen = 0;
        for (int t = 0; t < tierCount; t++) {
            if (tiers[t] == investment) {
                seen = 1;
                break;
            }
        }
        if (!seen) {
            tiers[tierCount++] = investment;
        }
    }

    for (int i = 0; i < tierCount - 1; i++) {
        for (int j = i + 1; j < tierCount; j++) {
            if (tiers[i] > tiers[j]) {
                int tmp = tiers[i];
                tiers[i] = tiers[j];
                tiers[j] = tmp;
            }
        }
    }

    int prevTier = 0;
    for (int t = 0; t < tierCount; t++) {
        int tier = tiers[t];
        int contributors = 0;
        int layerEligible[MAX_PLAYERS];
        int layerEligibleCount = 0;

        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (pTable->players[i].total_hand_investment >= tier) {
                contributors++;
                if (!pTable->players[i].isFolded && !pTable->players[i].outOfGame) {
                    layerEligible[layerEligibleCount++] = i;
                }
            }
        }

        int layerPot = (tier - prevTier) * contributors;
        prevTier = tier;

        if (layerPot <= 0) {
            continue;
        }

        if (layerEligibleCount == 0) {
            continue;
        }

        AwardPotLayer(pTable, layerPot, layerEligible, layerEligibleCount);
    }

    pTable->pot = 0;
}
