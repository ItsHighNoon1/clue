#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include "frames.h"

typedef struct {
    uint16_t port;
    int8_t num_categories;
    int16_t* num_cards;
    char*** card_names;
} Settings_t;

typedef struct {
    int fd;
    int eliminated;
    struct sockaddr_in6 address;
    int8_t id;
    int8_t name_length;
    char* name;
    int16_t hand_size;
    int16_t* hand;
} Player_t;

#define SERVER_LOBBY_WAIT_TIME 10
#define SERVER_SOCKET_TIMEOUT 3

Settings_t* read_settings_file(char* file_path); // Read settings.txt into the Settings_t structure
int open_socket(uint16_t port); // Open TCP server socket on specified port and return fd
void send_error_frame(int fd, const char* reason); // Send FRAME_TYPE_ERROR to a certain client fd
Player_t* get_players(int fd, RulesFrame_t* rules, int rules_len, int* num_players); // Wait for SERVER_LOBBY_WAIT_TIME seconds for players to connect
void handle_sigint(int signum); // Handle SIGINT by exiting to clean up sockets
void start_game(Settings_t* settings, char** card_names, int total_cards, Player_t* players, int num_players); // Setup the game
void shuffle(void* arr, int n, size_t size); // Fisher-Yates shuffle
void run_game(Settings_t* settings, char** card_names, int16_t* solution, Player_t* players, int num_players); // Setup complete, begin player 1 turn
void abort_game(Player_t* players, int num_players, const char* reason); // Send abort frame to everyone
int qsort_int16s(const void* left, const void* right);
int player_has_card(Player_t* player, int16_t card);

int main(int argc, char** argv) {
    signal(SIGINT, handle_sigint);

    // Read the settings file
    char* config_file = "settings.txt";
    if (argc >= 2) {
        config_file = argv[1];
    }
    Settings_t* settings = read_settings_file(config_file);
    if (settings == NULL) {
        printf("Failed while reading settings file\n");
        exit(1);
    }

    // Open server socket
    int sock_fd = open_socket(settings->port);
    if (sock_fd == -1) {
        printf("Failed to open socket\n");
        exit(1);
    }

    // Print out info about the game and sum up the lengths of the card names
    int all_names_len = 0;
    int total_cards = 0;
    printf("Started server on port %d\n", settings->port);
    for (int i = 0; i < settings->num_categories; i++) {
        printf("\nCategory %d (%d cards)\n", i, settings->num_cards[i]);
        for (int j = 0; j < settings->num_cards[i]; j++) {
            total_cards++;
            all_names_len += strlen(settings->card_names[i][j]);
            printf("%s\n", settings->card_names[i][j]);
        }
    }
    printf("\n");

    // Prepare the rules frame for anyone who connects
    char** card_names_debug = malloc(total_cards * sizeof(char*));
    int rules_len = sizeof(RulesFrame_t) + settings->num_categories * sizeof(int16_t) + total_cards * sizeof(int16_t) + total_cards + all_names_len;
    RulesFrame_t* rules = malloc(rules_len);
    int16_t* rules_category_sizes = (int16_t*)&rules->num_cards_in_category;
    int16_t* rules_category_card_ids = (int16_t*)((char*)rules_category_sizes + settings->num_categories * sizeof(int16_t));
    char* rules_card_names = (char*)rules_category_card_ids + total_cards * sizeof(int16_t);
    rules->player_id = 0;
    rules->num_categories = settings->num_categories;
    rules->num_cards = total_cards;
    int card_idx = 0;
    for (int i = 0; i < settings->num_categories; i++) {
        rules_category_sizes[i] = settings->num_cards[i];
        for (int j = 0; j < settings->num_cards[i]; j++) {
            rules_category_card_ids[card_idx] = card_idx; // Only after writing this do I realize it's unnecessary... but it's good QoL
            card_names_debug[card_idx] = settings->card_names[i][j];
            card_idx++;
            int8_t name_length = strlen(settings->card_names[i][j]);
            *rules_card_names = name_length;
            rules_card_names++;
            memcpy(rules_card_names, settings->card_names[i][j], name_length);
            rules_card_names += name_length;
        }
    }

    // Allow some players to connect before the game begins
    listen(sock_fd, 127);
    printf("Waiting for players...\n");
    int num_players;
    Player_t* players = get_players(sock_fd, rules, rules_len, &num_players);
    if (num_players <= 0) {
        printf("No players connected!\n");
        exit(0);
    }
    printf("\n");

    // Now start the game
    printf("Starting game\n");
    start_game(settings, card_names_debug, total_cards, players, num_players);

    // We don't actually need to free since the OS will do it for us
    // but I want to visualize what is allocated
    for (int i = 0; i < num_players; i++) {
        close(players[i].fd);
        free(players[i].name);
    }
    free(players);
    free(rules);
    free(card_names_debug);
    for (int i = 0; i < settings->num_categories; i++) {
        for (int j = 0; j < settings->num_cards[i]; j++) {
            free(settings->card_names[i][j]);
        }
        free(settings->card_names[i]);
    }
    free(settings->card_names); // 3 layers of allocation wow
    free(settings->num_cards);
    free(settings);

    printf("Server shut down gracefully\n");
    exit(0);
}

