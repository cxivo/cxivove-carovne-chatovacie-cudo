// server.c
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <errno.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <termios.h>

#define MAX_CONNECTIONS 256
#define BUFFER_SIZE 1024
#define MAX_NAME_LENGTH 20

// globalne premenne su prec

// odosle spravu jednemu file descriptoru
int send_one(char buffer[], int fd) {
    int sent = 0, total_sent = 0, need_to_send = strlen(buffer);
    do { 
        sent = send(fd, buffer + total_sent, need_to_send - total_sent, 0);
                        
        // ak nastala chyba
        if (sent <= 0) {
            return -1;  // tak co uz, nedostane tuto spravu. mozno sa aj tak odpojil a akurat by som do eteru krical
        } else {
            total_sent += sent;
        }
    } while (sent > 0 && total_sent != need_to_send);
    return total_sent;
}

// odosle spravu vsetkym pripojenym uzivatelom okrem uzivatela cislo except. Ak except == -1, spravu odosle vsetkym, akurat nevypise spravu na konzolu
void send_yall(char buffer[], int except, int* connections, char names[][MAX_NAME_LENGTH + 1], int num_connected) {
    // vypise na konzolu
    if (except != -1) {
        printf("%s\n", buffer);
    }

    for (int i = 0; i < num_connected; i++) {
        // nechcem tu spravu poslat aj odosielatelovi, este by si myslel ze sa mu vysmievam
        if (i != except) {
            if (send_one(buffer, connections[i]) == -1) {
                fprintf(stderr, "Chyba pri odosielani spravy pouzivatelovi [%s]\n", names[i]);
            }
        }
    }
}

// precita spravu z file descriptoru dlzky najviac max_len a s prislusnymi flagmi
int receive(int fd, char buffer[], int max_len, int flags) {
    int received = 0, total_received = 0;
    do { 
        received = recv(fd, buffer + total_received, max_len - total_received, flags);
        
        // odpojil sa
        if (received == 0) {
            return 0; // obyčajné odpojenie bez chyby
        }

        // ak nastala ina chyba nez ze nemozem citat bez cakania
        if (received == -1) {
            if (errno == EAGAIN) {
                return total_received;
            }
            //fprintf(stderr, "Chyba pri citani z file descriptoru %d\n", fd);
            return -1;  // chyba
        }

        if (received > 0) {
            total_received += received;
        }
    } while ((received > 0) && (total_received < max_len));
    return total_received;
}

// odpoji uzivatela na indexe. Ak send_msg == 1, odosle o odpojeni uzivatela spravu ostatnym, vrati novy najvacsi file descriptor
int disconnect(int index, int send_msg, int sockfd, int* connections, char names[][MAX_NAME_LENGTH + 1], int num_connected) {
    if (shutdown(connections[index], SHUT_WR) == -1) {
        fprintf(stderr, "Pri ukoncovani spojenia s [%s] nastala chyba\n", names[index]);
    } else {
        char buffer[1];
        int received = receive(connections[index], buffer, 1, 0);
        if ((received == -1 && errno != ENOTCONN) || received > 0) {
            fprintf(stderr, "Pri potvrdeni ukoncenia spojenia s [%s] nastala chyba\n", names[index]);
        } 
    }
    
    if (close(connections[index]) == -1) {
        fprintf(stderr, "Nepodarilo sa bezpecne ukoncit spojenie s [%s]\n", names[index]);
    } else if (send_msg) {
        char msg[30 + MAX_NAME_LENGTH];
        sprintf(msg, "Uzivatel [%s] sa odpojil.", names[index]);
        send_yall(msg, index, connections, names, num_connected);
    }

    // posun vsetkych pripojeni
    for (int j = index; j < num_connected - 1; j++) {
        connections[j] = connections[j + 1];
        strcpy(names[j], names[j + 1]);
    }
    strcpy(names[num_connected - 1], "\0");
    num_connected--;

    // najdenie najvacsieho file descriptora kvoli selectu
    int max_fd = sockfd;
    for (int j = 0; j < num_connected; j++) {
        if (connections[j] > max_fd) {
            max_fd = connections[j];
        }
    }
    return max_fd;
}

