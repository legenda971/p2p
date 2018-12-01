#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h> /* memset() */
#include "define/metadata.h"
#include "define/request_respons/database.h"
#include "define/peer.h"

/*

argv[1] - read fd
argv[2] - write fd

*/

#define MAX_PEERLIST 10

int main(int argc, char **argv)
{
    int read_fd, write_fd;
    struct peer peer_list[MAX_PEERLIST];
    int size_peer_list = 0;
    struct metadata meta_data;
    char is_metadata = 0;

    sscanf(argv[0], "%d", &read_fd);
    sscanf(argv[1], "%d", &write_fd);

    char request = 1;
    while (1)
    {

        if (read(read_fd, &request, sizeof(request)) <= 0)
        {
            perror("DB - chyba pri nacitani z fd");
            exit(-1);
        }

        printf("--------------------------------\n");
        printf("Database - Request : %d\n", request);

        struct peer new_peer;
        struct metadata new_metadata;
        char *buffer;

        switch (request)
        {
        case NEW_PEER:

            if (read(read_fd, &new_peer, sizeof(struct peer)) < 0)
            {
                perror("DB - chyba pri nacitani z fd");
                exit(-1);
            }

            peer_list[size_peer_list] = new_peer;

            size_peer_list++;

            printf("Database - Novy peer,%d.%d.%d.%d:%d\n", new_peer.ip[0], new_peer.ip[1], new_peer.ip[2], new_peer.ip[3], new_peer.port);
            printf("Database - Pocet novych peerov je %d\n", size_peer_list);

            break;

        case DELETE_PEER:

            if (read(read_fd, &new_peer, sizeof(struct peer)) < 0)
            {
                perror("DB - chyba pri nacitani z fd");
                exit(-1);
            }

            for (int i = 0; i < size_peer_list; i++)
            {
                if (!memcmp( &new_peer, &peer_list[i], sizeof(struct peer)))
                {
                    while (i != (size_peer_list - 1))
                    {
                        memcpy( &peer_list[i], &peer_list[i + 1], sizeof(struct peer));
                        i++;
                    }
                    size_peer_list--;

                    printf("Database - Pocet novych peerov je %d\n", size_peer_list);
                    break;
                }

                if (i == (size_peer_list - 1))
                    printf("Database - Peer %d.%d.%d.%d:%d sa nenasiel\n", new_peer.ip[0], new_peer.ip[1], new_peer.ip[2], new_peer.ip[3], new_peer.port);
            }

            break;

        case NEW_METADATA:

            if (read(read_fd, &new_metadata, sizeof(struct metadata)) < 0)
            {
                perror("DB - chyba pri nacitani z fd");
                exit(-1);
            }

            if (!is_metadata)
            {
                meta_data = new_metadata;
                is_metadata = 1;

                printf("Database - Nove metadata ! :\nMeno :%s\nVelkost Suboru: %d\nVelkost Bloku: %d\n",
                       new_metadata.name, new_metadata.file_size, new_metadata.size_block);
            }
            else
                printf("Database - Uz existuju metadata !\n");

            break;

        case DELETE_METADATA:
            if (is_metadata)
            {
                memset(&meta_data, sizeof(struct metadata), 0);
                is_metadata = 0;
            }

            break;

        case EXIST_METADATA:
            if (write(write_fd, &is_metadata, sizeof(is_metadata)) < 0)
            {
                perror("DB - chyba pri nacitani z fd");
                exit(-1);
            }
            break;

        case PEER_LIST:
#define SIZEOF_RESPONSE_PEERLIST sizeof(struct peer) * size_peer_list + sizeof(size_peer_list)
            buffer = (char *)malloc(sizeof(struct peer) * size_peer_list + sizeof(size_peer_list));
            *((int *)buffer) = size_peer_list;
            memcpy(buffer + sizeof(int), peer_list, sizeof(struct peer) * size_peer_list);

            if (write(write_fd, buffer, sizeof(struct peer) * size_peer_list + sizeof(size_peer_list)) < 0)
            {
                perror("DB - chyba pri nacitani z fd");
                exit(-1);
            }

            printf("Database - Odosielam udaje.\n");
#undef SIZEOF_RESPONSE_PEERLIST
            break;

        case METADATA:
            printf("Database - Posielam metadata.\nn");
            if (write(write_fd, &meta_data, sizeof(struct metadata)) < 0)
            {
                perror("DB - chyba pri nacitani z fd");
                exit(-1);
            }
            break;

        default:
            printf("Database - Neidentifikovatelny request\n");
        }
    }

    return 0;
}