Settings_t* read_settings_file(char* file_path) {
    FILE* fp = fopen(file_path, "r");
    if (fp == NULL) {
        perror(NULL);
        return NULL;
    }
    Settings_t* settings = malloc(sizeof(Settings_t));
    char line[256];
    int line_number = 0;
    
    // The first line of the file should be the port
    if (fgets(line, sizeof(line), fp) == NULL) {
        printf("Expected integer port number on line %d\n", line_number);
        free(settings);
        return NULL;
    }
    line_number++;
    settings->port = atoi(line);
    if (settings->port == 0) {
        printf("Expected integer port number on line %d\n", line_number);
        free(settings);
        return NULL;
    }

    // Then there should be an empty line
    if (fgets(line, sizeof(line), fp) == NULL) {
        printf("Expected blank line on line %d\n", line_number);
        free(settings);
        return NULL;
    }
    line_number++;
    if (strlen(line) > 1) {
        printf("Expected blank line on line %d\n", line_number);
        free(settings);
        return NULL;
    }

    // Now we need to set up the categories
    int total_cards = 0;
    int categories_len = 0;
    int categories_size = 10;
    int16_t* categories_lengths = malloc(categories_size * sizeof(int16_t));
    char*** categories_names = malloc(categories_size * sizeof(char**));
    int category_len = 0;
    int category_size = 10;
    char** category_names = malloc(category_size * sizeof(char*));
    while (1) {
        if (fgets(line, sizeof(line), fp) == NULL) {
            // Done with the file
            break;
        }
        line_number++;
        int line_length = strlen(line);
        if (line_length <= 1) {
            // The line is blank, advance to the next category
            if (category_len == 0) {
                continue;
            }
            if (categories_len >= categories_size) {
                categories_size *= 2;
                categories_lengths = realloc(categories_lengths, categories_size * sizeof(int16_t));
                categories_names = realloc(categories_names, categories_size * sizeof(char**));
            }
            categories_lengths[categories_len] = category_len;
            categories_names[categories_len] = realloc(category_names, category_len * sizeof(char*));
            categories_len++;
            category_len = 0;
            category_size = 10;
            category_names = malloc(category_size * sizeof(char*));
        } else if (line_length > 127) {
            // Too long to store the length in a int8
            printf("Card name too long on line %d (max length 127)\n", line_number);
        } else {
            // Add to current category
            if (category_len >= category_size) {
                category_size *= 2;
                category_names = realloc(category_names, category_size * sizeof(char*));
            }
            char* new_string = malloc(line_length);
            memcpy(new_string, line, line_length);
            new_string[line_length - 1] = '\0';
            category_names[category_len] = new_string;
            category_len++;
            total_cards++;
        }
    }

    if (category_len > 0) {
        if (categories_len >= categories_size) {
            categories_size *= 2;
            categories_lengths = realloc(categories_lengths, categories_size * sizeof(int16_t));
            categories_names = realloc(categories_names, categories_size * sizeof(char**));
        }
        categories_lengths[categories_len] = category_len;
        categories_names[categories_len] = realloc(category_names, category_len * sizeof(char*));
        categories_len++;
    } else {
        free(category_names);
    }

    fclose(fp);

    if (total_cards > 0x7FFF) {
        // Too big for an int16
        printf("Too many cards %d (maximum 32767)\n", total_cards);
    }

    settings->num_categories = categories_len;
    settings->num_cards = categories_lengths;
    settings->card_names = categories_names;
    
    return settings;
}

