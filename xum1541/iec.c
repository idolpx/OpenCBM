/*
 * xum1541 IEC routines
 * Copyright (c) 2009-2010 Nate Lawson <nate@root.org>
 *
 * Based on: firmware/xu1541.c
 * Copyright: (c) 2007 by Till Harbaum <till@harbaum.org>
 *
 * Imported at revision:
 * Revision 1.16    2009/01/24 14:51:01    strik
 * New version 1.17;
 * Do not return data for XUM1541_READ after an EOI occurred.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include "xum1541.h"

/* specifiers for the lines (must match values from opencbm.h) */
#define IEC_DATA    0x01
#define IEC_CLOCK   0x02
#define IEC_ATN     0x04
#define IEC_RESET   0x08

/* fast conversion between logical and physical mapping */
static const uint8_t iec2hw_table[] PROGMEM = {
    0,
    IO_DATA,
              IO_CLK,
    IO_DATA | IO_CLK,
                       IO_ATN,
    IO_DATA |          IO_ATN,
              IO_CLK | IO_ATN,
    IO_DATA | IO_CLK | IO_ATN,
                                IO_RESET,
    IO_DATA |                   IO_RESET,
              IO_CLK |          IO_RESET,
    IO_DATA | IO_CLK |          IO_RESET,
                       IO_ATN | IO_RESET,
    IO_DATA |          IO_ATN | IO_RESET,
              IO_CLK | IO_ATN | IO_RESET,
    IO_DATA | IO_CLK | IO_ATN | IO_RESET,
};

static uint8_t
iec2hw(uint8_t iec)
{
    return pgm_read_byte(iec2hw_table + iec);
}

// Initialize all IEC lines to idle
void
cbm_init(void)
{
    DEBUGF(DBG_ALL, "init\n");

    iec_release(IO_ATN | IO_CLK | IO_DATA | IO_RESET);
    DELAY_US(100);
}

static uint8_t
check_if_bus_free(void)
{
    // Let go of all lines and wait for the drive to have time to react.
    iec_release(IO_ATN | IO_CLK | IO_DATA | IO_RESET);
    DELAY_US(50);

    // If DATA is held, drive is not yet ready.
    if (iec_get(IO_DATA) != 0)
        return 0;

    /*
     * DATA is free, now make sure it is stable for 50 us. Nate has seen
     * it glitch if DATA is stable for < 38 us before we pull ATN.
     */
    DELAY_US(50);
    if (iec_get(IO_DATA) != 0)
        return 0;

    /*
     * Assert ATN and wait for the drive to have time to react. It typically
     * does so almost immediately.
     */
    iec_set(IO_ATN);
    DELAY_US(100);

    // If DATA is still unset, no drive answered.
    if (iec_get(IO_DATA) == 0) {
        iec_release(IO_ATN);
        return 0;
    }

    // Good, at least one drive reacted. Now, test releasing ATN.
    iec_release(IO_ATN);
    DELAY_US(100);

    /*
     * The drive released DATA, so we're done.
     *
     * Nate noticed on a scope that the drive pulls DATA for 60 us,
     * 150-500 us after releasing it in response to when we release ATN.
     */
    return (iec_get(IO_DATA) == 0) ? 1 : 0;
}

// Wait up to 1.5 secs to see if drive answers ATN toggle.
static void
wait_for_free_bus(void)
{
    uint16_t i;

    for (i = (uint16_t)(XUM1541_TIMEOUT * 10000); i != 0; i--) {
        if (check_if_bus_free())
            return;

        // Bail out early if host signalled an abort.
        DELAY_US(100);
        if (!TimerWorker())
            return;
    }
    DEBUGF(DBG_ERROR, "wait4free bus to\n");
}

void
cbm_reset(void)
{
    DEBUGF(DBG_ALL, "reset\n");
    iec_release(IO_DATA | IO_ATN | IO_CLK);

    /*
     * Hold the device in reset a while. 20 ms was too short and it didn't
     * fully reset (e.g., motor did not run). Nate checked with a scope
     * and his 1541-B grabs DATA exactly 25 ms after RESET goes active.
     * 30 ms seems good. It takes about 1.2 seconds before the drive answers
     * by grabbing DATA.
     *
     * There is a small glitch at 25 ms after grabbing RESET where RESET out
     * goes inactive for 1 us. This corresponds with the drive grabbing CLK
     * and DATA, and for about 40 ns, ATN also. Nate assumes this is
     * crosstalk from the VIAs being setup by the 6502.
     */
    iec_set(IO_RESET);
    DELAY_MS(30);
    iec_release(IO_RESET);

    wait_for_free_bus();
}

