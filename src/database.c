#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "metadata.c"

/*

argv[1] - read fd
argv[2] - write fd

*/
int main(int argc, char ** argv){
    int read_fd, write_fd;

    sscanf(argv[0], "%d", &read_fd);
    sscanf(argv[1], "%d", &write_fd);

    char temp;
    while(1){
        if(read(read_fd, &temp, sizeof(char)) < 0){
            perror("DB - chyba pri nacitani z fd");
            exit(-1);
        }

        switch(temp){
            default:
                printf("DB - %d\n", temp);
        }
    }

    return 0;
}