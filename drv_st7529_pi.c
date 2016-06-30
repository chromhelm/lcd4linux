/* $Id:$
 * $URL: $
 *
 * st7529_pi driver for LCD4Linux
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
 * struct DRIVER drv_st7529_pi
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
#include <wiringPi.h>

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

/* graphic display? */
#include "drv_generic_graphic.h"

static char Name[] = "st7529_pi";

static const uint8_t GreySet1[16] =
{ 0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30 };

static const uint8_t GreySet2[16] =
{ 1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31 };


#define SW_PIXEL_ON 0x00
#define SW_PIXEL_OFF 0x1F

#define PIXEL_PER_BYTE 1
#define BYTES_PER_ROW (DCOLS / PIXEL_PER_BYTE)
#define VIDEO_BUFFER_SIZE (DROWS * BYTES_PER_ROW)

#define DEFAULT_CONTRAST 0x118

static unsigned char* video_buffer = NULL;
static int clockHighDelay;
static int clockLowDelay;
static int pinRST;
static int pinMOSI;
static int pinCS;
static int pinCLK;

static int drv_st7529_pi_contrast(int contrast);

//Helper funktions
static int GetPosFromCordiats(int row, int column)
{
    return (row * BYTES_PER_ROW) + column / PIXEL_PER_BYTE;
}

static void delayLoop(uint16_t loops)
{
    uint16_t i;
    for(i = 0; i < loops; i++)
    {
            asm("nop;");
    }
}

/****************************************/
/***  hardware dependant functions    ***/
/****************************************/

static void display_write(bool command, uint8_t data)
{
    uint8_t mask = 0x80;

    digitalWrite(pinCS, LOW);

    digitalWrite(pinCLK, LOW);
    digitalWrite(pinMOSI, command ? LOW : HIGH);
    delayLoop(clockLowDelay);
    digitalWrite(pinCLK, HIGH);
    command <<= 1;                  // just to keep timing equal
    delayLoop(clockHighDelay);

    while(mask)
    {
        digitalWrite(pinCLK, LOW);
        digitalWrite(pinMOSI, data & mask ? HIGH : LOW);
        delayLoop(clockLowDelay);
        digitalWrite(pinCLK, HIGH);
        mask >>= 1;
        delayLoop(clockHighDelay);
    }

    digitalWrite(pinCS, HIGH);
}

static void display_SetPos(uint8_t y, uint8_t x, uint8_t h, uint8_t w)
{ 
  display_write(true, 0x30);
  
  display_write(true, 0x15);                              // Kolonne adresse Set
  display_write(false, x/3);                              // Start Kolonne passer for "omvendt" LCD
  display_write(false, x /3 + (w-1)/3);                        // Slut Kolonne
  
  display_write(true, 0x75);                              // Linie adresse Set
  display_write(false, 0x10 + y);                         // Start Linie (LCD er ikke mappet til (0,0)
  display_write(false, 0x10 + y + h);                     // Slut Linie
}

static void display_SetPosXY(uint8_t y, uint8_t x)
{ 
    display_SetPos(y, x, DROWS, DCOLS);
}

static void display_SetPosNull()
{  
  display_SetPos(0, 0, DROWS, DCOLS);
}

static void display_EXTCommands(bool out)
{
  display_write(true, 0x30 | out);
}

static void display_Sleep(bool sleep)
{
  display_write(true, 0x94 | sleep);
}

static void display_On(bool on)
{
  display_write(true, 0xAE | on);
}

static void display_Fill(uint8_t value)
{
  uint16_t i;
    
  display_SetPosNull();
  display_write(true, 0x5C);  
  for(i = 0; i < DROWS * DCOLS; i+=3)
  {
      display_write(false, (value << 3) | value >> 2);
      display_write(false, (value << 6) | value);
  }
}

