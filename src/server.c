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

#include "define/request_respons/database.h"
#include "define/metadata.h"
#include "define/peer.h"

#define DEFAULT_LISTEN_PORT 56320

void closeSocket(int Socket);
void newMetadata(int read_socket, int write_pipe, sem_t *semaphore);
void newPeer(int write_pipe, sem_t *semaphore, struct sockaddr_in *addres);
void deletePeer(int write_pipe, sem_t *semaphore, struct sockaddr_in *addres);

int main(int argc, char **argv)
{

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
    srand((unsigned)time(NULL));
    for (int i = 0; i < sizeof(name_semaphone); i++)
        name_semaphone[i] = rand() % SCHAR_MAX;
    name_semaphone[9] = 0;

    if ((mutex = sem_open(name_semaphone, O_CREAT | O_EXCL, 0644, 1)) == SEM_FAILED)
    {
        perror("Chyba pri vytvarani semaphoru :");
        exit(1);
    }

    /*
    switch (fork())
    {
    case -1:
        perror("Chyba pri forkovani:");
        exit(-1);
    case 0:
        while (1)
        {
            sem_wait(mutex);
            printf("Dieta vstupuje do kritickej casti\n");

            if (read(read_fd, &temp, sizeof(int)) < 0)
            {
                perror("Chyba pri citani zo socketu\n");
                exit(-1);
            }

            printf("Dieta precital %d\n", temp);

            sem_post(mutex);
            printf("Dieta vystupuje z kritickej casti\n");
            //sleep(0.1);
        }
        break;
    default:
        while (1)
        {
            sem_wait(mutex);
            printf("Rodic vstupuje do kritickej casti\n");

            if (read(read_fd, &temp, sizeof(int)) < 0)
            {
                perror("Chyba pri citani zo socketu\n");
                exit(-1);
            }

            printf("Rodic precital %d\n", temp);

            sem_post(mutex);

            printf("Rodic vystupuje z kritickej casti\n");
            //sleep(0.1);
        }
    }

    exit(0);*/

    /* Vytvorenie socketu */
    int socketListen;
    if ((socketListen = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Chyba pri vytvarani socktu.\n");
        exit(-1);
    }

    /* Vytvorenie adresy */
    struct sockaddr_in addressListen;
    addressListen.sin_family = AF_INET;                  //Pripojenie zo siete
    addressListen.sin_port = htons(DEFAULT_LISTEN_PORT); // Port
    addressListen.sin_addr.s_addr = INADDR_ANY;          // Prijimanie spojenie od kazdej IP

    /* Bind socket a adresy */
    if (bind(socketListen, (struct sockaddr *)&addressListen, sizeof(addressListen)) < 0)
    {
        perror("Chyba pri vystavovani socketu.\n");
        exit(-2);
    }

    /* Nastavenie pocuvania */
    if (listen(socketListen, 2) < 0)
    {
        perror("Chyba pri nastavovani pocuvania.\n");
        exit(-3);
    }

    printf("Socket bol uspesne vytoreny a nastavený.\n");

    /* Vytvorenie spojeni s clientami */
    int socketClient;
    struct sockaddr_in addressClient;
    socklen_t size;
    while (1)
    {

        if ((socketClient = accept(socketListen, (struct sockaddr *)&addressClient, &size)) < 0)
        {
            perror("Chyva pri vytvarani spojenia s klientom.\n");
            exit(-4);
        }

        switch (fork())
        {
        case -1:
            perror("Chyba pri vytvarani noveho procesu.");
            exit(-7);

        case 0:
            closeSocket(socketListen);
            char request_form_client;
            while (1)
            {
                if ((read(socketClient, &request_form_client, sizeof(request_form_client))) <= 0)
                {
                    perror("Chyba pri citani zo Socketu");
                    deletePeer(write_fd, mutex, &addressClient);
                    exit(-2);
                }

                switch ((enum request)request_form_client)
                {
                case NEW_METADATA:
                    newMetadata(socketClient, write_fd, mutex);
                    newPeer(write_fd, mutex, &addressClient);
                    break;
                }
            }
            exit(0);

        default:
            closeSocket(socketClient);
        }
    }
    printf("Server sa vypina.\n");

    exit(0);
}

void closeSocket(int Socket)
{
    if (close(Socket) < 0)
    {
        perror("Chyba pri zatvarani socketu");
        exit(-8);
    }

    return;
}

void newMetadata(int read_socket, int write_pipe, sem_t *semaphore)
{
    struct metadata new_metadata;
    if (read(read_socket, &new_metadata, sizeof(struct metadata)) < 0)
    {
        perror("Chyba pri citani zo socketu");
        exit(-2);
    }

    sem_wait(semaphore);
    printf("Server - Vstupujem do kritickej casti\n");
    printf("NEW METADATA\n");

    //printf("Server - Nove metadata ! :\nMeno :%s\nVelkost Suboru: %d\nVelkost Bloku: %d\n", new_metadata.name, new_metadata.file_size, new_metadata.size_block);

    char buffer[sizeof(struct metadata) + 1];
    /* Request */
    buffer[0] = (enum request)NEW_METADATA;
    //strcpy(buffer + 1, &new_metadata);
    memcpy(buffer + 1, &new_metadata, sizeof(struct metadata));

    if (write(write_pipe, buffer, sizeof(buffer)) < 0)
    {
        perror("Chyba pri zapisovani do fd");
        exit(-3);
    }

    sem_post(semaphore);
    printf("Server - Vystupujem z kritickej casti\n");
}

void newPeer(int write_pipe, sem_t *semaphore, struct sockaddr_in *addres)
{
    sem_wait(semaphore);
    printf("Server - Vstupujem do kritickej casti\n");
    printf("NEW PEER\n");
    struct peer new_peer;

    memcpy(new_peer.ip, &(addres->sin_addr.s_addr), sizeof(new_peer.ip));
    new_peer.port = addres->sin_port;

    char buffer[sizeof(struct peer) + 1];

    buffer[0] = (enum request)NEW_PEER;
    memcpy(buffer + 1, &new_peer, sizeof(struct peer));

    if (write(write_pipe, buffer, sizeof(buffer)) < 0)
    {
        perror("2. Chyba pri zapisovani do fd");
        exit(-3);
    }

    sem_post(semaphore);
    printf("Server - Vystupujem z kritickej casti\n");
}

void deletePeer(int write_pipe, sem_t *semaphore, struct sockaddr_in *addres)
{
    sem_wait(semaphore);
    printf("Server - Vstupujem do kritickej casti\n");
    printf("NEW PEER\n");

    struct peer new_peer;

    memcpy(new_peer.ip, &(addres->sin_addr.s_addr), sizeof(new_peer.ip));
    new_peer.port = addres->sin_port;

    char buffer[sizeof(struct peer) + 1];

    buffer[0] = (enum request)DELETE_PEER;
    memcpy(buffer + 1, &new_peer, sizeof(struct peer));

    if (write(write_pipe, buffer, sizeof(buffer)) < 0)
    {
        perror("2. Chyba pri zapisovani do fd");
        exit(-3);
    }

    sem_post(semaphore);
    printf("Server - Vystupujem z kritickej casti\n");
}