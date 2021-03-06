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
#include <dirent.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

#include "define/metadata.h"
#include "define/request_respons/database.h"
#include "define/peer.h"
#include "define/request_respons/client.h"

#define PORT 5320
#define MAX_PORT 54000
#define MIN_PORT 53000
#define MAX_CONNEDTED_CLIENT 10

typedef struct downloadingFile
{
    struct metadata metadata;
    double complete;  // V percentach
    pthread_t thread; // Thread ktory stahuje

    struct downloadingFile *next;
};

typedef struct threadArguments
{
    int server_fd;
    struct metadata *metadata;
    char *file_buffer;
    char *bitmap_buffer;
    FILE *file;
    double *complete;
};

typedef struct client
{
    char stat;
    int fd_socket;
    char request;
    char *bitmap;

    struct client *next;
};

void zoznamPeerov(WINDOW *window, int socket_fd, int y_middle, int x_middle);
void thread_wrapper(struct threadArguments *arguments);
void thread_downloaingFile(int server_fd, struct metadata *metadata, char *file_buffer, char *bitmap_buffer, FILE *file, double *complete);
void sendBlock(int wr_fd, char *buffer_file, int number_of_block, int size_block, int file_size);
void saveBlock(int wr_fd, char *buffer_file, int number_of_block, int size_block, int file_size, char *bitmap_buffer);
char isFull(char *bitmap, int size_bitmap, int number_of_block);
struct client *newClientToList(struct client *client_list, struct client *new_client);
void deleteClient(struct client **begin_client_list, struct client *delete_client);
void initMasterFD(int *master_fd, int fd_server);
struct client *connectToSeeder(struct client *begin_client_list, int size_peer_list, struct peer *peer_list);
int numberOfBlock(int size_file, int size_block);
int sizeBitmap(int number_of_block);
void statsRequest(int socket_fd, int my_stats);
void bitmapRequest(int socket_fd, char *bitmap_buffer, int size_bitmap);
char blockInBitmap(char *bitmap_buffer, int order_of_block);
void blockToBitmap(char *bitmap_buffer, int order_of_block);
void printBitmap(char *bitmap_buffer, int size_bitmap);
int sizeFile(const char *file_name);
char *selectMenu(WINDOW *window, int y_middle, int x_middle, char dir_or_file);

void downloadedFile(WINDOW *window, int y_middle, int x_middle, struct downloadingFile *begin_downloading_file);
void downloadMetadata(WINDOW *window, int socket_fd, int y_middle, int x_middle, struct downloadingFile **begin_downloading_file);
void uploadMetadata(WINDOW *window, int fd_server, int y_middle, int x_middle, struct downloadingFile **begin_downloading_file);
timer_t vytvorCasovac(int);
void spustiCasovac(timer_t, int);

