#include "main.h"

extern uint32_t _estack;		//defined in linker script
int main( void );
void blink( void );
void Reset_Handler( void );
void Default_Handler( void );
void expensive_wait( int multiplier );

__attribute__((section(".isr_vector")))
uint32_t *isr_vectors[] =
{
	(uint32_t *)&_estack,
	(uint32_t *)Reset_Handler,
	(uint32_t *)Default_Handler,
};

#define PIN 5

int main( void )
{
	STMtest();

	GPIO_init();
	GPIO_changeFunction( PIN, PIN_OUTPUT );

	blink();

	return 0;

	CLOCK_init();		//seems to work without this sometimes

	GPIO_init();
	GPIO_changeFunction( PIN, PIN_OUTPUT );

	ADC_enable( 1 );	//gets stuck

	SPI_enable( 1 );

	//STMtest();

	blink();

	return 0;
}

void blink( void )
{
	while(1)
	{
		//set diode:
		GPIO_set( PIN );
		expensive_wait( 1 );

		//unset diode
		GPIO_unset( PIN );
		expensive_wait( 20 );
	}
}

void Reset_Handler( void )
{
	main();
}

void Default_Handler( void )
{
	main();
}


void expensive_wait( int multiplier )
{
	volatile int i = 0;
	multiplier *= 20000;

	for( i = 0; i < multiplier; i++ )
	{
		;
	}
}
