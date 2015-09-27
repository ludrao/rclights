/* 
 * File:   main.c
 * Author: cuero
 *
 * Created on 19 janvier 2014, 18:36
 */

// PIC12F1840 Configuration Bit Settings

// 'C' source line config statements

// Crap to remove warning in the IDE...
//#ifndef __PICCPRO__
//#define __PICCPRO__
//#endif
//#ifndef EEPROM_SIZE
//#define EEPROM_SIZE
//#endif
// End of Crap

#define _XTAL_FREQ 16000000 // used for __delay_ms macro

#include <xc.h>


// #pragma config statements should precede project file includes.
// Use project enums instead of #define for ON and OFF.

// CONFIG1
#pragma config FOSC = INTOSC    // Oscillator Selection (INTOSC oscillator: I/O function on CLKIN pin)
#pragma config WDTE = OFF       // Watchdog Timer Enable (WDT enabled)
#pragma config PWRTE = OFF      // Power-up Timer Enable (PWRT disabled)
#pragma config MCLRE = OFF      // MCLR Pin Function Select (MCLR/VPP pin function is Digital Input)
#pragma config CP = OFF         // Flash Program Memory Code Protection (Program memory code protection is disabled)
#pragma config CPD = OFF        // Data Memory Code Protection (Data memory code protection is disabled)
#pragma config BOREN = ON       // Brown-out Reset Enable (Brown-out Reset enabled)
#pragma config CLKOUTEN = OFF   // Clock Out Enable (CLKOUT function is disabled. I/O or oscillator function on the CLKOUT pin)
#pragma config IESO = OFF       // Internal/External Switchover (Internal/External Switchover mode is disabled)
#pragma config FCMEN = ON       // Fail-Safe Clock Monitor Enable (Fail-Safe Clock Monitor is enabled)

// CONFIG2
#pragma config WRT = OFF        // Flash Memory Self-Write Protection (Write protection off)
#pragma config PLLEN = OFF      // PLL Enable (4x PLL disabled)
#pragma config STVREN = ON      // Stack Overflow/Underflow Reset Enable (Stack Overflow or Underflow will cause a Reset)
#pragma config BORV = LO        // Brown-out Reset Voltage Selection (Brown-out Reset Voltage (Vbor), low trip point selected.)
#pragma config LVP = OFF        // Low-Voltage Programming Enable (Low-voltage programming disabled)




// EEPROM layout:
// 0 : neutral position 
__EEPROM_DATA(125, 0, 0, 0, 0, 0, 0, 0); // Define EEPROM initial data. Here Only the First value is used

unsigned char neutral;

void init()
{

    // Configure Oscillator
    // Set the system clock to 16MHz
    OSCCONbits.IRCF = 0xF;


    // Configure Ports (IO)
    LATA = 0;   // Set IO to 0
    ANSELA = 0; // no alog inputs

    // RA1 is the 3rd channel RC signal (Light On/Off control) (with weak pull up)
    // RA2 is the motor RC signal (input) (with Weak Pull Up)
    // RA3 is the button input (setup button) (with Weak Pull Up)
    // RA4 is front light  (output)
    // RA5 is brake light  (output as PWM)
    // Other IOs as inputs
    TRISA = 0x0F;
    WPUA = 0x0E; // RA1, RA2, RA3 weak pull up enabled
    OPTION_REGbits.nWPUEN = 0; // Enable weak pull ups


    // Setup RA5 PWM
    APFCONbits.CCP1SEL = 1; // RA5 is CCP1 pin (PWM output)
    // PWM frequency is set to 0,9765625 (16MHz/(4*(PR2+1)*16)
    PR2 = 0xFF;
    T2CONbits.T2CKPS = 2; // prescaler of 16
    T2CONbits.TMR2ON = 1; // start timer
    CCP1CONbits.P1M = 0;



    // Set up interupts on Inputs pins (RA1, RA2) (Interupt-On-Change)
    INTCONbits.IOCIE = 1;
    IOCAP = 0x6; // enable rising edge detection on both RA1, RA2
    IOCAN = 0x0; // No falling edge detection for now!

    // Setup Timer 1
    T1CONbits.TMR1CS = 0; // Use FOSC/4 as clock source (=4MHz)
    T1CONbits.T1CKPS = 0; // No prescaler
    T1CONbits.TMR1ON = 1; // Start Timer1 !

    // Initialize the neutral position
    neutral = eeprom_read(0);
}






