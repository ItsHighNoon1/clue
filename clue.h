#ifndef __clue_h__
#define __clue_h__
/**
 * @file clue.h
 * @author ItsHighNoon
 * @brief Single-header library for Clue clients (https://github.com/ItsHighNoon1/clue)
 * @date 01-29-2026
 * @copyright Copyright (c) 2026
 * 
 * A single-header library to simplify the creation of Clue clients. Two paradigms are
 * available: a low level interface that only covers networking details, and a higher
 * level interface where your logic can be defined in a set of functions passed as
 * callbacks.
 *
 * In one source file, you must #define _CLUE_IMPL before including this file.
 */

#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//TODO win32?
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>

////////////
// MACROS //
////////////
#define CLUE_OK 0
#define CLUE_ERR 1
#define CLUE_CLOSED 2

////////////
// FRAMES //
////////////
#define FRAME_TYPE_UNKNOWN 0
typedef struct {
    int8_t type;
    int8_t _reserved[3];
    int32_t data_length;
    char data[0];
} FrameHeader_t;

#define FRAME_TYPE_DEBUG 1
typedef struct {
    int32_t error_length;
    char error[0];
} DebugFrame_t;

#define FRAME_TYPE_CONNECT 2
typedef struct {
    int8_t name_length;
    char name[0];
} ConnectFrame_t;

#define FRAME_TYPE_RULES 3
typedef struct {
    int8_t player_id;
    int8_t num_categories;
    int16_t num_cards;
    int16_t num_cards_in_category[0];
    struct {
        int8_t name_length;
        char name[0];
    } card_names[0];
} RulesFrame_t;

#define FRAME_TYPE_START 4
typedef struct {
    int16_t your_hand_size;
    int8_t num_players;
    int8_t _reserved;
    int16_t your_hand[0];
    int8_t player_order[0];
    int16_t player_hand_sizes[0];
    struct {
        int8_t name_length;
        char name[0];
    } player_names[0];
} StartFrame_t;

#define FRAME_TYPE_TURN 5
typedef struct {
    int8_t player_id;
} TurnFrame_t;

#define FRAME_TYPE_ACTION 6
typedef struct {
    int8_t player_id;
    int8_t responder_id;
    int8_t solving;
    int8_t _reserved;
    int16_t suggestion[0];
} ActionFrame_t;

#define FRAME_TYPE_REPLY 7
typedef struct {
    int8_t player_id;
    int8_t _reserved;
    int16_t card_id;
} ReplyFrame_t;

#define FRAME_TYPE_GAME_END 8
typedef struct {
    int8_t winner;
    int8_t won_by_default;
} GameEndFrame_t;

///////////////////////
// CLIENT STRUCTURES //
///////////////////////
typedef struct {
    int stream;
} ClueConnection_t;

typedef struct _ClueHighLevelClient_t {
    struct {
        char* name;
        void* user;
        // PARMS: message, message len
        void(*on_debug)(struct _ClueHighLevelClient_t*, char*, int);
        // PARMS: rules frame, is in native endianness
        void(*on_rules)(struct _ClueHighLevelClient_t*, RulesFrame_t* frame);
        // PARMS: start frame, is in native endianness
        void(*on_start)(struct _ClueHighLevelClient_t*, StartFrame_t* frame);
        // PARMS: player who has a turn (never us)
        void(*on_other_turn)(struct _ClueHighLevelClient_t*, int8_t);
        // PARMS: suggestion array (output), suggestion array len, solving or not (output)
        void(*on_our_turn)(struct _ClueHighLevelClient_t*, int16_t*, int, int*);
        // PARMS: player taking action, player responding, suggestion array, suggestion array len, solving or not
        void(*on_other_query)(struct _ClueHighLevelClient_t*, int8_t, int8_t, int16_t*, int, int);
        // PARMS: player asking us, suggestion array, suggestion array len, response (output)
        void(*on_query)(struct _ClueHighLevelClient_t*, int8_t, int16_t*, int, int16_t*);
        // PARMS: player asking, player responding, showed or not
        void(*on_other_reply)(struct _ClueHighLevelClient_t*, int8_t, int8_t, int);
        // PARMS: player responding, showed card
        void(*on_reply)(struct _ClueHighLevelClient_t*, int8_t, int16_t);
        // PARMS: winning player, won by default or not
        void(*on_end)(struct _ClueHighLevelClient_t*, int8_t, int);
    } user;
    struct {
        ClueConnection_t conn;
        int8_t our_id;
        int8_t num_categories;
        int8_t total_players;
        int8_t current_player;
        int16_t total_cards;
        int16_t* cards_in_categories;
        char** card_names;
        char** player_names;
    } priv;
} ClueHighLevelClient_t;