// odpoji file descriptor
void disconnect_fd(int fd) {
    if (shutdown(fd, SHUT_WR) == -1) {
        fprintf(stderr,"Pri ukoncovani spojenia s pripojenim nastala chyba\n");
    } else {
        char buffer[1];
        int received = receive(fd, buffer, 1, 0);
        if (received == -1 && errno != ENOTCONN) {
            fprintf(stderr, "Pri potvrdeni ukoncenia spojenia s pripojenim nastala chyba\n");
        } 
    }
    
    if (close(fd) == -1) {
        fprintf(stderr, "Nepodarilo sa bezpecne ukoncit spojenie s pripojenim\n");
    }
}

// ukonci vsetky spojenia a vypne server
void quit_server(int sockfd, int* connections, char names[][MAX_NAME_LENGTH + 1], int num_connected) {
    for (int i = 0; i < num_connected; i++) {
        disconnect(i, 0, sockfd, connections, names, num_connected);
    }
    if (close(sockfd) == -1) {
        fprintf(stderr, "Chyba pri zatvarani pocuvacieho socketu\n");
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

/////////////////////////////////////////////

int main (int argc, char* argv[]) {
    int num_connected = 0;
    int connections[MAX_CONNECTIONS];
    char names[MAX_CONNECTIONS][MAX_NAME_LENGTH + 1];
    int max_fd;
    int sockfd;
    int port = 1234;
    int input_length = 0;

    // kontrola argumentov
    if (argc > 2) {
        fprintf(stderr, "Prilis vela argumentov! Zadajte cislo pocuvacieho portu (alebo default = 1234)\n");
		exit(1);
    }

    if (argc == 2) {
        port = atoi(argv[1]);
        if (port <= 0) {
            fprintf(stderr, "Zle cislo portu\n");
            exit(1);
        }
    }

    // vytvorenie socketu
    sockfd = socket(PF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sockfd == -1) {
        fprintf(stderr, "Nepodarilo sa vytvorit socket\n");
		exit(1);
    }

    // vytvorenie adresy
    struct sockaddr_in myaddress;
	myaddress.sin_family = AF_INET;
	myaddress.sin_addr.s_addr = INADDR_ANY;
	myaddress.sin_port = htons(port);

    // bind
    if (bind(sockfd, (struct sockaddr*) &myaddress, sizeof(myaddress)) == -1) {
        fprintf(stderr, "Nepodarilo sa naviazat na adresu\n");
		exit(1);
    }

    // listen
    if (listen(sockfd, 8) == -1) {
        fprintf(stderr, "Chyba pri nastaveni deskriptora na pocuvanie prichadzajucich pripojeni\n");
		exit(1);
    }

    printf("Server bol spusteny na porte %d\nNa vypnutie serveru pouzite /exit, pre viac info pouzite /help\n\n", port);

    // vypnutie kanonickeho modu a echa
    struct termios terminal;
    if (tcgetattr(0, &terminal) == -1) {
        fprintf(stderr, "Nebolo mozne zitit vlastnost terminalu\n");
    } else {
        terminal.c_lflag &= ~ICANON;
        terminal.c_lflag &= ~ECHO;
        if (tcsetattr(0, TCSANOW, &terminal) == -1) {
            fprintf(stderr, "Nebolo mozne zmenit vlastnosti terminalu\n");
        }
    }

    max_fd = sockfd;  // kvoli selectu potrebujem vediet najvacsi file descriptor
    fd_set readfds;  // mnozina pre select
    char buffer[BUFFER_SIZE];  // prosim pouzivatelov aby si neposielali copypasty

    // select
    while (1) {
        // nastav mnozinu vstupov, na ktore sa ma cakat
        FD_ZERO(&readfds);

        FD_SET(sockfd, &readfds);
        FD_SET(0, &readfds);

        for (int i = 0; i < num_connected; i++) {
            FD_SET(connections[i], &readfds);
        }

        int select_num = select(max_fd + 1, &readfds, NULL, NULL, NULL);

        if ((select_num == -1) && (errno == EINTR)) {
            // mohlo nastat skoncenie selektu z ineho dovodu ako moznost citat
            continue;
        }

        // vstup z konzoly
        if (FD_ISSET(0, &readfds)) {
            int read_result = read(0, buffer + input_length, BUFFER_SIZE - input_length - 1);
            if (read_result == -1) {
                // sorry jako, ak uz nejde ani citat z konzoly, tak to tento pocitac tazko zvladne byt serverom
                fprintf(stderr, "Chyba pri citani vstupu z konzoly\n");
                //quit_server();
            }
            
            // spracovavam znak po znaku
            while (read_result > 0) {
                if ((buffer[input_length] == '\x7f' || buffer[input_length] == '\b') && input_length > 0) {  // backspace alebo delete
                    input_length--;
                    if (write(1, "\b \b", 3) != 3) {  // vymaze predosly znak
                        fprintf(stderr, "Chyba pri pisani do stdout\n");
                    }
                } else if (buffer[input_length] == '\x1b' && input_length < BUFFER_SIZE - MAX_NAME_LENGTH - 3) {  // escape characters, ktore vedia narobit sarapatu
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


                    /// prikazy sa zacinaju lomokou /
                    if (buffer[0] == '/') {
                        if (strncmp(buffer, "/exit", 5) == 0) {
                            printf("Vypinam server...\n");
                            quit_server(sockfd, connections, names, num_connected);
                        } else if (strncmp(buffer, "/help", 5) == 0) {
                            printf("====================\n");
                            printf("/help pre zobrazenie pomoci (ako ste si uz vsimli)\n");
                            printf("/exit pre ukoncenie servera\n");
                            printf("/list pre vylistovanie pripojenych uzivatelov\n");
                            printf("/kick <cislo> pre vyhodnie uzivatela oznaceneho prislusnym cislom v /list\n");
                            printf("====================\n");
                        } else if (strncmp(buffer, "/list", 5) == 0) {
                            if (num_connected == 0) {
                                printf("Nie su pripojeni ziadni uzivatelia.\n");
                            } else {
                                printf("====================\n");
                                printf("Aktualne pripojeni:\n");
                                for (int i = 0; i < num_connected; i++) {
                                    printf("%d - %s\n", i + 1, names[i]);
                                }
                                printf("====================\n");
                            }
                        } else if (strncmp(buffer, "/kick", 5) == 0) {
                            // ak je za prikazom medzera + aspon jeden znak
                            if (input_length > 6 && buffer[5] == ' ') {
                                int index = atoi(buffer + 6) - 1;
                                if (index >= 0 && index < num_connected) {
                                    char msg[40 + MAX_NAME_LENGTH];
                                    sprintf(msg, "Uzivatel [%s] bol vyhodeny zo servera", names[index]);
                                    send_yall(msg, index, connections, names, num_connected);
                                    max_fd = disconnect(index, 0, sockfd, connections, names, num_connected);
                                } else {
                                    printf("Tento uzivatel neexistuje\n");
                                }
                            } else {
                                printf("Pouzitie: /kick <cislo pouzivatela> (na zistenie cisla pouzivatela pouzitie /list)\n");
                            }
                        } else {
                            printf("'%s' nie je znamy prikaz\n", buffer);
                        }
                    } else if (input_length > 0) {
                        // posle spravu kazdemu
                        char server_label[BUFFER_SIZE + 16] = "{SERVER} ";  // specialne zatvorecky, aby sa nam pleb netvaril ako server
                        strcat(server_label, buffer);
                        send_yall(server_label, -1, connections, names, num_connected);
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

        // vycistim riadok od pouzivatelovych kecov
        for (int i = 0; i < input_length; i++) {
            if (write(1, "\b \b", 3) != 3) {
                fprintf(stderr, "Chyba pri mazani vstupu\n");
            }
        }

        // nove pripojenie
        if (FD_ISSET(sockfd, &readfds)) {
            // sem ulozim prichadzajuce data
            char from_net[BUFFER_SIZE];

            struct sockaddr_in other;
            socklen_t other_len = sizeof(other);
            
            int new_fd = accept(sockfd, (struct sockaddr*)&other, &other_len);
	        if (new_fd == -1) {
                fprintf(stderr, "Chyba pri prijimani noveho spojenia\n");
            } else if (num_connected < MAX_CONNECTIONS) {
                // ak mame pre nove pripojenie miesto
                //printf("novy fd dieta: %d\n", new_fd);
                connections[num_connected] = new_fd;
                num_connected++;

                int received = receive(new_fd, from_net, MAX_NAME_LENGTH, MSG_DONTWAIT);
                if (received <= 0) {
                    fprintf(stderr, "Chyba (%d) pri pripajani uzivatela s file descriptorom %d\n", errno, new_fd);
                    disconnect(num_connected - 1, 0, sockfd, connections, names, num_connected);
                } else {
                    strncpy(names[num_connected - 1], from_net, MAX_NAME_LENGTH);

                    // koncovy null, pre istotu
                    names[num_connected - 1][received] = '\0';

                    // kvoli selectu potrebujem vediet najvacsi file descriptor
                    if (new_fd > max_fd) {
                        max_fd = new_fd;
                    }

                    // posle novopripojenemu privitanie a ostatnym informaciu o nom
                    if (send_one("Teraz ste pripojeni na server", new_fd) <= 0) {
                        fprintf(stderr, "Chyba pri odosielani uvitacej spravy uzivatelovi [%s]\n", names[num_connected - 1]);
                    }
                    char msg[40 + MAX_NAME_LENGTH];
                    sprintf(msg, "Uzivatel [%s] sa pripojil na server", names[num_connected - 1]);
                    send_yall(msg, num_connected - 1, connections, names, num_connected);
                }
            } else {
                fprintf(stderr, "Server je plny! Nebolo mozne pripojit ucastnika s file descriptorom %d\n", new_fd);
                if (send_one("Tento server je plny, skuste sa pripojit neskor", new_fd) <= 0) {
                    fprintf(stderr, "Chyba pri odosielani spravy o plnom serveri\n");
                }
                disconnect_fd(new_fd);
            }

            select_num--;
        }

        // niekto nieco napisal
        for (int i = 0; i < num_connected && select_num > 0; i++) {
            // dostali sme spravu z tohto pripojenia
            if (FD_ISSET(connections[i], &readfds)) {
                char from_net[BUFFER_SIZE];

                // skopirujem meno
                int name_length = strlen(names[i]);

                // sprava bude vo formate "[meno] sprava"
                from_net[0] = '[';
                strcpy(from_net + 1, names[i]);
                from_net[name_length + 1] = ']';
                from_net[name_length + 2] = ' ';
                from_net[name_length + 3] = '\0';

                // nacitam spravu do buffera
                int received = receive(connections[i], from_net + name_length + 3, BUFFER_SIZE - name_length - 4, MSG_DONTWAIT);
                
                // ak nastala ina chyba nez ze nemozem citat bez cakania
                if ((received == -1) && (errno != EAGAIN) && (errno != EWOULDBLOCK)) {
                    fprintf(stderr, "Chyba pri pokuse o prijatie spravy od [%s]\n", names[i]);
                    max_fd = disconnect(i, 1, sockfd, connections, names, num_connected);  // aby som sa nezacyklil v chybach (ano, stalo sa to)
					i--;  // vo for slucke potrebujem este raz skontrolovat deskriptor na tejto pozicii, lebo som ich posunul
                }

                // odpojil sa a treba ho odstranit zo zoznamu
                if (received == 0) {
                    max_fd = disconnect(i, 1, sockfd, connections, names, num_connected);
                    i--;  // vo for slucke potrebujem este raz skontrolovat deskriptor na tejto pozicii, lebo som ich posunul
                }

                if (received > 0) {
                    // koncovy null
                    from_net[received + name_length + 3] = '\0';
                    // ak napisal prikaz /list, poslem mu vypis
                    if (strncmp(from_net + name_length + 3, "/list", 5) == 0) {
                        char msg[70 + num_connected * (MAX_NAME_LENGTH + 8)];  // teraz sa to tam isto zmesti
                        sprintf(msg, "====================\n");
                        sprintf(msg + strlen(msg), "Aktualne pripojeni:\n");
                        for (int j = 0; j < num_connected; j++) {
                            sprintf(msg + strlen(msg), "%d - %s\n", j + 1, names[j]);
                        }
                        sprintf(msg + strlen(msg), "====================");
                        if (send_one(msg, connections[i]) <= 0) {
                            fprintf(stderr, "Chyba pri odosielani zoznamu pripojenych uzivatelovi [%s]\n", names[i]);
                        }
                    } else {
                        // preposlem spravu vsetkym
                        send_yall(from_net, i, connections, names, num_connected);
                    }
                }
                
                select_num--;
            }
        }

        // znova zapisem uzivatelov riadok
        if (write(1, buffer, input_length) != input_length) {
            fprintf(stderr, "Chyba pri opatovnom pisani vstupu\n");
        }
    }     
    return 0;
}