/*
 * Copyright 2015 Naoki Iwakami
 */
#include <avr/io.h>
#include <avr/interrupt.h>

uint16_t DecayCurveLookup[256];
uint16_t get_curve( uint16_t phase );

//////////////////////////////////////////////////
// Timer1 OVF ISR and main program synchtonization
//
volatile uint8_t g_sync_flags; // 8 bit flags used for main - ISR synchronization

#define SYNC_UPDATE_VALUE      0x1  //  _BV(0)
#define SYNC_UPDATE_VALUE_DONE 0xFE // ~_BV(0)

#define SYNC_ADC_INVOKE        0x2  //  _BV(1)
#define SYNC_ADC_STARTED       0x4  //  _BV(2)
#define SYNC_ADC_DONE          0xF9 // ~(_BV(1) | _BV(2))

//////////////////////////////////////////////////
// Envelope generator states
//
volatile int8_t g_gate;  // gate on/off

volatile uint8_t g_state;

#define STATE_IDLE      0 // Keeping current value.  Used when we finish releasing.
#define STATE_ATTACK    1
#define STATE_RELEASE   2 // Used for Decay and Release

volatile uint16_t g_phase; // Current transient phase.  Used for exp table lookup
volatile uint16_t g_value; // Current output value. Range is from 0 to 1023 (since PWM is 10 bit)

volatile uint16_t g_target_level;
volatile uint16_t g_decay_scale;
volatile uint16_t g_phase_granule;

#define ATTACK_THRESHOLD 1023

//////////////////////////////////////////////////
// ADSR parameters
volatile uint16_t g_attack;
volatile uint16_t g_decay;
volatile uint16_t g_sustain;
volatile uint16_t g_release;

//////////////////////////////////////////////////
// ADC
//

// ADC channel mapping. these are 4-bit integers that are passed to ADMUX register
#define ADC_CH_ATTACK  0x03
#define ADC_CH_DECAY   0x02
#define ADC_CH_SUSTAIN 0x01
#define ADC_CH_RELEASE 0x00

#define ADC_NUM_CHANNELS 4

volatile uint8_t g_adc_current_channel;

// ADC_INTERVAL determines frequency of ADC.
// ADC is triggered by Timer1 OVF interrupt handler by every ADC_INTERVAL call.
// 20HMz clock, 10bit p & f correct PWM, times 100 gives approx. 10ms interval
#define ADC_INTERVAL 100
volatile uint8_t g_adc_timing_counter;

/**
 * The TIMER1 (PWM) interrupt handler
 */
ISR(TIMER1_OVF_vect)
{
    OCR1A = g_value;

    g_sync_flags |= SYNC_UPDATE_VALUE;

    if (--g_adc_timing_counter == 0) {
        g_sync_flags |= SYNC_ADC_INVOKE;
        g_adc_timing_counter = ADC_INTERVAL;
    }
}

//////////////////////////////////////////////////
// setup routines
//
void setup_io()
{
    // I/O
    DDRB = _BV(1) | _BV(2);
    PORTD = _BV(4); // pull up gate switch

}

void setup_timer()
{
    // Timer1, Phase and Frequency correct PWM, no prescale, OC1A inverting move.
    TCCR1A = _BV(COM1A1);
    TCCR1B = _BV(WGM13) | _BV(CS10);
    OCR1A = 0; // set output zero
    TIMSK1 = _BV(TOIE1); // Timer/Counter1, Overflow Interrupt Enabled
    ICR1 = 0x03FF; // 10-bit PWM

}

void setup_adc()
{
   /** Setup and enable ADC **/
   ADMUX = (0<<REFS1)    // Reference Selection Bits
         | (1<<REFS0)    // AVcc - external cap at AREF
         // | (0<<ADLAR)    // ADC Left Adjust Result
         // | (0<<MUX2)     // Analog Channel Selection Bits
         // | (1<<MUX1)     // ADC2 (PC2 PIN25)
         // | (0<<MUX0)
		 ;
    
   ADCSRA = (1<<ADEN)|    // ADC ENable
            //(0<<ADSC)|     // ADC Start Conversion
            //(0<<ADATE)|    // ADC Auto Trigger Enable
            //(0<<ADIF)|     // ADC Interrupt Flag
            //(0<<ADIE)|     // ADC Interrupt Enable
            (1<<ADPS2)|    // ADC Prescaler Select Bits.  '111' is 128 that gives approx. 156kHz for 20MHz clock
            (0<<ADPS1)|
            (1<<ADPS0);

    // ADC channel switcher.
    // ADC is invoked every 10ms approximately with dogin round robin over ADC channels.
    g_adc_current_channel = 0;
}

