/*
 * Board interface routines for the Minimus AVR (32) USB
 * Copyright (c) 2010 Nate Lawson <nate@root.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include "xum1541.h"

#ifdef DEBUG
// Send a byte to the UART for debugging printf()
static int
uart_putchar(char c, FILE *stream)
{
    if (c == '\n')
        uart_putchar('\r', stream);
    loop_until_bit_is_set(UCSR1A, UDRE1);
    UDR1 = c;
    return 0;
}
static FILE mystdout = FDEV_SETUP_STREAM(uart_putchar, NULL, _FDEV_SETUP_WRITE);
#endif // DEBUG

// Initialize the board (timer, indicator LED, UART)
void
board_init(void)
{
    // Initialize just the IO pin for LED at this point
    LED_DDR  = LED_MASK;
    LED_PORT = LED_MASK;

#ifdef DEBUG
#define BAUD 115200
#include <util/setbaud.h>
    /*
     * Initialize the UART baud rate at 115200 8N1 and select it for
     * printf() output.
     */
#if USE_2X
    UCSR1A |= _BV(U2X1);
#else
    UCSR1A &= ~(_BV(U2X1));
#endif
    UCSR1B |= _BV(TXEN1);
    UBRR1 = UBRR_VALUE;
    stdout = &mystdout;
#endif

    // Setup 16 bit timer as normal counter with prescaler F_CPU/1024.
    // We use this to create a repeating 100 ms (10 hz) clock.
    OCR1A = (F_CPU / 1024) / 10;
    TCCR1B |= (1 << WGM12) | (1 << CS02) | (1 << CS00);
}

// Initialize the board IO ports for IEC mode
// This function has to work even if the ports were left in an indeterminate
// state by a prior initialization (e.g., auto-probe for IEEE devices).
void
board_init_iec(void)
{
    // IO port initialization
    IEC_DDR  = 0xFF;
    IEC_PORT = 0xFF;

    /* make port(s) input */
    PAR_PORT0_DDR &= ~PAR_PORT0_MASK;
    PAR_PORT1_DDR &= ~PAR_PORT1_MASK;

    /* disable pullups */
    PAR_PORT0_PORT &= ~PAR_PORT0_MASK;
    PAR_PORT1_PORT &= ~PAR_PORT1_MASK;
}

uint8_t
iec_poll_pins(void)
{
    return IEC_PIN & (IO_DATA | IO_CLK | IO_ATN | IO_SRQ | IO_RESET);
}

/*
 * Callback for when the timer fires.
 * Update LEDs or do other tasks that should be done about every ~100 ms
 */
void
board_update_display(uint8_t status)
{
    switch (status) {
    case STATUS_INIT:
        LED_PORT |= LED_MASK;
        break;
    case STATUS_READY:
        // Turn off LED
        LED_PORT &= ~LED_MASK;
        break;
    case STATUS_ACTIVE:
    case STATUS_ERROR:
        // Toggle LED
        LED_PIN |= LED_MASK;
        break;
    default:
        DEBUGF(DBG_ERROR, "badstsval %d\n", status);
        break;
    }
}

/*
 * Signal that the board_update_display() should be called if the timer
 * has fired (every ~100 ms).
 */
bool
board_timer_fired()
{
    // If timer fired, clear overflow bit and notify caller.
    if ((TIFR1 & (1 << OCF1A)) != 0) {
        TIFR1 |= (1 << OCF1A);
        return true;
    } else
        return false;
}
