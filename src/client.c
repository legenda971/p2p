#include <stdio.h>
#include <string.h>
#include <ncurses.h> /* (terminal) Install libncurses5-dev */
#include <stdlib.h>  /* exit() */
#include <math.h>    /* round() */
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <pthread.h>
#include <errno.h>
#include <sys/time.h>
#include <limits.h> /* SCHAR_MAX */

#include "define/metadata.h"
#include "define/request_respons/database.h"
#include "define/peer.h"
#include "define/request_respons/client.h"

#define PORT 5320
#define MAX_PORT 54000
#define MIN_PORT 53000
#define MAX_CONNEDTED_CLIENT 10

void zoznamPeerov(WINDOW *window, int socket_fd, int y_middle, int x_middle);
void uploadMetadata(WINDOW *window, int fd_server, int y_middle, int x_middle);
void downloadMetadata(WINDOW *window, int socket_fd, int y_middle, int x_middle);
void downloadedFile(WINDOW *window, int socked_fd, int y_middle, int x_middle);
//void thread_downloaingFile(int server_fd, struct metadata *metadata, FILE *file);
void thread_downloaingFile(int server_fd, struct metadata *metadata, char *file_buffer, char *bitmap_buffer);
void sendBlock(int wr_fd, char *buffer_file, int number_of_block, int size_block, int file_size);
void saveBlock(int wr_fd, char *buffer_file, int number_of_block, int size_block, int file_size);
char isFull(char *bitmap, int size_bitmap, int number_of_block);
struct client *deleteClient(struct client *begin_client_list, int client_order);
void connectToSeeder(struct client *begin_client_list, int size_peer_list, struct peer *peer_list);
void newClientToList(struct client *client_list, struct client *new_client);
void initMasterFD(int *master_fd, int fd_server);

typedef struct downloadingFile
{
    struct metadata _metadata;
    char complete;     // V percentach
    pthread_t *thread; // Thread ktory stahuje
    struct downloadingFile *next;
} downloadingFile;

typedef struct client
{
    char stat;
    int fd_socket;
    char request;
    char *bitmap;

    struct client *next;
};

