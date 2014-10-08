/*
 * Knock_lock.c
 *
 * Created: 10/3/2014 7:12:52 PM
 *  Author: Sean Koppenhafer
 */ 

#define F_CPU 8000000		//8MHz CPU clock
#define MAXIMUM_KNOCKS 10
#define SOUND_THRESHOLD 200

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>

int check_button(void);
uint8_t store_code(int*);
uint8_t check_code(int*, uint8_t);
void open_lock(void);
void close_lock(void);
void turn_on_timer(void);
void turn_off_timer(void);
int time_gap_average(int*, int);
void setup_ADC(void);
void start_conversion(void);
void write_eeprom(int*, uint8_t);
uint8_t read_eeprom(int*);
void clear_eeprom(void);

int current_time_ms = 0;

int main(void)
{
	uint8_t locked;			//0 for false, 1 for true
	uint8_t knock_number;
	uint8_t matching_knocks;
	int knock_times[MAXIMUM_KNOCKS];
	DDRB = 0x02;	//Make PORT1 of B an output and port 0 an input
	PORTB = 0x01;	//Turn on pull up resistors for button on port 0
	
	/* If read_eeprom returns 0, then no knock is currently stored
	which means we keep the solenoid unlocked */
	knock_number = read_eeprom(&knock_times[0]);
	if( !knock_number ) {
		open_lock();
		locked = 0;
	}
	else {
		close_lock();
		locked = 1;
	}
	setup_ADC();
	sei();			//Turn on external interrupts
	
    while(1)
    {
		//Active low button
        if( !check_button() ) {
			if(locked) {
				matching_knocks = check_code(&knock_times[0], knock_number);
				
				if(!matching_knocks) {
					open_lock();
					clear_eeprom();
					locked = 0;
				}
			}
			else {
				knock_number = store_code(&knock_times[0]);
				if(knock_number > 0) { //Needs to have a code inputted to lock
					close_lock();
					write_eeprom(&knock_times[0], knock_number);	//Store in case power turns off
					locked = 1;
				}
			}
		}
    }
	
	return 0;
}

/* Button is active low */
int check_button(void) {
	uint8_t pin_mask = 0x01;
	return PORTB & pin_mask;
}

uint8_t store_code(int* knock_times) {
	turn_on_timer();
	uint8_t knock_index;
	
	knock_index = 0;
	while( ADC < SOUND_THRESHOLD ) {
		if( check_button() )
			goto EXIT; //No knocks were recorded before the button was pressed
	}
	knock_times[knock_index++] = 0;
	turn_on_timer();
	
	//Grab input until button is pressed again
	while( check_button() ) {
		//Only store sounds that are louder than the threshold
		if( ADC > SOUND_THRESHOLD ) {
			knock_times[knock_index++] = current_time_ms;
		}
	}
	
	turn_off_timer();
EXIT:
	return knock_index;
}

/* Grabs unlock code and compares it */
uint8_t check_code(int* orig_knock_times, uint8_t orig_knock_number) {
	uint8_t timer_index;
	uint8_t knock_index;
	uint8_t retval;
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
			unlock_knock_times[knock_index++] = current_time_ms;
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
	uint8_t compare_ticks = 125;	//Interrupt every 1ms
	TCCR0A = (1 << WGM01);				//Set the CTC bit
	OCR0A = compare_ticks;				//Interrupt every 1ms
	TIMSK0 = (1 << OCIE0A);				//Enable compare register interrupts
	TCCR0B = (1 << CS01) | (1 << CS00);	//Set pre-scalar to 64
}

/* Shuts the timer off */
void turn_off_timer(void) {
	current_time_ms = 0;
	TCCR0A = (0 << WGM01);
	OCR0A = 0;
	TIMSK0 = (0 << OCIE0A);
	TCCR0B = (0 << CS01) & (0 << CS00);
}

/* Finds the average time gap */
int time_gap_average(int* times, int number_of_times) {
	int average;
	uint8_t index;
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

/* Stores the knock times in eeprom in case power is turned off */
void write_eeprom(int* knock_times, uint8_t knock_number) {
	uint8_t i;
	int eeprom_address;
	eeprom_address = 0;
	
	/* Write the number of knocks to address 0x00 in eeprom
	then write the knock times into the next addresses */
	eeprom_write_byte( (uint8_t*)eeprom_address, (uint8_t)knock_number );
	eeprom_address += 4;		//dword align the address
	
	for(i = 0; i < knock_number; i++) {
		eeprom_write_dword( (uint32_t*)eeprom_address, (uint32_t)knock_times[i] );
		eeprom_address += 4;
	}
}

/* Read the knock times back out of eeprom after a reset event */
uint8_t read_eeprom(int* knock_times) {
	uint8_t i;
	uint8_t knock_count;
	int eeprom_address;
	eeprom_address = 0;
	
	//Read in the number of knocks first
	knock_count = eeprom_read_byte( (uint8_t*)eeprom_address );
	eeprom_address += 4;
	
	if(knock_count > MAXIMUM_KNOCKS) {
		knock_count = 255;		//Return this as error code
		goto EXIT;
	}
	else if (!knock_count)
		goto EXIT;
		
	//If there are valid set of knocks in memory, read them in now
	for(i = 0; i < knock_count; i++) {
		knock_times[i] = eeprom_read_dword( (uint32_t*)eeprom_address );
		eeprom_address += 4;
	}
	
EXIT:
	return knock_count;
}

/* Clear the knock count only.  This will show that no knock is currently stored */
void clear_eeprom(void) {
	uint8_t knock_count;
	uint8_t* eeprom_address;
	knock_count = 0;
	eeprom_address = 0;
	
	eeprom_write_byte(eeprom_address, knock_count);
}

/* Interrupt service routine for timer */
ISR(TIMER0_COMPA_vect) {
	current_time_ms++;
}

/* Interrupt service routine for A to D converter */
ISR(ADC_vect) {
	start_conversion();
}
