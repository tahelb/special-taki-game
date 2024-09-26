#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>


#define BUFFER_SIZE 50 //according to our longest message
#define NAME_SIZE 10
#define TIMER 120

char buffer[BUFFER_SIZE] = { 0 };
int hand[7] = { 55,55,55,55,55,55,55 };                //The cards of the player
int drop_cards[7] = { 55,55,55,55,55,55,55 };        //The cards the player draws
int desk[7] = { 55,55,55,55,55,55,55 };           //The cards on the desk
int tcp_sock = -1, mul_sock = -1, hand_size = 0, desk_size, num_of_cards_to_drop;
ssize_t rec_check, send_check;

void error_hand(int error_num, char* error_msg);
void end_func();
void print_cards(int cards[7], int num);
int comp(const void* a, const void* b);
void print_one_card(int card);
void play();
void drop_cards_func();


int main(int argc, char** argv) {
    //variables
    struct timeval timeout;
    struct sockaddr_in serverAddr;
    struct sockaddr_in addr;
    socklen_t addr_size;
    fd_set  set, tmp_set;
    char ip[16] = { 0 }, name[NAME_SIZE + 1] = { 0 }, multicast_ip[16] = { 0 }, s_msg_type[2] = { 0 }, name_len[2] = { 0 }, P_name[NAME_SIZE + 1], shape[8] = { 0 };//arbitrary choice
    int ans;
    int tcp_port, mul_port, tmp_sock, i, ip_len, r_msg_type, j, players_num, P_name_len, r_t_now;
    int SN_expose, SN_query, SN_count = 0, decision_e, num_to_expose, took_card, card_val, check;
    int flag_start = 0, flag_deal = 0, error_check, server_sock_closed, win_type;
    struct ip_mreq mreq;
    //define the timeout
    timeout.tv_sec = TIMER;
    timeout.tv_usec = 0;
    //init, get the variables from the arguments
    strcpy(name, argv[1]);
    strcpy(ip, argv[2]);
    tcp_port = atoi(argv[3]);
    mul_port = atoi(argv[4]);
    //open TCP socket and send connect message
    tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_sock == -1) { error_hand(tcp_sock, "socket opening error"); }
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(ip);//inet_addr
    serverAddr.sin_port = htons(tcp_port);
    memset(serverAddr.sin_zero, '\0', sizeof serverAddr.sin_zero);
    addr_size = sizeof(serverAddr);
    error_check = connect(tcp_sock, (struct sockaddr*)&serverAddr, addr_size);
    if (error_check == -1) { error_hand(error_check, "connect error"); }
    //arranging the buffer before sending connect message
    memset(buffer, '\0', BUFFER_SIZE);                //clear the buffer
    //    printf("please enter your nickname, up to %d letters \n", NAME_SIZE+1);
    //    fgets(name,NAME_SIZE+1,stdin);
    buffer[0] = 0;                                                          //enter the msg_type to the buffer
    buffer[1] = strlen(name);                                            //enter the size of the payload (the name in this case)
    strcpy(buffer + 2, name);                                        //enter the nick name to the buffer
    //send connect message
    send_check = send(tcp_sock, buffer, BUFFER_SIZE, 0);
    if (send_check == -1) { error_hand((int)send_check, "send error"); }  //just for the check
    memset(buffer, '\0', BUFFER_SIZE);                //clear the buffer
    printf("connect message sent\n");
    //for the select
    FD_ZERO(&set);                                           //clear the set
    FD_SET(tcp_sock, &set);                                   //tcp_sock file descriptor to the set
    FD_SET(fileno(stdin), &set);                       //enter interrupt to the set
    // select to get the multicast ip by recv the approve msg
    tmp_set = set;
    printf("select, wait for approve message\n");
    error_check = select(FD_SETSIZE, &tmp_set, NULL, NULL, &timeout);
    if (error_check < 0) { error_hand(error_check, "select-approve error"); }
    if (error_check == 0) {//Timer, the server does not respond
        printf("the server didnt respond with an approve message\n");
        close(tcp_sock);
        exit(EXIT_SUCCESS);
    }
    for (i = 0; i < FD_SETSIZE; i++) {      //to check who is set
        if (FD_ISSET(i, &tmp_set)) {      //found one that set
            tmp_sock = i;
            if (tmp_sock == tcp_sock) {  //did it the tcp sock?
                rec_check = recv(tmp_sock, buffer, BUFFER_SIZE, 0);
                if (rec_check == -1) { error_hand((int)rec_check, "receive error"); }
                r_msg_type = (int)buffer[0];                                //type
                if (r_msg_type == 1) {
                    ip_len = (int)buffer[1];                                //len
                    strncpy(multicast_ip, buffer + 2, ip_len);      //payload
                }
                else {
                    printf("inappropriate message, logout1");
                    end_func();
                }
                //printf("%s",buffer+ip_len+1);
                printf("approve message received\n");
                memset(buffer, '\0', BUFFER_SIZE);                //clear the buffer
            }
            if (tmp_sock == fileno(stdin)) {     //did it enter?
                printf("quitting\n");
                end_func();
            }
        }
    }
    //open multicast socket
    mul_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (mul_sock == -1) { error_hand(mul_sock, "socket opening error"); }
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(mul_port);
    bind(mul_sock, (struct sockaddr*)&addr, sizeof(addr));
    mreq.imr_multiaddr.s_addr = inet_addr(multicast_ip);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(mul_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
    // prepare to select
    FD_SET(mul_sock, &set);                     //mul_sock file descriptor to the set
    // select, the server received the registration, wait for messages
    flag_start = 0;
    while (1) {
        timeout.tv_sec = TIMER;
        timeout.tv_usec = 0;
        tmp_set = set;
        if (flag_start) { error_check = select(FD_SETSIZE, &tmp_set, NULL, NULL, &timeout); }
        else {
            printf("select wait for game start message\n");
            error_check = select(FD_SETSIZE, &tmp_set, NULL, NULL, NULL);
        }
        if (error_check < 0) { error_hand(error_check, "select error"); }
        if (error_check == 0) { //timer
            printf("timeout logging off\n");
            end_func();
        }
        for (i = 0; i < FD_SETSIZE; i++) {
            if (FD_ISSET(i, &tmp_set)) {
                tmp_sock = i;
                if (tmp_sock == tcp_sock || tmp_sock == mul_sock) {
                    rec_check = recv(tmp_sock, buffer, BUFFER_SIZE, 0);
                    if (rec_check == -1) { error_hand((int)rec_check, "receive error"); }
                    if (rec_check == 0) {
                        printf("the server disconnected, logout\n");
                        server_sock_closed = 1;
                        end_func();
                    }
                    r_msg_type = (int)buffer[0];
                    if (flag_start == 0 && flag_deal == 0) {
                        if (r_msg_type == 2) {
                            flag_start = 1;
                            printf("Hi %s, enjoy the game\n", name);
                            printf("the players in this game are:\n");
                            players_num = (int)buffer[1];
                            P_name_len = 0;
                            r_t_now = 2;
                            for (j = 0; j < players_num; j++) {
                                P_name_len = (int)buffer[r_t_now];
                                r_t_now++;
                                strncpy(P_name, buffer + r_t_now, P_name_len);
                                printf("Player %d: %s\n", j + 1, P_name);
                                memset(P_name, '\0', NAME_SIZE);
                                r_t_now += P_name_len;
                            }
                            continue;
                        }
                        else {
                            printf("inappropriate message, logout2");
                            end_func();
                        }
                    }

                    if (flag_start == 1 && flag_deal == 0) {                //Get the hand of the cards
                        if (r_msg_type == 3) {
                            flag_deal = 1;
                            hand_size = 7;
                            for (j = 0; j < 7; j++) {
                                hand[j] = (int)buffer[j + 1];
                            }

                            printf("Your cards are:\n");
                            qsort(hand, hand_size, sizeof(int), comp);
                            print_cards(hand, hand_size);
                            continue;
                        }
                        else {
                            printf("inappropriate message, logout3");
                            end_func();
                        }
                    }
                    if (flag_start == 1 && flag_deal == 1) {
                        switch (r_msg_type) {
                        case 4: // Expose cards
                            SN_expose = (int)buffer[1];
                            if (SN_expose != SN_count) {
                                printf("SN not good\n");
                                end_func();
                            }
                            if (SN_expose == 0) { // The first expose message
                                desk[0] = (int)buffer[2];
                                desk_size = 1;
                                for (j = 1; j < 7; j++) {
                                    desk[j] = 55;
                                }
                                printf("The first card on the desk is: ");
                                print_one_card(desk[0]);
                                printf("\n");
                                SN_count++;
                                break;
                            }
                            SN_count++;
                            decision_e = (int)buffer[2];
                            num_to_expose = (int)buffer[3];
                            desk_size = num_to_expose;
                            for (j = 0; j < 7; j++) { // Save the exposed cards
                                if (j < num_to_expose) {
                                    desk[j] = (int)buffer[j + 4];
                                }
                                else {
                                    desk[j] = 55; // Fill the rest of the array with 55
                                }
                            }
                            qsort(desk, 7, sizeof(int), comp); // Sort the array

                            switch (decision_e) {
                            case 1: // Player dropped a sequence or set of cards
                                strcpy(P_name, buffer + 4 + num_to_expose);
                                printf("%s dropped a sequence or set of cards\n", P_name);
                                break;

                            case 2: // Player dropped a single card
                                took_card = (int)buffer[4]; // This represents the card that matches the top card by color or value
                                strcpy(P_name, buffer + 5 + num_to_expose);
                                printf("%s dropped a single card ", P_name);
                                print_one_card(took_card);
                                printf("\n");
                                break;

                            case 3: // Player drew a card from the deck
                                strcpy(P_name, buffer + 4 + num_to_expose);
                                printf("%s drew a card from the deck\n", P_name);
                                break;

                            case 4: // Player drew the top card from the visible pile
                                took_card = (int)buffer[4];
                                strcpy(P_name, buffer + 5 + num_to_expose);
                                printf("%s drew the top card ", P_name);
                                print_one_card(took_card);
                                printf(" from the visible pile\n");
                                break;

                            default:
                                printf("Unknown move\n");
                                break;
                            }
                            printf("The cards that the player dropped are:\n");
                            print_cards(desk, num_to_expose);

                            // Check if the player has 0 cards
                            if (hand_size == 0) {
                                printf("Congratulations! You have won the game by dropping all your cards!\n");
                                buffer[0] = 11; // Message type for winning
                                send_check = send(tcp_sock, buffer, BUFFER_SIZE, 0);
                                if (send_check == -1) {
                                    error_hand((int)send_check, "send error");
                                }
                                end_func(); // End the game
                            }
                            break;

                        case 5: // Query
                            SN_query = (int)buffer[1];
                            if (SN_query != SN_expose) {
                                printf("SN not good\n");
                                end_func();
                            }
                            memset(buffer, '\0', BUFFER_SIZE); // Clear the buffer
                            play();
                            break;

                        case 7: // Deal - get card from the deck
                            hand[hand_size - 1] = (int)buffer[1];
                            printf("You got this card: ");
                            print_one_card((int)buffer[1]);
                            printf("\n");
                            qsort(hand, hand_size, sizeof(int), comp);
                            printf("Your hand is now:\n");
                            print_cards(hand, hand_size);
                            break;

                        case 8: // Player left
                            strcpy(P_name, buffer + 1);
                            printf("%s left the game\n", P_name);
                            break;

                        case 11:
                            printf("Player won, game over!\n");
                            buffer[0] = 11; // Message type for winning
                            send_check = send(tcp_sock, buffer, BUFFER_SIZE, 0);
                            if (send_check == -1) {
                                error_hand((int)send_check, "send error");
                            }
                            end_func(); // End the game
                            break;

                        default:
                            printf("Inappropriate message, logging out4\n");
                            end_func();
                            break;
                        }
                    }
                }
            }
        }
    }
    return 0;
}