int main(int argc, char **argv)
{
    struct downloadingFile *begin_downloading_file;
    /* Socket initial */

    int socketListen;
    if ((socketListen = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Chyba pri vytvarani socktu.\n");
        exit(-1);
    }

    struct sockaddr_in adresa;
    bzero((char *)&adresa, sizeof(struct sockaddr_in));

    adresa.sin_family = AF_INET;
    adresa.sin_port = htons(PORT);
    adresa.sin_addr.s_addr = inet_addr("127.0.0.1");

    if ((connect(socketListen, (struct sockaddr *)&adresa, sizeof(adresa))) != 0)
    {
        perror("Chyba pri pripajani sa:\n");
        // cislo suborou musi byt 0 alebo kladne, socked file descriptor je cislo suboru
        exit(-1004);
    }

    printf("Client - Pripojený");

    /* NCURSES initial */
    initscr();
    noecho();
    cbreak();

    int y_max, x_max;
    getmaxyx(stdscr, y_max, x_max);

    WINDOW *menuwin = newwin(y_max - 1, x_max - 1, 1, 1);

    keypad(menuwin, true);

    int y_middle = y_max / 2;
    int x_middle = x_max / 2;

    const char *menu[] = {"Nahrat subor", "Stiahnut Subor", "Stahovane subory", "Koniec"};
    int size_menu = sizeof(menu) / sizeof(menu[0]);

    int akcia;
    int selected = 0;
    while (1)
    {
        wclear(menuwin);
        box(menuwin, 0, 0);

        for (int i = 0; i < size_menu; i++)
        {
            if (i == selected)
                wattron(menuwin, A_REVERSE);

            mvwprintw(menuwin, y_middle - (int)(size_menu / 2) + i, x_middle - (int)(strlen(menu[i]) / 2), "%s", menu[i]);
            wattroff(menuwin, A_REVERSE);
        }

        akcia = wgetch(menuwin);

        switch (akcia)
        {
        case KEY_UP:
            if (selected > 0)
                selected--;
            break;
        case KEY_DOWN:
            if (selected < (size_menu - 1))
                selected++;
            break;
        default:
            break;
        }

        if (akcia == 10)
        {
            switch (selected)
            {
            case 0:
                uploadMetadata(menuwin, socketListen, y_middle, x_middle);
                break;
            case 1:
                downloadMetadata(menuwin, socketListen, y_middle, x_middle);
                break;

            case 2:
                //downloadedFile(menuwin, socketListen, y_middle, x_middle);
                //zoznamPeerov(menuwin, socketListen, y_middle, x_middle);
                break;

            case 3:
                endwin();
                exit(0);
            default:
                break;
            }
        }

        wrefresh(menuwin);
    }

    endwin();
    exit(1);
}

void write_to_fd(int fd, void *buffer, size_t size)
{
    if (write(fd, buffer, size) < 0)
    {
        perror("Chyba pri zapise do fd :");
        exit(-1);
    }
}

void read_from_fd(int fd, void *buffer, size_t size)
{
    if (read(fd, buffer, size) < size)
    {
        perror("Chyba pri citani z fd :");
        exit(-2);
    }
}

void zoznamPeerov(WINDOW *window, int socket_fd, int y_middle, int x_middle)
{
    wclear(window);
    box(window, 0, 0);

    char request = (enum request)PEER_LIST;

    write_to_fd(socket_fd, &request, sizeof(request));

    int size_peer;
    read_from_fd(socket_fd, &size_peer, sizeof(size_peer));

    char *buffer = (char *)malloc(sizeof(struct peer) * size_peer);

    read_from_fd(socket_fd, buffer, sizeof(struct peer) * size_peer);

    for (int i = 0; i < size_peer; i++)
    {
        struct peer *temp = buffer + (sizeof(struct peer) * i);
        mvwprintw(window, y_middle - (int)(size_peer / 2) + i, x_middle - 10, "%d.%d.%d.%d:%d", temp->ip[0], temp->ip[1], temp->ip[2], temp->ip[3], temp->port);
    }

    while ((wgetch(window)) != 10)
        ;
}

void downloadMetadata(WINDOW *window, int socket_fd, int y_middle, int x_middle)
{
    wclear(window);
    box(window, 0, 0);

    char request = (enum request)METADATA;
    write_to_fd(socket_fd, &request, sizeof(request));

    struct metadata new_metadata;
    read_from_fd(socket_fd, &new_metadata, sizeof(struct metadata));

    mvwprintw(window, y_middle - 2, x_middle - 10, "Meno suboru\t: %s", new_metadata.name);
    mvwprintw(window, y_middle - 1, x_middle - 10, "Velkost suboru\t: %d", new_metadata.file_size);
    mvwprintw(window, y_middle, x_middle - 10, "Velkost bloku\t: %d", new_metadata.size_block);

    while ((wgetch(window)) != 10)
        ;

    int pocet_blokov = (new_metadata.file_size - (new_metadata.file_size % new_metadata.size_block)) / new_metadata.size_block;
    pocet_blokov++;

    char *file_buffer = (char *)malloc(new_metadata.file_size);
    char *bitmap_buffer = (char *)malloc((pocet_blokov + (8 - (pocet_blokov % 8))) / 8);

    memset(bitmap_buffer, 0, (pocet_blokov + (8 - (pocet_blokov % 8))) / 8);

    thread_downloaingFile(socket_fd, &new_metadata, file_buffer, bitmap_buffer);
}

void thread_downloaingFile(int server_fd, struct metadata *metadata, char *file_buffer, char *bitmap_buffer)
{
    endwin();
    int master_fd;
    struct client *begin_client_list = 0;

    /* MASTER STAT */
    char my_stat = 0;

    /* BIT MAP */
    int number_of_block = (metadata->file_size + (metadata->size_block - (metadata->file_size % metadata->size_block))) / metadata->size_block;
    int size_bitmap = (number_of_block + (8 - (number_of_block % 8))) / 8;

    /* Zoznam peerov */
    char request = (enum request)PEER_LIST;
    int size_peer_list;
    write_to_fd(server_fd, &request, sizeof(request));
    read_from_fd(server_fd, &size_peer_list, sizeof(size_peer_list));

    struct peer *peer_list = (struct peer *)malloc(sizeof(struct peer) * size_peer_list);

    read_from_fd(server_fd, peer_list, sizeof(struct peer) * size_peer_list);

    /* Pripajanie sa na seederov */
    connectToSeeder(begin_client_list, size_peer_list, peer_list);

    /* Init Master Socket */
    if ((master_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Server - Chyba pri vytvarani socktu.\n");
        exit(-1);
    }

    
    int opt = 1;
    if (setsockopt(master_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("Setsockopt");
        exit(-5);
    }

    struct sockaddr_in addressMaster;
    bzero((char *)&addressMaster, sizeof(struct sockaddr_in));

    addressMaster.sin_family = AF_INET;         //Pripojenie zo siete
    addressMaster.sin_addr.s_addr = INADDR_ANY; // Prijimanie spojenie od kazdej IP

    /* Random Listen PORT */
    unsigned short random_port;
    while (1)
    {
        srand(time(NULL));
        random_port = ((rand() % (MAX_PORT - MIN_PORT)) + MIN_PORT);

        addressMaster.sin_port = htons(random_port);
        int error = bind(master_fd, (struct sockaddr *)&addressMaster, sizeof(addressMaster));

        if(error < 0)
            continue;
        else
            break;
    }

    printf("Port je : %d\n", random_port);

    char buffer[sizeof(unsigned short) + 1];
    buffer[0] = (enum request)NEW_PEER;
    *((unsigned short *)(buffer + 1)) = random_port;

    write_to_fd(server_fd, buffer, sizeof(buffer));

    if (listen(master_fd, 10) != 0)
    {
        perror("Server - Chyba pri nastavovani pocuvania.\n");
        exit(-3);
    }

    /* SET MASTER STAT */
    if (!(isFull(bitmap_buffer, size_bitmap, number_of_block)))
    {
        my_stat = (enum CLIENT)DOWNLOADING;
        printf("Stahujme\n");
    }
    else
    {
        my_stat = (enum CLIENT)DOWNLOADED;
        printf("Stiahnute !\n");
    }
    int max_fd;
    fd_set readfd, writefd;
    while (1)
    {
        FD_ZERO(&readfd);
        FD_ZERO(&writefd);

        FD_SET(master_fd, &readfd);
        //FD_SET(master_fd, &writefd);

        max_fd = master_fd;
        struct client *temp = begin_client_list;
        while (temp != 0)
        {
            FD_SET(temp->fd_socket, &readfd);
            //FD_SET(temp->fd_socket, &writefd);
            if (temp->fd_socket > max_fd)
                max_fd = temp->fd_socket;

            temp = temp->next;
        }

        printf("Cakam\n");
        int activity = select(max_fd + 1, &readfd, &writefd, NULL, NULL);
        printf("ZMENA !!!!");

        if (activity < 0)
        {
            perror("Select Error :");
            exit(-1);
        }

        /* Nove pripojenie */
        if (FD_ISSET(master_fd, &readfd))
        {
            printf("Pokus o pripojenie\n}");
            struct client *new_client = (struct client *)malloc(sizeof(struct client));
            bzero(new_client, sizeof(struct client));
            struct sockaddr_in adresa;

            socklen_t size;
            if ((new_client->fd_socket = accept(master_fd, (struct sockaddr *)&adresa, &size)) < 0)
            {
                perror("Server - Chyva pri vytvarani spojenia s klientom.\n");
                free(new_client);
                continue;
            }

            printf("Nové pripojenie\n");

            if(begin_client_list = 0)
                begin_client_list = new_client;
            else
                newClientToList(begin_client_list, new_client);
        }

        temp = begin_client_list;
        int order = 0;
        while (temp != 0)
        {
            if (FD_ISSET(temp->fd_socket, &readfd))
            {
                printf("Zmena na clientovi.\n");

                int size_of_readed;
                char buffer;
                /* Odpojenie klientu */
                if ((size_of_readed = read(temp->fd_socket, &buffer, sizeof(buffer))) < 1)
                {
                    if (temp == 0)
                        printf("Client sa odpojil\n");
                    if (temp < 0)
                        printf("Doslo k chybe\n");

                    struct client *delete_client = temp;
                    if (order = 0)
                    {
                        begin_client_list = temp->next;
                    }
                    else
                    {
                        for (int i = 0; i < order - 1; i++)
                            temp = temp->next;

                        temp->next = delete_client->next;
                        free(delete_client);
                    }

                    temp = temp->next;
                    continue;
                }

                if (!temp->request)
                    temp->request = buffer;
            }

            temp = temp->next;
            order++;
        }

        if (FD_ISSET(temp->fd_socket, &writefd))
        {
            printf("Request : %d\n", temp->request);

            /* RESPONSE TO CLIENT */
            char *buffer;
            switch ((enum CLIENT)temp->request)
            {
            case STATS_REQUEST:
                buffer = (char *)malloc(sizeof(char) * 2);
                buffer[0] = (enum CLIENT)STATS_RESPONSE;
                buffer[1] = my_stat;
                write_to_fd(temp->fd_socket, buffer, sizeof(char) * 2);
                free(buffer);
                break;

            case BITMAP_REQUEST:
                buffer = (char *)malloc(sizeof(char) + size_bitmap);
                buffer[0] = (enum CLIENT)BITMAP_RESPONSE;
                memcpy(buffer + 1, bitmap_buffer, size_bitmap);
                write_to_fd(temp->fd_socket, buffer, sizeof(char) + size_bitmap);
                free(buffer);
                break;

            case STATS_RESPONSE:
                read_from_fd(temp->fd_socket, temp->stat, sizeof(char));
                break;

            case BITMAP_RESPONSE:
                read_from_fd(temp->fd_socket, temp->bitmap, size_bitmap);
                break;

            case SEND_BLOCK_REQUEST:
                sendBlock(temp->fd_socket, file_buffer, number_of_block, metadata->size_block, metadata->file_size);
                break;

            case SEND_BLOCK_RESPONSE:
                saveBlock(temp->fd_socket, file_buffer, number_of_block, metadata->size_block, metadata->file_size);
                break;

            default:
                printf("Nedefinovaný request\n");
            }
        }

        if(my_stat == (enum CLIENT)DOWNLOADED)
            continue;

        /* REQUEST TO CLIENTS */
        for (struct client *temp = begin_client_list; temp != 0; temp = temp->next)
        {
            if (FD_ISSET(temp->fd_socket, &writefd))
            {
                if (my_stat == (enum CLIENT)DOWNLOADING)
                {
                    if (temp->stat == 0)
                    {
                        char request = (enum CLIENT)STATS_REQUEST;
                        write_to_fd(temp->fd_socket, &request, sizeof(request));
                        continue;
                    }

                    if (temp->bitmap == 0 && temp->stat == (enum CLIENT)DOWNLOADING)
                    {
                        char request = (enum CLIENT)BITMAP_REQUEST;
                        write_to_fd(temp->fd_socket, &request, sizeof(request));
                        continue;
                    }

                    if (temp->stat == (enum CLIENT)DOWNLOADED)
                    {
                    }
                }
            }
        }
    }
}

void uploadMetadata(WINDOW *window, int fd_server, int y_middle, int x_middle)
{
    int akcia;
    wclear(window);
    box(window, 0, 0);
    mvwprintw(window, y_middle - 1, x_middle - (int)(27 / 2), "Prosim zadajte meno suboru");

    char name_dir[50];
    memset(name_dir, 0, sizeof(name_dir));

    while (akcia = wgetch(window))
    {
        /* ENTER */
        if (akcia == 10)
            break;

        /* BACKSPACE */
        if (akcia == 263)
        {
            /* I WAS HERE */
        }
        else
            name_dir[strlen(name_dir)] = akcia;

        mvwprintw(window, y_middle, x_middle - (int)(strlen(name_dir) / 2), "%s", name_dir);
        wrefresh(window);
    }

    struct stat file_stat;
    if (stat(name_dir, &file_stat) == -1)
    {
        wattron(window, A_REVERSE);
        mvwprintw(window, y_middle + 1, x_middle - (int)(66 / 2), "2. ERROR - Subor sa neda otvorit. Stlacte ENTER pre navrat do menu .");
        wattroff(window, A_REVERSE);
        while (wgetch(window) != 10)
            ;
    }

    int velkost_bloku = 1;
    int pocet_blokov;
    wclear(window);

    /* Vyber velkosti bloku */
    akcia = 0;
    do
    {

        switch (akcia)
        {
        case KEY_RIGHT:
            if (velkost_bloku < file_stat.st_size)
                velkost_bloku++;
            break;
        case KEY_LEFT:
            if (velkost_bloku > 1)
                velkost_bloku--;
            break;
        }

        double temp = ((double)file_stat.st_size) / ((double)velkost_bloku);
        if ((int)temp < temp)
            pocet_blokov = (temp) + 1;
        else
            pocet_blokov = temp;

        wclear(window);
        box(window, 0, 0);

        mvwprintw(window, y_middle - 2, x_middle - 10, "Meno suboru\t: %s", name_dir);
        mvwprintw(window, y_middle - 1, x_middle - 10, "Velkost suboru\t: %d", file_stat.st_size);
        mvwprintw(window, y_middle, x_middle - 10, "Pocet blokov\t: %d", pocet_blokov);
        mvwprintw(window, y_middle + 1, x_middle - 10, "Velkost bloku\t: %d", velkost_bloku);

        wattron(window, A_REVERSE);
        mvwprintw(window, y_middle + 3, x_middle - (int)(60 / 2), "Pre potvrdenie stlacte ENTER. Velkost bloku menite sipkami.");
        wattroff(window, A_REVERSE);
        wrefresh(window);
    } while ((akcia = wgetch(window)) != 10);

    struct metadata new_metadata;
    new_metadata.size_block = velkost_bloku;
    new_metadata.file_size = file_stat.st_size;
    strncpy(new_metadata.name, name_dir, sizeof(new_metadata.name));

    while ((akcia = wgetch(window)) != 10)
        ;
    /* Nahrat metadata na server */

    char buffer[sizeof(struct metadata) + 1];
    /* Request */

    buffer[0] = (enum request)NEW_METADATA;
    //strcpy(buffer + 1, &new_metadata);
    memcpy(buffer + 1, &new_metadata, sizeof(struct metadata));

    if (write(fd_server, buffer, sizeof(buffer)) < 0)
    {
        perror("Chyba pri zapisovani do fd");
        exit(-3);
    }

    endwin();

    char *file_buffer = (char *)malloc(new_metadata.file_size);
    char *bitmap_buffer = (char *)malloc((pocet_blokov + (8 - (pocet_blokov % 8))) / 8);

    memset(bitmap_buffer, 1, (pocet_blokov + (8 - (pocet_blokov % 8))) / 8);

    thread_downloaingFile(fd_server, &new_metadata, file_buffer, bitmap_buffer);
}

void sendBlock(int wr_fd, char *buffer_file, int number_of_block, int size_block, int file_size)
{
    int order_of_block;
    read_from_fd(wr_fd, &order_of_block, sizeof(order_of_block));

    char *buffer = (char *)malloc(size_block + sizeof(char) + sizeof(int));

    buffer[0] = (enum CLIENT)SEND_BLOCK_RESPONSE;
    *((int *)(buffer + 1)) = order_of_block;

    char *buffer_block = buffer[sizeof(char) + sizeof(int) - 1];

    if (order_of_block != (number_of_block - 1))
    {
        for (int i = 0; i < size_block; i++)
            buffer_block[i] = buffer_file[(size_block * order_of_block) + i];
    }
    else
    {
        for (int i = 0; i < file_size % size_block; i++)
            buffer_block[i] = buffer_file[(size_block * order_of_block) + i];
    }

    write_to_fd(wr_fd, buffer_block, size_block);

    free(buffer);
}

void saveBlock(int wr_fd, char *buffer_file, int number_of_block, int size_block, int file_size)
{
    int order_of_block;
    read_from_fd(wr_fd, &order_of_block, sizeof(order_of_block));

    char *buffer_block = (char *)malloc(size_block);

    read_from_fd(wr_fd, buffer_block, size_block);

    if (order_of_block != (number_of_block - 1))
    {
        for (int i = 0; i < size_block; i++)
            buffer_file[(size_block * order_of_block) + i] = buffer_block[i];
    }
    else
    {
        for (int i = 0; i < file_size % size_block; i++)
            buffer_file[(size_block * order_of_block) + i] = buffer_block[i];
    }

    free(buffer_block);
}

void connectToSeeder(struct client *begin_client_list, int size_peer_list, struct peer *peer_list)
{
    printf("SEED");
    struct sockaddr_in adresa;
    adresa.sin_family = AF_INET;
    bzero((char *)&adresa, sizeof(struct sockaddr_in));

    for (int i = 0; i < size_peer_list; i++)
    {
        struct client *new_client = (struct client *)malloc(sizeof(struct client));
        bzero(new_client, sizeof(sizeof(struct client)));

        new_client->fd_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (new_client->fd_socket < 0)
        {
            perror("Chyba pri vytvarani socketu :");
            free(new_client);
            continue;
        }

        adresa.sin_port = htons(peer_list[i].port);
        char number_to_string[25];
        sprintf(number_to_string, "%d.%d.%d.%d", peer_list[i].ip[0], peer_list[i].ip[1], peer_list[i].ip[2], peer_list[i].ip[3]);
        printf("Addresa je %s:%d\n", number_to_string, peer_list[i].port);
        adresa.sin_addr.s_addr = inet_addr("127.0.0.1");

        if (connect(new_client->fd_socket, (struct sockaddr *)&adresa, sizeof(adresa)) != 0)
        {
            perror("Chyba pri pripajani na socket :");
            free(new_client);
            continue;
        }

        if (!begin_client_list)
            begin_client_list = new_client;
        else
            newClientToList(begin_client_list, new_client);

        printf("Pripojene !\n");
    }
}

void newClientToList(struct client *client_list, struct client *new_client)
{

    while (client_list->next != 0)
        client_list = client_list->next;

    client_list->next = new_client;
}

struct client *deleteClient(struct client *begin_client_list, int client_order)
{
    if (!client_order)
    {
        free(begin_client_list);
        return 0;
    }

    struct client *temp;
    client_order--;
    for (; client_order != 0; client_order--)
        begin_client_list = begin_client_list->next;

    temp = begin_client_list->next;
    begin_client_list->next = temp->next;

    free(temp);

    return begin_client_list->next;
}

void initMasterFD(int *master_fd, int fd_server)
{
    if ((*master_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Server - Chyba pri vytvarani socktu.\n");
        exit(-1);
    }

    
    int opt = 1;
    if (setsockopt(*master_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0)
    {
        perror("Setsockopt");
        exit(-5);
    }

    struct sockaddr_in addressMaster;
    bzero((char *)&addressMaster, sizeof(struct sockaddr_in));

    addressMaster.sin_family = AF_INET;         //Pripojenie zo siete
    addressMaster.sin_addr.s_addr = INADDR_ANY; // Prijimanie spojenie od kazdej IP

    /* Random Listen PORT */
    unsigned short random_port;
    while (1)
    {
        random_port = ((rand() % (MAX_PORT - MIN_PORT)) + MIN_PORT);

        addressMaster.sin_port = htons(random_port);
        int error = bind(*master_fd, (struct sockaddr *)&addressMaster, sizeof(addressMaster));
        /*
        if ((error > 0) && (error != EADDRINUSE))
        {
            perror("Bind Error");
            exit(-5);
        }
        else
            break;*/

        if(error < 0 && errno != EADDRINUSE)
            perror("Bind chyba :");
        else if (error > 0)
            break;
    }

    printf("Port je : %d\n", random_port);

    char buffer[sizeof(unsigned short) + 1];
    buffer[0] = (enum request)NEW_PEER;
    *((unsigned short *)(buffer + 1)) = random_port;

    write_to_fd(fd_server, buffer, sizeof(buffer));

    if (listen(*master_fd, 5) < 0)
    {
        perror("Server - Chyba pri nastavovani pocuvania.\n");
        exit(-3);
    }
}

char isFull(char *bitmap, int size_bitmap, int number_of_block)
{
    for (int i = 0; i < size_bitmap - 1; i++)
    {
        if (bitmap[i] != SCHAR_MAX)
            return 0;
    }

    char last_block = bitmap[size_bitmap - 1];
    char temp = 1;

    temp << ((8 % number_of_block) - 1);

    if (last_block != temp)
        return 0;
    else
        return 1;
}