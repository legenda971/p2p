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
void thread_downloaingFile(int server_fd, struct metadata *metadata, char *file_buffer, char *bitmap_buffer);
void sendBlock(int wr_fd, char *buffer_file, int number_of_block, int size_block, int file_size);
void saveBlock(int wr_fd, char *buffer_file, int number_of_block, int size_block, int file_size, char *bitmap_buffer);
char isFull(char *bitmap, int size_bitmap, int number_of_block);
struct client *newClientToList(struct client *client_list, struct client *new_client); // return poiter to begin list
struct client *deleteClient(struct client *begin_client_list, struct client *delete_client);
void initMasterFD(int *master_fd, int fd_server);
struct client *connectToSeeder(struct client *begin_client_list, int size_peer_list, struct peer *peer_list);

int numberOfBlock(int size_file, int size_block);
int sizeBitmap(int number_of_block);

void statsRequest(int socket_fd, int my_stats);
void bitmapRequest(int socket_fd, char *bitmap_buffer, int size_bitmap);

char blockInBitmap(char *bitmap_buffer, int order_of_block);
void blockToBitmap(char *bitmap_buffer, int order_of_block);
void printBitmap(char *bitmap_buffer, int size_bitmap);

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
    int number_of_block = numberOfBlock(metadata->file_size, metadata->size_block);
    int size_bitmap = sizeBitmap(number_of_block);

    /* Zoznam peerov */
    char request = (enum request)PEER_LIST;
    int size_peer_list;
    write_to_fd(server_fd, &request, sizeof(request));
    read_from_fd(server_fd, &size_peer_list, sizeof(size_peer_list));

    struct peer *peer_list = (struct peer *)malloc(sizeof(struct peer) * size_peer_list);

    read_from_fd(server_fd, peer_list, sizeof(struct peer) * size_peer_list);

    /* Pripajanie sa na seederov */
    begin_client_list = connectToSeeder(begin_client_list, size_peer_list, peer_list);

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

        if (error < 0)
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

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    /* SET MASTER STAT */
    if (!isFull(bitmap_buffer, size_bitmap, number_of_block))
    {
        my_stat = (enum CLIENT)DOWNLOADING;
        printf("KAPPA - Stahujme\n");
    }
    else
    {
        my_stat = (enum CLIENT)DOWNLOADED;
        printf("KAPPA - Stiahnute !\n");
    }

    int max_fd;
    fd_set readfd, writefd;
    while (1)
    {
        FD_ZERO(&readfd);
        FD_ZERO(&writefd);

        FD_SET(master_fd, &readfd);
        FD_SET(master_fd, &writefd);

        max_fd = master_fd;
        struct client *temp = begin_client_list;
        while (temp != 0)
        {
            FD_SET(temp->fd_socket, &readfd);
            FD_SET(temp->fd_socket, &writefd);
            if (temp->fd_socket > max_fd)        /*
        if ((error > 0) && (error != EADDRINUSE))
        {
            perror("Bind Error");
            exit(-5);
        }
        else
            break;*/
                max_fd = temp->fd_socket;

            temp = temp->next;
        }

        sleep(1);
        //printf("---------------------------------\n");
        int activity = select(max_fd + 1, &readfd, &writefd, NULL, &tv);

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

            begin_client_list = newClientToList(begin_client_list, new_client);
        }

        /* Request from client */
        for (struct client *temp = begin_client_list; temp != 0; temp = temp->next)
        {
            if (FD_ISSET(temp->fd_socket, &readfd))
            {
                char request;
                if (read(temp->fd_socket, &request, sizeof(request)) == 0)
                {
                    printf("Klient sa odpojil\n");
                    temp = deleteClient(begin_client_list, temp);
                    if (!temp)
                        break;
                    else
                        continue;
                }

                if (temp->request == 0)
                    temp->request = request;
            }
            else
                continue;

            /* RESPONSE TO REQUEST FROM CLIENT */
            if (FD_ISSET(temp->fd_socket, &writefd))
            {
                printf("Request from client : %d\n", temp->request);

                switch ((enum CLIENT)temp->request)
                {
                case STATS_REQUEST:
                    statsRequest(temp->fd_socket, my_stat);
                    break;

                case BITMAP_REQUEST:
                    bitmapRequest(temp->fd_socket, bitmap_buffer, size_bitmap);
                    break;

                case STATS_RESPONSE:
                    read_from_fd(temp->fd_socket, &temp->stat, sizeof(char));
                    break;

                case BITMAP_RESPONSE:
                    temp->bitmap = (char *)malloc(size_bitmap);
                    read_from_fd(temp->fd_socket, temp->bitmap, size_bitmap);
                    printf("Response bitmap :");
                    for (int i = 0; i < size_bitmap; i++)
                        for (int o = 0; o < 8; o++)
                            printf("%d", !!((temp->bitmap[i] << o) & 128));
                    printf("\n");
                    break;

                case SEND_BLOCK_REQUEST:
                    printf("SEND BLOCK REQUEST\n");
                    sendBlock(temp->fd_socket, file_buffer, number_of_block, metadata->size_block, metadata->file_size);
                    break;

                case SEND_BLOCK_RESPONSE:
                    printf("SEND BLOCK RESPONSE\n");
                    saveBlock(temp->fd_socket, file_buffer, number_of_block, metadata->size_block, metadata->file_size, bitmap_buffer);

                    printf("Moja nova bitmapa :\n");
                    printBitmap(bitmap_buffer, size_bitmap);

                    if (!isFull(bitmap_buffer, size_bitmap, number_of_block))
                    {
                        my_stat = (enum CLIENT)DOWNLOADING;
                        printf("KAPPA - Stahujme\n");
                    }
                    else
                    {
                        my_stat = (enum CLIENT)DOWNLOADED;
                        printf("KAPPA - Stiahnute !\n");
                        char *buffer = (char *)malloc(metadata->file_size + 1);
                        memcpy(buffer, file_buffer, metadata->file_size);
                        buffer[metadata->file_size] = 0;
                        printf("%s\n", buffer);
                    }
                    break;

                default:
                    printf("Nedefinovaný request alebo response \n");
                }

                temp->request = 0;
            }
        }

        if (my_stat == (enum CLIENT)DOWNLOADED)
            continue;

        /* Request to client */
        char *request_bitmap = (char *)malloc(size_bitmap);
        bzero(request_bitmap, size_bitmap);
        for (struct client *temp = begin_client_list; temp != 0; temp = temp->next)
        {
            if (FD_ISSET(temp->fd_socket, &writefd))
            {
                if (temp->stat == 0)
                {
                    char buffer = (enum CLIENT)STATS_REQUEST;
                    write_to_fd(temp->fd_socket, &buffer, sizeof(buffer));
                    continue;
                }

                if (temp->bitmap == 0)
                {
                    char buffer = (enum CLIENT)BITMAP_REQUEST;
                    write_to_fd(temp->fd_socket, &buffer, sizeof(buffer));
                    continue;
                }

                /* Bitmap */
                for (int i = 0; i < number_of_block; i++)
                {
                    int order_block;
                    if (i < 8)
                        order_block = 0;
                    else
                        order_block = (i - (i % 8)) / 8;

                    char request_block = !blockInBitmap(request_bitmap, i);
                    char my_block = !blockInBitmap(bitmap_buffer, i);
                    char client_block = blockInBitmap(temp->bitmap, i);

                    printf("R %d | M %d | C %d\n", request_block, my_block, client_block);

                    if ((request_block && my_block) && client_block)
                    {
                        //printf("Blok ok \n");
                        char buffer[sizeof(char) + sizeof(int)];
                        buffer[0] = (enum CLIENT)SEND_BLOCK_REQUEST;
                        *((int *)(buffer + sizeof(char))) = i;

                        printf("Request - blok : %d\n", i);
                        write_to_fd(temp->fd_socket, &buffer, sizeof(buffer));

                        blockToBitmap(request_bitmap, i);

                        break;
                    }
                }
            }
            else
                printf("FD nema pristup\n");
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
            name_dir[strlen(name_dir) - 1] = 0;
            mvwprintw(window, y_middle, x_middle - (int)(strlen(name_dir) / 2), "          ");
        }
        else
            name_dir[strlen(name_dir)] = akcia;

        mvwprintw(window, y_middle, x_middle - (int)(strlen(name_dir) / 2), " %s", name_dir);
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

    FILE *file = fopen(name_dir, "rb");

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

        pocet_blokov = numberOfBlock(file_stat.st_size, velkost_bloku);

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

    wclear(window);
    endwin();

    int size_bitmap = sizeBitmap(pocet_blokov);

    char *file_buffer = (char *)malloc(new_metadata.file_size);
    char *bitmap_buffer = (char *)malloc(size_bitmap);

    fread(file_buffer, new_metadata.file_size, 1, file);

    /* Set bit map to 1 */

    for (int i = 0; i < size_bitmap - 1; i++)
    {
        bitmap_buffer[i] = 1;
        for (int o = 0; o < 7; o++)
        {
            bitmap_buffer[i] = bitmap_buffer[i] << 1;
            bitmap_buffer[i]++;
        }
    }

    /* Last Block */
    bitmap_buffer[size_bitmap - 1] = 1;
    for (int i = 0; i < ((pocet_blokov % 8) ? (pocet_blokov % 8) - 1 : 7); i++)
    {
        bitmap_buffer[size_bitmap - 1] = bitmap_buffer[size_bitmap - 1] << 1;
        bitmap_buffer[size_bitmap - 1]++;
    }

    thread_downloaingFile(fd_server, &new_metadata, file_buffer, bitmap_buffer);
}