static void display_Init()
{
    int i;

    if(pinRST >= 0)
    {
        digitalWrite(pinRST, LOW);
        udelay(10);
        digitalWrite(pinRST, HIGH);
        udelay(10);
    }

    display_EXTCommands(false);
 
    display_Sleep(false);
 
    display_write(true, 0xD1);                                 // OSC On
 
    display_write(true, 0x20);                                 // Power Control Set
    display_write(false, 0x08);                                // Booster Must Be On First
    udelay(1000);                                              // Delay 1 ms.
  
    display_write(true, 0x20);                                 // Power Control Set
    display_write(false, 0x0B);                                // Booster, Regulator, Follower ON

    drv_st7529_pi_contrast(DEFAULT_CONTRAST);

    display_write(true, 0xCA);                                 // Display Control
    display_write(false, 0x00);                                // CL = X1
    display_write(false, 0x23);                                // Duty = 128
    display_write(false, 0x00);                                // FR Inverse-Set Value

    display_write(true, 0xA6);                                 // Normal Display

    display_write(true, 0xBB);                                 // COM Scan Direction
    display_write(false, 0x02);                                // 0 -> 79  80 -> 159  (00)

    display_write(true, 0xBC);
    display_write(false, 0x01);                                //Normal
    display_write(false, 0x00);                                //RGB Arrangement
    display_write(false, 0x01);                                //65K COLOR

    display_SetPos(0, 0, 128, 160);

    display_EXTCommands(true); 
    display_write(true, 0x32);                                 // Analog Circuit Set
    display_write(false, 0x00);                                // OSC Frequency = 000 (Default)
    display_write(false, 0x00);                                // Booster Efficiency = 01 (Default)
    display_write(false, 0x02);                                // Bias = 1/14  (06)  (OPRINDELIG: 0x05)

    display_write(true, 0x22);                                 // Weight set
    display_write(false, 0x03);                                // Weighting value
    display_write(false, 0x02);                                // Edge value
    display_write(false, 0x02);                                // Enable flag

    display_write(true, 0x34);                                 // Software Initial

    display_write(true, 0x20);                                 // Grey set 1
    for(i=0; i<16; i++)
      display_write(false, GreySet1[i]);
    display_write(true, 0x21);                                 // Grey set 2
    for(i=0; i<16; i++)
      display_write(false, GreySet1[i]);
    display_EXTCommands(false);

    display_On(true);

    display_Fill(SW_PIXEL_OFF);
}


static int drv_st7529_pi_InitPins()
{
    if (wiringPiSetup () == -1)
        return 1;

    pinMode(pinCS, OUTPUT);
    digitalWrite(pinCS, HIGH);

    pinMode(pinCLK, OUTPUT);
    digitalWrite(pinCLK, HIGH);

    pinMode(pinMOSI, OUTPUT);

    if(pinRST >= 0)
    {
        pinMode(pinRST, OUTPUT);
        digitalWrite(pinRST, HIGH);
    }
    return 0;
}

static int drv_st7529_pi_FreePins()
{
    pinMode(pinCS, INPUT);
    pinMode(pinCLK, INPUT);
    pinMode(pinMOSI, INPUT);

    if(pinRST >= 0)
    {
        pinMode(pinRST, INPUT);
    }

    return 0;
}

static void drv_st7529_pi_blit(const int row, const int col, const int height, const int width)
{
    uint16_t pos, pos_1, pos_2;
    bool write = false;
    uint8_t original[3], r, c, w;

    w = width - (col % 3);
    for(r = row; r <  row + height; r++)
    {
        write = false;
        for(c = col - (col % 3); c < col + w; c+=3)
        {
            pos = GetPosFromCordiats(r, c);
            pos_1 = pos + 1;
            pos_2 = pos + 2;
            original[0] = video_buffer[pos];
            original[1] = video_buffer[pos_1];
            original[2] = video_buffer[pos_2];

            video_buffer[pos] = drv_generic_graphic_black(r, c);
            video_buffer[pos_1] = drv_generic_graphic_black(r, c + 1);
            video_buffer[pos_2] = drv_generic_graphic_black(r, c + 2);

            if (video_buffer[pos] != original[0]
                    || video_buffer[pos_1] != original[1]
                    || video_buffer[pos_2] != original[2])
            {
                if(write == false)
                {
                    display_SetPosXY(r, c);

                    display_write(true, 0x5C);

                    write = true;
                }
                display_write(false, ((video_buffer[pos] ? SW_PIXEL_ON << 3 : SW_PIXEL_OFF << 3))
                              | ((video_buffer[pos_1] ? SW_PIXEL_ON >> 2: SW_PIXEL_OFF >> 2)));
                display_write(false, ((video_buffer[pos_1] ? SW_PIXEL_ON << 6: SW_PIXEL_OFF << 6))
                              | ((video_buffer[pos_2] ? SW_PIXEL_ON : SW_PIXEL_OFF)));
            }
            else
            {
                write = false;
            }
        }
    }
}

/* example function used in a plugin */
static int drv_st7529_pi_contrast(int contrast)
{
    // adjust limits according to the display 
    if (contrast < 0)
	contrast = 0;
    if (contrast > 0x1ff)
	contrast = 0x1ff;

    display_write(true, 0x81);
    display_write(false, contrast & 0x3F);
    display_write(false, (contrast >> 6) & 0x07);
    debug("Set contrast to = %i", contrast);
    return contrast;
}

