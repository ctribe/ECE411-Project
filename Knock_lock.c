/*
 * Knock_lock.c
 *
 * Created: 10/3/2014 7:12:52 PM
 *  Author: Sean Koppenhafer
 */ 

#define F_CPU 20000000L
#define MAXIMUM_KNOCKS 10
#define SOUND_THRESHOLD 200

#include <avr/io.h>
#include <avr/interrupt.h>

int check_button(void);
unsigned char store_code(int*);
unsigned char check_code(int*, unsigned char);
void open_lock(void);
void close_lock(void);
void turn_on_timer(void);
void turn_off_timer(void);
int time_gap_average(int*, int);
void setup_ADC(void);
void start_conversion(void);

int current_time = 0;

int main(void)
{
	unsigned char locked;			//0 for false, 1 for true
	unsigned char knock_number;
	unsigned char matching_knocks;
	int knock_times[MAXIMUM_KNOCKS];
	
	locked = 0;
	DDRB = 0x02;
	PORTB = 0x03;	//Start with the lock open
	setup_ADC();
	sei();			//Turn on external interrupts
	
    while(1)
    {
		//Active low
        if( !check_button() ) {
			if(locked) {
				matching_knocks = check_code(&knock_times[0], knock_number);
				
				if(!matching_knocks) {
					open_lock();
					locked = 0;
				}
			}
			else {
				knock_number = store_code(&knock_times[0]);
				if(knock_number > 0) { //Needs to have a code inputted to lock
					close_lock();
					locked = 1;
				}
			}
		}
    }
	
	return 0;
}

/* Button is active low */
int check_button(void) {
	unsigned char pin_mask = 0x01;
	return PORTB & pin_mask;
}

unsigned char store_code(int* knock_times) {
	turn_on_timer();
	unsigned char knock_index;
	
	knock_index = 0;
	while( ADC < SOUND_THRESHOLD ) {	/*Wait for signal above the threshold*/}
	knock_times[knock_index++] = 0;
	turn_on_timer();
	
	//Grab input until button is pressed again
	while( check_button() ) {
		//Only store sounds that are louder than the threshold
		if( ADC > SOUND_THRESHOLD ) {
			knock_times[knock_index++] = current_time;
		}
	}
	
	current_time = 0;
	return knock_index;
}

/* Grabs unlock code and compares it */
unsigned char check_code(int* orig_knock_times, unsigned char orig_knock_number) {
	unsigned char timer_index;
	unsigned char knock_index;
	unsigned char retval;
	int unlock_knock_times[MAXIMUM_KNOCKS];
	int orig_gaps;
	int unlock_gaps;
	
	knock_index = 0;
	while( ADC > SOUND_THRESHOLD ) {	/*Wait for signal above the threshold*/}
	unlock_knock_times[knock_index++] = 0;
	turn_on_timer();
		
	//Grab input until button is pressed again
	while( check_button() ) {
		//Only store sounds that are louder than the threshold
		if(  ADC > SOUND_THRESHOLD ) {
			unlock_knock_times[knock_index++] = current_time;
		}
	}
	
	//Check if number of knocks are the same
	if(orig_knock_number != knock_index) {
		retval = 1;
		goto EXIT;
	}
	
	//Get average time gaps for each array
	orig_gaps = time_gap_average(orig_knock_times, orig_knock_number);
	unlock_gaps = time_gap_average(&unlock_knock_times[0], knock_index);
	
	//Compare the timer values - needs a threshold to accept reasonable values
	for(timer_index = 0; timer_index < knock_index; timer_index++) {
		if( orig_knock_times[timer_index] != unlock_knock_times[timer_index] ) {
			retval = 2;
			goto EXIT;
		}
	}
	retval = 0;
	
EXIT:
	turn_off_timer();
	return retval;
}

void open_lock(void) {
	PORTB |= 0x02;
}

void close_lock(void) {
	PORTB &= ~0x02;
}

/* Turns on the timer */
void turn_on_timer(void) {
	unsigned char compare_ticks = 195;	//Interrupt every 10ms
	TCCR0A = (1 << WGM01);				//Set the CTC bit
	OCR0A = compare_ticks;				//Interrupt every 10ms
	TIMSK0 = (1 << OCIE0A);				//Enable compare register interrupts
	TCCR0B = (1 << CS02) | (1 << CS00);	//Set pre-scalar to 1024
}

/* Shuts the timer off */
void turn_off_timer(void) {
	current_time = 0;
	TCCR0A = (0 << WGM01);
	OCR0A = 0;
	TIMSK0 = (0 << OCIE0A);
	TCCR0B = (0 << CS02) & (0 << CS00);
}

/* Finds the average time gap */
int time_gap_average(int* times, int number_of_times) {
	int average;
	unsigned char index;
	average = 0;
	
	for(index = 0; index < number_of_times; index++) {
		average += times[index];
	}
	average /= number_of_times;
	return average;
}

/* Sets up the analog to digital converter */
void setup_ADC(void) {
	ADMUX = (1 << REFS0) | (1 << MUX2) | (1 << MUX0);	//Vcc external reference voltage and ADC MUX 5
	ADCSRA = (1 << ADEN) | (1 << ADIE) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);	//Enable ADC, enable interrupts, and set pre-scaler to 128
	DIDR0 = (1 << ADC5D);
	
	start_conversion();
}

/* Starts an A to D conversion */
void start_conversion(void) {
	ADCSRA |= (1 << ADSC);	//Start the conversion
}

/* Interrupt service routine for timer */
ISR(TIMER0_COMPA_vect) {
	current_time++;
}

/* Interrupt service routine for A to D converter */
ISR(ADC_vect) {
	start_conversion();
}