/**
 * @brief Run a client with the specified logic.
 * 
 * @param client A pointer to a ClueHighLevelClient_t which contains pointers to player logic functions
 * @param addr String hostname or IP address of the server
 * @param port String port of the server (getaddrinfo made me!)
 * @return int CLUE_OK, CLUE_ERR, or CLUE_CLOSED
 *
 * Once this function is called, it will not return until the client disconnects from the server, which
 * typically happens at the end of the game. The client will connect to the server using the name
 * specified in client->user.name.
 *
 * When the server broadcasts information, the client will run one of the functions in client->user.
 * The client pointer is always a parameter to those functions, and client->user.user is a pointer
 * that is available for your use. This function is single threaded and there are no race considerations
 * to be aware of.
 *
 * I would never impose restrictions on your personal freedom, but there is probably nothing to be gained
 * from looking at the fields in client->priv.
 *
 * This function uses malloc and will print to standard output. If that's a problem, you probably want
 * to be using the low level interface anyways...
 */
int clue_hl_run(ClueHighLevelClient_t* client, char* addr, char* port);

/**
 * @brief Get the name of a card given an ID.
 * 
 * @param client Pointer to the high level client which is holding the card names
 * @param card_id ID of the card to get the name of
 * @return char* Null terminated card name
 */
char* clue_hl_get_card_name(ClueHighLevelClient_t* client, int16_t card_id);

/**
 * @brief Get the name of a player given an ID.
 * 
 * @param client Pointer to the high level client which is holding the player names
 * @param player_id ID of the player to get the name of
 * @return char* Null terminated player name
 */
char* clue_hl_get_player_name(ClueHighLevelClient_t* client, int8_t player_id);

/**
 * @brief Open a connection to a given TCP address and port.
 * 
 * @param conn Pointer to a ClueConnection_t to hold the connection
 * @param addr IP or hostname of the server
 * @param port Port of the server
 * @return int CLUE_OK or CLUE_ERR
 *
 * Address resolution by getaddrinfo(). See manpages for details.
 */
int clue_ll_connect(ClueConnection_t* conn, char* addr, char* port);

/**
 * @brief Send some number of bytes across the connection.
 * 
 * @param conn Pointer to a ClueConnection_t to send the bytes on
 * @param buf The bytes to send
 * @param size The number of bytes to send
 * @return int CLUE_OK or CLUE_ERR
 *
 * Only returns CLUE_ERR if an input field is bad. Does not do any kind
 * of checking to make sure the connection is still alive.
 */
int clue_ll_send(ClueConnection_t* conn, void* buf, int size);

/**
 * @brief Get some number of bytes from the connection.
 * 
 * @param conn Pointer to a ClueConnection_t to get the bytes from
 * @param buf Destination buffer for the bytes
 * @param size The number of bytes to get
 * @return int CLUE_OK, CLUE_ERR, or CLUE_CLOSED
 *
 * CLUE_ERR comes back for invalid parms or if the server sends a
 * number of bytes less than you want to get. CLUE_CLOSED comes back
 * if there are no bytes to read at all (because the connection died).
 */
int clue_ll_recv(ClueConnection_t* conn, void* buf, int size);