int main(int argc, char **argv)
{
    struct downloadingFile *begin_downloading_file = 0;
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
    timer_t casovac;
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
                uploadMetadata(menuwin, socketListen, y_middle, x_middle, &begin_downloading_file);
                break;
            case 1:
                downloadMetadata(menuwin, socketListen, y_middle, x_middle, &begin_downloading_file);
                break;

            case 2:
                downloadedFile(menuwin, y_middle, x_middle, begin_downloading_file);
                break;

            case 3:
                casovac = vytvorCasovac(SIGKILL);
                spustiCasovac(casovac, 5);
                wclear(menuwin);
                endwin();
                //exit(0);
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

void thread_wrapper(struct threadArguments *arguments)
{
    struct threadArguments *arg = (struct threadArguments *)arguments;
    thread_downloaingFile(arg->server_fd, arg->metadata, arg->file_buffer, arg->bitmap_buffer, arg->file, arg->complete);

    free(arg->metadata);
    free(arg->file_buffer);
    free(arg->bitmap_buffer);
    free(arg->metadata);

    free(arguments);
}

void thread_downloaingFile(int server_fd, struct metadata *metadata, char *file_buffer, char *bitmap_buffer, FILE *file, double *complete)
{
    //endwin();
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

    free(peer_list);

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
    }
    else
    {
        my_stat = (enum CLIENT)DOWNLOADED;
    }

    int max_fd;
    fd_set readfd, writefd;
    char *request_bitmap = (char *)malloc(size_bitmap);
    bzero(request_bitmap, size_bitmap);
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
            if (temp->fd_socket > max_fd)
                max_fd = temp->fd_socket;

            temp = temp->next;
        }

        sleep(2);
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
                    deleteClient(&begin_client_list, temp);

                    if (!temp)
                    {
                        begin_client_list = 0;
                        break;
                    }
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
                    break;

                case SEND_BLOCK_REQUEST:
                    sendBlock(temp->fd_socket, file_buffer, number_of_block, metadata->size_block, metadata->file_size);
                    break;

                case SEND_BLOCK_RESPONSE:
                    saveBlock(temp->fd_socket, file_buffer, number_of_block, metadata->size_block, metadata->file_size, bitmap_buffer);

                    if (!isFull(bitmap_buffer, size_bitmap, number_of_block))
                    {
                        my_stat = (enum CLIENT)DOWNLOADING;

                        /* Downloaded block */
                        int size = 0;
                        for (int i = 0; i < size_bitmap - 1; i++)
                        {
                            for (int o = 0; o < 8; o++)
                            {
                                if ((!!((bitmap_buffer[i] << o) & 0x80)) == 1)
                                    size++;
                            }
                        }

                        for (int i = 0; i < ((number_of_block % 8) ? (number_of_block % 8) : 8); i++)
                        {
                            int temp = (!!((bitmap_buffer[size_bitmap - 1] >> i) & 0x1));
                            if (temp == 1)
                                size++;
                        }

                        *complete = (((double)size / (double)number_of_block) * 100);
                    }
                    else
                    {
                        /*After download file*/

                        *complete = 100;

                        for (struct client *_temp = begin_client_list; _temp != 0; _temp = _temp->next)
                            free(_temp->bitmap);

                        my_stat = (enum CLIENT)DOWNLOADED;

                        if (fwrite(file_buffer, metadata->file_size, 1, file) < 0)
                        {
                            perror("Chyba pri zapise do suboru :");
                        }

                        fclose(file);
                    }
                    break;
                }

                temp->request = 0;
            }
        }

        if (my_stat == (enum CLIENT)DOWNLOADED)
            continue;

        /* Request to client */
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

                    if ((request_block && my_block) && client_block)
                    {
                        //printf("Blok ok \n");
                        char buffer[sizeof(char) + sizeof(int)];
                        buffer[0] = (enum CLIENT)SEND_BLOCK_REQUEST;
                        *((int *)(buffer + sizeof(char))) = i;

                        write_to_fd(temp->fd_socket, &buffer, sizeof(buffer));

                        blockToBitmap(request_bitmap, i);

                        break;
                    }
                }
            }
        }
    }
}

void sendBlock(int wr_fd, char *buffer_file, int number_of_block, int size_block, int file_size)
{
    int order_of_block;
    read_from_fd(wr_fd, &order_of_block, sizeof(order_of_block));

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

    if (!blockInBitmap(bitmap_buffer, order_of_block))
    {
        if (order_of_block != (number_of_block - 1) || !(file_size % size_block))
            memcpy(buffer_file + (size_block * order_of_block), buffer_block, size_block);
        else
            memcpy(buffer_file + (size_block * order_of_block), buffer_block, size_block - (file_size % size_block));

        blockToBitmap(bitmap_buffer, order_of_block);
    }

    free(buffer_block);
}

struct client *connectToSeeder(struct client *begin_client_list, int size_peer_list, struct peer *peer_list)
{
    struct sockaddr_in adresa;
    bzero(&adresa, sizeof(struct sockaddr_in));
    adresa.sin_family = AF_INET;

    for (int i = 0; i < size_peer_list; i++)
    {
        struct client *new_client = (struct client *)malloc(sizeof(struct client));
        bzero(new_client, sizeof(struct client));

        new_client->fd_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (new_client->fd_socket < 0)
        {
            perror("Chyba pri vytvarani socketu :");
            free(new_client);
            continue;
        }

        adresa.sin_port = htons(peer_list[i].port);
        char number_to_string[25];
        adresa.sin_addr.s_addr = inet_addr("127.0.0.1");

        if (connect(new_client->fd_socket, (struct sockaddr *)&adresa, sizeof(adresa)) != 0)
        {
            perror("Chyba pri pripajani na socket :");
            free(new_client);
            continue;
        }

        begin_client_list = newClientToList(begin_client_list, new_client);
    }

