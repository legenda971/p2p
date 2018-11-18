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

#include "define/metadata.h"
#include "define/request_respons/database.h"

#define PORT 56321

int main(int argc, char **argv)
{
    /* Socket initial */

    int socketListen;
    if ((socketListen = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Chyba pri vytvarani socktu.\n");
        exit(-1);
    }

    struct sockaddr_in adresa;
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

    const char *menu[] = {"Nahrat subor", "Stiahnut Subor", "Nastavenia", "Koniec"};
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
                /* Vyber suboru  */
                wclear(menuwin);
                box(menuwin, 0, 0);
                mvwprintw(menuwin, y_middle - 1, x_middle - (int)(27 / 2), "Prosim zadajte meno suboru");

                char name_dir[50];
                memset(name_dir, 0, sizeof(name_dir));
                /*do{
                        mvwprintw(menuwin, y_middle , x_middle - (int)(strlen(name_dir) / 2), "%s", name_dir);
                        wrefresh(menuwin);
                    }while((name_dir[strlen(name_dir)] = wgetch(menuwin)) != 10);

                    name_dir[strlen(name_dir) - 1] = 0;*/

                while (akcia = wgetch(menuwin))
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

                    mvwprintw(menuwin, y_middle, x_middle - (int)(strlen(name_dir) / 2), "%s", name_dir);
                    wrefresh(menuwin);
                }

                int file_fd;
                if ((file_fd = open(name_dir, O_RDONLY)) < 0)
                {
                    wattron(menuwin, A_REVERSE);
                    mvwprintw(menuwin, y_middle + 1, x_middle - (int)(66 / 2), "ERROR - Subor sa neda otvorit. Stlacte ENTER pre navrat do menu .");
                    wattroff(menuwin, A_REVERSE);
                    while (wgetch(menuwin) != 10)
                        ;
                }

                struct stat file_stat;
                if (stat(name_dir, &file_stat) == -1)
                {
                    wattron(menuwin, A_REVERSE);
                    mvwprintw(menuwin, y_middle + 1, x_middle - (int)(66 / 2), "2. ERROR - Subor sa neda otvorit. Stlacte ENTER pre navrat do menu .");
                    wattroff(menuwin, A_REVERSE);
                    while (wgetch(menuwin) != 10)
                        ;
                }

                int velkost_bloku = 1;

                wclear(menuwin);

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
                    int pocet_blokov;
                    if ((int)temp < temp)
                        pocet_blokov = (temp) + 1;
                    else
                        pocet_blokov = temp;

                    wclear(menuwin);
                    box(menuwin, 0, 0);

                    mvwprintw(menuwin, y_middle - 2, x_middle - 10, "Meno suboru\t: %s", name_dir);
                    mvwprintw(menuwin, y_middle - 1, x_middle - 10, "Velkost suboru\t: %d", file_stat.st_size);
                    mvwprintw(menuwin, y_middle, x_middle - 10, "Pocet blokov\t: %d", pocet_blokov);
                    mvwprintw(menuwin, y_middle + 1, x_middle - 10, "Velkost bloku\t: %d", velkost_bloku);

                    wattron(menuwin, A_REVERSE);
                    mvwprintw(menuwin, y_middle + 3, x_middle - (int)(60 / 2), "Pre potvrdenie stlacte ENTER. Velkost bloku menite sipkami.");
                    wattroff(menuwin, A_REVERSE);
                    wrefresh(menuwin);
                } while ((akcia = wgetch(menuwin)) != 10);

                struct metadata new_metadata;
                new_metadata.size_block = velkost_bloku;
                new_metadata.file_size = file_stat.st_size;
                strncpy(new_metadata.name, name_dir, sizeof(new_metadata.name));

                while ((akcia = wgetch(menuwin)) != 10);
                /* Nahrat metadata na server */

                char buffer[sizeof(struct metadata) + 1];
                /* Request */
                
                buffer[0] = (enum request)NEW_METADATA;
                strcpy(buffer + 1, &new_metadata);

                if (write(socketListen, buffer, sizeof(buffer)) < 0)
                {
                    perror("Chyba pri zapisovani do fd");
                    exit(-3);
                }

                break;

            case 4:
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