// Wait up to 2 ms for any of the masked lines to become active.
static uint8_t
iec_wait_timeout_2ms(uint8_t mask, uint8_t state)
{
    uint8_t count = 200;

    while ((iec_poll() & mask) == state && count-- != 0)
        DELAY_US(10);

    return ((iec_poll() & mask) != state);
}

// Wait up to 400 us for CLK to be pulled by the drive.
static void
iec_wait_clk(void)
{
    uint8_t count = 200;

    while (iec_get(IO_CLK) == 0 && count-- != 0)
        DELAY_US(2);
}

/*
 * Send a byte, one bit at a time via the IEC protocol.
 *
 * The minimum spec setup (Ts) and hold times (Tv) are both 20 us. However,
 * Nate found that the 16 Mhz AT90USB162 was not recognized by his
 * 1541 when using these intervals.
 *
 * It appears the spec is much too optimistic. The typical setup time (Ts)
 * of 70 us is also not quite long enough. Increasing the setup time to
 * 72 us appears to work consistently, but he chose the value 75 us to
 * give more margin. The 1541 consistently takes 120 us for Ts and
 * 70 us for Tv, which is why no one probably noticed this before.
 *
 * The hold time did not appear to have any effect. In fact, reducing the
 * hold time to 15 us still worked fine.
 */
static uint8_t
send_byte(uint8_t b)
{
    uint8_t i, ack = 0;

    for (i = 0; i < 8; i++) {
        // Wait for Ts (setup) with additional padding
        DELAY_US(75);

        // Set the bit value on the DATA line.
        if ((b & 1) == 0)
            iec_set(IO_DATA);

        // Trigger clock edge and hold valid for spec minimum time (Tv).
        iec_release(IO_CLK);
        DELAY_US(20);

        iec_set_release(IO_CLK, IO_DATA);
        b >>= 1;
    }

    /*
     * Wait up to 2 ms for DATA to be driven by device.
     * It takes around 70-80 us on Nate's 1541.
     */
    ack = iec_wait_timeout_2ms(IO_DATA, IO_DATA);
    if (!ack) {
        DEBUGF(DBG_ERROR, "sndbyte nak\n");
    }

    return ack;
}

/*
 * Wait for listener to release DATA line. We wait forever.
 * This is because the listener hold-off time (Th) is allowed to be
 * infinite (e.g., for printers or other slow equipment).
 *
 * Nate's 1541 responds in about 670 us for an OPEN from idle.
 * It responds in about 65 us between bytes of a transaction.
 */
static bool
wait_for_listener(void)
{
    /* release the clock line to indicate that we are ready */
    iec_release(IO_CLK);

    /* wait forever for client to do the same with the DATA line */
    while (iec_get(IO_DATA) != 0) {
        // If we got an abort, bail out of this loop.
        if (!TimerWorker())
            return false;
    }
    return true;
}

/* 
 * Write bytes to the drive via the CBM default protocol.
 * Returns number of successful written bytes or 0 on error.
 */
uint16_t
cbm_raw_write(uint16_t len, uint8_t flags)
{
    uint8_t atn, talk, data;
    uint16_t rv;

    rv = len;
    atn = flags & XUM_WRITE_ATN;
    talk = flags & XUM_WRITE_TALK;
    eoi = 0;

    DEBUGF(DBG_INFO, "cwr %d, atn %d, talk %d\n", len, atn, talk);

    usbInitIo(len, ENDPOINT_DIR_OUT);
    iec_release(IO_DATA);
    iec_set(IO_CLK | (atn ? IO_ATN : 0));

    // Wait for any device to pull data after we set CLK.
    if (!iec_wait_timeout_2ms(IO_DATA, IO_DATA)) {
        DEBUGF(DBG_ERROR, "write: no devs\n");
        iec_release(IO_CLK | IO_ATN);
        usbIoDone();
        return 0;
    }

    while (len && rv) {
        // Wait 50 us before starting.
        DELAY_US(50);

        // Be sure DATA line has been pulled by device
        if (iec_get(IO_DATA)) {
            // Release clock and wait forever for listener to release data
            if (!wait_for_listener()) {
                DEBUGF(DBG_ERROR, "write: w4l abrt\n");
                rv = 0;
                break;
            }

            /*
             * This is timing critical and if we are not sending an eoi
             * the iec_set(CLK) must be reached in less than ~150us.
             */
            if (len == 1 && !atn) {
                /*
                 * Signal eoi by waiting so long (>200us) that listener
                 * pulls DATA, then wait for it to be released.
                 */
                iec_wait_timeout_2ms(IO_DATA, IO_DATA);
                iec_wait_timeout_2ms(IO_DATA, 0);
            }
            iec_set(IO_CLK);

            // Get a data byte from host, quitting if it signalled an abort.
            if (usbRecvByte(&data) != 0) {
                rv = 0;
                break;
            }
            if (send_byte(data)) {
                len--;
                DELAY_US(100);
            } else {
                DEBUGF(DBG_ERROR, "write: io err\n");
                rv = 0;
            }
        } else {
            // Occurs if there is no device addressed by this command
            DEBUGF(DBG_ERROR, "write: dev not pres\n");
            rv = 0;
        }

        wdt_reset();
    }

    usbIoDone();
    if (rv != 0) {
        // If we're asking the device to talk, wait for it to grab CLK.
        if (talk) {
            iec_set(IO_DATA);
            iec_release(IO_CLK | IO_ATN);
            while (iec_get(IO_CLK) == 0) {
                if (!TimerWorker()) {
                    rv = 0;
                    break;
                }
            }
        } else
            iec_release(IO_ATN);

        // Wait 100 us before the next request.
        DELAY_US(100);
    } else {
        // If there was an error, just release all lines before returning.
        iec_release(IO_CLK | IO_ATN);
    }

    DEBUGF(DBG_INFO, "wrv=%d\n", rv);
    return rv;
}

