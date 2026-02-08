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

	GPIO_init();

//	ADC_init();
//	ADC_enable( 1 );	//gets stuck

	SPI_init();
	SPI_enable( 1 );


	//main_loop();

	SPI_test();


	//GPIO_changeFunction( PIN, PIN_OUTPUT );
	//blink_forever();

	return 0;
}

/*		//16 bit transfer would be good... 10bits for data and 6 for
*		constantly listen to input
* 		when asked to measure:
*			- insert a "wait!" response into SPI buffer
*			- measure
*
*		when done insert "data available" into SPI buffer
*/
void main_loop( void )
{
	int running = 1;
	while( running )
	{
		//1. fetch SPI input command


		//2. do something


		//3. provide reply
	}
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
