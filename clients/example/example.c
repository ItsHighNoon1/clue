/**
 * @file example.c
 * @author ItsHighNoon
 * @brief Example client for Clue (https://github.com/ItsHighNoon1/clue)
 * @date 01-29-2026
 * @copyright Copyright (c) 2026
 * 
 * This is an example client that uses the high level interface provided by clue.h.
 * All that is needed is to specify some callback functions in the ClueHighLevelClient_t
 * and call clue_hl_run with the address and port of the server. This implementation
 * is intentionally holey in the interest of brevity.
 *
 * This particular client plays the game randomly to keep the logic simple. The fun is
 * in making a client that can play Clue better than this example client can :).
 */

#include <stdlib.h>
#include <time.h>

#define _CLUE_IMPL
#include "../../clue.h"

// This bot's memory
typedef struct {
    int turns_played;
    int num_categories;
    int16_t* categories_len;
    int hand_size;
    int16_t* hand;
} ExampleKnowledge_t;

void on_debug(ClueHighLevelClient_t* client, char* msg, int msg_len) {
    // Server sent a message
    printf("Server: %*.s\n", msg_len, msg);
}

void on_rules(ClueHighLevelClient_t* client, RulesFrame_t* frame) {
    // Can set up deck related data structures at this time
    ExampleKnowledge_t* knowledge = client->user.user;
    knowledge->num_categories = frame->num_categories;
    knowledge->categories_len = malloc(knowledge->num_categories * sizeof(int16_t));
    for (int i = 0; i < frame->num_categories; i++) {
        knowledge->categories_len[i] = frame->num_cards_in_category[i]; // Random player needs to know what is valid
    }
}

void on_start(ClueHighLevelClient_t* client, StartFrame_t* frame) {
    // Can set up player related data structures at this time
    ExampleKnowledge_t* knowledge = client->user.user;
    knowledge->hand_size = frame->your_hand_size;
    knowledge->hand = malloc(knowledge->hand_size * sizeof(int16_t));
    for (int i = 0; i < frame->your_hand_size; i++) {
        knowledge->hand[i] = frame->your_hand[i];
    }
}

void on_our_turn(ClueHighLevelClient_t* client, int16_t* suggestion, int suggestion_len, int* solve) {
    // We are obligated to fill out the suggestion array and can indicate we want to solve
    ExampleKnowledge_t* knowledge = client->user.user;
    printf("My turn\n");
    if (knowledge->turns_played++ > 10) {
        *solve = 1; // Indicate we want to solve
        printf("Solving!!!\n");
    }
    int16_t base_card = 0;
    for (int i = 0; i < suggestion_len; i++) {
        suggestion[i] = base_card + rand() % knowledge->categories_len[i]; // Fill out suggestion
        base_card += knowledge->categories_len[i];
        printf("--%s\n", clue_hl_get_card_name(client, suggestion[i]));
    }
}

void on_query(ClueHighLevelClient_t* client, int8_t player_id, int16_t* suggestion, int suggestion_len, int16_t* response) {
    // Someone is asking us about something, need to respond
    ExampleKnowledge_t* knowledge = client->user.user;
    for (int i = 0; i < suggestion_len; i++) {
        for (int j = 0; j < knowledge->hand_size; j++) {
            if (suggestion[i] == knowledge->hand[j]) {
                *response = suggestion[i];
                return;
            }
        }
    }
    *response = -1;
}

void on_reply(ClueHighLevelClient_t* client, int8_t responder_id, int16_t card) {
    // Someone showed us or didn't show us a card
    printf("----%s shows us %s\n", clue_hl_get_player_name(client, responder_id), clue_hl_get_card_name(client, card));
}

void on_end(ClueHighLevelClient_t* client, int8_t winner, int won_by_default) {
    // Game ended
    printf("Game ended, %s won\n", clue_hl_get_player_name(client, winner));
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: %s [address] [port]\n", argv[0]);
        return -1;
    }
    srand(time(NULL));

    ClueHighLevelClient_t client;
    memset(&client, 0, sizeof(ClueHighLevelClient_t));
    ExampleKnowledge_t knowledge;
    knowledge.turns_played = 0;

    // These are the only fields you are required to set up before calling clue_hl_run
    client.user.name = "Example";
    client.user.on_our_turn = on_our_turn;
    client.user.on_query = on_query;

    // And some that are not required
    client.user.on_debug = on_debug;
    client.user.on_rules = on_rules;
    client.user.on_start = on_start;
    client.user.on_reply = on_reply;
    client.user.on_end = on_end;
    client.user.user = &knowledge;
    clue_hl_run(&client, argv[1], argv[2]);
}