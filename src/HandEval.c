#include <string.h>
#include "HandEval.h"

#define FIRST_RANK 2
#define LAST_RANK  15
#define JOKER_RANK 16

static const char kPlayableSuits[] = {'H', 'D', 'C', 'S'};

//=============================================================================

static void ClearHandValue(HandValue *pValue)
{
    if (pValue == NULL) {
        return;
    }

    pValue->category = HAND_CATEGORY_INVALID;
    memset(pValue->ranks, 0, sizeof(pValue->ranks));
    memset(pValue->cards, 0, sizeof(pValue->cards));
}

//=============================================================================

static unsigned char IsPlayableSuit(char suit)
{
    for (int i = 0; i < 4; i++) {
        if (kPlayableSuits[i] == suit) {
            return 1;
        }
    }

    return 0;
}

//=============================================================================

static unsigned char IsJoker(Card card)
{
    return (unsigned char)(card.rank == JOKER_RANK);
}

//=============================================================================

static unsigned char IsPlayableCard(Card card)
{
    return (unsigned char)(card.rank >= FIRST_RANK &&
                           card.rank <= LAST_RANK &&
                           IsPlayableSuit(card.suit));
}

//=============================================================================

static unsigned char CardsMatch(Card left, Card right)
{
    return (unsigned char)(left.rank == right.rank && left.suit == right.suit);
}

//=============================================================================

static unsigned char ReplacementAlreadyUsed(const Card cards[],
                                            int cardCount,
                                            int replacementIndex,
                                            Card replacement)
{
    for (int i = 0; i < cardCount; i++) {
        if (i == replacementIndex || IsJoker(cards[i])) {
            continue;
        }

        if (CardsMatch(cards[i], replacement)) {
            return 1;
        }
    }

    return 0;
}

//=============================================================================

static void StoreHandValue(HandValue *pValue,
                           HAND_CATEGORY category,
                           const int ranks[],
                           const Card cards[])
{
    pValue->category = category;

    for (int i = 0; i < HAND_VALUE_RANK_COUNT; i++) {
        pValue->ranks[i] = ranks[i];
    }

    for (int i = 0; i < POKER_HAND_SIZE; i++) {
        pValue->cards[i] = cards[i];
    }
}

//=============================================================================

int CompareHandValues(const HandValue *pLeft, const HandValue *pRight)
{
    if (pLeft->category > pRight->category) {
        return 1;
    }

    if (pLeft->category < pRight->category) {
        return -1;
    }

    for (int i = 0; i < HAND_VALUE_RANK_COUNT; i++) {
        if (pLeft->ranks[i] > pRight->ranks[i]) {
            return 1;
        }

        if (pLeft->ranks[i] < pRight->ranks[i]) {
            return -1;
        }
    }

    return 0;
}

//=============================================================================

const char *HandCategoryToString(HAND_CATEGORY category)
{
    switch (category) {
    case HAND_CATEGORY_HIGH_CARD:
        return "High Card";
    case HAND_CATEGORY_ONE_PAIR:
        return "One Pair";
    case HAND_CATEGORY_TWO_PAIR:
        return "Two Pair";
    case HAND_CATEGORY_THREE_OF_A_KIND:
        return "Three of a Kind";
    case HAND_CATEGORY_STRAIGHT:
        return "Straight";
    case HAND_CATEGORY_FLUSH:
        return "Flush";
    case HAND_CATEGORY_FULL_HOUSE:
        return "Full House";
    case HAND_CATEGORY_FOUR_OF_A_KIND:
        return "Four of a Kind";
    case HAND_CATEGORY_STRAIGHT_FLUSH:
        return "Straight Flush";
    default:
        return "Invalid Hand";
    }
}

//=============================================================================

static int FindHighestRankWithCount(const int rankCounts[], int targetCount)
{
    for (int rank = LAST_RANK; rank >= FIRST_RANK; rank--) {
        if (rankCounts[rank] == targetCount) {
            return rank;
        }
    }

    return 0;
}

//=============================================================================

static int FindHighestRankExcept(const int rankCounts[], int excludedRank)
{
    for (int rank = LAST_RANK; rank >= FIRST_RANK; rank--) {
        if (rank != excludedRank && rankCounts[rank] > 0) {
            return rank;
        }
    }

    return 0;
}

