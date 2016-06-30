/* $Id: widget_gtext.h 1164 2011-12-22 10:48:01Z mjona $
 * $URL: https://ssl.bulix.org/svn/lcd4linux/trunk/widget_gtext.h $
 *
 * simple text widget handling
 *
 * Copyright (C) 2003, 2004 Michael Reinelt <michael@reinelt.co.at>
 * Copyright (C) 2004 The LCD4Linux Team <lcd4linux-devel@users.sourceforge.net>
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


#ifndef _WIDGET_GTEXT_H_
#define _WIDGET_GTEXT_H_

#include "property.h"
#include "widget.h"
#include "widget_text.h"

typedef struct WIDGET_GTEXT {
    PROPERTY prefix;		/* label on the left side */
    PROPERTY postfix;		/* label on the right side */
    PROPERTY value;		/* value of text widget */
    PROPERTY style;		/* text style (plain/bold/slant) */
    char *string;		/* formatted value */
    int value_x_skip;
    int value_x_off;
    int postfix_x_off;

    int width;			/* field width */
    int precision;		/* number of digits after the decimal point */
    TEXT_ALIGN align;		/* alignment: L(eft), C(enter), R(ight), M(arquee), A(utomatic) */
    int update;			/* update interval */
    int scroll;			/* marquee starting point */
    int speed;			/* marquee scrolling speed */
    int direction;		/* pingpong direction, 0=right, 1=left */
    int delay;			/* pingpong scrolling, wait before switch direction */
} WIDGET_GTEXT;


extern WIDGET_CLASS Widget_GText;

#endif