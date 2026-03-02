#include "main.h"

void mySysTickHandler( void );
void mySPI1Handler( void );
void myTIM2Handler( void );

enum main_states
{
		STATE_STARTUP,
		STATE_IDLE,
		STATE_ABORT,
		STATE_CMDRECEIVED,
		STATE_REPLYREADY,
		STATE_FAST_TRANSFER,
		STATE_ACQUIRE_DATA
};

enum error_states
{
		ERRSTATE_NOERR,
		ERRSTATE_ERR
};

#define DATA_NULL 0x0
//send commands (i.e. replies): when reply starts with 1 --> REPLY + DATA, if most signifcant bit is 0 --> all remaining bits are data
#define RESP_WAIT 0xA3		// 10100011
#define RESP_ERR 0xAC		// 10101100
#define RESP_ACK 0xA6		// 10100110
#define RESP_DONE 0xA7		// 10100111

//commands from pi
#define CMD_ECHO  0x53		// 01010011		for debugging
#define CMD_ABORT 0x5F		// 01011111
#define CMD_QUERY 0x50 		// 01010000

//per channel: last 2 bits
//settings
#define CMD_SET_TB 		0x71	// 01110001		settings, set timebase	--> full duration
#define CMD_SET_TRIG 	0x72	// 01110010		settings, set trigger
#define CMD_SET_MODE 	0x73	// 01110011		settings set trigger mode (circular buffer or software trigger)


//data acquisition:
#define CMD_DRDY 0x60		// 01100000	-- ask if data is ready
#define CMD_ACQU 0x61 		// 01100001 -- tell to acquire data
#define CMD_FFETCH 0x62		// 01100010 -- fetch data

static inline uint16_t SPIPACKET(uint8_t cmd, uint8_t data)
{
    return ((uint16_t)cmd << 8) | (uint16_t)data;
}

static inline uint16_t SPIDATAPACKET( uint16_t data )
{
    return data & 0x7FFF;
}

uint16_t gLastCmd;
uint32_t gState;
//uint32_t gErrState;

uint32_t gDataReady;
uint32_t gDataIndex;
uint32_t gTransferLength;

uint32_t gDatapointsAcquired;

uint32_t gActiveChannel;

uint32_t gTickCounter_ms;
uint32_t gTim2Counter;

//volatile uint32_t gSomething = 9;			//this would go into the data section... does it need to be copied in reset handler? it does not seem so...

int main( void )
{
	//setVTOR();		//maybe this was missing?

	gState = STATE_STARTUP;
	//gErrState = ERRSTATE_NOERR;
	gLastCmd = 0;
	gActiveChannel = 0;
	gTickCounter_ms = 0;
	gTim2Counter = 0;

	//clear normal SRAM for scope data
	for( int i = 0; i < 16; i++ )
	{
		setWord( 0x20009000 + i*4, 0 );
	}
	for( int i = 0; i < 1024; i++ )
	{
		setWord( 0x20000004 + i*4, i+1 );
	}

	//setup handlers first
	setHandler_SPI1( mySPI1Handler );		//this seems to work, but somehow the interrupt never gets triggered...
	setHandler_TIM2( myTIM2Handler );
	setHandler_SysTick( mySysTickHandler );		// called every ms



	CLOCK_init( SYSCLK_PLL );		//could also use HSI, but probably more stable as with HSE
	//CLOCK_start_PLL( SYSCLK_HSE );	//needed for fast ADC already started when using PLL

	uint32_t clkSpd = CLOCK_get_sysClk();		//this uint32_t seems to mess up the SPI communication... SOMETIMES
	CLOCK_enable_sysTick( clkSpd );			//enabled --> mySysTickHandler() gets called every ms


	GPIO_init();
	ADC_init();
	ADC_enable( 1 );

	SPI_init( 1 );
	SPI_enable( 1, SPI_16BITSPERWORD );
	SPI_enable_interrupt( 1, SPI_RXNEI );

	SPI_send( SPIPACKET( RESP_ACK, DATA_NULL )  );	//load first acknowledge response (could do this within enable)

	DMA_init();	//inits both DMAs

	//timer shall fire 1000 times in a row to trigger 1000 requests
	TIMER_init();
	TIMER_enable( 2, clkSpd/1000, true );
	expensive_wait( 1 );
	TIMER_start( 2 );
	setWord( 0x20009014, 0xF0F0F0F0 );

	gDataIndex = 0;
	gState = STATE_IDLE;
	gLastCmd = 0;
	gDataReady = 0;
	gDatapointsAcquired = 0;

	main_loop();

	return 0;
}

