/*
 * Producer side of otd-wintab's shared event ring (see otdwt_ring.h).
 *
 * Vendored from otd-wintab (https://github.com/alexiade/otd-wintab),
 * src/wintab/ring.c, reduced to what a producer needs and built against
 * the real windows.h instead of Wine's compatibility shims.
 *
 * Copyright 2026 otd-wintab contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "ring.h"

/* OTD_WINTAB_INSTANCE=<suffix> selects a private ring, so a producer
 * never mixes with a running OpenTabletDriver. */
static void ring_names(WCHAR *mapping, WCHAR *signal, size_t len)
{
    WCHAR inst[64];
    DWORD n = GetEnvironmentVariableW(L"OTD_WINTAB_INSTANCE", inst, ARRAYSIZE(inst));

    if (n > 0 && n < ARRAYSIZE(inst))
    {
        _snwprintf(mapping, len, L"%ls-%ls", OTDWT_MAPPING_NAME, inst);
        _snwprintf(signal, len, L"%ls-%ls", OTDWT_EVENT_NAME, inst);
    }
    else
    {
        _snwprintf(mapping, len, L"%ls", OTDWT_MAPPING_NAME);
        _snwprintf(signal, len, L"%ls", OTDWT_EVENT_NAME);
    }
    mapping[len - 1] = signal[len - 1] = 0;
}

BOOL ring_create(struct ring_handle *h)
{
    WCHAR mapping[128], signal[128];

    ring_names(mapping, signal, ARRAYSIZE(mapping));
    memset(h, 0, sizeof(*h));
    h->mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                    sizeof(otdwt_ring), mapping);
    if (!h->mapping) return FALSE;
    h->ring = MapViewOfFile(h->mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(otdwt_ring));
    h->signal = CreateEventW(NULL, FALSE, FALSE, signal);
    if (!h->ring || !h->signal)
    {
        ring_close(h);
        return FALSE;
    }
    h->ring->magic = OTDWT_MAGIC;
    h->ring->version = OTDWT_VERSION;
    h->ring->ring_size = OTDWT_RING_SIZE;
    h->ring->event_size = sizeof(otdwt_event);
    h->ring->producer_pid = GetCurrentProcessId();
    return TRUE;
}

void ring_close(struct ring_handle *h)
{
    if (h->ring) UnmapViewOfFile((void *)h->ring);
    if (h->mapping) CloseHandle(h->mapping);
    if (h->signal) CloseHandle(h->signal);
    memset(h, 0, sizeof(*h));
}

void ring_publish(struct ring_handle *h, const otdwt_event *ev)
{
    otdwt_ring *r = h->ring;
    uint64_t idx = r->write_index;

    r->ring[idx & (OTDWT_RING_SIZE - 1)] = *ev;
    __atomic_store_n(&r->write_index, idx + 1, __ATOMIC_RELEASE);
    SetEvent(h->signal);
}
