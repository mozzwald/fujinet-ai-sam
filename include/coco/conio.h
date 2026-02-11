#ifndef CONIO_H
#define CONIO_H

#include <fujinet-fuji.h>
#include <coco.h>

void clear_screen(byte color);
char cgetc(void);
int wherex(void);
int wherey(void);
void gotoxy(int x, int y);
bool get_line(char *buf, int max_len);
void hirestxt_init(void);
void hirestxt_close(void);
void switch_colorset(void);
#define clrscr() clear_screen(1);

#define ARROW_UP 0x5E
#define ARROW_DOWN 0x0A
#define ARROW_LEFT 0x08
#define ARROW_RIGHT 0x09
#define ENTER 0x0D
#define BREAK 0x03
#define NEWLINE 0x0A

#endif /* CONIO_H */