void setup_misc()
{
    g_sync_flags = 0;
    g_gate = 0;

    g_state = STATE_IDLE;

    g_phase = 0;
    g_value = 0;

    g_phase_granule = 1;
    g_target_level = 0;

    g_attack = 0;
    g_decay = 0;
    g_sustain = 0;
    g_release = 0;

    g_adc_timing_counter = ADC_INTERVAL;
}

void setup()
{
    setup_io();
    setup_timer();
    setup_adc();
    setup_misc();
}

void update_value()
{
    if (g_gate > 1) {
        --g_gate;
    }
    else if (g_gate < 0) {
        ++g_gate;
    }

    // check gate first
    //
    if (g_gate == 0 || g_gate == 1) // chattering canceling.
                                    // g_gate is set 100 initially and TIMER1_OVF handler decrements to 1.
                                    // When the g_gate gets to 1, we are ready for checking gate again.
    {
        if ((PIND & _BV(PIND4)) == 0) {
            if (g_gate == 0) {
                // gate on, start ATTACK stage
                g_gate = 100;
                g_target_level = 1200;
                g_decay_scale = g_target_level;
                g_phase = 0;
                g_phase_granule = DecayCurveLookup[g_attack / 4] / 256;
                if (g_phase_granule == 0) {
                    g_phase_granule = 1;
                }
                g_state = STATE_ATTACK;
            }
        }
        else {
            if (g_gate != 0) {
                // gate off
                if (g_value == 0) {
                    g_phase = 0;
                    g_state = STATE_IDLE;
                }
                else {
                    // Move on to RELEASE stage
                    g_decay_scale = g_value;
                    g_phase = 0;
                    g_state = STATE_RELEASE;
                    g_phase_granule = DecayCurveLookup[g_release / 4] / 256;
                    if (g_phase_granule == 0) {
                        g_phase_granule = 1;
                    }
                    g_target_level = 0;
                }
                g_gate = 0;
            }
        }
    }
 
    // action
    //
    if (g_state == STATE_ATTACK) {
        float value = 65535 - get_curve(g_phase);
        value /= 65536;
        value *= g_decay_scale;
        if (value >= ATTACK_THRESHOLD) {
            // move on to DECAY stage 
            value = ATTACK_THRESHOLD;
            g_target_level = g_sustain;
            g_decay_scale = (1023 - g_target_level);
            g_phase = 0;
            g_phase_granule = DecayCurveLookup[g_decay / 4] / 256;
            if (g_phase_granule == 0) {
                g_phase_granule = 1;
            }
            g_state = STATE_RELEASE;
        }
        else {
            g_phase += g_phase_granule;
        }
        g_value = value;
    }
    else if (g_state == STATE_RELEASE) {
        float value = get_curve(g_phase);
        value /= 65536;
        value *= g_decay_scale;
        g_value = value + g_target_level;
        if (65535 - g_phase <= g_phase_granule) {
            g_phase = 0;
            g_state = STATE_IDLE;
        }
        else {
            g_phase += g_phase_granule; // TODO: calculate granule
        }
    }
}

void adc_invoke()
{
	ADMUX &= 0xF0; // clear ADC channels
    ADMUX |= g_adc_current_channel; // set the channel

    ADCSRA |= _BV(ADSC);

}

int16_t adc_read()
{
    if ( ADCSRA & _BV(ADSC) ) {
        // data is not ready yet
        return -1;
    }

    return ADC;
}

