/*
 * Board interface for the Minimus AVR (32) USB
 * Copyright (c) 2010 Nate Lawson <nate@root.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

//
// See https://jamie.lentin.co.uk/embedded/minimus/
//
// The board comes with most pins on the chip broken out along either side, at a standard 0.1" pitch.
// The labelling on the boards can be hard to read, and there is no labelling as to how the LEDs and switch are wired.
// To the chip the board looks like this:
//
// -----------------------------------------------------
// |   VCC PC4 PC5 RST PC6 PC7 PB7 PB6 PB5 PB4 PB3 PB2 |
// |                                                   |
// U                                              PWR  |
// S P                                                 |
// B O   [ ] 5V                 RST - RST     A - PD6  |
// | R   [ ]                                           |
// | T   [ ] 3.3V               HWB - PD7     B - PD5  |
// |                                                   |
// |   PC2 PD0 PD1 PD2 PD3 PD4 PD5 PD6 PD7 PB0 PB1 GND |
// -----------------------------------------------------
// i.e. PD5 & PD6 are also the on board LEDS, PD7 is the HWB button.
//
// By default it uses 5v I/O (Including VCC), but there's a built-in 50mA regulator for 3.3v I/O.
// To make the switch...
// 1. Cut bridge between 5v pad and centre pad
// 2. Solder bridge between 3.3v pad and centre pad
// To use an external 5v power source, chop the USB connector pin and apply power directly to VCC.
// To use an external 3.3v power source, also bridge all 3 power configuration pads and disable the on-board regulator:
//
// REGCR |= (1 << REGDIS);  // Disable regulator
//

#ifndef _BOARD_MINIMUS_H
#define _BOARD_MINIMUS_H

// Initialize the board (timer, indicators, UART)
void board_init(void);
// Initialize the IO ports for IEC mode
void board_init_iec(void);

// pinout is: PIN / PIN NAME ON BOARD

#define IO_DATA         _BV(2) // PB2 / 16
#define IO_CLK          _BV(3) // PB3 / 14
#define IO_ATN          _BV(4) // PB4 / 8
#define IO_SRQ          _BV(5) // PB5 / 9
#define IO_RESET        _BV(6) // PB6 / 10

#define IEC_PORT		PORTB
#define IEC_DDR			DDRB
#define IEC_PIN			PINB

#define LED_MASK        _BV(5) // PD5 / GREEN ONBOARD LED
#define LED_PORT        PORTD
#define LED_DDR         DDRD
#define LED_PIN         PIND // for LED toggle

// IEC and parallel port accessors
// Pins: 3, 2, RXI, TXO, A3, A2, A1, A0 make a Parallel Port
// in that order, coressponding to D0 - D7

#define PAR_PORT0_MASK   0xf0   /* port C pins 4-7 */
#define PAR_PORT0_DDR    DDRC
#define PAR_PORT0_PIN    PINC
#define PAR_PORT0_PORT   PORTC

#define PAR_PORT1_MASK   0x0f   /* port D pins 0-3 */
#define PAR_PORT1_DDR    DDRD
#define PAR_PORT1_PIN    PIND
#define PAR_PORT1_PORT   PORTD

#define SRQ_NIB_SUPPORT 1

/*
 * Use always_inline to override gcc's -Os option. Since we measured each
 * inline function's disassembly and verified the size decrease, we are
 * certain when we specify inline that we really want it.
 */
#define INLINE          static inline __attribute__((always_inline))

/*
 * Routines for getting/setting individual IEC lines and parallel port.
 *
 * We no longer add a short delay after changing line(s) state, even though
 * it takes about 0.5 us for the line to stabilize (measured with scope).
 * This is because we need to toggle SRQ quickly to send data to the 1571
 * and the delay was breaking our deadline.
 *
 * These are all inlines and this was incrementally measured that each
 * decreases the firmware size. Some (set/get) compile into a single
 * instruction (say, sbis). This works because the "line" argument is
 * almost always a constant.
 */