/**
 * @brief Flip the endianness of the multibyte integers in a frame.
 * 
 * @param frame A pointer to the frame
 * @param frame_type The type of frame
 * @param frame_len The length of the frame
 * @return int CLUE_OK or CLUE_ERR
 *
 * The endianness is flipped in place. This function uses ntohs (and ntohl) so
 * this function is safe to use on big endian systems. frame_len is only required
 * for FRAME_TYPE_ACTION as it has a variable array that cannot be determined
 * without prior information.
 */
int clue_ll_flip_endianness(void* frame, int frame_type, int frame_len);

/**
 * @brief Helper function to dump bytes as hex into stdout.
 * 
 * @param buf Bytes to dump
 * @param len Length of bytes
 */
void clue_ll_hex(void* buf, int len);

//////////////////////////////
// FUNCTION IMPLEMENTATIONS //
//////////////////////////////
#ifdef _CLUE_IMPL

int clue_hl_run(ClueHighLevelClient_t* client, char* addr, char* port) {
    int rc;
    if (client == NULL || addr == NULL || port == NULL) {
        return CLUE_ERR;
    }
    rc = clue_ll_connect(&client->priv.conn, addr, port);
    if (rc != CLUE_OK) {
        printf("Failed to connect to %s:%s\n", addr, port);
        return rc;
    }

    // Heap memory so NULL indicates not allocated
    client->priv.card_names = NULL;
    client->priv.player_names = NULL;

    int name_length = strlen(client->user.name);
    FrameHeader_t connect_frame_header;
    connect_frame_header.type = FRAME_TYPE_CONNECT;
    connect_frame_header.data_length = sizeof(ConnectFrame_t) + name_length;
    clue_ll_flip_endianness(&connect_frame_header, FRAME_TYPE_UNKNOWN, 0);
    ConnectFrame_t connect_frame;
    connect_frame.name_length = name_length;
    clue_ll_send(&client->priv.conn, &connect_frame_header, sizeof(FrameHeader_t));
    clue_ll_send(&client->priv.conn, &connect_frame, sizeof(ConnectFrame_t));
    clue_ll_send(&client->priv.conn, client->user.name, name_length);

    int game_continues = 1;
    while (game_continues) {
        FrameHeader_t any_header;
        rc = clue_ll_recv(&client->priv.conn, &any_header, sizeof(FrameHeader_t));
        if (rc == CLUE_ERR) {
            printf("Server sent incomplete frame header\n");
            return CLUE_ERR;
        } else if (rc == CLUE_CLOSED) {
            printf("Server closed\n");
            return CLUE_CLOSED;
        }
        clue_ll_flip_endianness(&any_header, FRAME_TYPE_UNKNOWN, 0);
        
        char* frame = (char*)malloc(any_header.data_length);
        rc = clue_ll_recv(&client->priv.conn, frame, any_header.data_length);
        if (rc == CLUE_ERR) {
            printf("Server sent incomplete frame\n");
            return CLUE_ERR;
        } else if (rc == CLUE_CLOSED) {
            printf("Server closed\n");
            return CLUE_CLOSED;
        }
        clue_ll_flip_endianness(frame, any_header.type, any_header.data_length);

        switch (any_header.type) {
            case FRAME_TYPE_DEBUG: {
                DebugFrame_t* debug_frame = (DebugFrame_t*)frame;
                if (client->user.on_debug != NULL) {
                    client->user.on_debug(client, debug_frame->error, debug_frame->error_length);
                }
                break;
            }
            case FRAME_TYPE_RULES: {
                RulesFrame_t* rules_frame = (RulesFrame_t*)frame;
                client->priv.total_cards = rules_frame->num_cards;
                char* rules_card_names_start = ((char*)rules_frame) + sizeof(RulesFrame_t) + rules_frame->num_categories * sizeof(int16_t);
                int card_names_buffer_size = any_header.data_length - (sizeof(RulesFrame_t) + rules_frame->num_categories * sizeof(int16_t));
                client->priv.our_id = rules_frame->player_id;
                client->priv.num_categories = rules_frame->num_categories;
                client->priv.cards_in_categories = (int16_t*)malloc(rules_frame->num_categories * sizeof(int16_t));
                memcpy(client->priv.cards_in_categories, rules_frame->num_cards_in_category, rules_frame->num_categories * sizeof(int16_t));
                client->priv.card_names = (char**)malloc(rules_frame->num_cards * sizeof(char*));
                char* card_names = (char*)malloc(card_names_buffer_size);
                memcpy(card_names, rules_card_names_start + 1, card_names_buffer_size - 1);
                for (int card_idx = 0; card_idx < rules_frame->num_cards; card_idx++) {
                    client->priv.card_names[card_idx] = card_names;
                    card_names[*rules_card_names_start] = '\0';
                    card_names += *rules_card_names_start + 1;
                    rules_card_names_start += *rules_card_names_start + 1;
                }
                if (client->user.on_rules != NULL) {
                    client->user.on_rules(client, rules_frame);
                }
                break;
            }
            case FRAME_TYPE_START: {
                StartFrame_t* start_frame = (StartFrame_t*)frame;
                client->priv.total_players = start_frame->num_players;
                char* player_names_start = ((char*)start_frame) + sizeof(StartFrame_t)
                    + start_frame->your_hand_size * sizeof(int16_t)
                    + start_frame->num_players * (sizeof(int16_t) + sizeof(int8_t));
                int player_names_buffer_size = any_header.data_length - (player_names_start - (char*)start_frame);
                client->priv.player_names = (char**)malloc(start_frame->num_players * sizeof(char*));
                char* player_names = (char*)malloc(player_names_buffer_size);
                memcpy(player_names, player_names_start + 1, player_names_buffer_size - 1);
                for (int player_idx = 0; player_idx < start_frame->num_players; player_idx++) {
                    client->priv.player_names[player_idx] = player_names;
                    player_names[*player_names_start] = '\0';
                    player_names += *player_names_start + 1;
                    player_names_start += *player_names_start + 1;
                }
                if (client->user.on_start != NULL) {
                    client->user.on_start(client, start_frame);
                }
                break;
            }
            case FRAME_TYPE_TURN: {
                TurnFrame_t* turn_frame = (TurnFrame_t*)frame;
                client->priv.current_player = turn_frame->player_id;
                if (turn_frame->player_id == client->priv.our_id) {
                    int16_t suggestion[client->priv.num_categories];
                    int solving = 0;
                    if (client->user.on_our_turn != NULL) {
                        client->user.on_our_turn(client, suggestion, client->priv.num_categories, &solving);
                    } else {
                        printf("client->user.on_our_turn must be specified\n");
                    }
                    ActionFrame_t* action_frame = (ActionFrame_t*)malloc(sizeof(ActionFrame_t) + sizeof(suggestion));
                    memcpy(action_frame->suggestion, suggestion, sizeof(suggestion));
                    action_frame->solving = solving;
                    FrameHeader_t action_header;
                    action_header.type = FRAME_TYPE_ACTION;
                    action_header.data_length = sizeof(ActionFrame_t) + sizeof(suggestion);
                    clue_ll_flip_endianness(&action_header, FRAME_TYPE_UNKNOWN, 0);
                    clue_ll_flip_endianness(action_frame, FRAME_TYPE_ACTION, sizeof(ActionFrame_t) + sizeof(suggestion));
                    clue_ll_send(&client->priv.conn, &action_header, sizeof(FrameHeader_t));
                    clue_ll_send(&client->priv.conn, action_frame, sizeof(ActionFrame_t) + sizeof(suggestion));
                    free(action_frame);
                } else {
                    if (client->user.on_other_turn != NULL) {
                        client->user.on_other_turn(client, turn_frame->player_id);
                    }
                }
                break;
            }
            case FRAME_TYPE_ACTION: {
                ActionFrame_t* action_frame = (ActionFrame_t*)frame;
                if (action_frame->responder_id == client->priv.our_id) {
                    int16_t show_card = -1;
                    if (client->user.on_query != NULL) {
                        client->user.on_query(client, action_frame->player_id, action_frame->suggestion, client->priv.num_categories, &show_card);
                    } else {
                        printf("client->user.on_query must be specified\n");
                    }
                    ReplyFrame_t reply_frame;
                    reply_frame.card_id = show_card;
                    FrameHeader_t reply_header;
                    reply_header.type = FRAME_TYPE_REPLY;
                    reply_header.data_length = sizeof(ReplyFrame_t);
                    clue_ll_flip_endianness(&reply_header, FRAME_TYPE_UNKNOWN, 0);
                    clue_ll_flip_endianness(&reply_frame, FRAME_TYPE_REPLY, 0);
                    clue_ll_send(&client->priv.conn, &reply_header, sizeof(FrameHeader_t));
                    clue_ll_send(&client->priv.conn, &reply_frame, sizeof(ReplyFrame_t));
                } else if (action_frame->player_id != client->priv.our_id) {
                    if (client->user.on_other_query != NULL) {
                        client->user.on_other_query(client, action_frame->player_id, action_frame->responder_id,
                            action_frame->suggestion, client->priv.num_categories, action_frame->solving);
                    }
                }
                break;
            }
            case FRAME_TYPE_REPLY: {
                ReplyFrame_t* reply_frame = (ReplyFrame_t*)frame;
                if (client->priv.current_player == client->priv.our_id) {
                    if (client->user.on_reply != NULL) {
                        client->user.on_reply(client, reply_frame->player_id, reply_frame->card_id);
                    }
                } else {
                    if (client->user.on_other_reply != NULL) {
                        client->user.on_other_reply(client, reply_frame->player_id, client->priv.current_player, reply_frame->card_id == 0);
                    }
                }
                break;
            }
            case FRAME_TYPE_GAME_END: {
                GameEndFrame_t* end_frame = (GameEndFrame_t*)frame;
                if (client->user.on_end != NULL) {
                    client->user.on_end(client, end_frame->winner, end_frame->won_by_default);
                }
                game_continues = 0;
                break;
            }
            default: {
                printf("Received mystery frame %d, probably a bug\n", any_header.type);
                clue_ll_hex(frame, any_header.data_length);
            }
        }
    }

    if (client->priv.card_names) {
        free(client->priv.card_names[0]);
        free(client->priv.card_names);
        client->priv.card_names = NULL;
    }
    if (client->priv.player_names) {
        free(client->priv.player_names[0]);
        free(client->priv.player_names);
        client->priv.player_names = NULL;
    }

    return CLUE_OK;
}