int main(void)
{
    cli();
    setup();
    sei();

    while (1)
    {
        if ( ! g_sync_flags ) {
            continue;
        }

        if ( (g_sync_flags & SYNC_UPDATE_VALUE) ) {
            update_value();
            g_sync_flags &= SYNC_UPDATE_VALUE_DONE;
        } 

        if ( (g_sync_flags & SYNC_ADC_INVOKE) ) {
            if ( (g_sync_flags & SYNC_ADC_STARTED) ) {
                // try reading the value
                int16_t temp = adc_read();
                if (temp >= 0) {
                    switch (g_adc_current_channel) {
                    case ADC_CH_ATTACK:
                        g_attack = temp;
                        break;
                    case ADC_CH_DECAY:
                        g_decay = temp;
                        break;
                    case ADC_CH_SUSTAIN:
                        g_sustain = temp;
                        break;
                    case ADC_CH_RELEASE:
                        g_release = temp;
                        break;
                    }
                    if (++g_adc_current_channel == ADC_NUM_CHANNELS) {
                        g_adc_current_channel = 0;
                    }
                    g_sync_flags &= SYNC_ADC_DONE;
                }
            }
            else {
                adc_invoke();
                g_sync_flags |= SYNC_ADC_STARTED;
            }
        }
    }
}

uint16_t get_curve( uint16_t phase )
{
    uint8_t lookup_key = (phase >> 8);
    uint8_t fine = phase & 0xFF;

    uint16_t prev = DecayCurveLookup[lookup_key];
    uint16_t next = (lookup_key < 255) ? DecayCurveLookup[lookup_key+1] : 0;

    float diff = prev - next;
    diff *= (255 - fine);
    diff /= 256;
    next += diff;


    return next;
}


uint16_t DecayCurveLookup[256] = {
  65535,  63715,  61945,  60224,  58552,  56925,  55344,  53807,
  52312,  50859,  49447,  48073,  46738,  45440,  44178,  42951,
  41758,  40598,  39470,  38374,  37308,  36272,  35264,  34285,
  33332,  32406,  31506,  30631,  29780,  28953,  28149,  27367,
  26607,  25868,  25149,  24451,  23772,  23112,  22470,  21845,
  21239,  20649,  20075,  19518,  18975,  18448,  17936,  17438,
  16953,  16483,  16025,  15580,  15147,  14726,  14317,  13919,
  13533,  13157,  12792,  12436,  12091,  11755,  11428,  11111,
  10802,  10502,  10211,   9927,   9651,   9383,   9123,   8869,
   8623,   8383,   8150,   7924,   7704,   7490,   7282,   7080,
   6883,   6692,   6506,   6325,   6150,   5979,   5813,   5651,
   5494,   5342,   5193,   5049,   4909,   4772,   4640,   4511,
   4386,   4264,   4145,   4030,   3918,   3810,   3704,   3601,
   3501,   3404,   3309,   3217,   3128,   3041,   2956,   2874,
   2795,   2717,   2641,   2568,   2497,   2427,   2360,   2294,
   2231,   2169,   2108,   2050,   1993,   1938,   1884,   1831,
   1781,   1731,   1683,   1636,   1591,   1547,   1504,   1462,
   1421,   1382,   1343,   1306,   1270,   1235,   1200,   1167,
   1135,   1103,   1072,   1043,   1014,    986,    958,    932,
    906,    880,    856,    832,    809,    787,    765,    744,
    723,    703,    683,    664,    646,    628,    611,    594,
    577,    561,    545,    530,    516,    501,    487,    474,
    461,    448,    435,    423,    412,    400,    389,    378,
    368,    357,    348,    338,    329,    319,    311,    302,
    294,    285,    277,    270,    262,    255,    248,    241,
    234,    228,    221,    215,    209,    204,    198,    192,
    187,    182,    177,    172,    167,    162,    158,    154,
    149,    145,    141,    137,    133,    130,    126,    123,
    119,    116,    113,    110,    106,    104,    101,     98,
     95,     92,     90,     87,     85,     83,     80,     78,
     76,     74,     72,     70,     68,     66,     64,     62,
     61,     59,     57,     56,     54,     53,     51,     50,
};

