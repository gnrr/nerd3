/*
 * nerd3.c
 *
 * Created: 2013/05/30 11:00:36
 *  Device: ATMEGA88P
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>
#include "mystd.h"
#include "keycode.h"

/* -------------------------------------------------
 * definitions, globals and consts
 */
u1 HHKScanCode;
u1 ScanBuf[64];                         // 64 keys (0:pressed)

#define RINGBUF_SIZE 32
#include "ringbuf.h"
RINGBUF TXBuf;

/* -------------------------------------------------
 * initializers
 */
static inline void init_devices(void)
{
    /* HHK SENSE */

    /* TIMER1 (16bit) */
    TCCR1B = (0x01<<CS10) | (1<<WGM12); // CLKio/1, CTC(Clear Timer on Compare Match)
                                        // CLKio: 1 MHz = internal RC osc (factory default)
    TIMSK1 = 1<<OCIE1A;                 // Compare A match interrupt
    OCR1A  = 200;                       // bit0 is inverted 200us cycled (T=400us)
    
    /* PORTB */
    DDRB  = 0x7F;                       // b7:    IN   #PRESS
                                        // b6:    OUT  #OE
                                        // b5-b0: OUT  bit5-bit0
    PORTB = 0x00;


    /* Bluetooth */

    /* UART0: async, 9600, data=8, stop=1, pari=none */
    UBRR0  = 12;                                       // 1 MHz = internal RC osc (factory default)
    UCSR0A = (1<<U2X0);
    UCSR0B = (1<<RXEN0) | (1<<TXEN0);                  /* enable receiver/transmitter */
}

static inline void init_rams(void)
{
    HHKScanCode = 0;

    for(u1 i=0; i<sizeof(ScanBuf); i++)
        ScanBuf[i] = 0x80;
}

/* -------------------------------------------------
 * sub functions
 */
static inline void uart_send(u1 ch)
{
  while( !(UCSR0A & (1<<UDRE0)) ) ;        /* wait for empty transmit buffer */

  UDR0 = ch;
}

#if 0
static inline void send_hid_release_all(void)
{
                /* 0     1     2     3     4     5     6     7     8     9     10   */
                /* -     -     -    mod    -    scan1 scan2 scan3 scan4 scan5 scan6 */
    u1 rep[11] = {0xFD, 0x09, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    for(u1 i=0; i<11; i++)
        uart_send(rep[i]);
}
#endif

static inline void send_hid_report(void)
{
                /* 0     1     2     3     4     5     6     7     8     9     10   */
                /* -     -     -    mod    -    scan1 scan2 scan3 scan4 scan5 scan6 */
    u1 rep[11] = {0xFD, 0x09, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    u1 *pmod  = &rep[3];
    u1 *pscan = &rep[5];
        
    if(TXBuf.unread_count == 0) return; // do nothing

    while(TXBuf.unread_count > 0) {
        u1 c, m;
        ringbuf_read(&TXBuf, &c);
        m = modifier_bit(c);
        if(m == 0x00) {
            // common keys
            *pscan++ = HHK2HID_TBL[c];
            if(pscan > &rep[10]) break;
        } else {
            // modifier keys
            *pmod |= m;
        }
    }

    for(u1 i=0; i<11; i++)
        uart_send(rep[i]);              // send report 'pressed' something

    if(TXBuf.unread_count == 0)         // prepare sending report 'release all' next call
        ringbuf_write(&TXBuf, 0x3F);    // 0x3F --> USID_NONE
}

static inline void test_send(u1 hhk_code)
{
                /* 0     1     2     3     4     5     6     7     8     9     10   */
                /* -     -     -    mod    -    scan1 scan2 scan3 scan4 scan5 scan6 */
    u1 rep[11] = {0xFD, 0x09, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    //u1 hhk_code = 0x05; // z

    rep[5] = HHK2HID_TBL[hhk_code];
    
    for(u1 i=0; i<11; i++)
        uart_send(rep[i]);
}

static inline void test_send2(void)
{
                /* 0     1     2     3     4     5     6     7     8     9     10   */
                /* -     -     -    mod    -    scan1 scan2 scan3 scan4 scan5 scan6 */
    u1 rep[11] = {0xFD, 0x09, 0x01, 0x00, 0x00, 0x04, 0x07, 0x09, 0x00, 0x00, 0x00};

    for(u1 i=0; i<11; i++)
        uart_send(rep[i]);
}

/* -------------------------------------------------
 * main
 */
int main(void)
{
    init_devices();
    init_rams();
    __ei();

#if 0
    test_send(0x05); // z
    test_send(0x23); // i
    test_send(0x0D); // f
    test_send(0x06); // x
    test_send(0x3F); // none

    test_send2();    // adf
    test_send(0x3F); // none --> zifxadf
#endif

    ringbuf_write(&TXBuf, 0x06); // x --> 0x1B
    ringbuf_write(&TXBuf, 0x23); // i --> 0x0C
    ringbuf_write(&TXBuf, 0x0E); // v --> 0x19
    send_hid_report();  // ivx
    send_hid_report();  // released all


    while(1)
    {
        //TODO:: Please write your application code
//        send_hid_report(); 
    }

    return EXIT_SUCCESS;
}

/* -------------------------------------------------
 * interrupt service routines
 */
ISR(TIMER1_COMPA_vect)
{
    /* HHK key sense */
    u1 sw;

    __di();

    HHKScanCode = (HHKScanCode + 1) & 0x3F;
    PORTB = HHKScanCode | 0x40;         // #OE(PB6) --> HI
    PORTB &= 0x3F;                      // #OE(PB6) --> LO
    sw = PINB & 0x80;                   // #PRESS==LO�Ȃ瓖�Y�L�[��������Ă���, HI�Ȃ烊���[�X����Ă���
    PORTB |= 0x40;                      // #OE(PC6) --> HI

    if(sw != ScanBuf[HHKScanCode]) {
        ScanBuf[HHKScanCode] = sw;      // update
        if(sw == 0) 
            //uart_send(hhk2ascii_tbl[HHKScanCode]);
            ringbuf_write(&TXBuf, HHKScanCode);     // press --> send
    }

    __ei();
}

/* this program ends here */