//=============================================================================

static int AddRanksExcept(const int rankCounts[],
                          int excludedRank,
                          int ranks[],
                          int startIndex)
{
    int outIndex = startIndex;

    for (int rank = LAST_RANK; rank >= FIRST_RANK; rank--) {
        if (rank != excludedRank && rankCounts[rank] > 0) {
            ranks[outIndex++] = rank;
            if (outIndex >= HAND_VALUE_RANK_COUNT) {
                break;
            }
        }
    }

    return outIndex;
}

//=============================================================================

static int GetStraightHighRank(const int rankCounts[])
{
    for (int highRank = LAST_RANK; highRank >= FIRST_RANK + 4; highRank--) {
        unsigned char isStraight = 1;

        for (int rank = highRank - 4; rank <= highRank; rank++) {
            if (rankCounts[rank] == 0) {
                isStraight = 0;
                break;
            }
        }

        if (isStraight) {
            return highRank;
        }
    }

    /* Standard ace-low wheel: A, 2, 3, 4, 5. */
    if (rankCounts[LAST_RANK] > 0 &&
        rankCounts[2] > 0 &&
        rankCounts[3] > 0 &&
        rankCounts[4] > 0 &&
        rankCounts[5] > 0) {
        return 5;
    }

    return 0;
}

//=============================================================================

static void FillHighCardRanks(const int rankCounts[], int ranks[])
{
    int outIndex = 0;

    for (int rank = LAST_RANK; rank >= FIRST_RANK; rank--) {
        for (int count = 0; count < rankCounts[rank]; count++) {
            ranks[outIndex++] = rank;
            if (outIndex >= HAND_VALUE_RANK_COUNT) {
                return;
            }
        }
    }
}

//=============================================================================

static void EvaluateFiveNaturalCards(const Card cards[], HandValue *pValue)
{
    int rankCounts[LAST_RANK + 1] = {0};
    int ranks[HAND_VALUE_RANK_COUNT] = {0};
    unsigned char isFlush = 1;

    ClearHandValue(pValue);

    for (int i = 0; i < POKER_HAND_SIZE; i++) {
        if (!IsPlayableCard(cards[i])) {
            return;
        }

        rankCounts[cards[i].rank]++;

        if (i > 0 && cards[i].suit != cards[0].suit) {
            isFlush = 0;
        }
    }

    int straightHighRank = GetStraightHighRank(rankCounts);

    if (isFlush && straightHighRank > 0) {
        ranks[0] = straightHighRank;
        StoreHandValue(pValue, HAND_CATEGORY_STRAIGHT_FLUSH, ranks, cards);
        return;
    }

    int fourRank = FindHighestRankWithCount(rankCounts, 4);
    if (fourRank > 0) {
        ranks[0] = fourRank;
        ranks[1] = FindHighestRankExcept(rankCounts, fourRank);
        StoreHandValue(pValue, HAND_CATEGORY_FOUR_OF_A_KIND, ranks, cards);
        return;
    }

    int threeRank = FindHighestRankWithCount(rankCounts, 3);
    int pairRank = FindHighestRankWithCount(rankCounts, 2);
    if (threeRank > 0 && pairRank > 0) {
        ranks[0] = threeRank;
        ranks[1] = pairRank;
        StoreHandValue(pValue, HAND_CATEGORY_FULL_HOUSE, ranks, cards);
        return;
    }

    if (isFlush) {
        FillHighCardRanks(rankCounts, ranks);
        StoreHandValue(pValue, HAND_CATEGORY_FLUSH, ranks, cards);
        return;
    }

    if (straightHighRank > 0) {
        ranks[0] = straightHighRank;
        StoreHandValue(pValue, HAND_CATEGORY_STRAIGHT, ranks, cards);
        return;
    }

    if (threeRank > 0) {
        ranks[0] = threeRank;
        AddRanksExcept(rankCounts, threeRank, ranks, 1);
        StoreHandValue(pValue, HAND_CATEGORY_THREE_OF_A_KIND, ranks, cards);
        return;
    }

    int pairs[2] = {0};
    int pairCount = 0;
    for (int rank = LAST_RANK; rank >= FIRST_RANK; rank--) {
        if (rankCounts[rank] == 2) {
            pairs[pairCount++] = rank;
            if (pairCount == 2) {
                break;
            }
        }
    }

    if (pairCount == 2) {
        ranks[0] = pairs[0];
        ranks[1] = pairs[1];

        for (int rank = LAST_RANK; rank >= FIRST_RANK; rank--) {
            if (rank != pairs[0] && rank != pairs[1] && rankCounts[rank] > 0) {
                ranks[2] = rank;
                break;
            }
        }

        StoreHandValue(pValue, HAND_CATEGORY_TWO_PAIR, ranks, cards);
        return;
    }

    if (pairRank > 0) {
        ranks[0] = pairRank;
        AddRanksExcept(rankCounts, pairRank, ranks, 1);
        StoreHandValue(pValue, HAND_CATEGORY_ONE_PAIR, ranks, cards);
        return;
    }

    FillHighCardRanks(rankCounts, ranks);
    StoreHandValue(pValue, HAND_CATEGORY_HIGH_CARD, ranks, cards);
}