void sendBlock(int wr_fd, char *buffer_file, int number_of_block, int size_block, int file_size)
{
    int order_of_block;
    read_from_fd(wr_fd, &order_of_block, sizeof(order_of_block));

    printf("Posielam blok : %d\n", order_of_block);

    char *buffer = (char *)malloc(size_block + sizeof(char) + sizeof(int));

    buffer[0] = (enum CLIENT)SEND_BLOCK_RESPONSE;
    *((int *)(buffer + sizeof(char))) = order_of_block;

    if (order_of_block != (number_of_block - 1) || !(file_size % size_block))
    {
        memcpy((buffer + sizeof(int) + sizeof(char)), buffer_file + (size_block * order_of_block), size_block);
    }
    else
    {
        memcpy((buffer + sizeof(int) + sizeof(char)), buffer_file + (size_block * order_of_block), size_block - (file_size % size_block));
    }

    write_to_fd(wr_fd, buffer, size_block + sizeof(char) + sizeof(int));

    free(buffer);
}

void saveBlock(int wr_fd, char *buffer_file, int number_of_block, int size_block, int file_size, char *bitmap_buffer)
{
    int order_of_block;
    read_from_fd(wr_fd, &order_of_block, sizeof(order_of_block));

    char *buffer_block = (char *)malloc(size_block);

    read_from_fd(wr_fd, buffer_block, size_block);

    printf("Order block is : %d\n", order_of_block);

    if (!blockInBitmap(bitmap_buffer, order_of_block))
    {
        printf("Blok este nemam !\n");
        if (order_of_block != (number_of_block - 1) || !(file_size % size_block))
            memcpy(buffer_file + (size_block * order_of_block), buffer_block, size_block);
        else
            memcpy(buffer_file + (size_block * order_of_block), buffer_block, size_block - (file_size % size_block));

        blockToBitmap(bitmap_buffer, order_of_block);
    }
    else
    {
        printf("Blok už mam.\n");
    }

    free(buffer_block);
}

