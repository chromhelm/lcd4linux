/* $Id: drv_GLCD2Serial.c 975 2009-01-18 11:16:20Z michael $
 * $URL: https://ssl.bulix.org/svn/lcd4linux/trunk/drv_GLCD2Serial.c $
 *
 * GLCD2Serial driver for LCD4Linux
 *
 * Copyright (C) 2016 Wilhelm Wiens <wilhelmwiens@gmail.com>
 *
 * This file is part of LCD4Linux.
 *
 * LCD4Linux is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * LCD4Linux is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/* 
 *
 * exported fuctions:
 *
 * struct DRIVER drv_GLCD2Serial
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>

#include "debug.h"
#include "cfg.h"
#include "qprintf.h"
#include "udelay.h"
#include "plugin.h"
#include "widget.h"
#include "widget_text.h"
#include "widget_icon.h"
#include "widget_bar.h"
#include "drv.h"

/* text mode display? */
//#include "drv_generic_text.h"

/* graphic display? */
#include "drv_generic_graphic.h"

/* GPO's? */
//#include "drv_generic_gpio.h"

/* serial port? */
#include "drv_generic_serial.h"

static char Name[] = "GLCD2Serial";

#define BYTES_PER_ROW (DCOLS / 8)
#define SERIAL_BUFFER_SIZE ((5 + BYTES_PER_ROW * DROWS) * 2 + 2)
#define VIDEO_BUFFER_SIZE (DROWS * BYTES_PER_ROW)

static unsigned char* video_buffer = NULL;
static unsigned char* dirty_buffer = NULL;
static unsigned char* serialBuffer = NULL;

static int PackMesseg(unsigned char* buffer, int length);
static int SendEntireDisplay();
static int SendPartialLine(int pos);
static void DrawPartialImage(int row, int col, int height, int width);
static int SendLine(int pos);
static int RemoveSubtitution(unsigned char *buffer, int length);
static void Send();
static void Reset();

static uint32_t SendBytesCountMax = 0;
static uint32_t SendBytesCountMin = UINT32_MAX;
static uint32_t SendBytesCountAvg = 0;
static uint32_t SendBytesCountAvg10 = 0;
static uint32_t SendBytesCountAvg100 = 0;
static uint32_t SendBytesCountAvg1000 = 0;
static uint32_t SendBytesCountNULL = 0;
static uint32_t SendBytesCount = 0;
static uint32_t SendBytesCountCount = 0;

//Helper funktions
static int GetRowFromOffset(int offset)
{
    return (offset * 8) / DCOLS;
}

static int GetColumnFromOffset(int offset)
{
    return (offset * 8) % DCOLS;
}

static int GetOffset(int row, int column)
{
    return (row * BYTES_PER_ROW) + column / 8;
}

static unsigned char GetMask(int column)
{
    return 0x80 >> (column % 8);
}


/****************************************/
/***  hardware dependant functions    ***/
/****************************************/

static int drv_GLCD2Serial_open(const char *section)
{
    /* open serial port */
    /* don't mind about device, speed and stuff, this function will take care of */

    if (drv_generic_serial_open(section, Name, 0) < 0)
	return -1;
    debug("Serial Port Open");
    sleep(2);
    return 0;
}

static int drv_GLCD2Serial_close(void)
{
    /* close whatever port you've opened */
    drv_generic_serial_close();
    debug("Serial Port Close");
    return 0;
}

/* dummy function that sends something to the display */
static void drv_GLCD2Serial_send(char *data, const unsigned int len)
{
    int length = 0;
    length = PackMesseg((unsigned char*)data, len);
    drv_generic_serial_write(data, length);

    if(length == 0 || len == 0)
    {
        debug();
    }

    SendBytesCount += length;
}

/* for graphic displays only */
static void drv_GLCD2Serial_blit(const int row, const int col, const int height, const int width)
{
    SendBytesCount = 0;
    /* update offscreen buffer */
    for (int r = row; r < row + height; r++) {
        for (int c = col; c < col + width; c++) {

            int pos;
            unsigned char mask;

            pos = GetOffset(r, c);
            mask = GetMask(c);

            int original = video_buffer[pos];

            if (drv_generic_graphic_black(r, c))
                video_buffer[pos] |= mask;
            else
                video_buffer[pos] &= ~(mask);

            if (video_buffer[pos] != original)
                dirty_buffer[pos] |= mask;
        }
    }
    Send();

    if(SendBytesCount == 0) SendBytesCountNULL++;
    else
    {
        if(SendBytesCount > SendBytesCountMax) SendBytesCountMax = SendBytesCount;
        if(SendBytesCount < SendBytesCountMin) SendBytesCountMin = SendBytesCount;
    }
    SendBytesCountAvg = SendBytesCountAvg + SendBytesCount - SendBytesCountAvg / 2;
    SendBytesCountAvg10 = SendBytesCountAvg10 + SendBytesCount - SendBytesCountAvg10 / 10;
    SendBytesCountAvg100 = SendBytesCountAvg100 + SendBytesCount - SendBytesCountAvg100 / 100;
    SendBytesCountAvg1000 = SendBytesCountAvg1000 + SendBytesCount - SendBytesCountAvg1000 / 1000;
    SendBytesCountCount++;

//    debug("Send byte min=%-3u avg=%-3u avg10=%-3u avg100=%-3u avg1000=%-3u max=%-3u last=%-3u  NULL=%-4u count=%u",
//          SendBytesCountMin,
//          SendBytesCountAvg / 2,
//          SendBytesCountAvg10 / 10,
//          SendBytesCountAvg100 / 100,
//          SendBytesCountAvg1000 / 1000,
//          SendBytesCountMax,
//          SendBytesCount,
//          SendBytesCountNULL,
//          SendBytesCountCount);
}