    return begin_client_list;
}

struct client *newClientToList(struct client *client_list, struct client *new_client)
{

    if (client_list == 0)
        return new_client;

    struct client *start = client_list;

    while (client_list->next != 0)
        client_list = client_list->next;

    client_list->next = new_client;

    return start;
}

void deleteClient(struct client **begin_client_list, struct client *delete_client)
{
    if (memcmp(*begin_client_list, delete_client, sizeof(struct client)) == 0)
    {
        *begin_client_list = delete_client->next;
        free(delete_client->bitmap);
        free(delete_client);
        return;
    }

    for (struct client *temp = *begin_client_list; temp != 0; temp = temp->next)
    {
        if (memcmp(temp->next, delete_client, sizeof(struct client)) == 0)
        {
            temp->next = temp->next->next;
            free(temp->next->bitmap);
            free(temp->next);
            return;
        }
    }

    return;
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

    buffer[0] = (enum CLIENT)BITMAP_RESPONSE;
    memcpy(buffer + 1, bitmap_buffer, size_bitmap);
    write_to_fd(socket_fd, buffer, sizeof(char) + size_bitmap);

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

int sizeFile(const char *file_name)
{
    struct stat file_stat;
    stat(file_name, &file_stat);

    return file_stat.st_size;
}

char *selectMenu(WINDOW *window, int y_middle, int x_middle, char dir_or_file)
{
    struct dirent **namelist;
    int size_namelist;
    char *currentDir = (char *)malloc(sizeof(char) * 256);
    bzero(currentDir, sizeof(char) * 256);
    currentDir[0] = '.';

    if ((size_namelist = scandir(currentDir, &namelist, NULL, alphasort)) == -1)
    {
        perror("scandir");
        exit(EXIT_FAILURE);
    }

    int akcia;
    int selected = 0;

    while (1)
    {
        wclear(window);
        box(window, 0, 0);
        mvwprintw(window, 1, 2, "%s", currentDir);

        wattron(window, A_REVERSE);
        if (dir_or_file)
            mvwprintw(window, 1, x_middle - 14, "Select FILE witch [ ENTER ]");
        else
            mvwprintw(window, 1, x_middle - 16, "Select actually DIR witch [ x ]");

        wattroff(window, A_REVERSE);

        mvwprintw(window, 3, x_middle - 15, "%-15s | TYPE | SIZE", "NAME");
        mvwprintw(window, 4, x_middle - 15, "------------------------------");

        for (int i = 0; i < size_namelist; i++)
        {
            if (i == selected)
                wattron(window, A_REVERSE);

            char type[10];
            /* TYPE */
            switch (namelist[i]->d_type)
            {
            case DT_DIR:
                strcpy(type, "DIR ");
                break;

            case DT_REG:
                strcpy(type, "FILE");
                break;

            default:
                strcpy(type, "DT_DEF");
                break;
            }

            char nameFile[256];
            strcpy(nameFile, currentDir);
            strcpy(nameFile + strlen(nameFile), "/");
            strcpy(nameFile + strlen(nameFile), namelist[i]->d_name);

            mvwprintw(window, i + 5, x_middle - 15, "%-15s | %s | %d", namelist[i]->d_name, type, sizeFile(nameFile));
            wattroff(window, A_REVERSE);
        }

        akcia = wgetch(window);

        switch (akcia)
        {
        case KEY_UP:
            if (selected > 0)
                selected--;
            break;
        case KEY_DOWN:
            if (selected < (size_namelist - 1))
                selected++;
            break;
        default:
            break;
        }

        /* ENTER */
        if (akcia == 10)
        {
            int len = strlen(currentDir);

            if ((namelist[selected]->d_type == DT_REG) && dir_or_file)
            {
                strcpy(currentDir + len, "/");
                strcpy(currentDir + len + 1, namelist[selected]->d_name);

                for (int i = 0; i < size_namelist; i++)
                    free(namelist[i]);
                free(namelist);

                return currentDir;
            }

            if (namelist[selected]->d_type == DT_DIR && selected != 0)
            {
                /* Slected .. */
                if (selected == 1 && len != 1)
                {
                    char *temp = currentDir;

                    while (*temp)
                        temp++;

                    while (*temp != '/')
                    {
                        *temp = 0;
                        temp--;
                    }

                    *temp = 0;
                }
                else
                {
                    strcpy(currentDir + len, "/");
                    strcpy(currentDir + len + 1, namelist[selected]->d_name);
                }

                if ((size_namelist = scandir(currentDir, &namelist, NULL, alphasort)) == -1)
                {
                    perror("scandir");
                    exit(EXIT_FAILURE);
                }

                selected = 0;
                mvwprintw(window, 1, 2, "%s", currentDir);
            }
        }
        else if ((akcia == 'x') && !dir_or_file)
        {
            for (int i = 0; i < size_namelist; i++)
                free(namelist[i]);
            free(namelist);

            return currentDir;
        }
        wrefresh(window);
    }
}

void downloadedFile(WINDOW *window, int y_middle, int x_middle, struct downloadingFile *begin_downloading_file)
{
    //nodelay(window, 1);

    wtimeout(window, 500);
    while (wgetch(window) != 10)
    {
        wclear(window);
        box(window, 0, 0);

        /* Size */
        int size = 0;

        if (begin_downloading_file == 0)
        {
            mvwprintw(window, y_middle, x_middle - 13, "Nestahuju sa ziadne subory.");
            continue;
        }

        mvwprintw(window, 2, x_middle - 24, "%-15s | PROGRES BAR | VELKOST SUBORU", "MENO SUBORU");
        mvwprintw(window, 3, x_middle - 24, "----------------------------------------------");

        struct downloadingFile *temp = begin_downloading_file;
        for (; temp != 0; temp = temp->next)
        {
            mvwprintw(window, 4 + size, x_middle - 24, "%-15s   [", temp->metadata.name);

            wattron(window, A_REVERSE);

            char progres_bar[20];
            bzero(progres_bar, sizeof(progres_bar));
            sprintf(progres_bar, "   %d%%   ", (int)temp->complete);

            int i = 0;
            for (; i < ((int)temp->complete / 10); i++)
                mvwprintw(window, 4 + size, x_middle - 5 + i, "%c", progres_bar[i]);

            wattroff(window, A_REVERSE);

            mvwprintw(window, 4 + size, x_middle - 5 + i, "%s]     %d", &progres_bar[i], temp->metadata.file_size);

            size++;

            wrefresh(window);
        }
    }

    nodelay(window, 0);

    return;
}

void downloadMetadata(WINDOW *window, int socket_fd, int y_middle, int x_middle, struct downloadingFile **begin_downloading_file)
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

    char *dir = selectMenu(window, y_middle, x_middle, 0);
    strcpy(dir + strlen(dir), "/");
    strcpy(dir + strlen(dir), new_metadata.name);

    FILE *file;
    if ((file = fopen(dir, "wb")) < 0)
        exit(0);

    free(dir);

    struct threadArguments *arg = (struct threadArguments *)malloc(sizeof(struct threadArguments));

    *begin_downloading_file = (struct downloadingFile *)malloc(sizeof(struct downloadingFile));

    (**begin_downloading_file).next = 0;
    (**begin_downloading_file).complete = 1;
    memcpy(&(**begin_downloading_file).metadata, &new_metadata, sizeof(struct metadata));

    arg->server_fd = socket_fd;
    arg->metadata = (struct metadata *)malloc(sizeof(struct metadata));
    memcpy(arg->metadata, &new_metadata, sizeof(struct metadata));

    arg->file_buffer = file_buffer;
    arg->bitmap_buffer = bitmap_buffer;
    arg->file = file;
    arg->complete = &(**begin_downloading_file).complete;

    pthread_create(&((**begin_downloading_file).thread), NULL, thread_wrapper, arg);
}