struct client *connectToSeeder(struct client *begin_client_list, int size_peer_list, struct peer *peer_list)
{
    printf("SEED");
    struct sockaddr_in adresa;
    bzero(&adresa, sizeof(struct sockaddr_in));
    adresa.sin_family = AF_INET;

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

        begin_client_list = newClientToList( begin_client_list, new_client);

        printf("Pripojene !\n");
    }

    return begin_client_list;
}

struct client *newClientToList(struct client *client_list, struct client *new_client)
{

    if (client_list == 0)
        return new_client;

    struct client * start = client_list;

    while (client_list->next != 0)
        client_list = client_list->next;

    client_list->next = new_client;

    return start;
}

struct client *deleteClient(struct client *begin_client_list, struct client *delete_client)
{
    if (memcmp(begin_client_list, delete_client, sizeof(struct client)) == 0)
    {
        free(delete_client);
        return 0;
    }

    for (struct client *temp = begin_client_list; temp != 0; temp = temp->next)
    {
        if (memcmp(temp->next, delete_client, sizeof(struct client)) == 0)
        {
            temp->next = temp->next->next;
            free(temp->next);
            return temp;
        }
    }

    return 0;
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

        if (error < 0 && errno != EADDRINUSE)
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
        for (int o = 0; o < 8; o++)
        {
            if ((!!((bitmap[i] << o) & 0x80)) != 1)
                return 0;
        }
    }

    for (int i = 0; i < ((number_of_block % 8) ? (number_of_block % 8) : 8); i++)
    {
        int temp = (!!((bitmap[size_bitmap - 1] >> i) & 0x1));
        if (temp != 1)
            return 0;
    }

    return 1;
}