INLINE uint8_t
iec_get(uint8_t line)
{
    return (IEC_PIN & line) == 0;
}

INLINE void
iec_set(uint8_t line)
{
    IEC_DDR |= line;
}

INLINE void
iec_release(uint8_t line)
{
    IEC_DDR &= ~line;
}

INLINE void
iec_set_release(uint8_t s, uint8_t r)
{
    iec_set(s);
    iec_release(r);
}

// Make 8-bit port all inputs and read parallel value
INLINE uint8_t
iec_pp_read(void)
{
    uint8_t retval;

  /* make port(s) input */
  PAR_PORT0_DDR &= ~PAR_PORT0_MASK;
  PAR_PORT1_DDR &= ~PAR_PORT1_MASK;

  /* disable pullups */
  PAR_PORT0_PORT &= ~PAR_PORT0_MASK;
  PAR_PORT1_PORT &= ~PAR_PORT1_MASK;

  /* and read value */
  retval  = PAR_PORT0_PIN & PAR_PORT0_MASK;
  retval |= PAR_PORT1_PIN & PAR_PORT1_MASK;

  return retval;
}

// Make 8-bits of port output and write out the parallel data
INLINE void
iec_pp_write(uint8_t val)
{
  /* make ports output */
  PAR_PORT0_DDR |= PAR_PORT0_MASK;
  PAR_PORT1_DDR |= PAR_PORT1_MASK;

  /* mask pins */
  PAR_PORT0_PORT &= ~PAR_PORT0_MASK;
  PAR_PORT1_PORT &= ~PAR_PORT1_MASK;

  /* and put data bits on port */
  PAR_PORT0_PORT |= val & PAR_PORT0_MASK;
  PAR_PORT1_PORT |= val & PAR_PORT1_MASK;
}

INLINE uint8_t
iec_srq_read(void)
{
    uint8_t i, data;

    data = 0;
    for (i = 8; i != 0; --i) {
        // Wait for the drive to pull IO_SRQ.
        while (!iec_get(IO_SRQ))
            ;

        // Wait for drive to release SRQ, then delay another 375 ns for DATA
        // to stabilize before reading it.
        while (iec_get(IO_SRQ))
            ;
        DELAY_US(0.375);

        // Read data bit
        data = (data << 1) | (iec_get(IO_DATA) ? 0 : 1);
   }

   return data;
}

/*
 * Write out a byte by sending each bit on the DATA line (inverted) and
 * clocking the CIA with SRQ. We don't want clock jitter so the body of
 * the loop must not have any branches. At 500 Kbit/sec, each loop iteration
 * should take 2 us or 32 clocks per bit at 16 MHz.
 */
INLINE void
iec_srq_write(uint8_t data)
{
    uint8_t i;
    uint8_t port_base_data = (IEC_DDR & IO_ATN) | IO_SRQ;

    for (i = 8; i != 0; --i) {
        /*
         * Take the high bit of the data byte. Shift it down to the IO_DATA
         * pin for the board. Combine it (inverted) with the IO_SRQ line
         * being set. Write both of these to the port at the same time.
         *
         * This is 8 clock cycles with gcc 9.1.0 at both -Os and -O2.
         */
        IEC_DDR = (((data >> 5) & IO_DATA) ^ IO_DATA) | port_base_data;

        data <<= 1;          // get next bit: 1 clock
        DELAY_US(0.3);       // (nibtools relies on this timing, do not change)
        iec_release(IO_SRQ); // release SRQ: 2 clocks
        DELAY_US(0.80);      // (nibtools relies on this timing, do not change)

        // Decrement i and loop: 3 clock cycles when branch taken
        // Total: 13 clocks per loop (minus delays); 19 clocks left.
    }
}

// Since this is called with a runtime-specified mask, inlining doesn't help.
uint8_t iec_poll_pins(void);

// Status indicators (LEDs)
void board_update_display(uint8_t status);
bool board_timer_fired(void);

#endif // _BOARD_MINIMUS_H