void uploadMetadata(WINDOW *window, int fd_server, int y_middle, int x_middle, struct downloadingFile **begin_downloading_file)
{
    char name_file[50];
    memset(name_file, 0, sizeof(name_file));
    char *currentDir = selectMenu(window, y_middle, x_middle, 1);

    struct stat file_stat;
    if (stat(currentDir, &file_stat) == -1)
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

    FILE *file;
    if ((file = fopen(currentDir, "rb")) <= 0)
    {
        wclear(window);
        endwin();
        perror("Subor :");
        exit(0);
    }

    /* FILE name */
    int size_len = strlen(currentDir);
    for (int i = 0; i < size_len; i++)
    {
        if (currentDir[size_len - i] == '/')
        {
            strcpy(name_file, &currentDir[size_len - i + 1]);
            break;
        }
    }

    /* Vyber velkosti bloku */
    int akcia = 0;
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

        mvwprintw(window, y_middle - 2, x_middle - 10, "Meno suboru\t: %s", name_file);
        mvwprintw(window, y_middle - 1, x_middle - 10, "Velkost suboru\t: %d", file_stat.st_size);
        mvwprintw(window, y_middle, x_middle - 10, "Pocet blokov\t: %d", pocet_blokov);
        mvwprintw(window, y_middle + 1, x_middle - 10, "Velkost bloku\t: %d", velkost_bloku);

        wattron(window, A_REVERSE);
        mvwprintw(window, y_middle + 3, x_middle - (int)(60 / 2), "Pre potvrdenie stlacte ENTER. Velkost bloku menite sipkami.");
        wattroff(window, A_REVERSE);
        wrefresh(window);
    } while ((akcia = wgetch(window)) != 10);

    struct metadata *new_metadata = (struct metadata *)malloc(sizeof(struct metadata));
    new_metadata->size_block = velkost_bloku;
    new_metadata->file_size = file_stat.st_size;
    strncpy(new_metadata->name, name_file, sizeof(new_metadata->name));

    while ((akcia = wgetch(window)) != 10)
        ;
    /* Nahrat metadata na server */

    char buffer[sizeof(struct metadata) + 1];
    /* Request */

    buffer[0] = (enum request)NEW_METADATA;
    //strcpy(buffer + 1, &new_metadata);
    memcpy(buffer + 1, new_metadata, sizeof(struct metadata));

    if (write(fd_server, buffer, sizeof(buffer)) < 0)
    {
        perror("Chyba pri zapisovani do fd");
        exit(-3);
    }

    wclear(window);
    //endwin();

    int size_bitmap = sizeBitmap(pocet_blokov);

    char *file_buffer = (char *)malloc(new_metadata->file_size);
    char *bitmap_buffer = (char *)malloc(size_bitmap);

    if (fread(file_buffer, new_metadata->file_size, 1, file) < 0)
    {
        perror("Chyba pri citani :");
        exit(0);
    }

    fclose(file);

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

    free(currentDir);

    *begin_downloading_file = (struct downloadingFile *)malloc(sizeof(struct downloadingFile));

    (**begin_downloading_file).next = 0;
    (**begin_downloading_file).complete = 100;
    memcpy(&(**begin_downloading_file).metadata, new_metadata, sizeof(struct metadata));

    struct threadArguments *arg = (struct threadArguments *)malloc(sizeof(struct threadArguments));

    arg->server_fd = fd_server;
    arg->metadata = new_metadata;
    arg->file_buffer = file_buffer;
    arg->bitmap_buffer = bitmap_buffer;
    arg->file = file;
    arg->complete = 100;

    pthread_create(&((**begin_downloading_file).thread), NULL, thread_wrapper, arg);

    return;
}

timer_t vytvorCasovac(int signal)
{
    struct sigevent kam;
    kam.sigev_notify = SIGEV_SIGNAL;
    kam.sigev_signo = signal;

    timer_t casovac;
    timer_create(CLOCK_REALTIME, &kam, &casovac);
    return (casovac);
}

void spustiCasovac(timer_t casovac, int sekundy)
{
    struct itimerspec casik;
    casik.it_value.tv_sec = sekundy;
    casik.it_value.tv_nsec = 0;
    casik.it_interval.tv_sec = 0;
    casik.it_interval.tv_nsec = 0;
    timer_settime(casovac, CLOCK_REALTIME, &casik, NULL);
}