void error_hand(int error_num, char* error_msg) {
    errno = error_num;
    perror(error_msg);

    exit(EXIT_FAILURE);
}

void end_func() {
    printf("LOGOUT\n");
    if (tcp_sock != -1) { close(tcp_sock); }
    if (mul_sock != -1) { close(mul_sock); }
    exit(EXIT_SUCCESS);
}

void print_cards(int cards[7], int num) {
    int card_val, i;
    for (i = 0; i < num; i++) {
        printf("%d)  ", (i+1));
        card_val = (cards[i]) % 13 + 1;
        if (cards[i] == 52 || cards[i] == 53) {
            printf("joker\n");
        }
        switch (card_val) {
        case 1:
            switch (cards[i]) {
            case 0 ... 12:
                printf("A heart\n");
                break;
            case 13 ... 25:
                printf("A diamond\n");
                break;
            case 26 ... 38:
                printf("A club\n");
                break;
            case 39 ... 51:
                printf("A spade\n");
                break;
            }
            break;
        case 2 ... 10:
            switch (cards[i]) {
            case 0 ... 12:
                printf("%d heart\n", card_val);
                break;
            case 13 ... 25:
                printf("%d diamond\n", card_val);
                break;
            case 26 ... 38:
                printf("%d club\n", card_val);
                break;
            case 39 ... 51:
                printf("%d spade\n", card_val);
                break;
            }
            break;
        case 11:
            switch (cards[i]) {
            case 0 ... 12:
                printf("J heart\n");
                break;
            case 13 ... 25:
                printf("J diamond\n");
                break;
            case 26 ... 38:
                printf("J club\n");
                break;
            case 39 ... 51:
                printf("J spade\n");
                break;
            }
            break;
        case 12:
            switch (cards[i]) {
            case 0 ... 12:
                printf("Q heart\n");
                break;
            case 13 ... 25:
                printf("Q diamond\n");
                break;
            case 26 ... 38:
                printf("Q club\n");
                break;
            case 39 ... 51:
                printf("Q spade\n");
                break;
            }
            break;
        case 13:
            switch (cards[i]) {
            case 0 ... 12:
                printf("K heart\n");
                break;
            case 13 ... 25:
                printf("K diamond\n");
                break;
            case 26 ... 38:
                printf("K club\n");
                break;
            case 39 ... 51:
                printf("K spade\n");
                break;
            }
            break;
        }
    }
    return;
}

