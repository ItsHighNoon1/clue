#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "frames.h"

#define NAME "Randy"

typedef struct {
    int player_id;
    int total_cards;
    char** card_names;
    int hand_size;
    int* hand;
    int num_categories;
    int* num_cards_in_category;
    int turns_played;
} Knowledge_t;

int connect_to_server(char** argv);
void handle_frame(Frame_t* header, int fd);

Knowledge_t knowledge;
FILE* debug_file = NULL;

int main(int argc, char** argv) {
    srand(time(0));

    // Zero out what we know about the game
    memset(&knowledge, 0, sizeof(knowledge));

    int rc;
    Frame_t header = {};

    if (argc < 3) {
        printf("Usage: ./randy <ip> <port>\n");
        exit(1);
    }
    int fd = connect_to_server(argv);
    if (argc > 3) {
        debug_file = fopen(argv[3], "w");
    }

    ConnectFrame_t connect_frame;
    connect_frame.name_length = strlen(NAME);
    header.type = FRAME_TYPE_CONNECT;
    header.data_length = sizeof(connect_frame) + strlen(NAME);

    send(fd, &header, sizeof(header), MSG_DONTWAIT); // Send header
    send(fd, &connect_frame, sizeof(connect_frame), MSG_DONTWAIT); // Send connect frame header
    send(fd, NAME, strlen(NAME), 0); // Send variable name length

    while (1) {
        int data_length = recv(fd, &header, sizeof(header), MSG_WAITALL);
        if (data_length < sizeof(header)) {
            if (data_length == -1 && errno == EAGAIN) {
                continue;
            } else if (data_length == -1) {
                perror(NULL);
                exit(1);
            }
            printf("Server sent incomplete frame\n");
            break;
        }
        handle_frame(&header, fd); // This consumes the rest of the stream
    }
    printf("Exited loop\n");
    
    close(fd);
    
    exit(0);
}

int connect_to_server(char** argv) {
    int rc;

    struct sockaddr_in6 address = {};
    address.sin6_family = AF_INET6;
    address.sin6_port = atoi(argv[2]);
    if (address.sin6_port == 0) {
        printf("%s not a valid port\n", argv[2]);
        exit(1);
    }
    rc = inet_pton(AF_INET6, argv[1], &address.sin6_addr);
    if (rc == 0) {
        printf("%s not a valid IP address\n", argv[1]);
        exit(1);
    } else if (rc == -1) {
        perror(NULL);
        exit(1);
    }

    int socket_fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (socket_fd == -1) {
        perror(NULL);
        exit(1);
    }
    rc = connect(socket_fd, (const struct sockaddr*)&address, sizeof(address));
    if (rc == -1) {
        perror(NULL);
        exit(1);
    }

    return socket_fd;
}

