#ifndef HANDEVAL_H
#define HANDEVAL_H

#include "GameData.h"

#define POKER_HAND_SIZE 5
#define HAND_VALUE_RANK_COUNT 5

typedef enum {
    HAND_CATEGORY_INVALID = -1,
    HAND_CATEGORY_HIGH_CARD = 0,
    HAND_CATEGORY_ONE_PAIR,
    HAND_CATEGORY_TWO_PAIR,
    HAND_CATEGORY_THREE_OF_A_KIND,
    HAND_CATEGORY_STRAIGHT,
    HAND_CATEGORY_FLUSH,
    HAND_CATEGORY_FULL_HOUSE,
    HAND_CATEGORY_FOUR_OF_A_KIND,
    HAND_CATEGORY_STRAIGHT_FLUSH
} HAND_CATEGORY;

typedef struct {
    HAND_CATEGORY category;
    int ranks[HAND_VALUE_RANK_COUNT];
    Card cards[POKER_HAND_SIZE];
} HandValue;


void EvaluateBestHand(const Card cards[], int cardCount, HandValue *pValue);


int CompareHandValues(const HandValue *pLeft, const HandValue *pRight);

const char *HandCategoryToString(HAND_CATEGORY category);

#endif // HANDEVAL_H