int comp(const void* a, const void* b)
{
    return (*(int*)a - *(int*)b);
}

void print_one_card(int card) {
    int card_val;
    char shape[8];
    card_val = (card % 13) + 1;
    //print the cards
    switch (card / 13) {
    case 0:
        strcpy(shape, "heart");
        break;
    case 1:
        strcpy(shape, "diamond");
        break;
    case 2:
        strcpy(shape, "club");
        break;
    case 3:
        strcpy(shape, "spade");
        break;
    }
    switch (card_val) {
    case 1:
        printf("A %s ", shape);
        break;
    case 11:
        printf("J %s ", shape);
        break;
    case 12:
        printf("Q %s ", shape);
        break;
    case 13:
        printf("K %s ", shape);
        break;
    case 2 ... 10:
        printf("%d %s ", card_val, shape);
        break;
    }
}

void play() {
    int decision, i, check;
    int card_from_pile_1, chosen_opt_from_pile;
    card_from_pile_1 = desk[desk_size - 1];  // The top card on the visible pile
    qsort(hand, hand_size, sizeof(int), comp);  // Sort the player's hand
    printf("It's your turn\n");
    printf("Remember, these are your cards:\n");
    print_cards(hand, hand_size);  // Display player's cards
    printf("The cards on the desk are:\n");
    print_cards(desk, desk_size);  // Display cards on the desk

    // Display instructions to the player for choosing a move
    printf("Please choose your move:\n");
    printf("1. Drop a sequence or set of cards according to the rules\n");  // Option to drop a sequence or set of cards
    printf("2. Drop a single card that matches the top card by color or value\n");  // Option to drop a single card matching the top card by color or value
    printf("3. Draw a card from the deck\n");  // Option to draw a card from the deck
    printf("4. Draw the top card from the visible pile\n");  // Option to draw the top card from the visible pile

    printf("Enter your move: ");
    check = scanf("%d", &decision);  // Read the player's decision
    if (check < 0) {
        error_hand(check, "scanf on play func error");  // Error handling for input
    }

    // Validate player's choice
    for (i = 0; i < 3; i++) {
        if (decision < 1 || decision > 4) {  // Ensure the choice is within the valid range
            if (i == 2) {
                end_func();  // End the function if invalid input persists
            }
            printf("Invalid input, please select again\n");
            check = scanf("%d", &decision);  // Re-read the player's decision
            if (check < 0) {
                error_hand(check, "scanf on play func error");  // Error handling for input
            }
        }
    }

    // If the player chooses to drop a set or sequence of cards
    if (decision == 1) {
        drop_cards_func();  // Function to handle dropping cards logic (set or sequence)
        if (decision == 1) {
            buffer[0] = 6;  // Message type 6 for dropping cards
            buffer[1] = decision;
            buffer[2] = num_of_cards_to_drop;  // Number of cards to drop
            for (i = 0; i < num_of_cards_to_drop; i++) {
                buffer[i + 3] = drop_cards[i];  // Cards selected to be dropped
            }
            send_check = send(tcp_sock, buffer, BUFFER_SIZE, 0);  // Send message to server
            if (send_check == -1) {
                error_hand((int)send_check, "send error");  // Error handling for sending
            }
            memset(buffer, '\0', BUFFER_SIZE);  // Clear the buffer
            return;
        }
    }


    // If the player chooses to drop a single card
    if (decision == 2) {
        int single_card;
        for(i = 0; i < 3; i++){
            printf("Choose a single card to drop:\n");
            print_cards(hand, hand_size);  // Display the player's cards for selection
            check = scanf("%d", &single_card);  // Read the chosen card to drop
            if (check < 0) {
                error_hand(check, "scanf on play func error");  // Error handling for input
            }

            int top_card_value = desk[desk_size - 1] % 13;  // Get the value of the top card on the desk
            int top_card_color = desk[desk_size - 1] / 13;  // Get the color (suit) of the top card on the desk
            int chosen_card_value = hand[single_card - 1] % 13;  // Get the value of the chosen card
            int chosen_card_color = hand[single_card - 1] / 13;  // Get the color (suit) of the chosen card

           
            // Check if the chosen card matches either the color or the value of the top card on the desk
            if (chosen_card_value != top_card_value && chosen_card_color != top_card_color && (hand[single_card - 1] < 51) && (desk[desk_size - 1] < 51)) {
                printf("Invalid move! The card must match either the value or the suit of the top card on the pile.\n");
                printf("try again you have %d more chance.\n" , (2 - i));
                if (i == 2) {
                    decision = 3;  
                }
            }
            else {
                i = 3;
            }
        }

        if (decision == 2) {
            buffer[0] = 6;  // Message type 6 for dropping cards
            buffer[1] = decision;
            buffer[2] = 1;  // Number of cards to drop is 1
            buffer[3] = hand[single_card - 1];  // The selected card to drop
            hand[single_card - 1] = 55;  // Mark the card as dropped
            qsort(hand, hand_size, sizeof(int), comp);  // Re-sort the hand
            hand_size--;

            send_check = send(tcp_sock, buffer, BUFFER_SIZE, 0);  // Send message to server
            if (send_check == -1) {
                error_hand((int)send_check, "send error");  // Error handling for sending
            }

            memset(buffer, '\0', BUFFER_SIZE);  // Clear the buffer
            return;  // End the turn after the single card is dropped
        }
    }


    // If the player chooses to draw a card from the deck
    if (decision == 3) {
        buffer[0] = 6;  // Message type 7 for drawing from the deck
        buffer[1] = decision;
        send_check = send(tcp_sock, buffer, BUFFER_SIZE, 0);  // Send message to server
        if (send_check == -1) {
            error_hand((int)send_check, "send error");  // Error handling for sending
        }
        rec_check = recv(tcp_sock, buffer, BUFFER_SIZE, 0);  // Receive the card from the server
        if (rec_check == -1) {
            error_hand((int)rec_check, "receive error");  // Error handling for receiving
        }
        int drawn_card = (int)buffer[1];  // The card drawn from the deck
        printf("You drew a card from the deck: ");
        print_one_card(drawn_card);  // Display the drawn card
        printf("\n");
        hand[hand_size++] = drawn_card;  // Add the drawn card to the player's hand
        qsort(hand, hand_size, sizeof(int), comp);  // Re-sort the hand
        return;  // End the turn after drawing the card
    }

    // If the player chooses to draw the top card from the visible pile
    if (decision == 4) {
        buffer[0] = 6;  // Message type 8 for drawing from the visible pile
        buffer[1] = decision;
        send_check = send(tcp_sock, buffer, BUFFER_SIZE, 0);  // Send message to server
        if (send_check == -1) {
            error_hand((int)send_check, "send error");  // Error handling for sending
        }
        rec_check = recv(tcp_sock, buffer, BUFFER_SIZE, 0);  // Receive the card from the server
        if (rec_check == -1) {
            error_hand((int)rec_check, "receive error");  // Error handling for receiving
        }
        int drawn_card = (int)buffer[1];  // The card drawn from the visible pile
        printf("You drew the top card from the visible pile: ");
        print_one_card(drawn_card);  // Display the drawn card
        printf("\n");
        hand[hand_size++] = drawn_card;  // Add the drawn card to the player's hand
        qsort(hand, hand_size, sizeof(int), comp);  // Re-sort the hand
        return;  // End the turn after drawing the card
    }

    // Clear the buffer at the end of the turn
    memset(buffer, '\0', BUFFER_SIZE);
}


