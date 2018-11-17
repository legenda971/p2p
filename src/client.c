#include <stdio.h>
#include <string.h>
#include <ncurses.h> /* (terminal) Install libncurses5-dev */
#include <stdlib.h>  /* exit() */

int main(int argc, char **argv)
{
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
    /* Connect to server */

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

        wrefresh(menuwin);
    }

    endwin();
    exit(1);
}