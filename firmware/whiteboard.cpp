#include <stdint.h>
#include <stdio.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include "parser.h"
#include "serial.h" // for printf
#include "motor.h"
#include "motors.h"

// Serial port settings
#define SERIAL_BAUD 9600
// #define SERIAL_U2X

// Serial port calculations
#if defined(SERIAL_U2X)
#define SERIAL_UBRR_VAL ((F_CPU / 8 / SERIAL_BAUD) - 1)
#else
#define SERIAL_UBRR_VAL ((F_CPU / 16 / SERIAL_BAUD) - 1)
#endif

#define SERVO_TOP (F_CPU / 50 / 64) // 50=servo signal, 64=prescaling
// How far from the centre to move the servo
#define SERVO_MOVEMENT (SERVO_TOP / 4)

namespace Events {
    const uint8_t SERIAL_RX = 1;
    const uint8_t MOTORL = 2;
    const uint8_t MOTORR = 4;
    char serial_rx;
}
#define events (GPIOR0)

Parser parser;

Motor motors[] = {
    Motor(&OCR0A, &OCR0B, &PINB, _BV(PORTB0)),
    Motor(&OCR2B, &OCR2A, &PINC, _BV(PORTC5))
};
Motors motor_manager(motors, &OCR1A,
        SERVO_TOP / 2 + SERVO_MOVEMENT, SERVO_TOP / 2 - SERVO_MOVEMENT);

static void handle_input() {
    if (parser.handle(Events::serial_rx)) {
        if (parser.command == Parser::DRAW || parser.command == Parser::MOVE) {
//            motor_manager.set_drawing(parser.command == Parser::DRAW);
            motor_manager.move(parser.args);
        }
    }
    events &= ~Events::SERIAL_RX;
}

static void check_motors_stopped() {
    if (motor_manager.motors[0].driver.get_speed() == 0
            && motor_manager.motors[1].driver.get_speed() == 0)
        putchar('1');
}

ISR(PCINT0_vect) {
    events |= Events::MOTORL;
}

ISR(PCINT1_vect) {
    events |= Events::MOTORR;
}

ISR(USART_RX_vect) {
    events |= Events::SERIAL_RX;
    Events::serial_rx = UDR0;
}

void init_serial() {
    // Init serial
    UBRR0H = (uint8_t)(SERIAL_UBRR_VAL >> 8);
    UBRR0L = (uint8_t)(SERIAL_UBRR_VAL);
    // Asynchronous, no parity, 1 stop bit, 8 data bits
    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);
    // Enable TX and RX, interrupts on RX
    UCSR0B = _BV(TXEN0) | _BV(RXEN0) | _BV(RXCIE0);
    UCSR0A = 0
#if defined(SERIAL_U2X)
      | _BV(U2X0)
#endif
    ;
}

void init_timers() {
    TCCR0A =
        // Set output pins on compare match, clear on overflow
        _BV(COM0A1) | _BV(COM0A0)
        | _BV(COM0B1) | _BV(COM0B0)
        // Fast PWM mode, between 0 and 0xFF
        | _BV(WGM01) | _BV(WGM00);
    TCCR0B =
        // 8x prescaling
        _BV(CS01);

    // Repeat for timer 2
    TCCR2A =
        // Set timers on compare match, clear on overflow
        _BV(COM2A1) | _BV(COM2A0)
        | _BV(COM2B1) | _BV(COM2B0)
        // Fast PWM mode, between 0 and 0xFF
        | _BV(WGM21) | _BV(WGM20);
    TCCR2B =
        // 8x prescaling
        _BV(CS21);

    // Use timer 1 for the servo
    ICR1 = SERVO_TOP;
    OCR1A = SERVO_TOP / 2; // set to midpoint
    TCCR1A =
            // Output on OC1A (PB1). Clear on compare match, set on overflow.
            _BV(COM1A1)
            // Fast PWM, top=ICR1
            | _BV(WGM11);
    TCCR1B = _BV(WGM13) | _BV(WGM12)
            // 64x prescaling (although 8x should do)
            | _BV(CS11) | _BV(CS10);
}

static void init_encoders() {
    DDRB &= ~_BV(PORTB0);
    DDRC &= ~_BV(PORTC5);

    // Enable interrupts
    PCMSK0 = _BV(PCINT0); // pin B0
    PCMSK1 = _BV(PCINT13); // pin C5
    PCICR = _BV(PCIE1) | _BV(PCIE0);
}

int main(void) {
    init_serial();
    serial_init();
    init_timers();
    init_encoders();

    DDRB |= _BV(PORTB5)
    // Set the servo output
          | _BV(PORTB1)
    // and the motors
          | _BV(PORTB3);
    DDRD |= _BV(PORTD3) | _BV(PORTD5) | _BV(PORTD6);

    sei();

    while(true) {
        while (events) {
            if (events & Events::MOTORL) {
                motor_manager.check(0);
                check_motors_stopped();
                events &= ~Events::MOTORL;
            } else if (events & Events::MOTORR) {
                motor_manager.check(1);
                check_motors_stopped();
                events &= ~Events::MOTORR;
            } else if (events & Events::SERIAL_RX)
                handle_input();
        }
        sleep_mode();
    }
}