void myTIM2Handler( void )		//not ticking yet
{
	gTim2Counter++;
	setWord( 0x20009004, gTim2Counter );	;
}

void mySysTickHandler( void )	//ticks every ms
{
	gTickCounter_ms++;
	setWord( 0x20009000, gTickCounter_ms );		//just to check if works
}

//using handler works ok. maybe to improve speed one could employ DMA...
void mySPI1Handler( void )		// THE HANDLER has to be executed quickly: receive and send depending on current state
{
	//maybe check what interrup exactly this was
	setWord( 0x2000901C, getWord( 0x2000901C ) + 1);	//to check if its working
	uint16_t received = SPI_receive();	// should we check for -1 (probably not necessary)
	uint8_t *receivedCMD = (uint8_t*)&received + 1;

	if( receivedCMD[0] == CMD_ABORT )	//ignores current state
	{
		gState = STATE_ABORT;			//let
		SPI_send( SPIPACKET( RESP_ACK, DATA_NULL )  );
		return;
	}

	if( gState == STATE_FAST_TRANSFER )	//just send data until done --> potentially "very fast" 30k transfers per second, when STM32 is running at 72kHz
	{
		SPI_send( getWord( 0x20000000 + gDataIndex*4) );
		gDataIndex++;
		if( gDataIndex > gTransferLength )
		{
			gState = STATE_IDLE;
			gDataIndex = 0;
			gDataReady = 0;		//reset here?
		}
		return;
	}
	else if( gState == STATE_IDLE )
	{
		//should we decide here if that request is fine?
		//abort & very simple cases:
		if( (receivedCMD[0] == CMD_FFETCH) && (gDataReady == 0) )
		{
			SPI_send( SPIPACKET( RESP_ERR, DATA_NULL )  );
			return;
		}

		if( receivedCMD[0] == CMD_DRDY )
		{
			SPI_send( SPIPACKET( RESP_ACK, gDataReady ) );
			return;
		}

		gLastCmd = received;
		gState = STATE_CMDRECEIVED;

		SPI_send( SPIPACKET( RESP_ACK, DATA_NULL )  );		//if it is asked for data we should check data ready here??
		return;
	}
	else
	{
		SPI_send( SPIPACKET( RESP_WAIT, DATA_NULL )  );
	}
}

void main_loop( void )
{
	int running = 1;
	while( running )
	{
		setWord( 0x2000900C, getWord(0x2000900C) + 1 );
		if( gState == STATE_ABORT )
		{

		}
		else if( gState == STATE_CMDRECEIVED )
		{
			uint8_t cmd = gLastCmd >> 8;
			//uint8_t data = gLastCmd & 0xFF;
			switch( cmd )	//only cmd
			{
				case CMD_FFETCH:		//
					//1. check if data is ready
					if( gDataReady == 1 )
					{
						gTransferLength = gDatapointsAcquired;
						setWord( 0x20000000, gTransferLength );		//set length of remaing transfer into first bit.
						gState = STATE_FAST_TRANSFER;
					}

					break;
				case CMD_ACQU:
					//prep data acqu
					gDataReady = 0;
					gDatapointsAcquired = 0;
					gState = STATE_ACQUIRE_DATA;
					//do the actual acquisition
					break;
				default:
					gState = STATE_IDLE;
					break;
			}
		}
		else if( gState == STATE_ACQUIRE_DATA )
		{
			// acquire data --> this could be done via DMA
			while( gDatapointsAcquired < 100 )
			{
				setWord( (0x20000004 + 4*gDatapointsAcquired), ADC_read_single( 1 ) );
				gDatapointsAcquired++;
			}
			// then set Data is ready and go back to idle. --> this could be done in some interrupt
			gDataReady = 1;
			gState = STATE_IDLE;
		}
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