//=============================================================================

static void EvaluateWildcardAssignments(Card cards[],
                                        const int jokerIndexes[],
                                        int jokerCount,
                                        int depth,
                                        HandValue *pBest)
{
    if (depth == jokerCount) {
        HandValue candidate;
        EvaluateFiveNaturalCards(cards, &candidate);

        if (candidate.category != HAND_CATEGORY_INVALID &&
            (pBest->category == HAND_CATEGORY_INVALID ||
             CompareHandValues(&candidate, pBest) > 0)) {
            *pBest = candidate;
        }

        return;
    }

    int jokerIndex = jokerIndexes[depth];

    for (int rank = FIRST_RANK; rank <= LAST_RANK; rank++) {
        for (int suitIndex = 0; suitIndex < 4; suitIndex++) {
            Card replacement;
            replacement.rank = rank;
            replacement.suit = kPlayableSuits[suitIndex];

            if (ReplacementAlreadyUsed(cards,
                                       POKER_HAND_SIZE,
                                       jokerIndex,
                                       replacement)) {
                continue;
            }

            cards[jokerIndex] = replacement;
            EvaluateWildcardAssignments(cards,
                                        jokerIndexes,
                                        jokerCount,
                                        depth + 1,
                                        pBest);
        }
    }

    cards[jokerIndex].rank = JOKER_RANK;
    cards[jokerIndex].suit = 'N';
}

//=============================================================================

static void EvaluateFiveCards(const Card cards[], HandValue *pValue)
{
    Card workingCards[POKER_HAND_SIZE];
    int jokerIndexes[POKER_HAND_SIZE] = {0};
    int jokerCount = 0;

    ClearHandValue(pValue);

    for (int i = 0; i < POKER_HAND_SIZE; i++) {
        workingCards[i] = cards[i];
        if (IsJoker(cards[i])) {
            jokerIndexes[jokerCount++] = i;
        }
    }

    if (jokerCount == 0) {
        EvaluateFiveNaturalCards(workingCards, pValue);
        return;
    }

    EvaluateWildcardAssignments(workingCards, jokerIndexes, jokerCount, 0, pValue);
}

//=============================================================================

void EvaluateBestHand(const Card cards[], int cardCount, HandValue *pValue)
{
    ClearHandValue(pValue);

    if (cards == NULL || pValue == NULL || cardCount < POKER_HAND_SIZE) {
        return;
    }

    for (int a = 0; a < cardCount - 4; a++) {
        for (int b = a + 1; b < cardCount - 3; b++) {
            for (int c = b + 1; c < cardCount - 2; c++) {
                for (int d = c + 1; d < cardCount - 1; d++) {
                    for (int e = d + 1; e < cardCount; e++) {
                        Card fiveCards[POKER_HAND_SIZE];
                        HandValue candidate;

                        fiveCards[0] = cards[a];
                        fiveCards[1] = cards[b];
                        fiveCards[2] = cards[c];
                        fiveCards[3] = cards[d];
                        fiveCards[4] = cards[e];

                        EvaluateFiveCards(fiveCards, &candidate);

                        if (candidate.category != HAND_CATEGORY_INVALID &&
                            (pValue->category == HAND_CATEGORY_INVALID ||
                             CompareHandValues(&candidate, pValue) > 0)) {
                            *pValue = candidate;
                        }
                    }
                }
            }
        }
    }
}