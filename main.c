#include "main.h"


int main( void )
{
	setWord( 0x20009000, 0 );
    setWord( 0x20009004, 0 );
    setWord( 0x20009008, 0 );
    setWord( 0x2000900C, 0 );
	setWord( 0x20009010, 0 );

	//STMtest();

	CLOCK_init();		//seems to work without this sometimes

	GPIO_init();

//	ADC_init();
//	ADC_enable( 1 );	//gets stuck

	SPI_init();
	SPI_enable( 1 );
//	SPI_test();

	main_loop();

//	GPIO_changeFunction( PIN, PIN_OUTPUT );	//messes with the SPI
//	blink_forever();

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
		//pre-load wait command
		//1. fetch SPI input command
		//interrupt does this now

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
	uint32_t counter = getWord( 0x20009008 ) + 1;
	setWord( 0x20009008, counter );

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




void expensive_wait( int multiplier )
{
	volatile int i = 0;
	multiplier *= 20000;

	for( i = 0; i < multiplier; i++ )
	{
		;
	}
}