/* start graphic display */
static int drv_st7529_pi_start(const char *section)
{
    char *s;
    int contrast;
    int ret = -1;

    do
    {
        /* read display size from config */
        s = cfg_get(section, "Size", NULL);
        if (s == NULL || *s == '\0') {
            error("%s: no '%s.Size' entry from %s", Name, section, cfg_source());
            break;
        }

        DROWS = -1;
        DCOLS = -1;
        if (sscanf(s, "%dx%d", &DCOLS, &DROWS) != 2 || DCOLS < 1 || DROWS < 1) {
            error("%s: bad Size '%s' from %s", Name, s, cfg_source());
            break;
        }

        free(s);
        s = cfg_get(section, "Font", "6x8");
        if (s == NULL || *s == '\0') {
            error("%s: no '%s.Font' entry from %s", Name, section, cfg_source());
            break;
        }

        XRES = -1;
        YRES = -1;
        if (sscanf(s, "%dx%d", &XRES, &YRES) != 2 || XRES < 1 || YRES < 1) {
            error("%s: bad Font '%s' from %s", Name, s, cfg_source());
            break;
        }

        /* Fixme: provider other fonts someday... */
        if (XRES != 6 && YRES != 8) {
            error("%s: bad Font '%s' from %s (only 6x8 at the moment)", Name, s, cfg_source());
            break;
        }

        /* timing */
        cfg_number(section, "Timing.High", 100, 0, 10000, &clockHighDelay);
        cfg_number(section, "Timing.Low", 100, 0, 10000, &clockLowDelay);        

        if(cfg_number(section, "Pin.CLK", -1, 0, 40, &pinCLK) != 1
           || cfg_number(section, "Pin.CS", -1, 0, 40, &pinCS) != 1
           || cfg_number(section, "Pin.MOSI", -1, 0, 40, &pinMOSI) != 1
           || cfg_number(section, "Pin.RST", -1, 0, 40, &pinRST) == -1)
        {
            break;
        }

        if(pinCLK == -1
                || pinCS == -1
                || pinMOSI == -1)
        {
            error("%s: Not all necessery pins are defined", Name);
            break;
        }

        if(pinCLK == pinCS || pinCLK == pinMOSI || pinCLK == pinRST
                || pinCS == pinMOSI || pinCS == pinRST
                || pinMOSI == pinRST)
        {
            error("%s: Pin functions can not share the same pysikal pin", Name);
            break;
        }


        /* you surely want to allocate a framebuffer or something... */
        video_buffer = malloc(VIDEO_BUFFER_SIZE);
        memset(video_buffer, 0, VIDEO_BUFFER_SIZE);
        
        drv_st7529_pi_InitPins();

        display_Init();

        if (cfg_number(section, "Contrast", 0, 0, 511, &contrast) > 0) {
            drv_st7529_pi_contrast(contrast);
        }

        ret = 0;
    }while(0);

    if(s != NULL) free(s);

    return ret;
}

/****************************************/
/***            plugins               ***/
/****************************************/

static void plugin_contrast(RESULT * result, RESULT * arg1)
{
    double contrast;

    contrast = drv_st7529_pi_contrast(R2N(arg1));
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
int drv_st7529_pi_list(void)
{
    printf("st7529_pi driver");
    return 0;
}


/* initialize driver & display */
/* use this function for a graphic display */
int drv_st7529_pi_init(const char *section, const int quiet)
{
    int ret;

    /* real worker functions */
    drv_generic_graphic_real_blit = drv_st7529_pi_blit;

    /* start display */
    if ((ret = drv_st7529_pi_start(section)) != 0)
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

    udelay_init();

    return 0;
}

/* close driver & display */
/* use this function for a graphic display */
int drv_st7529_pi_quit(const int quiet)
{
    info("%s: shutting down.", Name);

    /* clear display */
    drv_generic_graphic_clear();

    /* say goodbye... */
    if (!quiet) {
        drv_generic_graphic_greet("goodbye!", NULL);
    }

    drv_generic_graphic_quit();

    debug("closing connection");
    /* free resorces */
    drv_st7529_pi_FreePins();

    if(video_buffer != NULL)
    {
        free(video_buffer);
    }
    return (0);
}


/* use this one for a text display */
DRIVER drv_st7529_pi = {
    .name = Name,
    .list = drv_st7529_pi_list,
    .init = drv_st7529_pi_init,
    .quit = drv_st7529_pi_quit,
};