int open_socket(uint16_t port) {
    int rc;
    int socket_fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (socket_fd == -1) {
        perror(NULL);
        return -1;
    }
    int opt_false = 0;
    rc = setsockopt(socket_fd, IPPROTO_IPV6, IPV6_V6ONLY, &opt_false, sizeof(opt_false));
    if (rc == -1) {
        perror(NULL);
        return -1;
    }
    struct timeval timeout;
    timeout.tv_sec = SERVER_SOCKET_TIMEOUT;
    timeout.tv_usec = 0;
    rc = setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    if (rc == -1) {
        perror(NULL);
        return -1;
    }
    struct sockaddr_in6 socket_address = {
        AF_INET6, // sin6_family
        port, // sin6_port
        0, // sin6_flowinfo
        IN6ADDR_ANY_INIT, // sin6_addr
        0 // sin6_scope_id
    };
    rc = bind(socket_fd, (const struct sockaddr*)&socket_address, sizeof(socket_address));
    if (rc == -1) {
        perror(NULL);
        return -1;
    }
    return socket_fd;
}

void send_error_frame(int fd, const char* reason) {
    // Don't bother waiting
    printf("Sending error frame: %s\n", reason);
    Frame_t header = {};
    ErrorFrame_t error = {};
    error.error_length = strlen(reason);
    header.type = FRAME_TYPE_ERROR;
    header.data_length = sizeof(error) + error.error_length;
    send(fd, &header, sizeof(header), MSG_DONTWAIT);
    send(fd, &error, sizeof(error), MSG_DONTWAIT);
    send(fd, reason, error.error_length, 0); // Going to block on this because the game is probably about to abort (read: close the socket)
}

