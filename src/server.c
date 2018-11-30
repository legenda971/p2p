#include <stdio.h>  /* printf() */
#include <stdlib.h> /* exit(), malloc(), free() */
#include <sys/socket.h>
#include <sys/types.h> /* key_t, sem_t, pid_t */
#include <netdb.h>
#include <unistd.h>
#include <semaphore.h>
#include <fcntl.h>  /* O_CREAT */
#include <string.h> /* strcpy */
#include <limits.h> /* SCHAR_MAX */
#include <time.h>   /* time */
#include <signal.h>

#include "define/request_respons/database.h"
#include "define/metadata.h"
#include "define/peer.h"

#define DEFAULT_LISTEN_PORT 5320
#define SEMAPHORE_NAME "MySemapnore4"

void closeSocket(int Socket);
void newMetadata(int read_socket, int write_pipe, sem_t *semaphore);
void newPeer(int read_socket, int write_pipe, sem_t *semaphore, struct sockaddr_in *addres, unsigned short * port);
void deletePeer(int write_pipe, sem_t *semaphore, struct sockaddr_in *addres, unsigned short port);
void socketAdressToPeer(struct peer *temp, struct sockaddr_in *address);
void downloadPeerList(int write_pipe, int read_pipe, sem_t *semaphore, int socket_fd);
void downloadMetadata(int write_pipe, int read_pipe, sem_t *semaphore, int socket_fd);
void writeFd(int fd, void *buffer, size_t size_buffer);
void readFd(int fd, void *buffer, size_t size_buffer);
void deleteMetadata(int write_pipe, sem_t *semaphore);
char existMetadata(int write_pipe, int read_pipe, sem_t *semaphore, int socket_fd);

void sigintSignalFunction();