char* clue_hl_get_card_name(ClueHighLevelClient_t* client, int16_t card_id) {
    if (client == NULL || card_id < 0 || card_id >= client->priv.total_cards) {
        return NULL;
    }
    return client->priv.card_names[card_id];
}

char* clue_hl_get_player_name(ClueHighLevelClient_t* client, int8_t player_id) {
    if (client == NULL || player_id < 0 || player_id >= client->priv.total_players) {
        return NULL;
    }
    return client->priv.player_names[player_id];
}

int clue_ll_connect(ClueConnection_t* conn, char* addr, char* port) {
    if (conn == NULL || addr == NULL || port == NULL) {
        return CLUE_ERR;
    }
    conn->stream = 0;
    int rc;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* addresses_head;
    rc = getaddrinfo(addr, port, &hints, &addresses_head);
    if (rc != 0) {
        return CLUE_ERR;
    }
    for (struct addrinfo* address = addresses_head; address != NULL; address = address->ai_next) {
        int fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd == -1) {
            continue;
        }
        if (connect(fd, address->ai_addr, address->ai_addrlen) != -1) {
            conn->stream = fd;
            break;
        }
        close(fd);
    }
    freeaddrinfo(addresses_head);
    if (conn->stream == 0) {
        return CLUE_ERR;
    }
    return CLUE_OK;
}