Player_t* get_players(int fd, RulesFrame_t* rules, int rules_len, int* num_players) {
    *num_players = 0;
    int size_players = 10;
    Player_t* players = malloc(sizeof(Player_t) * size_players);
    time_t begin_time;
    begin_time = time(0) + SERVER_LOBBY_WAIT_TIME;
    while (time(0) < begin_time && *num_players < 128) {
        struct sockaddr_in6 client_address;
        socklen_t client_address_length = sizeof(client_address);
        int client_fd = accept(fd, (struct sockaddr*)&client_address, &client_address_length);
        if (client_fd == -1) {
            if (errno == EAGAIN) {
                // Accept timed out, expected since we block
                continue;
            }
            perror(NULL);
            exit(1);
        }

        // Got a real connection, await a connect frame
        Frame_t frame_header;
        size_t data_len = recv(client_fd, &frame_header, sizeof(frame_header), MSG_WAITALL);
        if (data_len < sizeof(frame_header)) {
            if (data_len == -1 && errno == EAGAIN) {  
                send_error_frame(client_fd, "Timed out");
            } else {
                send_error_frame(client_fd, "Incomplete frame header");
            }
            close(client_fd);
            continue;
        }
        ConnectFrame_t connect_frame;
        data_len = recv(client_fd, &connect_frame, sizeof(connect_frame), MSG_WAITALL);
        if (data_len < sizeof(connect_frame)) {
            if (data_len == -1 && errno == EAGAIN) {  
                send_error_frame(client_fd, "Timed out");
            } else {
                send_error_frame(client_fd, "Incomplete connect frame");
            }
            close(client_fd);
            continue;
        }
        if (connect_frame.name_length < 0) {
            send_error_frame(client_fd, "Negative name length not allowed");
            close(client_fd);
            continue;
        }
        char* player_name = malloc(connect_frame.name_length + 1);
        data_len = recv(client_fd, player_name, connect_frame.name_length, MSG_WAITALL);
        if (data_len < connect_frame.name_length) {
            if (data_len == -1 && errno == EAGAIN) {  
                send_error_frame(client_fd, "Timed out");
            } else {
                send_error_frame(client_fd, "Incomplete connect frame name");
            }
            close(client_fd);
            free(player_name);
            continue;
        }
        if (strnlen(player_name, connect_frame.name_length) < connect_frame.name_length) {
            // This guy thinks he's really funny sending a null character in the name
            send_error_frame(client_fd, "Null character not allowed in name");
            close(client_fd);
            free(player_name);
            continue;
        }
        player_name[connect_frame.name_length] = '\0';

        // Got a full connect frame, send rules and add the player to the list
        Frame_t rules_frame_header = {};
        rules_frame_header.type = FRAME_TYPE_RULES;
        rules_frame_header.data_length = rules_len;
        send(client_fd, &rules_frame_header, sizeof(rules_frame_header), MSG_DONTWAIT);
        rules->player_id = *num_players;
        if (send(client_fd, rules, rules_len, 0) < 0) {
            perror(NULL);
            close(client_fd);
            free(player_name);
            continue;
        }

        char ip_tmp[128];
        inet_ntop(AF_INET6, &client_address.sin6_addr, ip_tmp, sizeof(struct in6_addr));
        printf("%s connected from %s %d\n", player_name, ip_tmp, client_address.sin6_port);
        if (*num_players >= size_players) {
            size_players *= 2;
            players = realloc(players, size_players * sizeof(Player_t));
        }
        players[*num_players].fd = client_fd;
        players[*num_players].eliminated = 0;
        players[*num_players].name_length = connect_frame.name_length;
        players[*num_players].name = player_name;
        players[*num_players].id = *num_players;
        players[*num_players].address = client_address;
        (*num_players)++;
    }

    return realloc(players, *num_players * sizeof(Player_t));
}

void handle_sigint(int signum) {
    // Tired of the port being bound
    exit(0);
}

void start_game(Settings_t* settings, char** card_names, int total_cards, Player_t* players, int num_players) {
    srand(time(0));
    assert(settings->num_categories > 0);
    assert(total_cards - settings->num_categories > 0);

    // Pick out the cards that are in the solution and put the rest in the deck
    printf("Solution: ");
    int16_t base_idx = 0;
    int16_t solution[settings->num_categories];
    int deck_len = 0;
    int16_t deck[total_cards - settings->num_categories];
    for (int i = 0; i < settings->num_categories; i++) {
        // Choose the solution for this card
        solution[i] = base_idx + rand() % settings->num_cards[i];
        if (i == settings->num_categories - 1) {
            printf("(%d) %s\n", solution[i], card_names[solution[i]]);
        } else {
            printf("(%d) %s, ", solution[i], card_names[solution[i]]);
        }
        
        // Put the rest in the deck
        for (int j = 0; j < settings->num_cards[i]; j++) {
            if (base_idx + j == solution[i]) {
                continue;
            }
            deck[deck_len++] = base_idx + j;
        }

        base_idx += settings->num_cards[i];
    }
    assert(deck_len == total_cards - settings->num_categories);

    // Shuffle the deck and shuffle the player order. Also need to total player name lengths
    int total_player_name_length = 0;
    shuffle(deck, deck_len, sizeof(int16_t));
    shuffle(players, num_players, sizeof(Player_t));
    for (int i = 0; i < num_players; i++) {
        players[i].hand = malloc((deck_len / num_players + 1) * sizeof(int16_t));
        total_player_name_length += players[i].name_length;
    }

    // Deal the player hands
    int deal_idx = 0;
    for (int i = 0; i < deck_len; i++) {
        players[deal_idx].hand[players[deal_idx].hand_size++] = deck[i];
        deal_idx++;
        deal_idx = deal_idx % num_players;
    }

    // Send everyone the game start frame which is personalized
    for (int i = 0; i < num_players; i++) {
        // We are going to sneak and sort the player's hand here to make things easier
        qsort(players[i].hand, players[i].hand_size, sizeof(int16_t), qsort_int16s);

        printf("(%d) %s's hand:\n", players[i].id, players[i].name);
        for (int j = 0; j < players[i].hand_size; j++) {
            printf("  (%d) %s\n", players[i].hand[j], card_names[players[i].hand[j]]);
        }

        Frame_t header = {};
        header.type = FRAME_TYPE_START;
        header.data_length = sizeof(StartFrame_t) +
            sizeof(int16_t) * players[i].hand_size + // your_hand
            sizeof(int8_t) * num_players + // num_players
            sizeof(int16_t) * num_players + // player_hand_sizes
            sizeof(int8_t) * num_players + // name_length
            total_player_name_length; // name

        StartFrame_t* start_frame = calloc(header.data_length, 1);
        start_frame->your_hand_size = players[i].hand_size;
        start_frame->num_players = num_players;
        int16_t* your_hand = (int16_t*)&start_frame->your_hand;
        int8_t* player_order = (int8_t*)((char*)your_hand + players[i].hand_size * sizeof(int16_t));
        int16_t* player_hand_sizes = (int16_t*)((char*)player_order + num_players * sizeof(int8_t));
        char* player_names = (char*)player_hand_sizes + sizeof(int16_t) * num_players;

        memcpy(your_hand, players[i].hand, sizeof(int16_t) * players[i].hand_size);
        for (int j = 0; j < num_players; j++) {
            player_order[j] = players[j].id;
            player_hand_sizes[j] = players[j].hand_size;
            *player_names++ = players[j].name_length;
            memcpy(player_names, players[j].name, players[j].name_length);
            player_names += players[j].name_length;
        }

        send(players[i].fd, &header, sizeof(header), MSG_DONTWAIT);
        if (send(players[i].fd, start_frame, header.data_length, 0) < 0) {
            abort_game(players, num_players, "Player disconnected");
        }
        free(start_frame);
    }

    run_game(settings, card_names, solution, players, num_players);
}