int numberOfBlock(int size_file, int size_block)
{
    int temp = (size_file - (size_file % size_block)) / size_block;
    if ((size_file % size_block))
        temp++;

    return temp;
}

int sizeBitmap(int number_of_block)
{
    return (number_of_block + (8 - (number_of_block % 8))) / 8;
}

void statsRequest(int socket_fd, int my_stat)
{
    char buffer[2];
    buffer[0] = (enum CLIENT)STATS_RESPONSE;
    buffer[1] = my_stat;

    write_to_fd(socket_fd, buffer, sizeof(buffer));
}

void bitmapRequest(int socket_fd, char *bitmap_buffer, int size_bitmap)
{
    char *buffer = (char *)malloc(sizeof(char) + size_bitmap);
    printf("Size bitmap : %d\n", size_bitmap);
    buffer[0] = (enum CLIENT)BITMAP_RESPONSE;
    memcpy(buffer + 1, bitmap_buffer, size_bitmap);
    write_to_fd(socket_fd, buffer, sizeof(char) + size_bitmap);

    printf("My bitmap :");
    for (int i = 0; i < size_bitmap; i++)
        for (int o = 0; o < 8; o++)
            printf("%d", !!((bitmap_buffer[i] << o) & 128));
    printf("\n");

    free(buffer);
}

char blockInBitmap(char *bitmap_buffer, int order_of_block)
{
    int temp = (order_of_block < 8) ? 0 : (order_of_block - (order_of_block % 8)) / 8;

    if ((!!((bitmap_buffer[temp] >> (order_of_block % 8)) & 0x1)) == 1)
        return 1;
    else
        return 0;
}

void blockToBitmap(char *bitmap_buffer, int order_of_block)
{
    int temp;

    if (order_of_block < 8)
        temp = 0;
    else
        temp = (order_of_block - (order_of_block % 8)) / 8;

    bitmap_buffer[temp] += (1 << (order_of_block % 8));
}

void printBitmap(char *bitmap_buffer, int size_bitmap)
{
    for (int i = 0; i < 2; i++)
        for (int o = 0; o < 8; o++)
            printf("%d", !!((bitmap_buffer[i] << o) & 128));
    printf("\n");
}