int main(int argc, char **argv)
{

    signal(SIGINT, sigintSignalFunction);

    int pipe_fd[2];
    pipe(pipe_fd);

    int pipe_fdd[2];
    pipe(pipe_fdd);

    /* DB */
    switch (fork())
    {
    case -1:
        perror("Chyba pri fork :");
        exit(-1);
    case 0:
        close(pipe_fd[1]);
        close(pipe_fdd[0]);

        char arg1[20], arg2[20];
        snprintf(arg1, sizeof(arg1), "%d", pipe_fd[0]);
        snprintf(arg2, sizeof(arg2), "%d", pipe_fdd[1]);

        if (execl("database", arg1, arg2, NULL) < 0)
        {
            perror("Chyba pri execl :");
            exit(-1);
        }
    }

    close(pipe_fd[0]);
    close(pipe_fdd[1]);

    int write_fd = pipe_fd[1];
    int read_fd = pipe_fdd[0];

    /* Shared semaphore TEST */

    sem_t *mutex;
    /* Generovanie random mena pre semafor */

    char name_semaphone[10];
    /*
    int qwe = sizeof(name_semaphone);
    srand((unsigned)time(NULL));
    for (int i = 0; i < qwe; i++)
        name_semaphone[i] = (rand() % ('z' - 'a')) + 'a';
    name_semaphone[9] = 0;*/

    //sem_unlink(SEMAPHORE_NAME);

    /* Vytvorenie socketu */
    int socketListen;
    if ((socketListen = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Server - Chyba pri vytvarani socktu.\n");
        exit(-1);
    }

    /* Vytvorenie adresy */
    struct sockaddr_in addressListen;
    bzero((char *)&addressListen, sizeof(struct sockaddr_in));

    addressListen.sin_family = AF_INET;                  //Pripojenie zo siete
    addressListen.sin_port = htons(DEFAULT_LISTEN_PORT); // Port
    addressListen.sin_addr.s_addr = INADDR_ANY;          // Prijimanie spojenie od kazdej IP

    /* Bind socket a adresy */
    if (bind(socketListen, (struct sockaddr *)&addressListen, sizeof(addressListen)) < 0)
    {
        perror("Server - Chyba pri vystavovani socketu.\n");
        exit(-2);
    }

    /* Nastavenie pocuvania */
    if (listen(socketListen, 2) < 0)
    {
        perror("Server - Chyba pri nastavovani pocuvania.\n");
        exit(-3);
    }

    printf("server - Socket bol uspesne vytoreny a nastavený.\n");

    if ((mutex = sem_open( SEMAPHORE_NAME, O_CREAT | O_EXCL, 0644, 1)) == SEM_FAILED)
    {
        perror("Server - Chyba pri vytvarani semaphoru :");
        exit(1);
    }

    /* Vytvorenie spojeni s clientami */
    int socketClient;
    struct sockaddr_in addressClient;
    socklen_t size;

    while (1)
    {
        bzero((char *)&addressClient, sizeof(struct sockaddr_in));

        if ((socketClient = accept(socketListen, (struct sockaddr *)&addressClient, &size)) < 0)
        {
            perror("Server - Chyva pri vytvarani spojenia s klientom.\n");
            exit(-4);
        }

        printf("Server - Client sa pripojil.\n");

        switch (fork())
        {
        case -1:
            perror("Server - Chyba pri vytvarani noveho procesu.");
            exit(-7);

        case 0:
            closeSocket(socketListen);
            char request_form_client;
            unsigned short port = 0;
            char usePeer = 0;
            while (1)
            {
                if ((read(socketClient, &request_form_client, sizeof(request_form_client))) <= 0)
                {
                    perror("Server - Chyba pri citani zo Socketu");
                    if(usePeer)
                        deletePeer(write_fd, mutex, &addressClient, port);
                    exit(-2);
                }

                switch ((enum request)request_form_client)
                {
                case NEW_METADATA:
                    newMetadata(socketClient, write_fd, mutex);
                    break;

                case METADATA:
                    downloadMetadata(write_fd, read_fd, mutex, socketClient);
                    break;

                case PEER_LIST:
                    downloadPeerList(write_fd, read_fd, mutex, socketClient);
                    break;

                case NEW_PEER:
                    newPeer(socketClient, write_fd, mutex, &addressClient, &port);
                    usePeer = 1;
                    break;
                }
            }

            exit(0);

        default:
            closeSocket(socketClient);
        }
    }
    printf("Server - Vypina sa.\n");

    sem_close(&mutex);
    exit(0);
}

void closeSocket(int Socket)
{
    if (close(Socket) < 0)
    {
        perror("Server - Chyba pri zatvarani socketu");
        exit(-8);
    }

    return;
}

void newMetadata(int read_socket, int write_pipe, sem_t *semaphore)
{
    struct metadata new_metadata;
    if (read(read_socket, &new_metadata, sizeof(struct metadata)) < 0)
    {
        perror("Server - Chyba pri citani zo socketu");
        exit(-2);
    }

    sem_wait(semaphore);
    printf("Server - Vstupujem do kritickej casti\n");
    printf("Server - NEW METADATA\n");

    //printf("Server - Nove metadata ! :\nMeno :%s\nVelkost Suboru: %d\nVelkost Bloku: %d\n", new_metadata.name, new_metadata.file_size, new_metadata.size_block);

    char buffer[sizeof(struct metadata) + 1];
    /* Request */
    buffer[0] = (enum request)NEW_METADATA;
    //strcpy(buffer + 1, &new_metadata);
    memcpy(buffer + 1, &new_metadata, sizeof(struct metadata));

    if (write(write_pipe, buffer, sizeof(buffer)) < 0)
    {
        perror("Server - Chyba pri zapisovani do fd");
        exit(-3);
    }

    sem_post(semaphore);
    printf("Server - Vystupujem z kritickej casti\n");
}

void newPeer( int read_socket, int write_pipe, sem_t *semaphore, struct sockaddr_in * addres, unsigned short * port)
{
    sem_wait(semaphore);
    printf("Server - Vstupujem do kritickej casti\n");
    printf("Server - NEW PEER\n");

    char buffer[sizeof(struct peer) + 1];
    buffer[0] = (enum request)NEW_PEER;
    socketAdressToPeer((struct peer *)(buffer + 1), addres);

    readFd(read_socket, port, sizeof(unsigned short));

    ((struct peer *)(buffer +1))->port = *port;

    printf("Server - port je  : %d", *port);

    if (write(write_pipe, buffer, sizeof(buffer)) < 0)
    {
        perror("Server - Chyba pri zapisovani do fd");
        exit(-3);
    }

    sem_post(semaphore);
    printf("Server - Vystupujem z kritickej casti\n");
}

void deletePeer(int write_pipe, sem_t *semaphore, struct sockaddr_in *addres, unsigned short port)
{
    sem_wait(semaphore);
    printf("Server - Vstupujem do kritickej casti\n");
    printf("Sercer - DELETE PEER\n");

    char buffer[sizeof(struct peer) + 1];
    buffer[0] = (enum request)DELETE_PEER;

    memcpy(((struct peer *)(buffer + sizeof(char)))->ip, &(addres->sin_addr.s_addr), sizeof(((struct peer *)(buffer + sizeof(char)))->ip));
    ((struct peer *)(buffer + sizeof(char)))->port = port;

    if (write(write_pipe, buffer, sizeof(buffer)) < 0)
    {
        perror("Server - Chyba pri zapisovani do fd");
        exit(-3);
    }

    sem_post(semaphore);
    printf("Server - Vystupujem z kritickej casti\n");
}

void socketAdressToPeer(struct peer *_peer, struct sockaddr_in *address)
{
    memcpy(_peer->ip, &(address->sin_addr.s_addr), sizeof(_peer->ip));
    _peer->port = address->sin_port;
}

void downloadPeerList(int write_pipe, int read_pipe, sem_t *semaphore, int socket_fd)
{
    sem_wait(semaphore);
    printf("Server - Vstupujem do kritickej casti\n");
    printf("Sercer - download peer list\n");

    char request = (enum request)PEER_LIST;
    printf("Sizeof(enum request) = %d", sizeof(enum request));

    if (write(write_pipe, &request, 1) < 0)
    {
        perror("Server - Chyba pri zapisovani do fd");
        exit(-3);
    }

    int size_peerlist;

    if (read(read_pipe, &size_peerlist, sizeof(size_peerlist)) < 0)
    {
        perror("Server - Chyba pri citani z fd");
        exit(-3);
    }

    char *buffer = (char *)malloc(sizeof(struct peer) * size_peerlist + sizeof(size_peerlist));
    *((int *)buffer) = size_peerlist;
    if (read(read_pipe, (buffer + sizeof(size_peerlist)), sizeof(struct peer) * size_peerlist) < 0)
    {
        perror("Server - Chyba pri citani z fd");
        exit(-3);
    }

    sem_post(semaphore);
    printf("Server - Vystupujem z kritickej casti\n");

    if (write(socket_fd, buffer, sizeof(struct peer) * size_peerlist + sizeof(size_peerlist)) < 0)
    {
        perror("Server - Chyba pri zapise dat do fd :");
        exit(-5);
    }
}

void downloadMetadata(int write_pipe, int read_pipe, sem_t *semaphore, int socket_fd)
{
    sem_wait(semaphore);
    printf("Server - Vstupujem do kritickej casti\n");
    printf("Sercer - Metadata\n");

    char temp = (enum request)METADATA;
    writeFd(write_pipe, &temp, sizeof(temp));

    struct metadata new_metadata;
    readFd(read_pipe, &new_metadata, sizeof(struct metadata));

    sem_post(semaphore);
    printf("Server - Vystupujem z kritickej casti\n");

    writeFd(socket_fd, &new_metadata, sizeof(struct metadata));
}

void writeFd(int fd, void *buffer, size_t size_buffer)
{
    if (write(fd, buffer, size_buffer) < 0)
    {
        perror("Server - Chyba pri zapise dat do fd :");
        exit(-5);
    }
}

void readFd(int fd, void *buffer, size_t size_buffer)
{
    if (read(fd, buffer, size_buffer) < size_buffer)
    {
        perror("Server - Chyba pri zapise dat do fd :");
        exit(-5);
    }
}

void deleteMetadata(int write_pipe, sem_t *semaphore)
{
    sem_wait(semaphore);
    printf("Server - Vstupujem do kritickej casti\n");
    printf("Sercer - DELETE Metadata\n");

    char temp = (enum request)DELETE_METADATA;
    writeFd(write_pipe, &temp, sizeof(temp));

    sem_post(semaphore);
    printf("Server - Vystupujem z kritickej casti\n");
}

char existMetadata(int write_pipe, int read_pipe, sem_t *semaphore, int socket_fd)
{
    sem_wait(semaphore);
    printf("Server - Vstupujem do kritickej casti\n");
    printf("Sercer - EXIST Metadata\n");

    char temp = (enum request)EXIST_METADATA;
    writeFd(write_pipe, &temp, sizeof(temp));

    readFd(read_pipe, &temp, sizeof(temp));

    sem_post(semaphore);
    printf("Server - Vystupujem z kritickej casti\n");

    writeFd(socket_fd, &temp, sizeof(temp));

    return temp;
}

void sigintSignalFunction(){
    printf("Server - bezpecne vypnutie\n");
    sem_close(SEMAPHORE_NAME);
    sem_unlink(SEMAPHORE_NAME);
    exit(0);
}