static void Send()
{
    int metode = 0;
    int pos = 0;

    while(pos < VIDEO_BUFFER_SIZE)
    {
        if(dirty_buffer[pos])
        {
            if(metode == 0) pos = SendPartialLine(pos);
            else if(metode == 1) pos = SendPartialLine(pos);
            else if(metode == 2) pos = SendLine(pos);
        }
        else pos++;
    }
}

static int SendEntireDisplay()
{
    serialBuffer[0] = 0x01;
    memcpy(serialBuffer + 1, video_buffer, VIDEO_BUFFER_SIZE);

    drv_GLCD2Serial_send((char*)serialBuffer, 1 + VIDEO_BUFFER_SIZE);

    return VIDEO_BUFFER_SIZE;
}

//sending min 3 bytes and max to end of row
static int SendPartialLine(int pos)
{
    int i = 0;
    pos -= pos % 3;
    serialBuffer[0] = 0x02;
    serialBuffer[1] = GetRowFromOffset(pos);
    serialBuffer[2] = GetColumnFromOffset(pos);

    do
    {
        serialBuffer[3 + i] = video_buffer[pos + i];
        dirty_buffer[pos + i] = 0;
        i++;
    }
    while(((pos + i) % 3) ||
          (dirty_buffer[pos + i] && (GetColumnFromOffset(pos + i) != 0)));
    
    drv_GLCD2Serial_send((char*)serialBuffer, 3 + i);

    return pos + i;
}

static void DrawPartialImage(int row, int col, int height, int width)
{ //not working
    int i = 0;
    
    serialBuffer[0] = 0x02;
    serialBuffer[1] = row;
    serialBuffer[2] = col;
    serialBuffer[3] = height;
    serialBuffer[4] = width;
    
    drv_GLCD2Serial_send((char*)serialBuffer, 5 + (i / 8) + 1);

}

static int SendLine(int pos)
{
    int i = 0;
    
    pos -= pos % BYTES_PER_ROW;

    serialBuffer[0] = 0x02;
    serialBuffer[1] = GetRowFromOffset(pos);
    serialBuffer[2] = 0;

    while(i < BYTES_PER_ROW)
    {
        serialBuffer[3 + i] = video_buffer[pos + i];
        dirty_buffer[pos + i] = 0;
        i++;
    }
    
    drv_GLCD2Serial_send((char*)serialBuffer, 3 + i);
    return pos + i;
}

static int PackMesseg(unsigned char* buffer, int length)
{
    memmove(buffer+1,buffer, length++);
    buffer[0] = 0xAA;                   //Frame start
    
    for(int i = 1; i < length; i++)     // do suptiyution
    {
        if(buffer[i] == 0xAA ||
           buffer[i] == 0xCC ||
           buffer[i] == 0xFF)
        {
            memmove(buffer+i+1,buffer+i, length-i);
            buffer[i++] = 0xFF;
            buffer[i]  ^= 0xFF;
            length++;
        }
    }
    buffer[length++] = 0xCC;            // end of frame
    return length;
}

static int RemoveSubtitution(unsigned char *buffer, int length)
{
    for(int i = 0; i < length; i++)
    {
        if((unsigned char)buffer[i] == 0xFF)
        {
            memmove(buffer+i,buffer+i+1, length-i);
            buffer[i] ^= 0xFF;
            length--;
        }
    }
    return length;
}

static void Reset()
{
    /* reset & initialize display */
    /* assume 0x00 to be a 'reset' command */
    serialBuffer[0] = 0x00;
    drv_GLCD2Serial_send((char *)serialBuffer, 1);
    sleep(1);
}

/* remove unless you have GPO's */
/*static int drv_GLCD2Serial_GPO(const int num, const int val)
{
    char cmd[4];

    // assume 0x42 to be the 'GPO' command/
    cmd[0] = 0x42;
    cmd[1] = num;
    cmd[2] = (val > 0) ? 1 : 0;

    drv_GLCD2Serial_send(cmd, 3);

    return 0;
}
*/

/* example function used in a plugin */
static int drv_GLCD2Serial_contrast(int contrast)
{
    char cmd[2];

    // adjust limits according to the display 
    if (contrast < 0)
	contrast = 0;
    if (contrast > 255)
	contrast = 255;

    // call a 'contrast' function
    // assume 0x04 to be the 'set contrast' command 
    cmd[0] = 0x04;
    cmd[1] = contrast;
    drv_GLCD2Serial_send(cmd, 2);
    debug("Set contrast to = %i", contrast);
    return contrast;
}