void handle_frame(Frame_t* header, int fd) {
    char* buffer = calloc(header->data_length, 1);
    size_t data_length = recv(fd, buffer, header->data_length, MSG_WAITALL);
    if (debug_file) {
        fwrite(header, sizeof(*header), 1, debug_file);
        fwrite(buffer, data_length, 1, debug_file);
    }
    if (data_length < header->data_length) {
        if (data_length == -1) {
            perror(NULL);
            exit(1);
        }
        printf("Server sent incomplete frame (type %d, %ld bytes), have to exit\n", header->type, data_length);
        if (debug_file) {
            fclose(debug_file);
            debug_file = NULL;
        }
        exit(0);
    }

    if (header->type == FRAME_TYPE_ERROR) {
        // Server sends this to us when we mess up. Print
        ErrorFrame_t* error = (ErrorFrame_t*)buffer;
        printf("Server reported error: %.*s\n", error->error_length, error->error);
        exit(1);
    } else if (header->type == FRAME_TYPE_ABORT) {
        // Server sends this to us when it messes up. Print
        AbortFrame_t* abort = (AbortFrame_t*)buffer;
        printf("Server aborted: %.*s\n", abort->error_length, abort->error);
        exit(0);
    } else if (header->type == FRAME_TYPE_RULES) {
        // Populate our knowledge with what we can
        RulesFrame_t* rules = (RulesFrame_t*)buffer;
        knowledge.player_id = rules->player_id;
        knowledge.total_cards = rules->num_cards;
        knowledge.num_categories = rules->num_categories;
        knowledge.num_cards_in_category = malloc(knowledge.num_categories * sizeof(int));
        int16_t* num_cards_in_category = (int16_t*)&rules->num_cards_in_category;
        for (int i = 0; i < knowledge.num_categories; i++) {
            knowledge.num_cards_in_category[i] = num_cards_in_category[i];
        }
        char* card_name_data = (char*)num_cards_in_category + rules->num_categories * sizeof(int16_t) + rules->num_cards * sizeof(int16_t);
        knowledge.card_names = malloc(knowledge.total_cards * sizeof(char*));
        for (int i = 0; i < knowledge.total_cards; i++) {
            int card_name_length = *card_name_data;
            card_name_data++;
            knowledge.card_names[i] = malloc(card_name_length + 1);
            memcpy(knowledge.card_names[i], card_name_data, card_name_length);
            knowledge.card_names[i][card_name_length] = '\0';
            card_name_data += card_name_length;
        }
        printf("Connected as player %d, %d categories, %d cards\n", rules->player_id, rules->num_categories, rules->num_cards);
    } else if (header->type == FRAME_TYPE_START) {
        // Since we are playing randomly, we don't care about the meta information, just our hand
        StartFrame_t* start = (StartFrame_t*)buffer;
        knowledge.hand_size = start->your_hand_size;
        knowledge.hand = malloc(knowledge.hand_size * sizeof(int));
        int16_t* my_hand = (int16_t*)&start->your_hand;
        printf("I got dealt:\n");
        for (int i = 0; i < knowledge.hand_size; i++) {
            knowledge.hand[i] = my_hand[i];
            assert(knowledge.card_names != NULL);
            printf("  %s\n", knowledge.card_names[knowledge.hand[i]]);
        }
    } else if (header->type == FRAME_TYPE_TURN) {
        TurnFrame_t* turn = (TurnFrame_t*)buffer;
        if (turn->player_id == knowledge.player_id) {
            // It is our turn
            printf("My turn\n");
            knowledge.turns_played++;

            if (knowledge.turns_played > 5) {
                // Yolo guess since 100 turns have happened and the game probably isn't ending
                Frame_t solve_attempt_header = {};
                solve_attempt_header.type = FRAME_TYPE_SOLVE_ATTEMPT;
                solve_attempt_header.data_length = sizeof(SolveAttemptFrame_t) + knowledge.num_categories * sizeof(int16_t);
                SolveAttemptFrame_t* solve_attempt = malloc(solve_attempt_header.data_length);
                int base_idx = 0;
                printf("Guessing: ");
                for (int i = 0; i < knowledge.num_categories; i++) {
                    solve_attempt->cards[i] = rand() % knowledge.num_cards_in_category[i] + base_idx;
                    base_idx += knowledge.num_cards_in_category[i];
                    printf("(%d) %s, ", solve_attempt->cards[i], knowledge.card_names[solve_attempt->cards[i]]);
                }
                printf("\n");
                send(fd, &solve_attempt_header, sizeof(solve_attempt_header), MSG_DONTWAIT);
                send(fd, solve_attempt, solve_attempt_header.data_length, MSG_DONTWAIT);
                free(solve_attempt);
            } else {
                // Make a random suggestion
                Frame_t suggestion_header = {};
                suggestion_header.type = FRAME_TYPE_TURN_RESPONSE;
                suggestion_header.data_length = sizeof(TurnResponseFrame_t) + knowledge.num_categories * sizeof(int16_t);
                TurnResponseFrame_t* suggestion = malloc(suggestion_header.data_length);
                int base_idx = 0;
                printf("Suggesting: ");
                for (int i = 0; i < knowledge.num_categories; i++) {
                    suggestion->suggestion[i] = rand() % knowledge.num_cards_in_category[i] + base_idx;
                    base_idx += knowledge.num_cards_in_category[i];
                    printf("(%d) %s, ", suggestion->suggestion[i], knowledge.card_names[suggestion->suggestion[i]]);
                }
                printf("\n");
                send(fd, &suggestion_header, sizeof(suggestion_header), MSG_DONTWAIT);
                send(fd, suggestion, suggestion_header.data_length, MSG_DONTWAIT);
                free(suggestion);
            }
        }
    } else if (header->type == FRAME_TYPE_QUERY) {
        QueryFrame_t* query = (QueryFrame_t*)buffer;

        // If not for us, ignore
        if (query->player_id == knowledge.player_id) {
            printf("I have to respond to: ");
            for (int i = 0; i < knowledge.num_categories; i++) {
                printf("(%d) %s, ", query->suggestion[i], knowledge.card_names[query->suggestion[i]]);
            }
            printf("\n");

            // Ok, it's possible the entire suggestion is in our hand, so build a list
            assert(knowledge.num_categories > 0);
            int16_t cards_held[knowledge.num_categories];
            int num_cards_held = 0;
            for (int i = 0; i < knowledge.hand_size; i++) {
                for (int j = 0; j < knowledge.num_categories; j++) {
                    if (knowledge.hand[i] == query->suggestion[j]) {
                        cards_held[num_cards_held++] = knowledge.hand[i];
                        break;
                    }
                }
            }
            if (num_cards_held == 0) {
                // We don't need to pass, the server will do it for us
            } else {
                // Now we can be random
                QueryResponseFrame_t query_response = {};
                query_response.card_id = cards_held[rand() % num_cards_held];
                Frame_t query_response_header = {};
                query_response_header.type = FRAME_TYPE_QUERY_RESPONSE;
                query_response_header.data_length = sizeof(query_response);
                printf("I am responding with (%d) %s\n", query_response.card_id, knowledge.card_names[query_response.card_id]);
                send(fd, &query_response_header, sizeof(query_response_header), MSG_DONTWAIT);
                send(fd, &query_response, query_response_header.data_length, MSG_DONTWAIT);
            }
        }
    } else if (header->type == FRAME_TYPE_QUERY_RETURN) {
        // Randy does not care about these (but you probably should!)
    } else if (header->type == FRAME_TYPE_SOLVE_RESULT) {
        // Randy really does not care about this... unless he wins
        SolveResultFrame_t* result = (SolveResultFrame_t*)buffer;
        if (result->player == knowledge.player_id && result->correct) {
            printf("gg id like to thank monte carlo for this victory\n");
        }
    } else {
        printf("Unhandled frame %d\n", header->type);
    }

    free(buffer);
}