void shuffle(void* arr, int n, size_t size) {
    // Shuffle array in place via Fisher-Yates
    char tmp[size];
    for (int i = 0; i < n; i++) {
        int idx = rand() % (i + 1);
        memcpy(tmp, (char*)arr + size * idx, size);
        memcpy((char*)arr + size * idx, (char*)arr + size * i, size);
        memcpy((char*)arr + size * i, tmp, size);
    }
}

void run_game(Settings_t* settings, char** card_names, int16_t* solution, Player_t* players, int num_players) {
    int turn_idx = -1; // Since we index at the start
    int received_size;
    while (1) {
        turn_idx++;
        turn_idx = turn_idx % num_players;

        // First: are all the players eliminated?
        int not_eliminated = 0;
        for (int i = 0; i < num_players; i++) {
            if (!players[i].eliminated) {
                not_eliminated++;
            }
        }
        if (not_eliminated == 0) {
            abort_game(players, num_players, "All players eliminated");
        }

        // The game continues. Skip anyone who is eliminated
        while (players[turn_idx].eliminated) {
            turn_idx++;
            turn_idx = turn_idx % num_players;
        }

        // It's someones turn. Tell everyone and await their response
        printf("(%d) %s's turn\n", players[turn_idx].id, players[turn_idx].name);
        TurnFrame_t turn_frame = {};
        turn_frame.player_id = players[turn_idx].id;
        Frame_t turn_frame_header = {};
        turn_frame_header.type = FRAME_TYPE_TURN;
        turn_frame_header.data_length = sizeof(turn_frame);
        for (int i = 0; i < num_players; i++) {
            // Note: we won't bother checking for send timeouts, only receive timeouts
            send(players[i].fd, &turn_frame_header, sizeof(turn_frame_header), MSG_DONTWAIT);
            send(players[i].fd, &turn_frame, turn_frame_header.data_length, MSG_DONTWAIT);
        }

        // We expect to get their response
        Frame_t turn_response_frame_header = {};
        received_size = recv(players[turn_idx].fd, &turn_response_frame_header, sizeof(turn_response_frame_header), 0);
        if (received_size < (int)sizeof(turn_response_frame_header)) {
            if (received_size == -1) {
                if (errno == EAGAIN) {
                    send_error_frame(players[turn_idx].fd, "Timed out");
                } else {
                    perror(NULL);
                }
            } else {
                send_error_frame(players[turn_idx].fd, "Incomplete frame header");
            }
            abort_game(players, num_players, "Communication error");
        }

        // They can either take a stab at the answer...
        if (turn_response_frame_header.type == FRAME_TYPE_SOLVE_ATTEMPT) {
            int expectected_len = settings->num_categories * sizeof(int16_t);
            int16_t* client_guess = malloc(expectected_len);
            received_size = recv(players[turn_idx].fd, client_guess, expectected_len, 0);
            if (received_size < (int)expectected_len || expectected_len != turn_response_frame_header.data_length) {
                if (received_size == -1) {
                    if (errno == EAGAIN) {
                        send_error_frame(players[turn_idx].fd, "Timed out");
                    } else {
                        perror(NULL);
                    }
                } else {
                    send_error_frame(players[turn_idx].fd, "Incomplete solution attempt");
                }
                // Technically recoverable
                continue;
            }

            printf("(%d) %s attempted to solve: ", players[turn_idx].id, players[turn_idx].name);
            int wrong = 0;
            for (int i = 0; i < settings->num_categories; i++) {
                if (i == settings->num_categories - 1) {
                    printf("(%d) %s\n", client_guess[i], card_names[client_guess[i]]);
                } else {
                    printf("(%d) %s, ", client_guess[i], card_names[client_guess[i]]);
                }

                // n^2 because lazy and probably faster than sorting
                int found = 0;
                for (int j = 0; j < settings->num_categories; j++) {
                    if (solution[i] == client_guess[j]) {
                        found++;
                        break;
                    }
                }
                if (found == 0) {
                    wrong = 1;
                }
            }

            Frame_t solve_broadcast_frame_header = {};
            solve_broadcast_frame_header.type = FRAME_TYPE_SOLVE_RESULT;
            solve_broadcast_frame_header.data_length = sizeof(SolveResultFrame_t) + settings->num_categories * sizeof(int16_t);
            SolveResultFrame_t* solve_broadcast_frame = malloc(solve_broadcast_frame_header.data_length);
            solve_broadcast_frame->player = players[turn_idx].id;
            solve_broadcast_frame->correct = wrong ? 0 : 1;
            memcpy(solve_broadcast_frame->cards, client_guess, settings->num_categories * sizeof(int16_t));
            for (int i = 0; i < num_players; i++) {
                send(players[i].fd, &solve_broadcast_frame_header, sizeof(solve_broadcast_frame_header), MSG_DONTWAIT);
                send(players[i].fd, &solve_broadcast_frame, solve_broadcast_frame_header.data_length, MSG_DONTWAIT);
            }
            if (!wrong) {
                printf("(%d) %s won!\n", players[turn_idx].id, players[turn_idx].name);
                break;
            } else {
                printf("(%d) %s was eliminated\n", players[turn_idx].id, players[turn_idx].name);
                players[turn_idx].eliminated = 1;
            }
        }
        // or do a suggestion
        else if (turn_response_frame_header.type == FRAME_TYPE_TURN_RESPONSE) {
            int expectected_len = settings->num_categories * sizeof(int16_t);
            int16_t* client_suggestion = malloc(expectected_len);
            received_size = recv(players[turn_idx].fd, client_suggestion, expectected_len, 0);
            if (received_size < (int)expectected_len || expectected_len != turn_response_frame_header.data_length) {
                if (received_size == -1) {
                    if (errno == EAGAIN) {
                        send_error_frame(players[turn_idx].fd, "Timed out");
                    } else {
                        perror(NULL);
                    }
                } else {
                    send_error_frame(players[turn_idx].fd, "Incomplete suggestion");
                }
                // Technically recoverable
                continue;
            }

            // This time we are going to sort the client input for validation purposes
            qsort(client_suggestion, settings->num_categories, sizeof(int16_t), qsort_int16s);

            // Did the client supply a valid suggestion?
            printf("(%d) %s suggests: ", players[turn_idx].id, players[turn_idx].name);
            int base_idx = 0;
            int legal = 1;
            for (int i = 0; i < settings->num_categories; i++) {
                if (i == settings->num_categories - 1) {
                    printf("(%d) %s\n", client_suggestion[i], card_names[client_suggestion[i]]);
                } else {
                    printf("(%d) %s, ", client_suggestion[i], card_names[client_suggestion[i]]);
                }
                int offset_in_category = client_suggestion[i] - base_idx;
                if (offset_in_category < 0 || offset_in_category >= settings->num_cards[i]) {
                    // No lol
                    legal = 0;
                }
                base_idx += settings->num_cards[i];
            }
            if (!legal) {
                printf("But it was illegal...\n");
                send_error_frame(players[turn_idx].fd, "Not one card per category suggested");
                continue;
            }

            // The suggestion is valid... go around
            for (int suggestion_turn_idx = (turn_idx + 1) % num_players; suggestion_turn_idx != turn_idx; suggestion_turn_idx = (suggestion_turn_idx + 1) % num_players) {
                Frame_t query_frame_header = {};
                query_frame_header.data_length = sizeof(QueryFrame_t) + sizeof(int16_t) * settings->num_categories;
                query_frame_header.type = FRAME_TYPE_QUERY;
                QueryFrame_t* query_frame = malloc(query_frame_header.data_length);
                query_frame->player_id = players[suggestion_turn_idx].id;
                memcpy(query_frame->suggestion, client_suggestion, settings->num_categories * sizeof(int16_t));
                for (int i = 0; i < num_players; i++) {
                    send(players[i].fd, &query_frame_header, sizeof(query_frame_header), MSG_DONTWAIT);
                    send(players[i].fd, query_frame, query_frame_header.data_length, MSG_DONTWAIT);
                }
                free(query_frame);

                int has_one = 0;
                for (int i = 0; i < settings->num_categories; i++) {
                    if (player_has_card(&players[suggestion_turn_idx], client_suggestion[i])) {
                        has_one = 1;
                        break;
                    }
                }
                if (has_one) {
                    // This player has a card and we need to ask them which one they want to show
                    printf("(%d) %s is obligated to show\n", players[suggestion_turn_idx].id, players[suggestion_turn_idx].name);

                    // And they respond
                    Frame_t query_response_frame_header = {};
                    QueryResponseFrame_t query_response_frame = {};
                    received_size = recv(players[suggestion_turn_idx].fd, &query_response_frame_header, sizeof(query_response_frame_header), 0);
                    if (received_size < sizeof(query_response_frame_header)) {
                        if (received_size == -1) {
                            if (errno == EAGAIN) {
                                send_error_frame(players[suggestion_turn_idx].fd, "Timed out");
                            } else {
                                perror(NULL);
                            }
                        } else {
                            send_error_frame(players[suggestion_turn_idx].fd, "Obligated to respond");
                        }
                        // Bricked
                        abort_game(players, num_players, "Player failed to respond to suggestion");
                    }
                    assert(query_response_frame_header.data_length == sizeof(query_response_frame));
                    received_size = recv(players[suggestion_turn_idx].fd, &query_response_frame, sizeof(query_response_frame_header), 0);
                    if (received_size < query_response_frame_header.data_length) {
                        if (received_size == -1) {
                            if (errno == EAGAIN) {
                                send_error_frame(players[suggestion_turn_idx].fd, "Timed out");
                            } else {
                                perror(NULL);
                            }
                        } else {
                            send_error_frame(players[suggestion_turn_idx].fd, "Incomplete query response");
                        }
                        // Bricked
                        abort_game(players, num_players, "Player failed to respond to suggestion");
                    }

                    // Do they actually have that card?
                    if (!player_has_card(&players[suggestion_turn_idx], query_response_frame.card_id)) {
                        printf("(%d) %s tried to cheat by showing (%d) %s\n",
                            players[suggestion_turn_idx].id, players[suggestion_turn_idx].name, query_response_frame.card_id, card_names[query_response_frame.card_id]);
                        abort_game(players, num_players, "Player responded to a suggestion illegally");
                    }
                    printf("(%d) %s shows (%d) %s\n",
                            players[suggestion_turn_idx].id, players[suggestion_turn_idx].name, query_response_frame.card_id, card_names[query_response_frame.card_id]);

                    // This player has a card so we will broadcast that
                    QueryAnouncementFrame_t show_frame = {};
                    show_frame.player_id = players[suggestion_turn_idx].id;
                    show_frame.card_id = -1;
                    Frame_t show_frame_header = {};
                    show_frame_header.type = FRAME_TYPE_QUERY_RETURN;
                    show_frame_header.data_length = sizeof(show_frame);
                    for (int i = 0; i < num_players; i++) {
                        if (i == suggestion_turn_idx) {
                            // No need to poke the shower
                            continue;
                        } else if (i == turn_idx) {
                            show_frame.card_id = query_response_frame.card_id;
                        } else {
                            show_frame.card_id = 0;
                        }
                        send(players[i].fd, &show_frame_header, sizeof(show_frame_header), MSG_DONTWAIT);
                        send(players[i].fd, &show_frame, show_frame_header.data_length, MSG_DONTWAIT);
                    }
                    break;
                } else {
                    // This player doesn't have a card so we will broadcast that
                    printf("(%d) %s passed\n", players[suggestion_turn_idx].id, players[suggestion_turn_idx].name);
                    QueryAnouncementFrame_t noshow_frame = {};
                    noshow_frame.player_id = players[suggestion_turn_idx].id;
                    noshow_frame.card_id = -1;
                    Frame_t noshow_frame_header = {};
                    noshow_frame_header.type = FRAME_TYPE_QUERY_RETURN;
                    noshow_frame_header.data_length = sizeof(noshow_frame);
                    for (int i = 0; i < num_players; i++) {
                        send(players[i].fd, &noshow_frame_header, sizeof(noshow_frame_header), MSG_DONTWAIT);
                        send(players[i].fd, &noshow_frame, noshow_frame_header.data_length, MSG_DONTWAIT);
                    }
                }
            }
        }
        // or they messed up
        else {
            printf("(%d) %s sent bad frame %d\n", players[turn_idx].id, players[turn_idx].name, turn_response_frame_header.type);
            send_error_frame(players[turn_idx].fd, "Expected either FRAME_TYPE_TURN_RESPONSE or FRAME_TYPE_SOLVE_ATTEMPT");
            char* garbage = malloc(turn_response_frame_header.data_length);
            recv(players[turn_idx].fd, garbage, turn_response_frame_header.data_length, 0); // Really wish I could just NULL here...
            free(garbage);

            // Technically recoverable
            continue;
        }
    }

    abort_game(players, num_players, "Game ended");
}

