#include <fujinet-fuji.h>
#include <ctype.h>
#include <conio.h>

#ifdef clrscr
#undef clrscr
#endif

#include <hirestxt.h>

#define SCREEN_BUFFER (byte*) 0xA00

byte colorset = 0;
bool hirestxt_mode = false;
bool cursor_on = false;

void hirestxt_init(void)
{
    hirestxt_mode = true;

    // Define a `HiResTextScreenInit` object:
    struct HiResTextScreenInit init =
        {
            42,                 /* characters per row */
            writeCharAt_42cols, /* must be consistent with previous field */
            SCREEN_BUFFER,      /* pointer to the text screen buffer */
            TRUE,               /* redirects printf() to the 42x24 text screen */
            (word *)0x112,      /* pointer to a 60 Hz async counter (Color Basic's TIMER) */
            0,                  /* default cursor blinking rate */
            NULL,               /* use inkey(), i.e., Color Basic's INKEY$ */
            NULL,               /* no sound on '\a' */
        };

    width(32);                               /* PMODE graphics will only appear from 32x16 (does nothing on CoCo 1&2) */
    pmode(4, (byte *)init.textScreenBuffer); /* hires text mode */
    pcls(255);
    screen(1, colorset);
    initHiResTextScreen(&init);
}

void hirestxt_close(void)
{
    if (hirestxt_mode)
    {
        hirestxt_mode = false;
        closeHiResTextScreen();
        width(32);
        pmode(0, 0);
        screen(0, 0);
        clrscr();
    }
}

void switch_colorset(void)
{
    if (hirestxt_mode)
    {
        if (colorset == 0)
        {
            colorset = 1;
        }
        else
        {
            colorset = 0;
        }

        screen(1, colorset);
    }
}

void gotoxy(int x, int y)
{
    if (hirestxt_mode)
    {
        moveCursor((byte) x, (byte) y);
    }
    else
    {
        locate((byte) x, (byte) y);
    }
}

int wherex(void)
{
    if (hirestxt_mode)
    {
        return (int) getCursorColumn();
    }
    else
    {
        return 0;
    }
}

int wherey(void)
{
    if (hirestxt_mode)
    {
        return (int) getCursorRow();
    }
    else
    {
        return 0;
    }
}

void cursor(bool onoff)
{
    if (hirestxt_mode)
    {
        if (!cursor_on && onoff)
        {
            animateCursor();
        }
        else if (cursor_on && !onoff)
        {
            removeCursor();
        }
    }

    cursor_on = onoff;
}

void clear_screen(byte color)
{
    if (hirestxt_mode)
    {
        clrscr();
    }
    else
    {
        cls(color);
    }
}

char cgetc()
{
    byte shift = false;
    byte k;

    while (true)
    {
        if (hirestxt_mode)
        {
            if (cursor_on)
            {
                k = waitKeyBlinkingCursor();
            }
            else
            {
                k = inkey();
            }
        }
        else
        {
            if (cursor_on)
            {
                k = waitkey(cursor_on);
            }
            else
            {
                k = inkey();
            }
        }

        if (isKeyPressed(KEY_PROBE_SHIFT, KEY_BIT_SHIFT))
        {
            shift = 0x00;
        }
        else
        {
            if (k > '@' && k < '[')
            {
                shift = 0x20;
            }
        }

        return (char)k + shift;
    }
}

bool get_line(char *buf, int max_len)
{
    uint8_t c;
    uint16_t i = 0;

    do
    {
        cursor(true);
        c = cgetc();
        if (c == BREAK)
        {
            cursor(false);
            return false;
        }
        else if (isprint(c))
        {
            putchar(c);
            buf[i] = c;
            if (i < max_len - 1)
                i++;
        }
        else if (c == ARROW_LEFT)
        {
            if (i)
            {
                putchar(ARROW_LEFT);
                putchar(' ');
                putchar(ARROW_LEFT);
                --i;
            }
        }
    } while (c != ENTER);
    putchar('\n');
    buf[i] = '\0';

    cursor(false);
    return true;
}
