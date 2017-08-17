# vceg
Voltage Controlled Envelope Generator

This is a prototype of voltage controlled envelope generator that is useful for controlling analog synthesizers.

### Platform

The implementation is done onto AVR ATMega328 micro processor. The schematic is shown as below. Input ports A, D, S, and R accepts voltages ranging 0V through 5V.  Output range is also from 0V to 5V.

![alt tag](vc_trial2_schematic.png)

### Design
The program is written in C.  The output levels changes exponentially. They are calculated using a lookup table that samples a decreasing exponential function at 256 points by 16-bit integers.

Phase and frequency correct 10-bit PWM is used for generating analog output. I’ve tried all types of PWM supported by the processor, which are fast PWM, phase correct PWM, and phase and frequency correct PWM.  Phase and frequency correct PWM gave the best sound quality. PWM threshold that determines output level is updated at the end of every PWM cycle, triggered by the Timer overflow interrupt.

AD conversion is invoked about every 10 milliseconds in order to read control voltages. Control Voltage parameters are:

- A: Attack Time
- D: ecay Time
- S: Sustain Level
- R: Release Time

The processor iterates four ADC input pins for A, D, S, and R, since it is capable of reading only one ADC pin at a time.

The voltages at A, D, and R pins are converted to time values exponentially. It gives natual feeling in modifying a time value.  The exponential table for envelope curve is reused for the conversion.