uint16_t
cbm_raw_read(uint16_t len)
{
    uint8_t ok, bit, b;
    uint16_t to, count;

    DEBUGF(DBG_INFO, "crd %d\n", len);
    usbInitIo(len, ENDPOINT_DIR_IN);
    count = 0;
    do {
        to = 0;

        /* wait for clock to be released. typically times out during: */
        /* directory read */
        while (iec_get(IO_CLK)) {
            if (to >= 50000 || !TimerWorker()) {
                /* 1.0 (50000 * 20us) sec timeout */
                DEBUGF(DBG_ERROR, "rd to\n");
                usbIoDone();
                return 0;
            }
            to++;
            DELAY_US(20);
        }

        // XXX is this right? why treat EOI differently here?
        if (eoi) {
            usbIoDone();
            return 0;
        }

        /* release DATA line */
        iec_release(IO_DATA);

        /* use special "timer with wait for clock" */
        iec_wait_clk();

        // Is the talking device signalling EOI?
        if (iec_get(IO_CLK) == 0) {
            eoi = 1;
            iec_set(IO_DATA);
            DELAY_US(70);
            iec_release(IO_DATA);
        }

        /*
         * Disable IRQs to make sure the byte transfer goes uninterrupted.
         * This isn't strictly needed since the only interrupt we use is the
         * one for USB control transfers.
         */
        cli();

        // Wait up to 2 ms for CLK to be asserted
        ok = iec_wait_timeout_2ms(IO_CLK, IO_CLK);

        // Read all 8 bits of a byte
        for (bit = b = 0; bit < 8 && ok; bit++) {
            // Wait up to 2 ms for CLK to be released
            ok = iec_wait_timeout_2ms(IO_CLK, 0);
            if (ok) {
                b >>= 1;
                if (iec_get(IO_DATA) == 0)
                    b |= 0x80;

                // Wait up to 2 ms for CLK to be asserted
                ok = iec_wait_timeout_2ms(IO_CLK, IO_CLK);
            }
        }

        sei();

        if (ok) {
            // Acknowledge byte received ok
            iec_set(IO_DATA);

            // Send the data byte to host, quitting if it signalled an abort.
            if (usbSendByte(b))
                break;
            count++;
            DELAY_US(50);
        }

        wdt_reset();
    } while (count != len && ok && !eoi);

    if (!ok) {
        DEBUGF(DBG_ERROR, "read io err\n");
        count = 0;
    }

    DEBUGF(DBG_INFO, "rv=%d\n", count);
    usbIoDone();
    return count;
}

/* wait forever for a specific line to reach a certain state */
bool
xu1541_wait(uint8_t line, uint8_t state)
{
    uint8_t hw_mask, hw_state;

    /* calculate hw mask and expected state */
    hw_mask = iec2hw(line);
    hw_state = state ? hw_mask : 0;

    while ((iec_poll() & hw_mask) == hw_state) {
        if (!TimerWorker())
            return false;
        DELAY_US(10);
    }

    return true;
}

uint8_t
xu1541_poll(void)
{
    uint8_t iec_state, rv = 0;

    iec_state = iec_poll();
    if ((iec_state & IO_DATA) == 0)
        rv |= IEC_DATA;
    if ((iec_state & IO_CLK) == 0)
        rv |= IEC_CLOCK;
    if ((iec_state & IO_ATN) == 0)
        rv |= IEC_ATN;

    return rv;
}

void
xu1541_setrelease(uint8_t set, uint8_t release)
{
    iec_set_release(iec2hw(set), iec2hw(release));
}
