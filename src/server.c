#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#

#define DEFAULT_LISTEN_PORT 56300

void closeSocket(int Socket);
void afterFork();

int main(int argc, char **argv)
{

    int pipe_fd[2];
    pipe(pipe_fd);

    int pipe_fdd[2];
    pipe(pipe_fdd);


    int test = 50;
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

        if(execl("database", arg1, arg2, NULL) < 0){
            perror("Chyba pri execl :");
            exit(-1);
        }
    }

    close(pipe_fd[0]);
    close(pipe_fdd[1]);

    int temp = 0;
    while(1){
        sleep(1);
        if(write(pipe_fd[1], &temp, sizeof(int)) < 0){
            perror("Chyba pri zapisovani do sockutu :");
            exit(-5);
        }
        temp++;
    }

    exit(0);

    switch(fork()){
        case -1:
            perror("Chyba pri forkovani:");
            exit(-1);
        case 0:
            sleep(1);
            for(int i = 0; i <= 5; i++){
                int tmp;
                read(pipe_fdd[0], &tmp, sizeof(int));
                printf("Child :%d\n", tmp);
                sleep(1);
            }
            break;
        default:
            for(int i = 0; i <= 5; i++){
                int tmp;
                read(pipe_fdd[0], &tmp, sizeof(int));
                printf("Parent :%d\n", tmp);
                sleep(1);
            }
            break;
    }

    exit(0);

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
    /*int socketClient;
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
            afterFort();
        default:
            closeSocket(socketClient);
        }

        printf("Server sa vypina.\n");*/

        return 0;
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