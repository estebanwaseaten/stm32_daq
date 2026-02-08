#include "main.h"

extern uint32_t _estack;		//defined in linker script

void Reset_Handler( void );
void Default_Handler( void );

__attribute__((section(".isr_vector")))
uint32_t *isr_vectors[] =
{
	(uint32_t *)&_estack,
	(uint32_t *)Reset_Handler,
	(uint32_t *)Default_Handler,	//shall we add SPI interrup handler?
};


int main( void )
{
	setWord( 0x20009000, 0 );
    setWord( 0x20009004, 0 );
    setWord( 0x20009008, 0 );
    setWord( 0x2000900C, 0 );

	//STMtest();

	CLOCK_init();		//seems to work without this sometimes

	//GPIO_init();

//	ADC_init();
//	ADC_enable( 1 );	//gets stuck

	SPI_init();
	SPI_enable( 1 );
	SPI_test();


	//GPIO_changeFunction( PIN, PIN_OUTPUT );
	//blink_forever();

	return 0;
}

void blink_forever( void )
{
	while(1)
	{
		//set diode:
		blink();
	}
}

void blink( void )
{
	GPIO_set( PIN );
	expensive_wait( 1 );

	//unset diode
	GPIO_unset( PIN );
	expensive_wait( 20 );

	GPIO_set( PIN );
	expensive_wait( 1 );
	GPIO_unset( PIN );
	expensive_wait( 2 );
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
