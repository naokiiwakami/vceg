# vceg
Voltage Controlled Envelope Generator

This is a prototype of voltage controlled envelope generator that is useful for driving analog synthesizers.

### Platform

The implementation is targetting AVR ATMega328 micro processor.  Below is the schematics.  Input ports A, D, S, and R accepts voltages ranging 0V through 5V.  Output is also ranges from 0V to 5V.

![alt tag](vc_trial2_schematic.png)

### Design
The program is written in C.  Envelope generator curve is calculated from a lookup table that simulates decreasing exponential curve with 256 16-bit values.

Phase and frequency correct 10-bit PWM is used for generating analog output.  I’ve tried all types of PWM that AVR supports, that are fast PWM, phase correct PWM, and phase and frequency correct PWM.  Phase and frequency correct PWM gave the best sound quality.  PWM threshold that determines output level is updated by every PWM cycle, triggered by the Timer overflow interrupt handler.

ADC is invoked every 10msec approximately.  Since the micro processor is only capable of reading one ADC pin at a time, ADC pins are switched and voltages at A, D, S, and R inputs are read in round robin manner.

The voltages at A, D, and R inputs are mapped to transient times exponentially for natural tweak feeling.  The exponential table for envelope curve is reused for this mapping here.
