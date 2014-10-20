/*
 * nocLock.c
 *
 * Created: 10/3/2014 7:12:52 PM
 *  Authors: Sean Koppenhafer, Travis Berger, Cameron Tribe, Jaime Rodriguez
 */ 

#define F_CPU 8000000				//8MHz CPU clock
#define MAXIMUM_KNOCKS 100
#define SOUND_THRESHOLD 415
#define TIME_OFFSET_MS 100			//Time tolerance for input knocks
#define POST_KNOCK_DELAY_MS 100		//Time to delay in ms after a knock spike on ADC

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <inttypes.h>
#include <util/delay.h>

/* Functions that deal with the knocks */
uint8_t check_button(void);
uint8_t store_code(int*);
uint8_t check_code(int*, uint8_t);

/* Output control functions */
void open_lock(void);
void close_lock(void);
void green_LED_on(void);
void green_LED_off(void);
void red_LED_on(void);
void red_LED_off(void);

/* Timer functions */
void turn_on_timer(void);
void turn_off_timer(void);

int time_gap_average(int*, int);

/* ADC functions */
void setup_ADC(void);
void start_conversion(void);

/* EEPROM functions */
void write_eeprom(int*, uint8_t);
uint8_t read_eeprom(int*);
void clear_eeprom(void);

volatile int current_time_ms = 0;

int main(void)
{
	uint8_t locked;			//0 for false, 1 for true
	uint8_t knock_number;
	uint8_t matching_knocks;
	uint8_t button_return;
	int knock_times[MAXIMUM_KNOCKS];
	DDRB = 0x0E;	//Make PORT1-3 of B an output and port 0 an input
	PORTB = 0x01;	//Turn on pull up resistors for button on port 0
	button_return = 0xFF;
	setup_ADC();
	
	/*If read_eeprom returns 0, then no knock is currently stored
	which means we keep the solenoid unlocked */
	knock_number = read_eeprom(&knock_times[0]);
	if( knock_number == 0xFF || knock_number == 0 ) {	//EEPROM starts off as 0xFF after being reprogrammed
		open_lock();
		locked = 0;
		green_LED_on();
	}
	else {
		close_lock();
		locked = 1;
		red_LED_on();
	}
	
    while(1)
    {
		//Active low button
        if( !button_return ) {
			if(locked) {
				matching_knocks = check_code(&knock_times[0], knock_number);
				
				if(!matching_knocks) {
					open_lock();
					clear_eeprom();
					locked = 0;
					red_LED_off();
					green_LED_on();
				}
			}
			else {
				knock_number = store_code(&knock_times[0]);
				if(knock_number > 0) { //Needs to have a code inputted to lock
					close_lock();
					write_eeprom(&knock_times[0], knock_number);	//Store in case power turns off
					locked = 1;
					green_LED_off();
					red_LED_on();
				}
			}
		}
		
		//Check the button status
		button_return = check_button();
    }
	
	return 0;
}

/* Button is active low */
uint8_t check_button(void) {
	uint8_t pin_mask = 0x01;
	uint8_t retval = PINB & pin_mask;
	return retval;
}

uint8_t store_code(int* knock_times) {
	uint8_t knock_index;
	uint8_t button_return;
	sei();			//Turn on global interrupts
	
	button_return = check_button();
	knock_index = 0;
	while( ADC < SOUND_THRESHOLD ) {
		if( !button_return ) {
			goto EXIT; //No knocks were recorded before the button was pressed
		}
		//Update button status
		button_return = check_button();
	}
	knock_times[knock_index++] = 0;
	_delay_ms(POST_KNOCK_DELAY_MS);		//Delay after first spike in knock value
	turn_on_timer();
	
	button_return = check_button();
	//Grab input until button is pressed again
	while( button_return ) {
		//Only store sounds that are louder than the threshold
		if( ADC > SOUND_THRESHOLD ) {
			knock_times[knock_index++] = current_time_ms;
			_delay_ms(POST_KNOCK_DELAY_MS);		//Delay after first spike in knock value
		}
		
		button_return = check_button();
	}
	
	turn_off_timer();
EXIT:
	cli();
	return knock_index;
}

/* Grabs unlock code and compares it */
uint8_t check_code(int* orig_knock_times, uint8_t orig_knock_number) {
	uint8_t timer_index;
	uint8_t knock_index;
	uint8_t retval;
	int unlock_knock_times[MAXIMUM_KNOCKS];
	//int orig_gaps;
	//int unlock_gaps;
	uint8_t button_return;
	sei();
	
	button_return = check_button();
	knock_index = 0;
	while( ADC < SOUND_THRESHOLD ) {
		if( !button_return ) {
			retval = 3;
			goto EXIT; //No knocks were recorded before the button was pressed
		}
		//Update button status
		button_return = check_button();
	}
	unlock_knock_times[knock_index++] = 0;
	_delay_ms(POST_KNOCK_DELAY_MS);		//Delay after first spike in knock value
	turn_on_timer();
	
	button_return = check_button();
	//Grab input until button is pressed again
	while( button_return ) {
		//Only store sounds that are louder than the threshold
		if(  ADC > SOUND_THRESHOLD ) {
			unlock_knock_times[knock_index++] = current_time_ms;
			_delay_ms(POST_KNOCK_DELAY_MS);		//Delay after first spike in knock value
		}
		button_return = check_button();
	}
	
	//Check if number of knocks are the same
	if(orig_knock_number != knock_index) {
		retval = 1;
		goto EXIT;
	}
	
	//Get average time gaps for each array
	//orig_gaps = time_gap_average(orig_knock_times, orig_knock_number);
	//unlock_gaps = time_gap_average(&unlock_knock_times[0], knock_index);
	
	int orig, new;
	//Compare the timer values - needs a threshold to accept reasonable values
	for(timer_index = 0; timer_index < knock_index; timer_index++) {
		orig = orig_knock_times[timer_index];
		new = unlock_knock_times[timer_index];
		if( (new < (orig - TIME_OFFSET_MS)) || (new > (orig + TIME_OFFSET_MS)) ) {
			retval = 2;
			goto EXIT;
		}
	}
	retval = 0;
	
EXIT:
	turn_off_timer();
	cli();
	return retval;
}

/* Active high solenoid */
void open_lock(void) {
	PORTB |= 0x02;
}

void close_lock(void) {
	PORTB &= ~0x02;
}

void green_LED_on(void) {
	PORTB |= 0x04;
}

void green_LED_off(void) {
	PORTB &= ~0x04;
}

void red_LED_on(void) {
	PORTB |= 0x08;
}

void red_LED_off(void) {
	PORTB &= ~0x08;
}

/* Turns on the timer */
void turn_on_timer(void) {
	uint8_t compare_ticks = 250;		//Interrupt every 1ms on 16MHz clock
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
	TCNT0 = 0;	//Reset timer counter
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
	eeprom_write_byte( (uint8_t*)eeprom_address, knock_number );
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
	
	//EEPROM resets to 0xFF
	if (!knock_count || knock_count == 0xFF)
		goto EXIT;
	else if(knock_count > MAXIMUM_KNOCKS) {
		knock_count = 255;		//Return this as error code
		goto EXIT;
	}
		
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
