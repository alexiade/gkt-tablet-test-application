/*
 * Producer side of otd-wintab's shared event ring (see otdwt_ring.h).
 *
 * Vendored from otd-wintab (https://github.com/alexiade/otd-wintab),
 * src/wintab/ring.h, reduced to what a producer needs.
 *
 * Copyright 2026 otd-wintab contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef DOODLE_RING_H
#define DOODLE_RING_H

#include <windows.h>
#include "otdwt_ring.h"

struct ring_handle
{
    HANDLE mapping;
    HANDLE signal;      /* auto-reset; set after each publish */
    otdwt_ring *ring;
};

/* Creates (or joins) the ring named by OTD_WINTAB_INSTANCE. */
BOOL ring_create(struct ring_handle *h);
void ring_publish(struct ring_handle *h, const otdwt_event *ev);
void ring_close(struct ring_handle *h);

#endif