void abort_game(Player_t* players, int num_players, const char* reason) {
    Frame_t header = {};
    ErrorFrame_t error = {};
    error.error_length = strlen(reason);
    header.type = FRAME_TYPE_ABORT;
    header.data_length = sizeof(error) + error.error_length;
    for (int i = 0; i < num_players; i++) {
        send(players[i].fd, &header, sizeof(header), MSG_DONTWAIT);
        send(players[i].fd, &error, sizeof(error), MSG_DONTWAIT);
        send(players[i].fd, reason, error.error_length, MSG_DONTWAIT);
    }
    printf("Aborting game with reason: %s\n", reason);
    exit(1);
}

int qsort_int16s(const void* left, const void* right) {
    // Lame
    int16_t* left_int = (int16_t*)left;
    int16_t* right_int = (int16_t*)right;
    return *left_int - *right_int;
}

int player_has_card(Player_t* player, int16_t card) {
    // Does the player have the card?
    int low_idx = 0;
    int high_idx = player->hand_size - 1;
    while (low_idx <= high_idx) {
        int mid_idx = (high_idx + low_idx) / 2;
        if (card < player->hand[mid_idx]) {
            high_idx = mid_idx - 1;
        } else if (card > player->hand[mid_idx]) {
            low_idx = mid_idx + 1;
        } else {
            return 1;
        }
    }
    return 0;
}