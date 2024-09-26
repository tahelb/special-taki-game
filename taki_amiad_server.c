#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>


#define BUFFER_SIZE 50 //according to our longest message
#define NAME_SIZE 10
#define MAIN_TIMER 5
#define GAME_TIMER 90


//initials
fd_set sets[4], welcome_set;
struct sockaddr_in serverAddr;
struct sockaddr_storage serverStorage;
struct timeval timeout[4];
socklen_t addr_size;
int sockets[4][4], idx[3] = { 1,2,3 };
int loged_clients[4] = { 0 }, players_hand[3][4][7];
char multicast_ips[3][16] = { 0 }, clients_name[4][4][NAME_SIZE] = { 0 };//3 games + main, each game can have up to four players and each name size is up to NAME_SIZE length
int multicast_port, card_deck[3][54], cards_out_of_deck[3];
pthread_t threads[3] = { -1,-1,-1 };
int pile[3][54]; // Define the pile for each game
int pile_top_index[3] = { 0, 0, 0 }; // Index to track the top of the pile for each game


void error_hand(int error_num, char* error_msg);

void sort(int arr[], int size);

void* game(void* game_number);

void free_all();

int get_new_card(int game_num);

void refresh_deck(int game_num);

void log_out_player(int game_num, int sock, char* name);

