// klient.c
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <errno.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <termios.h>

#define BUFFER_SIZE 1024
#define MAX_NAME_LENGTH 20

int send_out(char buffer[], int fd) {
    int sent = 0, total_sent = 0, need_to_send = strlen(buffer);
    do { 
        sent = send(fd, buffer + total_sent, need_to_send - total_sent, 0);            
        // ak nastala chyba
        if (sent <= 0) {
            fprintf(stderr, "Pri posielani spravy nastala chyba\n");
            return -1;
        } else {
            total_sent += sent;
        }
    } while (sent > 0 && total_sent != need_to_send);
    return total_sent;
}

int receive(int fd, char buffer[], int max_len, int flags) {
    int received = 0, total_received = 0;
    do { 
        received = recv(fd, buffer + total_received, max_len - total_received, flags);
        
        // ak nastala ina chyba nez ze nemozem citat bez cakania
        if (received == -1) {
            if (errno == EAGAIN) {
                return total_received;
            }
            //fprintf(stderr, "Chyba pri citani z file descriptoru %d\n", fd);
            return -1;  // chyba
        }

        // odpojil sa
        if (received == 0) {
            return 0;
        }

        if (received > 0) {
            total_received += received;
        }
    } while ((received > 0) && (total_received != max_len));
    return total_received;
}

// odpoji sa zo serveru a ukonci sa
void disconnect(int fd) {
    if (shutdown(fd, SHUT_WR) == -1) {
        fprintf(stderr, "Pri ukoncovani spojenia so serverom nastala chyba\n");
    } else {
        char buffer[1];
        int received = receive(fd, buffer, 1, 0);
        if ((received == -1 && errno != ENOTCONN) || (received > 0)) {
            fprintf(stderr, "Pri potvrdeni ukoncenia spojenia so serverom nastala chyba\n");
        } 
    }
    
    if (close(fd) == -1) {
        fprintf(stderr, "Nepodarilo sa bezpecne ukoncit spojenie so serverom\n");
    } else {
        printf("Boli ste odpojeni zo servera.\n");
    }

    // opatovne zapnutie kanonickeho modu
    struct termios terminal;
    if (tcgetattr(0, &terminal) == -1) {
        fprintf(stderr, "Nebolo mozne zitit vlastnost terminalu\n");
    } else {
        terminal.c_lflag |= ICANON;
        terminal.c_lflag |= ECHO;
        if (tcsetattr(0, TCSANOW, &terminal) == -1) {
            fprintf(stderr, "Nebolo mozne zmenit vlastnosti terminalu\n");
        }
    }

    exit(0);
}

