// Linux shim for <conio.h>. The MSVC CRT exposes _getch(), _kbhit(),
// _putch(), _cprintf() here. eseutil uses _getch() to wait for a single
// keypress at help/prompt boundaries — implement it via termios so the
// terminal reads one char without echo or line-buffering.
#pragma once

#include <termios.h>
#include <unistd.h>
#include <stdio.h>

static inline int _getch( void )
{
    struct termios told, tnew;
    int            ch;

    if ( tcgetattr( STDIN_FILENO, &told ) != 0 )
    {
        return getchar();
    }
    tnew = told;
    tnew.c_lflag &= ~( ICANON | ECHO );
    tnew.c_cc[VMIN]  = 1;
    tnew.c_cc[VTIME] = 0;
    tcsetattr( STDIN_FILENO, TCSANOW, &tnew );
    ch = getchar();
    tcsetattr( STDIN_FILENO, TCSANOW, &told );
    return ch;
}
