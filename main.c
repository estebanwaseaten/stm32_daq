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


void setWord( uint32_t addr, uint32_t word )
{
	volatile uint32_t *ptr = (uint32_t*)addr;
	*ptr = word;
}

//uint32_t someGlobal;
//direct memory access: *(volatile uint32_t *)(some address) = some value

int main( void )
{

	GPIOinit();
	GPIOchangeFunction( PIN, PIN_OUTPUT );


	//test debugging capabilities by just writing to RAM: (seems to work fine)
	uint32_t loop_counter = 0x10000000;
	setWord( 0x20000200, loop_counter++ );

	while(1)
	{
		//set diode:
		GPIOset( PIN );
		expensive_wait( 1 );

		//unset diode
		GPIOunset( PIN );
		expensive_wait( 1 );

		//set diode
		GPIOset( PIN );
		expensive_wait( 2 );

		//unset diiode
		GPIOunset( PIN );
		expensive_wait( 2 );

		setWord( 0x20000200, loop_counter++ );
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
	multiplier *= 200000;

	for( i = 0; i < multiplier; i++ )
	{
		;
	}
}