int main (int argc, char* argv[]) {
    int sockfd;
    int port = 1234;
    struct in_addr address;
    char buffer[BUFFER_SIZE];  // prosim pouzivatelov aby si neposielali copypasty
    int input_length = 0;

    // kontrola argumentov
    if (argc > 3 || argc < 2) {
        fprintf(stderr, "Nespravny pocet argumentov: zadajte adresu, pripadne aj port (default = 1234)\n");
		exit(1);
    }

    if (inet_aton(argv[1], &address) == 0) {
        fprintf(stderr, "Neplatna IP adresa\n");
		exit(1);
    }

    if (argc == 3) {
        port = atoi(argv[2]);
        if (port <= 0) {
            fprintf(stderr, "Zle cislo portu\n");
            exit(1);
        }
    }

    // vytvorenie socketu
    sockfd = socket(PF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        fprintf(stderr, "Nepodarilo sa vytvorit socket\n");
		exit(1);
    }

    // vytvorenie adresy
    struct sockaddr_in myaddress;
	myaddress.sin_family = AF_INET;
	myaddress.sin_addr = address;
	myaddress.sin_port = htons(port);

    // zapnutie kanonickeho modu
    struct termios terminal;
    if (tcgetattr(0, &terminal) == -1) {
        fprintf(stderr, "Nebolo mozne zitit vlastnost terminalu\n");
    } else {
        terminal.c_lflag |= ICANON;
        terminal.c_lflag |= ECHO;
        if (tcsetattr(0, TCSANOW, &terminal) == -1) {
            fprintf(stderr, "Nebolo mozne zmenit vlastnosti terminalu\n");
        }
    }

    // meno
    int name_length = -2;  // aby nebola splnena ziadna podmienka vypisu nizsie
    do {
        if (name_length == -1) {
            fprintf(stderr, "Pri citani vstupu nastala neocakavan chyba\n");
		    exit(1);
        }
        if (name_length > -1 && name_length < 3) {
            printf("Meno musi mat aspon 3 znaky\n");
        }
        if (name_length > MAX_NAME_LENGTH) {
            printf("Meno nemoze byt dlhsie nez %d znakov\n", MAX_NAME_LENGTH);
        }
        printf("Zadajte prezyvku: ");
        fflush(stdout);
        name_length = read(0, buffer, BUFFER_SIZE);
    } while (name_length < 3 || name_length > MAX_NAME_LENGTH);

    // pripojenie
    if (connect(sockfd, (struct sockaddr *) &myaddress, sizeof(myaddress)) == -1) {
        fprintf(stderr, "Nepodarilo pripojit na server\n");
		exit(1);
    }

    // koncovy null miesto enteru
    buffer[name_length - 1] = '\0';

    // posielanie mena
    if (send_out(buffer, sockfd) == -1) {
        fprintf(stderr, "Nepodarilo sa odoslat prihlasovacie meno\n");
		disconnect(sockfd);
    }

    // ASCII umenie nikdy nesklame
    printf("Civove carovne chatovacie cudo\n\n");
    printf(" __ __    __ __    __ __    __ __ \n");
    printf(" \\ V /    \\ V /    \\ V /    \\ V / \n");
    printf(" _\\_/_    _\\_/_    _\\_/_    _\\_/_ \n");
    printf("/ ____|  / ____|  / ____|  / ____|\n");
    printf("| |      | |      | |      | |    \n");
    printf("| |      | |      | |      | |    \n");
    printf("| |____  | |____  | |____  | |____\n");
    printf("\\_____|  \\_____|  \\_____|  \\_____|\n");                                  
    printf("Na ukoncenie programu pouzite /exit, pre viac info pouzite /help\n\n");

    // vypnutie kanonickeho modu
    if (tcgetattr(0, &terminal) == -1) {
        fprintf(stderr, "Nebolo mozne zitit vlastnost terminalu\n");
    } else {
        // vypnutie kanonickeho modu
        terminal.c_lflag &= ~ICANON;
        terminal.c_lflag &= ~ECHO;
        if (tcsetattr(0, TCSANOW, &terminal) == -1) {
            fprintf(stderr, "Nebolo mozne zmenit vlastnosti terminalu\n");
        }
    }

    // select
    while (1) {
        // nastav mnozinu vstupov, na ktore sa ma cakat
        fd_set readfds;  // mnozina pre select
        FD_ZERO(&readfds);
        FD_SET(0, &readfds);
        FD_SET(sockfd, &readfds);
        int select_num = select(sockfd + 1, &readfds, NULL, NULL, NULL);

        if ((select_num == -1) && (errno == EINTR)) {
            // mohlo nastat skoncenie selektu z ineho dovodu ako moznost citat
            continue;
        }

        // vstup z konzoly
        if (FD_ISSET(0, &readfds)) {
            int read_result = read(0, buffer + input_length, BUFFER_SIZE - input_length - 1);
            if (read_result == -1) {
                // sorry jako, ak uz nejde ani citat z konzoly, tak to tento pocitac tazko zvladne komunikovat cez internet
                fprintf(stderr, "Chyba pri citani vstupu z konzoly\n");
                //disconnect(sockfd);
            } else {
                // spracovavam znak po znaku
                while (read_result > 0) {
                    if ((buffer[input_length] == '\x7f' || buffer[input_length] == '\b') && input_length > 0) {  // backspace alebo delete
                    input_length--;
                    if (write(1, "\b \b", 3) != 3) {  // vymaze predosly znak
                        fprintf(stderr, "Chyba pri pisani do stdout\n");
                    }
                    } else if (buffer[input_length] == '\x1b' && input_length < BUFFER_SIZE - MAX_NAME_LENGTH - 3) {  // escape characters, ktore vedia narobit sarapatu + kontrola, ci sa zmesti
                        buffer[input_length] = '^';  // je to skarede, ale funguje to... nahrada pre esc je ^[ takze trochu sa to podoba
                        if (write(1, "^", 1) != 1) {
                            fprintf(stderr, "Chyba pri pisani do stdout\n");
                        }
                        input_length++;
                    } else if (buffer[input_length] == '\n' || buffer[input_length] == '\x04') {  
                        // koniec vstupu (enter alebo CTRL+D), odosli
                        if (write(1, "\n", 1) != 1) {
                            fprintf(stderr, "Chyba pri pisani do stdout\n");
                        }
                        // koncovy null
                        buffer[input_length] = '\0';


                        // prikazy sa zacinaju lomokou /
                        if (buffer[0] == '/') {
                            if ((strncmp(buffer, "/quit", 5) == 0) || (strncmp(buffer, "/exit", 5) == 0)) {
                                printf("Odpajam sa zo serveru...\n");
                                disconnect(sockfd);
                            } else if (strncmp(buffer, "/help", 5) == 0) {
                                printf("====================\n");
                                printf("/help pre zobrazenie pomoci (ako ste si uz vsimli)\n");
                                printf("/exit pre ukoncenie\n");
                                printf("/list pre vylistovanie pripojenych uzivatelov\n");
                                printf("====================\n");
                            } else if (strncmp(buffer, "/list", 5) == 0) {
                                send_out(buffer, sockfd);
                            } else {
                                printf("'%s' nie je znamy prikaz\n", buffer);
                            }
                        } else if (input_length > 0) {
                            send_out(buffer, sockfd);
                        }
                        input_length = 0;
                        break;
                    } else if (input_length < BUFFER_SIZE - MAX_NAME_LENGTH - 3) {  // ak je na vstupe BUFFER_SIZE - MAX_NAME_LENGTH - 1 alebo viac znakov, neda sa dalej pisat 
                        // je to mensie nez BUFFER_SIZE o meno - chcem, aby ostatnym uzivatelom prisla pekne jedna cela sprava a nie dve oddelene (ako to robilo predtym)
                        if (write(1, buffer + input_length , 1) != 1) {
                            fprintf(stderr, "Chyba pri pisani do stdout\n");
                        }
                        input_length++;
                    }
                    read_result--;
                }

                select_num--;
            }
        }
        // sprava od servera
        if (FD_ISSET(sockfd, &readfds)) {
            char from_net[BUFFER_SIZE];

            // presuniem co uzivatel napisal do docasneho uloziska a vycistim riadok
            for (int i = 0; i < input_length; i++) {
                if (write(1, "\b \b", 3) != 3) {
                    fprintf(stderr, "Chyba pri mazani vstupu\n");
                }
            }

            // nacitam spravu do buffera
            int received = receive(sockfd, from_net, BUFFER_SIZE, MSG_DONTWAIT);
            
            // ak nastala ina chyba nez ze nemozem citat bez cakania
            if ((received == -1) && (errno != EAGAIN) && (errno != EWOULDBLOCK)) {
                fprintf(stderr, "Pri pokuse o prijatie spravy zo servera nastala chyba %d\n", errno);
                disconnect(sockfd);
            }

            // odpojil sa a treba ho odstranit zo zoznamu
            if (received == 0) {
                printf("Server ukoncil spojenie\n");
                disconnect(sockfd);
            }

            if (received > 0) { 
                // ukoncovaci null               
                from_net[received] = '\0';
                printf("%s\n", from_net);
            }

            // znova zapisem uzivatelov riadok
            if (write(1, buffer, input_length) != input_length) {
                fprintf(stderr, "Chyba pri opatovnom pisani vstupu\n");
            }
        }
    }       
    return 0;
}