void drop_cards_func() {
    int check, i, j, k;
    int the_same = 1;  // Flag to check if all cards are of the same value
    int up_stream = 0;  // Flag to check if the cards are in an increasing sequence
    int same_shape = 1;  // Flag to check if all cards are of the same suit
    int joker_count = 0; // Count the number of jokers
    int hand_indx[7];  // Array to store indices of the selected cards
    int decision;

    // Ask the player how many cards they want to drop
    for (i = 0; i < 2; i++) {
        printf("How many cards do you want to drop? (You must drop at least 2 cards)\n");
        check = scanf("%d", &num_of_cards_to_drop);  // Read the number of cards to drop
        if (check < 0) {
            error_hand(check, "scanf on play func error");  // Error handling for input
        }


        // If the player chooses to drop only one card, set the decision to 2 and return
        if (num_of_cards_to_drop == 1) {
            decision = 2;  // Set decision to 2 for single card drop
            return;  // Exit the function and return control to play()
        }


        // Validate the number of cards to drop
        for (j = 0; j < 3; j++) {
            if (num_of_cards_to_drop > hand_size || num_of_cards_to_drop < 1) {
                if (j == 2) {
                    end_func();  // End function if invalid input persists
                }
                printf("Invalid input, please select again, you have two more chances\n");
                check = scanf("%d", &num_of_cards_to_drop);  // Re-read the number of cards to drop
                if (check < 0) {
                    error_hand(check, "scanf on play func error");  // Error handling for input
                }
            }
        }

        printf("Choose the cards that you want to drop\n");
        print_cards(hand, hand_size);
        printf("Select number between 1 to the number of cards in your hand\n");

        // Read the indices of the cards to drop from the player's hand
        for (k = 0; k < num_of_cards_to_drop; k++) {
            printf("Enter card number %d: ", k + 1);
            check = scanf("%d", &hand_indx[k]);  // Read the index
            drop_cards[k] = hand[hand_indx[k] - 1];  // Get the actual card value
            printf("\n");
            if (check < 0) {
                error_hand(check, "scanf on play func error");  // Error handling for input
            }

            // Check if the card is a Joker
            if (drop_cards[k] == 52 || drop_cards[k] == 53) {
                joker_count++;  // Increase joker count
            }
        }

        hand_size = hand_size - num_of_cards_to_drop;
        


        qsort(drop_cards, num_of_cards_to_drop, sizeof(int), comp);  // Sort the dropped cards

        // Check if all cards have the same value (Set) ignoring Jokers
        for (k = 0; (k < num_of_cards_to_drop - joker_count - 1); k++) {
            if ((drop_cards[k] % 13) != (drop_cards[k + 1] % 13)) {
                the_same = 0;
            }
        }

        // Check if all cards have the same suit (Color) ignoring Jokers
        for (k = 0; (k < num_of_cards_to_drop - joker_count - 1); k++) {
            if ((drop_cards[k] / 13) != (drop_cards[k + 1] / 13)) {
                same_shape = 0;
            }
        }

        // Check if the cards are in an increasing sequence (Run) considering Jokers
        if (same_shape) {
            up_stream = 1;
            for (k = 0; (k < num_of_cards_to_drop - joker_count - 1); k++) {
                if (drop_cards[k] != (drop_cards[k + 1] - 1)) {
                    up_stream = 0;
                }
            }
        }


        // If the player has a joker and either a valid set or sequence, it's automatically valid
        if (joker_count > 0 && (the_same || up_stream)) {
            // Valid move: update the player's hand and do not consider the pile rules
            for (k = 0; k < num_of_cards_to_drop; k++) {
                hand[hand_indx[k] - 1] = 55;  // Mark the dropped cards
            }
            qsort(hand, hand_size, sizeof(int), comp);  // Re-sort the hand
            return;
        }

        int top_card_value = desk[desk_size - 1] % 13;  // Get the value of the top card on the desk
        int top_card_color = desk[desk_size - 1] / 13;  // Get the color (suit) of the top card on the desk

        // Check the validity of a sequence according to the new rules
        int valid_sequence = 0;
        if (up_stream) {
            int first_card_value = drop_cards[0] % 13;
            int first_card_color = drop_cards[0] / 13;
            int last_card_value = drop_cards[num_of_cards_to_drop - 1] % 13;

            // Check if the sequence matches the new rules
            if (first_card_value == top_card_value || first_card_color == top_card_color ||
                last_card_value == top_card_value) {
                valid_sequence = 1;
            }
        }

        // Condition to check the validity of a set of cards
        int valid_set = 0;
        if (the_same) {
            for (k = 0; k < num_of_cards_to_drop; k++) {
                if ((drop_cards[k] / 13) == top_card_color ||
                    (drop_cards[k] % 13) == top_card_value) {
                    valid_set = 1;
                    break;
                }
            }
        }

        // If neither a valid set nor a valid sequence, prompt to choose again
        if ((the_same == 0 && up_stream == 0) || (up_stream && !valid_sequence) || (the_same && !valid_set)) {
            printf("Invalid input, please select again, you have two more chances\n");
        }
        else {
            // Valid move: update the player's hand
            for (k = 0; k < num_of_cards_to_drop; k++) {
                hand[hand_indx[k] - 1] = 55;  // Mark the dropped cards
            }
            qsort(hand, hand_size, sizeof(int), comp);  // Re-sort the hand
            return;
        }
    }
}




