#include <i86.h>
#include <dos.h>
#include <stdio.h>
#include <stdint.h>

extern int getch(void);
extern int putch(int);

int screen_width = 80;
int screen_height = 25;

void msdos_init_screen(void)
{
    union REGS r;
    uint8_t far *bda_rows;

    r.h.ah = 0x0F;
    int86(0x10, &r, &r);
    screen_width = r.h.ah;
    if (screen_width < 20 || screen_width > 132)
        screen_width = 80;

    bda_rows = (uint8_t far *)MK_FP(0x0040, 0x0084);
    if (*bda_rows >= 20 && *bda_rows <= 50)
        screen_height = *bda_rows + 1;
    else
        screen_height = 25;

    clrscr();
}

char cgetc(void)
{
    int c = getch();
    if (c == 0 || c == 0xE0)
        (void)getch();
    return (char)c;
}

void cputc(char c)
{
    putch((unsigned char)c);
}

void clrscr(void)
{
    union REGS r;
    r.h.ah = 0x06;
    r.h.al = 0x00;
    r.h.bh = 0x07;
    r.h.ch = 0; r.h.cl = 0;
    r.h.dh = (uint8_t)(screen_height - 1);
    r.h.dl = (uint8_t)(screen_width  - 1);
    int86(0x10, &r, &r);
    r.h.ah = 0x02;
    r.h.bh = 0x00;
    r.h.dh = 0; r.h.dl = 0;
    int86(0x10, &r, &r);
}

void gotoxy(int x, int y)
{
    union REGS r;
    if (x < 1) x = 1;
    if (y < 1) y = 1;
    r.h.ah = 0x02;
    r.h.bh = 0x00;
    r.h.dh = (uint8_t)(y - 1);
    r.h.dl = (uint8_t)(x - 1);
    int86(0x10, &r, &r);
}

int wherex(void)
{
    union REGS r;
    r.h.ah = 0x03;
    r.h.bh = 0x00;
    int86(0x10, &r, &r);
    return (int)r.h.dl + 1;
}

int wherey(void)
{
    union REGS r;
    r.h.ah = 0x03;
    r.h.bh = 0x00;
    int86(0x10, &r, &r);
    return (int)r.h.dh + 1;
}
