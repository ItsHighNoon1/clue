#ifndef __frames_h__
#define __frames_h__

#include <stdint.h>

#define FRAME_TYPE_UNKNOWN 0
// Format of all frames. Includes the type, length of the data, and location where the data is.
typedef struct {
    int8_t type;
    int8_t _reserved[3];
    int32_t data_length;
    char data[0];
} Frame_t;

#define FRAME_TYPE_ERROR 1
// A frame sent by the server to the client to communicate an error. This is for debug purposes
// when the client does something wrong so that the programmer can fix their client.
typedef struct {
    int32_t error_length;
    char error[0];
} ErrorFrame_t;

#define FRAME_TYPE_ABORT 2
// A frame sent by the server to the client when the server needs to abort the game gracefully.
// Usually this is because a client has disconnected.
typedef struct {
    int32_t error_length;
    char error[0];
} AbortFrame_t;

#define FRAME_TYPE_CONNECT 2
// The first frame a client sends to the server. It has the name of the client.
typedef struct {
    int8_t name_length;
    char name[0];
} ConnectFrame_t;

#define FRAME_TYPE_RULES 3
// The frame sent back by the server when a client is allowed to connect. It has the rules
// of this particular game which allows to client to set up any data structures it needs.
// It also provides the names of the cards which are useful for debugging and also fun.
typedef struct {
    int8_t player_id; // The player ID you have been assigned.
    int8_t num_categories; // The number of categories in the game. The default Clue has 3 (character, weapon, room)
    int16_t num_cards; // The total number of cards in the game.
    int16_t num_cards_in_category[0]; // The number of cards in each category. Length <num_categories>
    struct {
        int16_t cards[0]; // The card ID. Length <num_cards_in_category[category]>
    } categories[0]; // Length <num_categories>
    struct {
        // Flavor text names for the cards. Can be ignored
        int8_t name_length;
        char name[0];
    } card_names[0]; // Length <num_cards>
} RulesFrame_t;

#define FRAME_TYPE_START 4
// The frame sent by the server to indicate the start of the game. Contains the client's hand.
typedef struct {
    int16_t your_hand_size;
    int8_t num_players;
    int8_t _reserved;
    int16_t your_hand[0]; // Length <your_hand_size>
    int8_t player_order[0]; // Length <num_players>. Index 0 is the ID of the first player, etc.
    int16_t player_hand_sizes[0]; // Length <num_players>
    struct {
        // Names for the players. Can be ignored.
        int8_t name_length;
        char name[0];
    } player_names[0]; // Length <num_players>
} StartFrame_t;

#define FRAME_TYPE_TURN 5
// The frame sent by the server to indicate it is a client's turn.
typedef struct {
    int8_t player_id; // If this is your ID, you must respond with a FRAME_TYPE_TURN_RESPONSE
} TurnFrame_t;

#define FRAME_TYPE_TURN_RESPONSE 6
// Sent by the client in response to FRAME_TYPE_TURN to indicate the suggestion.
typedef struct {
    int16_t suggestion[0]; // Length <num_categories> from FRAME_TYPE_RULES
} TurnResponseFrame_t;

#define FRAME_TYPE_QUERY 7
// Sent by the server to players in order following a suggestion.
typedef struct {
    int8_t player_id; // If this is your ID, you must respond with a FRAME_TYPE_TURN_RESPONSE
    int8_t _reserved;
    int16_t suggestion[0]; // Length <num_categories> from FRAME_TYPE_RULES
} QueryFrame_t;

#define FRAME_TYPE_QUERY_RESPONSE 8
// Sent by the client in response to FRAME_TYPE_QUERY to indicate the response.
typedef struct {
    int16_t card_id; // If you are not obligated to show card_id, the server will either:
                     // (1) Choose a random card that you are obligated to show, if possible
                     // (2) Not show a card.
} QueryResponseFrame_t;

#define FRAME_TYPE_QUERY_RETURN 9
// Sent by the server when a client responds to a suggestion.
typedef struct {
    int8_t player_id;
    int8_t _reserved;
    int16_t card_id; // -1 (0xFFFF) indicates no card. If you are not the client that made the suggestion but a card was showed, this is always 0
} QueryAnouncementFrame_t;

#define FRAME_TYPE_SOLVE_ATTEMPT 10
// Sent by a client in response to FRAME_TYPE_TURN when trying to solve the game.
typedef struct {
    int16_t cards[0]; // Length <num_categories> from FRAME_TYPE_RULES
} SolveAttemptFrame_t;

#define FRAME_TYPE_SOLVE_RESULT 11
// Sent by the server to all players in response to a solve attempt.
typedef struct {
    int8_t player;
    int8_t correct; // 0 if the game continues, 1 if the game ends
    int16_t cards[0]; // Length <num_categories> from FRAME_TYPE_RULES
} SolveResultFrame_t;

#endif