int main(int argc, char** argv) { //argv[] = {port,mul1,mul2,mul3,mul_port}
    int welcome_sock, error_check, tmp_sock, new_sock, nready, i, j;
    int games_counter = 0, type, name_len, time_flag = 0;
    //int game_over_flag = 0; // Initialize game over flag
    int port = atoi(argv[1]);
    ssize_t bytes_rec, send_check;
    fd_set tmp_set;
    if (argc < 6) {
        fprintf(stderr, "Usage: %s <port> <multicast_ip1> <multicast_ip2> <multicast_ip3> <multicast_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char buffer[BUFFER_SIZE] = { 0 }, len[2] = { 0 };
    timeout[0].tv_sec = 30;
    timeout[0].tv_usec = 0;

    for (i = 0; i < 3; i++) {
        strcpy(multicast_ips[i], argv[2 + i]);
    }
    multicast_port = atoi(argv[5]);
    FD_ZERO(&welcome_set);
    welcome_sock = socket(AF_INET, SOCK_STREAM, 0); //defining the welcome socket
    if (welcome_sock == -1) { error_hand(welcome_sock, "welcome socket error"); }
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = htons(INADDR_ANY);
    memset(serverAddr.sin_zero, '\0', sizeof serverAddr.sin_zero);
    error_check = bind(welcome_sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    if (error_check == -1) { error_hand(error_check, "bind error"); }
    error_check = listen(welcome_sock, SOMAXCONN);
    if (error_check != 0) { error_hand(error_check, "listen error"); }
    printf("Server listening on port %d\n", port);
    FD_SET(welcome_sock, &welcome_set);
    FD_SET(fileno(stdin), &welcome_set);
    sets[0] = welcome_set;
    srand(time(NULL));

    while (1) {
        tmp_set = sets[0];
        if (time_flag) {
            timeout[0].tv_sec = MAIN_TIMER;
            timeout[0].tv_usec = 0;
            nready = select(FD_SETSIZE, &tmp_set, NULL, NULL, &timeout[0]);
        }
        else { nready = select(FD_SETSIZE, &tmp_set, NULL, NULL, NULL); }
        if (nready < 0) { error_hand(nready, "select error"); }
        if (nready == 0) {//timeout from the select has occurred start a game
            //arrange the game veribels
            games_counter++;
            loged_clients[games_counter] = loged_clients[0]; //amount of clients loged to the game we are about to open
            loged_clients[0] = 0;
            FD_ZERO(&sets[games_counter]);
            for (j = 0; j < loged_clients[games_counter]; j++) {
                sockets[games_counter][j] = sockets[0][j];
                sockets[0][j] = 0;
                FD_SET(sockets[games_counter][j], &sets[games_counter]);
            }
            sets[games_counter] = sets[0]; //sockets to listen to using select
            for (j = 0; j < loged_clients[games_counter]; j++) {
                strcpy(clients_name[games_counter][j], clients_name[0][j]);    //transfer the names of the clients
                memset(clients_name[0][j], '\0', NAME_SIZE);
            }
            printf("start the game\n");
            error_check = pthread_create(&threads[games_counter - 1], NULL, game, (void*)&idx[games_counter - 1]);
            if (error_check != 0) { error_hand(error_check, "CREATING GAME THREAD FAILED"); }
            //open thread to start the game
            sets[0] = welcome_set;
            time_flag = 0;
        }
        for (i = 0; i < FD_SETSIZE; i++) {
            if (FD_ISSET(i, &tmp_set)) {
                tmp_sock = i;
                if (tmp_sock == welcome_sock) {// a welcome socket interrupt
                    new_sock = accept(welcome_sock, (struct sockaddr*)&serverStorage, &addr_size);
                    if (new_sock == -1) { error_hand(new_sock, "accept error"); }
                    bytes_rec = recv(new_sock, buffer, BUFFER_SIZE, 0);
                    if (bytes_rec < 0) { error_hand(bytes_rec, "receive error"); }
                    type = (int)buffer[0];
                    if (type != 0) { //if a client sent the wrong message so ignore him
                        memset(buffer, '\0', BUFFER_SIZE);
                        close(new_sock);
                        continue;
                    }
                    //analyze the received data:supposed to be [type = 0,name len,name]
                    name_len = (int)buffer[1];
                    strcpy(clients_name[0][loged_clients[0]], buffer + 2);
                    printf("%s has connected\n", clients_name[0][loged_clients[0]]);
                    memset(buffer, '\0', BUFFER_SIZE);
                    sockets[0][loged_clients[0]] = new_sock;
                    loged_clients[0]++;
                    //build message to send: approved message:[type,mul_ip_len,mul_ip]
                    type = 1;
                    buffer[0] = (char)type;
                    buffer[1] = (char)strlen(multicast_ips[games_counter]);
                    strcpy(buffer + 2, multicast_ips[games_counter]);
                    //send the message and reset buffer
                    send_check = send(new_sock, buffer, BUFFER_SIZE, 0);
                    if (send_check == -1) { error_hand(send_check, "send error"); }
                    memset(buffer, '\0', BUFFER_SIZE);
                    FD_SET(new_sock, &sets[0]);
                    //check clients state
                    if (loged_clients[0] == 2) {//activate timer
                        time_flag = 1;
                    }
                    if (loged_clients[0] == 4) { //max number of players per game so start a game
                        //arrange the game veribels
                        games_counter++;
                        loged_clients[games_counter] = loged_clients[0]; //amount of clients loged to the game we are about to open
                        loged_clients[0] = 0;
                        FD_ZERO(&sets[games_counter]);
                        for (j = 0; j < loged_clients[games_counter]; j++) {
                            sockets[games_counter][j] = sockets[0][j];
                            sockets[0][j] = 0;
                            FD_SET(sockets[games_counter][j], &sets[games_counter]);
                        }
                        sets[games_counter] = sets[0]; //sockets to listen to using select
                        for (j = 0; j < loged_clients[games_counter]; j++) {
                            strcpy(clients_name[games_counter][j], clients_name[0][j]);    //transfer the names of the clients
                            memset(clients_name[0][j], '\0', NAME_SIZE);
                        }
                        printf("start the game\n");
                        pthread_create(&threads[games_counter - 1], NULL, game, (void*)&idx[games_counter - 1]);
                        //open thread to start the game
                        sets[0] = welcome_set;
                        time_flag = 0;
                    }
                }
                else {
                    if (tmp_sock == fileno(stdin)) {
                        printf("quitting\n");
                        free_all();
                        exit(EXIT_SUCCESS);
                    }
                    else {//a client sent a message so kick him out
                        int flag = 0;
                        for (j = 0; j < loged_clients[0] - 1; j++) {
                            if (tmp_sock == sockets[0][j]) { flag = 1; }
                            if (flag) {
                                memset(clients_name[0][j], '\0', NAME_SIZE);
                                strcpy(clients_name[0][j], clients_name[0][j + 1]); //rearrange the names array
                                sockets[0][j] = sockets[0][j + 1];
                            }
                        }
                        close(tmp_sock);
                        loged_clients[0]--;
                    }
                }
            }
        }
    }
}

void error_hand(int error_num, char* error_msg) {
    errno = error_num;
    perror(error_msg);
    free_all();
    exit(EXIT_FAILURE);
}

void free_all() {
    int i, j;
    for (i = 0; i < 3; i++) {
        if (threads[i] == -1) { continue; }
        else { pthread_cancel(threads[i]); }
    }
    for (i = 0; i < 4; i++) {
        for (j = 0; j < loged_clients[i]; j++) {
            close(sockets[i][j]);
        }
    }
}

void sort(int arr[], int size) {
    int i, j, tmp;
    for (i = 0; i < size - 1; i++) {
        for (j = 0; j < size - i - 1; j++) {
            if (arr[j] > arr[j + 1]) {
                tmp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = tmp;
            }
        }
    }
}

int get_new_card(int game_num) {
    int finding_unused_card = 0, card;
    while (!finding_unused_card) {
        card = (rand() % 54);
        if (card_deck[game_num - 1][card] != 55) {
            finding_unused_card = 1;
            card_deck[game_num - 1][card] = 55;
            cards_out_of_deck[game_num]++;
        }
    }
    return card;
}

void refresh_deck(int game_num) {
    int i, j;
    for (i = 0; i < 54; i++) { card_deck[game_num - 1][i] = i; }
    cards_out_of_deck[game_num - 1] = 0;
    for (i = 0; i < loged_clients[game_num]; i++) {
        for (j = 0; j < 7; j++) {
            if (players_hand[game_num - 1][i][j] == 55) { continue; }
            card_deck[game_num - 1][players_hand[game_num - 1][i][j]] = 55;
            cards_out_of_deck[game_num - 1]++;
        }
    }
}

void log_out_player(int game_num, int sock, char* name) {
    int found_idx = 0, j, i;
    //disconnect the client
    FD_ZERO(&sets[game_num]);
    for (j = 0; j < loged_clients[game_num] - 1; j++) {
        if (sock == sockets[game_num][j] && !found_idx) { //find the relevant socket
            found_idx = 1;
            strcpy(name, clients_name[game_num][j]);
            for (i = 0; i < 7; i++) {
                if (players_hand[game_num][j][i] == 55) { continue; }
                card_deck[game_num - 1][players_hand[game_num][j][i]] = players_hand[game_num][j][i];//return the player cards to the deck
                cards_out_of_deck[game_num - 1]--;
            }
        }
        if (found_idx) {
            sockets[game_num][j] = sockets[game_num][j + 1]; //rearrange the sockets array
            memset(clients_name[game_num][j], '\0', NAME_SIZE);
            strcpy(clients_name[game_num][j], clients_name[game_num][j + 1]); //rearrange the names array
            for (i = 0; i < 7; i++) {
                players_hand[game_num][j][i] = players_hand[game_num][j + 1][i]; 
            }  //rearrange the hands array
        }
        FD_SET(sockets[game_num][j], &sets[game_num]);
    }
    printf("disconnecting %s\n", name);
    close(sock);
    loged_clients[game_num]--;
    if (loged_clients[game_num] == 1) {
        //only 2 players are connected, and we disconnect one of them so
        //there are not enough players to keep the game going
        printf("not enough players to keep the game going\n");
        printf("game %d is closing...\n", game_num);
        close(sockets[game_num][0]);
        pthread_exit(EXIT_SUCCESS);
    }
}

void* game(void* game_number) {
    struct sockaddr_in mul_addr;
    int mul_sock, ttl = 16, nready, error_check, i, j, k, m, top_card, game_num = *(int*)game_number;
    int tmp_sock, card, players_sum[loged_clients[game_num]];//players_hand[loged_clients[game_num]][7]
    int type, insert_idx = 0, len, SN = 0, player_num_turn = 0, decision, num_of_cards, tmp_hand[7];
    int found_idx, sum, tmp_sum, winner;
    int game_over_flag = 0; // Initialize game over flag
    char game_buff[BUFFER_SIZE] = { 0 }, tmp_name[BUFFER_SIZE];
    ssize_t bytes_rec, send_check;
    fd_set tmp_set;
    //defining the multicast address
    printf("game number %d\n", game_num);
    mul_sock = socket(AF_INET, SOCK_DGRAM, 0);
    mul_addr.sin_family = AF_INET;
    mul_addr.sin_addr.s_addr = inet_addr(multicast_ips[game_num - 1]);
    mul_addr.sin_port = htons(multicast_port);
    setsockopt(mul_sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    sleep(1);   //wait for the multicast tree to build
    //build message to send: game starts message = [type = 2,num of players(2-4),len_name 1,name 1,len_name_2,...]
    type = 2;
    game_buff[0] = (char)type;
    game_buff[1] = (char)loged_clients[game_num];
    insert_idx = 2;
    for (i = 0; i < loged_clients[game_num]; i++) {
        len = strlen(clients_name[game_num][i]);
        game_buff[insert_idx] = (char)len;
        insert_idx++;
        strcpy(game_buff + insert_idx, clients_name[game_num][i]);
        insert_idx += len;
    }

    //send the message and reset buffer
    send_check = sendto(mul_sock, game_buff, BUFFER_SIZE, 0, (struct sockaddr*)&mul_addr, sizeof(mul_addr)); //send multicast message that the game has started
    if (send_check == -1) { error_hand(send_check, "send error"); }
    memset(game_buff, '\0', BUFFER_SIZE);
    printf("game starts sent\n");
    sleep(5);

    //create deck
    for (i = 0; i < 54; i++) {
        card_deck[game_num - 1][i] = i;
    }
    cards_out_of_deck[game_num - 1] = 0;

    //deal the cards
    for (i = 0; i < 7; i++) {
        for (j = 0; j < loged_clients[game_num]; j++) {
            players_hand[game_num][j][i] = get_new_card(game_num);
        }
    }
    for (i = 0; i < loged_clients[game_num]; i++) {
        sort(players_hand[game_num][i], 7);
    }

    //build message and send it to each player: deal cards = [type = 3,card 1, card 2,...]
    type = 3;
    for (i = 0; i < loged_clients[game_num]; i++) {  // Each client gets their own cards
        game_buff[0] = (char)type;
        for (j = 0; j < 7; j++) {
            game_buff[1 + j] = (char)players_hand[game_num][i][j];
        }
        send_check = send(sockets[game_num][i], game_buff, BUFFER_SIZE, 0);
        if (send_check == -1) {
            error_hand(send_check, "send error");
        }
        printf("deal cards for client number %d sent\n", i + 1);
        memset(game_buff, '\0', BUFFER_SIZE);
    }
    printf("all deal cards messages sent\n");
    sleep(5);

    //choose the first card for the pile
    card = get_new_card(game_num);

    //build message and send it to the players: expose cards = [type = 4, SN = 0, card]
    type = 4;
    game_buff[0] = (char)type;
    game_buff[1] = (char)SN;
    game_buff[2] = (char)card;
    send_check = sendto(mul_sock, game_buff, BUFFER_SIZE, 0, (struct sockaddr*)&mul_addr, sizeof(mul_addr));
    if (send_check == -1) {
        error_hand(send_check, "send error");
    }
    memset(game_buff, '\0', BUFFER_SIZE);
    printf("expose cards messages sent\n");
    sleep(5);

    //build message and send it to a player: query = [type = 5,SN]
    type = 5;
    game_buff[0] = (char)type;
    game_buff[1] = (char)SN;
    send_check = send(sockets[game_num][player_num_turn], game_buff, BUFFER_SIZE, 0);
    if (send_check == -1) { error_hand(send_check, "send error"); }
    memset(game_buff, '\0', BUFFER_SIZE);
    printf("query message sent\n");
    sleep(5);

    while (!game_over_flag) { //anyone dont drop every cards
        timeout[game_num].tv_sec = GAME_TIMER;
        timeout[game_num].tv_usec = 0;
        tmp_set = sets[game_num];
        nready = select(FD_SETSIZE, &tmp_set, NULL, NULL, &timeout[game_num]);//need to set timer for taking too long
        if (nready < 0) { error_hand(error_check, "select error"); }
        if (nready == 0) {//timeout
            //the player took to long to make a move
            //so disconnect the client
            printf("TIMEOUT!! someone took too long to make a move\n");
            memset(tmp_name, '\0', BUFFER_SIZE);
            log_out_player(game_num, tmp_sock, tmp_name);
            //send a message notifying the player has left
            //build message and send it to the players: player left = [type = 8, name (the name of the player that left)]
            type = 8;
            game_buff[0] = type;
            strcpy(game_buff + 1, tmp_name);
            send_check = sendto(mul_sock, game_buff, BUFFER_SIZE, 0, (struct sockaddr*)&mul_addr, sizeof(mul_addr));
            if (send_check == -1) { error_hand(send_check, "send error"); }
            memset(game_buff, '\0', BUFFER_SIZE);
            printf("player left messages sent\n");
            //send query message to the next player
            player_num_turn = (player_num_turn) % loged_clients[game_num];//send a query message to the next player
            //build message and send it to a player: query = [type = 5,SN]
            type = 5;
            game_buff[0] = (char)type;
            game_buff[1] = (char)SN;
            send_check = send(sockets[game_num][player_num_turn], game_buff, BUFFER_SIZE, 0);
            if (send_check == -1) { error_hand(send_check, "send error"); }
            memset(game_buff, '\0', BUFFER_SIZE);
            printf("query message sent\n");
            sleep(1);
        }
        for (i = 0; i < FD_SETSIZE; i++) {
            if (FD_ISSET(i, &tmp_set)) {
                tmp_sock = i;
                if (tmp_sock == sockets[game_num][player_num_turn]) {// the right player sent the message
                    bytes_rec = recv(tmp_sock, game_buff, BUFFER_SIZE, 0);
                    if (bytes_rec < 0) { error_hand(bytes_rec, "receive error"); }
                    if (bytes_rec == 0) {//a player closed his socket
                        log_out_player(game_num, tmp_sock, tmp_name);//disconnect him
                        memset(game_buff, '\0', BUFFER_SIZE);
                        player_num_turn = (player_num_turn) % loged_clients[game_num];//send a query message to the next player
                        //build message and send it to a player: query = [type = 5,SN]
                        type = 5;
                        game_buff[0] = (char)type;
                        game_buff[1] = (char)SN;
                        send_check = send(sockets[game_num][player_num_turn], game_buff, BUFFER_SIZE, 0);
                        if (send_check == -1) { error_hand(send_check, "send error"); }
                        memset(game_buff, '\0', BUFFER_SIZE);
                        printf("query message sent\n");
                        sleep(1);
                        continue;
                    }
                    type = (int)game_buff[0];
                    if (type == 6) { //a player made his turn
                        int player_has_won = 0; //inititalize 
                        printf("play message recevied\n");
                        decision = (int)game_buff[1];
                        switch (decision) {
                        case 1: // The player discarded a set or sequence matching the top of the pile and didn't pick any card
                            printf("Player discarded a set or sequence\n");
                            num_of_cards = (int)game_buff[2];
                            memset(tmp_hand, 55, 7);
                            for (j = 0; j < num_of_cards; j++) { // Update tmp_hand with the cards from the message
                                tmp_hand[j] = (int)game_buff[3 + j];
                            }
                            k = 0;
                            for (j = 0; j < 7; j++) { // Rearrange player hand to mark discarded cards as used
                                if (tmp_hand[k] == players_hand[game_num][player_num_turn][j] || tmp_hand[k] == 52 || tmp_hand[k] == 53) { // Include joker check
                                    players_hand[game_num][player_num_turn][j] = 55; // Mark card as used
                                    k++;
                                    if (k == num_of_cards) { break; }
                                }
                            }

                            // Add the discarded cards to the pile
                            for (m = 0; m < num_of_cards; m++) {
                                pile[game_num][pile_top_index[game_num] + m] = tmp_hand[m]; // Update the pile with the discarded cards
                            }
                            pile_top_index[game_num] += num_of_cards; // Update the pile top index

                            // Send the top card of the pile to all players via multicast
                            top_card = pile[game_num][pile_top_index[game_num] - 1];

                            type = 4; // Message type for exposing cards
                            SN++; // Sequence number increment

                            // After any card play or draw action, check if the player has won
                            player_has_won = 1; // Assume player has won until we find a card
                            for (j = 0; j < 7; j++) {
                                if (players_hand[game_num][player_num_turn][j] != 55) { // If the player has a card that's not a placeholder
                                    player_has_won = 0; // Player still has cards
                                    break;
                                }
                            }
                            if (player_has_won == 1) {
                                type = 11;
                            }

                            game_buff[0] = (char)type;
                            game_buff[1] = (char)SN;
                            game_buff[2] = (char)decision;
                            game_buff[3] = (char)num_of_cards;
                            game_buff[4] = (char)top_card; 
                            strcpy(game_buff + 4 + num_of_cards, clients_name[game_num][player_num_turn]); // Add player's name
                            send_check = sendto(mul_sock, game_buff, BUFFER_SIZE, 0, (struct sockaddr*)&mul_addr, sizeof(mul_addr));
                            if (send_check == -1) { 
                                error_hand(send_check, "send error");
                            }
                            memset(game_buff, '\0', BUFFER_SIZE);
                            printf("expose cards message sent\n");

                            sort(players_hand[game_num][player_num_turn], 7);  // Sort player's hand after discarding
                         
                            player_num_turn = (player_num_turn + 1) % loged_clients[game_num];// Move to the next player's turn
                            type = 5;
                            game_buff[0] = (char)type;
                            game_buff[1] = (char)SN;
                            send_check = send(sockets[game_num][player_num_turn], game_buff, BUFFER_SIZE, 0);
                            break;

                        case 2: //The player discarded a single card matching the pile's top and didn't pick any card
                            printf("Player discarded a single card\n");
                           
                            memset(tmp_hand, 55, 7);
                            card = (int)game_buff[3];
                            k = 0;
                            for (j = 0; j < 7; j++) { // Rearrange player hand to mark discarded card as used
                                if (players_hand[game_num][player_num_turn][j] == card) {
                                    players_hand[game_num][player_num_turn][j] = 55;
                                    break; // Player discarded a card, exit the loop
                                }
                            }
                            
                            // Add the discarded card to the pile
                            pile[game_num][pile_top_index[game_num]] = card;
                            pile_top_index[game_num]++; // Update the pile top index

                            // Send the top card of the pile to all players via multicast
                            top_card = pile[game_num][pile_top_index[game_num] - 1];
                            type = 4; // Message type for exposing cards
                            SN++; // Sequence number increment
                           
                            // After any card play or draw action, check if the player has won
                            player_has_won = 1; // Assume player has won until we find a card
                            for (j = 0; j < 7; j++) {
                                if (players_hand[game_num][player_num_turn][j] != 55) { // If the player has a card that's not a placeholder
                                    player_has_won = 0; // Player still has cards
                                    break;
                                }
                            }
                            if( player_has_won == 1){
                                type = 11;
                             }

                            game_buff[0] = (char)type;
                            game_buff[1] = (char)SN;
                            game_buff[2] = (char)decision;
                            game_buff[3] = (char)1;
                            game_buff[4] = (char)top_card;
                            send_check = sendto(mul_sock, game_buff, BUFFER_SIZE, 0, (struct sockaddr*)&mul_addr, sizeof(mul_addr));
                            if (send_check == -1) {
                                error_hand(send_check, "send error");
                            }
                            memset(game_buff, '\0', BUFFER_SIZE);
                            printf("top card message sent\n");
                            strcpy(game_buff + 5, clients_name[game_num][player_num_turn]); // Add player's name
                            printf("expose single card message sent\n");
                            sort(players_hand[game_num][player_num_turn], 7);  // Sort player's hand after discarding

                           

                            player_num_turn = (player_num_turn + 1) % loged_clients[game_num]; // Move to the next player's turn
                            type = 5;
                            game_buff[0] = (char)type;
                            game_buff[1] = (char)SN;
                            send_check = send(sockets[game_num][player_num_turn], game_buff, BUFFER_SIZE, 0);
                            break;

                        case 3:
                            printf("Player drew a card from the deck\n");

                            // Draw a card from the deck and update player's hand
                            card = get_new_card(game_num);
                            cards_out_of_deck[game_num - 1]++;
                            // Add the new card to the player's hand
                            for (j = 0; j < 7; j++) {
                                if (players_hand[game_num][player_num_turn][j] == 55) { // Find an empty spot in the hand
                                    players_hand[game_num][player_num_turn][j] = card;
                                    break;
                                }
                            }
                            sort(players_hand[game_num][player_num_turn], 7);  // Sort the player's hand after drawing a new card

                            // Check if the deck is empty and refresh if needed
                            if (cards_out_of_deck[game_num - 1] == 54) {
                                refresh_deck(game_num);
                            }
                            // Build message and send it to the player: deal = [type = 7, card]
                            type = 7;
                            game_buff[0] = (char)type;
                            game_buff[1] = (char)card;
                            send_check = send(sockets[game_num][player_num_turn], game_buff, BUFFER_SIZE, 0);
                            if (send_check == -1) {
                                error_hand(send_check, "send error");
                            }
                            memset(game_buff, '\0', BUFFER_SIZE);
                            printf("deal message sent\n");
                            sleep(1);
                            player_num_turn = (player_num_turn + 1) % loged_clients[game_num]; // Move to the next player's turn

                            // Send a query message to the next player: query = [type = 5, SN]
                            type = 5;
                            game_buff[0] = (char)type;
                            game_buff[1] = (char)SN;
                            send_check = send(sockets[game_num][player_num_turn], game_buff, BUFFER_SIZE, 0);
                            if (send_check == -1) {
                                error_hand(send_check, "send error");
                            }
                            memset(game_buff, '\0', BUFFER_SIZE);
                            printf("query message sent\n");
                            sleep(1);

                            break;

                        case 4: // The player picked the top card from the pile and did not discard any
                            printf("Player picked the top card from the pile\n");
                            card = pile[game_num][pile_top_index[game_num] - 1];
                            // Add the new card to the player's hand
                            for (j = 0; j < 7; j++) {
                                if (players_hand[game_num][player_num_turn][j] == 55) { // Find an empty spot in the hand
                                    players_hand[game_num][player_num_turn][j] = card;
                                    break;
                                }
                            }

                            pile_top_index[game_num]--; // Update the pile top index

                            // Send the top card of the pile to all players via multicast
                            top_card = pile[game_num][pile_top_index[game_num] - 1];
                            // Send a message to the player about the new card
                            type = 7; // Message type for dealing a new card
                            game_buff[0] = (char)type;
                            game_buff[1] = (char)card; // The picked card
                            game_buff[4] = (char)top_card;

                            send_check = send(sockets[game_num][player_num_turn], game_buff, BUFFER_SIZE, 0);
                            if (send_check == -1) {
                                error_hand(send_check, "send error");
                            }
                            memset(game_buff, '\0', BUFFER_SIZE);
                            printf("deal message sent\n");
                            sort(players_hand[game_num][player_num_turn], 7);   // Sort player's hand after adding the new card
                            sleep(1);
                            player_num_turn = (player_num_turn + 1) % loged_clients[game_num]; // Move to the next player's turn

                            // Send a query message to the next player: query = [type = 5, SN]
                            type = 5;
                            game_buff[0] = (char)type;
                            game_buff[1] = (char)SN;
                            send_check = send(sockets[game_num][player_num_turn], game_buff, BUFFER_SIZE, 0);
                            if (send_check == -1) {
                                error_hand(send_check, "send error");
                            }
                            break;

                        default: // Handle an invalid decision type
                            printf("Invalid decision type received: %d\n", decision);
                            break;
                        }

                        if (player_has_won) {
                            printf("Player %s has won the game!\n", clients_name[game_num][player_num_turn]);
                            game_over_flag = 1;
                            winner = player_num_turn;
                            type = 11; // Type for winning message
                            game_buff[0] = (char)type;
                            strcpy(game_buff + 1, clients_name[game_num][player_num_turn]);
                            send_check = sendto(mul_sock, game_buff, BUFFER_SIZE, 0, (struct sockaddr*)&mul_addr, sizeof(mul_addr));
                            if (send_check == -1) { error_hand(send_check, "send error"); }
                            memset(game_buff, '\0', BUFFER_SIZE);
                            printf("Winner message sent\n");

                            sleep(30); // Sleep before ending the thread
                            printf("Thread is closing\n");
                            for (j = 0; j < loged_clients[game_num]; j++) {
                                close(sockets[game_num][j]);
                            }
                            pthread_exit(EXIT_SUCCESS);
                        }

                    }
                    else { // If an unknown message type was received, disconnect the player
                        log_out_player(game_num, tmp_sock, tmp_name);
                        // Send a message to notify all players that a player has left
                        type = 8; // Message type for a player leaving
                        game_buff[0] = (char)type;
                        strcpy(game_buff + 1, tmp_name);
                        send_check = sendto(mul_sock, game_buff, BUFFER_SIZE, 0, (struct sockaddr*)&mul_addr, sizeof(mul_addr));
                        if (send_check == -1) { error_hand(send_check, "send error"); }
                        memset(game_buff, '\0', BUFFER_SIZE);
                        printf("player left messages sent\n");
                        sleep(1);
                        break;
                    }
                }
                else {
                    //a player that shouldn't send a message sent one
                    //so disconnect the client
                    memset(tmp_name, '\0', BUFFER_SIZE);
                    log_out_player(game_num, tmp_sock, tmp_name);
                    //send a message notifying the player has left
                    //build message and send it to the players: expose cards = [type = 8, name (the name of the player that left)]
                    type = 8;
                    game_buff[0] = type;
                    strcpy(game_buff + 1, tmp_name);
                    send_check = sendto(mul_sock, game_buff, BUFFER_SIZE, 0, (struct sockaddr*)&mul_addr, sizeof(mul_addr));
                    if (send_check == -1) { error_hand(send_check, "send error"); }
                    memset(game_buff, '\0', BUFFER_SIZE);
                    printf("player left messages sent\n");
                    sleep(1);

                    // After handling the incorrect player message, check if the player has won
                    if (type == 11) {
                        printf("Player %s has won the game!\n", clients_name[game_num][player_num_turn]);
                        game_over_flag = 1; // Set the game over flag
                        winner = player_num_turn;

                        // Build the message announcing the winner
                        type = 11;
                        game_buff[0] = (char)type;
                        strcpy(game_buff + 1, clients_name[game_num][player_num_turn]);
                        send_check = sendto(mul_sock, game_buff, BUFFER_SIZE, 0, (struct sockaddr*)&mul_addr, sizeof(mul_addr));
                        if (send_check == -1) { error_hand(send_check, "send error"); }
                        memset(game_buff, '\0', BUFFER_SIZE);
                        printf("Winner message sent\n");
                        sleep(30); // Sleep for some time before closing the game
                        printf("Thread is closing\n");

                        // Close all sockets and exit the thread
                        for (j = 0; j < loged_clients[game_num]; j++) {
                            close(sockets[game_num][j]);
                        }
                        pthread_exit(EXIT_SUCCESS);
                    }
                    break;
                }
            }
        }
    }
}





