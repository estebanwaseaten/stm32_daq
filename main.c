#include "main.h"

extern uint32_t _estack;	//defined in linker script
int main( void );
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

	GPIO_init();
	GPIO_changeFunction( PIN, PIN_OUTPUT );


	//test debugging capabilities by just writing to RAM: (seems to work fine)
	uint32_t loop_counter = 0x10000000;
	setWord( 0x20001800, loop_counter++ );

/*	for (int i = 0; i < 100; i++)
	{
		__asm("nop");
	}*/


	STMtest();

	while(1)
	{
		//set diode:
		GPIO_set( PIN );
		expensive_wait( 1 );

		//unset diode
		GPIO_unset( PIN );
		expensive_wait( 3 );

		setWord( 0x20001800, loop_counter++ );
	}

	return 0;
}

void Reset_Handler( void )
{
	main();
}

void Default_Handler( void )
{

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