int clue_ll_send(ClueConnection_t* conn, void* buf, int size) {
    if (conn == NULL || buf == NULL || size <= 0) {
        return CLUE_ERR;
    }
    send(conn->stream, buf, size, MSG_DONTWAIT);
    return CLUE_OK;
}

int clue_ll_recv(ClueConnection_t* conn, void* buf, int size) {
    if (conn == NULL || buf == NULL || size <= 0) {
        return CLUE_ERR;
    }
    size_t received_len = recv(conn->stream, buf, size, MSG_WAITALL);
    if (received_len == -1) {
        return CLUE_CLOSED;
    } else if (received_len != size) {
        return CLUE_ERR;
    }
    return CLUE_OK;
}

int clue_ll_flip_endianness(void* frame, int frame_type, int frame_len) {
    if (frame == NULL) {
        return CLUE_ERR;
    }
    FrameHeader_t* frame_header = (FrameHeader_t*)frame;
    DebugFrame_t* debug_frame = (DebugFrame_t*)frame;
    RulesFrame_t* rules_frame = (RulesFrame_t*)frame;
    StartFrame_t* start_frame = (StartFrame_t*)frame;
    ActionFrame_t* action_frame = (ActionFrame_t*)frame;
    ReplyFrame_t* reply_frame = (ReplyFrame_t*)frame;
    switch (frame_type) {
        case FRAME_TYPE_CONNECT:
        case FRAME_TYPE_TURN:
        case FRAME_TYPE_GAME_END:
            break;
        case FRAME_TYPE_UNKNOWN:
            frame_header->data_length = ntohl(frame_header->data_length);
            break;
        case FRAME_TYPE_DEBUG:
            debug_frame->error_length = ntohl(debug_frame->error_length);
            break;
        case FRAME_TYPE_RULES:
            rules_frame->num_cards = ntohs(rules_frame->num_cards);
            for (int cat_idx = 0; cat_idx < rules_frame->num_categories; cat_idx++) {
                rules_frame->num_cards_in_category[cat_idx] = ntohs(rules_frame->num_cards_in_category[cat_idx]);
            }
            break;
        case FRAME_TYPE_START:
            start_frame->your_hand_size = ntohs(start_frame->your_hand_size);
            for (int hand_idx = 0; hand_idx < start_frame->your_hand_size; hand_idx++) {
                start_frame->your_hand[hand_idx] = ntohs(start_frame->your_hand[hand_idx]);
            }
            {
                int16_t* player_hand_sizes = (int16_t*)((char*)start_frame->your_hand 
                    + start_frame->your_hand_size * sizeof(int16_t) 
                    + start_frame->num_players * sizeof(int8_t));
                for (int player_idx = 0; player_idx < start_frame->num_players; player_idx++) {
                    player_hand_sizes[player_idx] = ntohs(player_hand_sizes[player_idx]);
                }
            }
            break;
        case FRAME_TYPE_ACTION:
            {
                int num_categories = (frame_len - sizeof(ActionFrame_t)) / sizeof (int16_t);
                for (int cat_idx = 0; cat_idx < num_categories; cat_idx++) {
                    action_frame->suggestion[cat_idx] = ntohs(action_frame->suggestion[cat_idx]);
                }
            }
            break;
        case FRAME_TYPE_REPLY:
            reply_frame->card_id = ntohs(reply_frame->card_id);
            break;
        default:
            return CLUE_ERR;
    }
    return CLUE_OK;
}

void clue_ll_hex(void* buf, int len) {
    printf("HEX DUMP:\n");
    char* bytes = (char*)buf;
    int x = 0;
    while (x < len) {
        printf("%02x", bytes[x]);
        x++;
        if (x % 8 == 0) {
            printf("\n");
        }
    }
    if (x % 8 != 0) {
        printf("\n");
    }
}

#endif
#endif