// Percentage for RA1 and RA2 channels (computed in the ISR) 0 (1ms pulse) to 250 (2ms pulse)
// 0 -> RA1 (CH3)
// 1 -> RA2 (Throttle/Brake)
unsigned char si[2] = {255, 255};

void interrupt isr(void)
{
    // Timestamps to measure pulse width
    static unsigned short RA_ts[2];

    unsigned short ts = TMR1; // store timer1 value as soon as the interuption is processed


    if(INTCONbits.IOCIF)
    {// Interrupt-On-Change Interrupt

        for (int i = 1; i<=2; i++)
        {
            unsigned char m = 1<<i;
            
            if (IOCAF & m)
            {// RAx changed
                if (IOCAP & m)
                {   // It is a rising edge
                    // Store rising edge timestamp
                    RA_ts[i-1] = ts;
                }
                else
                {   // It is a falling edge
                    // Measure elapsed time since last rising edge
                    // Compute RAx_p into a 0-250 range
                    unsigned short pw = ts - RA_ts[i-1];
                    if (pw < 4000) pw = 4000;
                    if (pw > 8000) pw = 8000;
                    si[i-1] = (unsigned char)((pw-4000)/16);
                }
                // Inverse edge detection
                IOCAP ^= m;
                IOCAN ^= m;
            }
        }

        // Reset all input interrupts flags
        IOCAF = 0;
        INTCONbits.IOCIF = 0; // clear the IOC interupt flag
    }
}

void setupmode()
{
    // Rapidly blink Front Light to signify the setup mode
    for (int i = 0 ; i<5 ; i++)
    {
        LATA4 = 1;
        __delay_ms(50);
        LATA4 = 0;
        __delay_ms(50);
    }

    // measure and store the new neutral position
    neutral = si[1];
    eeprom_write(0, neutral);
}



int main(void) {

    init();

    // Blink front / rear leds at startup
    LATA = 0x30;
    __delay_ms(150);
    LATA = 0x0;
    __delay_ms(150);

    // Init done, enable interupts!
    INTCONbits.GIE = 1;

    unsigned short db_count = 0;

    // Wait for engine signal detection
    unsigned char c = 0;
    unsigned char d = 1;
    while (si[1] == 255)
    {
        unsigned char i;
        for(i=0; i!=255; i++)
        {
            if (c<i)
                LATA = 0x10;
            else
                LATA = 0x20;
            __delay_us(3);
        }
        if ((c==0 && d<0) || (c==255 && d>0))
            d=-d;

        c += d;

    }

    // Activate PWM: BackLight at 30% (RA5)
    CCP1CONbits.CCP1M = 0b1100; // Configure as a single output active high PWM
    CCPR1L = 0x40;
    CCP1CONbits.DC1B = 0x0;

    // Switch on front light (RA4) on
    LATA4 = 1;

    // main loop
    while(1)
    {
        unsigned char lighton = 0;
        __delay_ms(1); // no need to loop faster than once evey 1 ms!


        // Enter in setup mode if the button is pressed more than 1 second
        if (!PORTAbits.RA3)
        {

            db_count++;
            if (db_count > 1000)
            {
                setupmode();
                db_count = 0;
            }
        }
        else
            db_count = 0;


        // Switch on the On/Off of the lights
        if (si[0] < 100)
            lighton = 1;
        else if (si[0] > 150)
            lighton = 0;

        // Set RA4 according to the light state
        LATA4 = lighton;


        // Control the rear light
        if (si[1] < neutral-23) // If pulse width is small enough (means braking)
        {
            // Switch on brake light to 100%
            CCPR1L = 0xFF;
            CCP1CONbits.DC1B = 0x3;
        }
        else if (si[1] >= neutral-5)
        {
            // If lights are on => rear lights on
            if (lighton)
            {
                // Switch back/brake light to ~30%
                CCPR1L = 0x40;
                CCP1CONbits.DC1B = 0x0;
            }
            else
            {
                // Switch back/brake light to 0%
                CCPR1L = 0x0;
                CCP1CONbits.DC1B = 0x0;
            }
        }
    }
    
    return 0;
}