/* start graphic display */
static int drv_GLCD2Serial_start(const char *section)
{
    char *s;
    int contrast;

    /* read display size from config */
    s = cfg_get(section, "Size", NULL);
    if (s == NULL || *s == '\0') {
	error("%s: no '%s.Size' entry from %s", Name, section, cfg_source());
	return -1;
    }

    DROWS = -1;
    DCOLS = -1;
    if (sscanf(s, "%dx%d", &DCOLS, &DROWS) != 2 || DCOLS < 1 || DROWS < 1) {
	error("%s: bad Size '%s' from %s", Name, s, cfg_source());
	return -1;
    }

    s = cfg_get(section, "Font", "6x8");
    if (s == NULL || *s == '\0') {
	error("%s: no '%s.Font' entry from %s", Name, section, cfg_source());
	return -1;
    }

    XRES = -1;
    YRES = -1;
    if (sscanf(s, "%dx%d", &XRES, &YRES) != 2 || XRES < 1 || YRES < 1) {
	error("%s: bad Font '%s' from %s", Name, s, cfg_source());
	return -1;
    }

    /* Fixme: provider other fonts someday... */
    if (XRES != 6 && YRES != 8) {
	error("%s: bad Font '%s' from %s (only 6x8 at the moment)", Name, s, cfg_source());
	return -1;
    }

    /* you surely want to allocate a framebuffer or something... */
    video_buffer = malloc(VIDEO_BUFFER_SIZE);
    memset(video_buffer, 0, VIDEO_BUFFER_SIZE);
    dirty_buffer = malloc(VIDEO_BUFFER_SIZE);
    memset(dirty_buffer, 0, VIDEO_BUFFER_SIZE);

    serialBuffer = malloc(SERIAL_BUFFER_SIZE);
    memset(serialBuffer, 0, SERIAL_BUFFER_SIZE);

    /* open communication with the display */
    if (drv_GLCD2Serial_open(section) < 0) {
	return -1;
    }

    Reset();

    if (cfg_number(section, "Contrast", 0, 0, 255, &contrast) > 0) {
	drv_GLCD2Serial_contrast(contrast);
    }

    return 0;
}


/****************************************/
/***            plugins               ***/
/****************************************/

static void plugin_contrast(RESULT * result, RESULT * arg1)
{
    double contrast;

    contrast = drv_GLCD2Serial_contrast(R2N(arg1));
    SetResult(&result, R_NUMBER, &contrast);
}


/****************************************/
/***        widget callbacks          ***/
/****************************************/


/* using drv_generic_text_draw(W) */
/* using drv_generic_text_icon_draw(W) */
/* using drv_generic_text_bar_draw(W) */
/* using drv_generic_gpio_draw(W) */


/****************************************/
/***        exported functions        ***/
/****************************************/


/* list models */
int drv_GLCD2Serial_list(void)
{
    printf("GLCD2Serial driver");
    return 0;
}


/* initialize driver & display */
/* use this function for a graphic display */
int drv_GLCD2Serial_init(const char *section, const int quiet)
{
    int ret;

    /* real worker functions */
    drv_generic_graphic_real_blit = drv_GLCD2Serial_blit;

    /* remove unless you have GPO's */
    //drv_generic_gpio_real_set = drv_GLCD2Serial_GPO;

    /* start display */
    if ((ret = drv_GLCD2Serial_start(section)) != 0)
        return ret;

    /* initialize generic graphic driver */
    if ((ret = drv_generic_graphic_init(section, Name)) != 0)
        return ret;

    if (!quiet) {
        char buffer[40];
        qprintf(buffer, sizeof(buffer), "%s %dx%d", Name, DCOLS, DROWS);
        if (drv_generic_graphic_greet(buffer, NULL)) {
            sleep(3);
            drv_generic_graphic_clear();
        }
    }

    /* register plugins */
    AddFunction("LCD::contrast", 1, plugin_contrast);

    return 0;
}

/* close driver & display */
/* use this function for a graphic display */
int drv_GLCD2Serial_quit(const int quiet)
{
    info("%s: shutting down.", Name);

    /* clear display */
    drv_generic_graphic_clear();

    /* remove unless you have GPO's */
    //drv_generic_gpio_quit();

    /* say goodbye... */
    if (!quiet) {
	drv_generic_graphic_greet("goodbye!", NULL);
    }

    drv_generic_graphic_quit();

    debug("closing connection");
    /* free resorces */
    drv_GLCD2Serial_close();

    if(video_buffer != NULL)
    {
        free(video_buffer);
    }
    if(dirty_buffer != NULL)
    {
        free(dirty_buffer);
    }
    if(serialBuffer != NULL)
    {
        free(serialBuffer);
    }

    return (0);
}


/* use this one for a text display */
DRIVER drv_GLCD2Serial = {
    .name = Name,
    .list = drv_GLCD2Serial_list,
    .init = drv_GLCD2Serial_init,
    .quit = drv_GLCD2Serial_quit,
};
