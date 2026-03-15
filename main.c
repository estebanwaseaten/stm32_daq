#include "main.h"

void mySysTickHandler( void );
void mySPI1Handler( void );
void myTIM2Handler( void );

void myDMA1_transfer_complete_SWtrig( void );
void myDMA1_HWtrig( void );
void myADC_watchdog_handler( void );

void updateHandlers( void );


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

enum trigger_modes
{
	TRIG_SOFTWARE,
	TRIG_CH1,
	TRIG_CH2,
	TRIG_CH3,
	TRIG_CH4
};

#define CH1 0x1
#define CH2 0x2
#define CH3 0x4
#define CH4 0x8

#define DATA_NULL 0x0
//send commands (i.e. replies): when reply starts with 1 --> REPLY + DATA, if most signifcant bit is 0 --> all remaining bits are data
#define RESP_WAIT 0xA3		// 10100011
#define RESP_ERR 0xAC		// 10101100
#define RESP_ACK 0xA6		// 10100110
#define RESP_DONE 0xA7		// 10100111



//COMMANDS FROM PI:
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

uint8_t gLastCmd;
uint8_t gLastData;
uint32_t gState;

uint32_t gDataReady;	//binary encoding 0b1111 for all four channels, 0b0001 for channel 1
uint32_t gDataStart;		//channel 1 & 2 --> 0x20000000, ...
uint32_t gDataBitShift;		//channel1: 0 channel2: 16
uint32_t gDataTransferSize;	//number of datapoints
uint32_t gDataIndex; 		//start with 0 and count up to gDataTransferSize
uint32_t gDatapointsAcquired;



uint32_t gTickCounter_ms;
uint32_t gTim2Counter;


//settings to be set via cmds
uint32_t g_settings_triggerMode;		//software or hardware (channel, v-level, t-position)
uint32_t g_settings_triggerLevel;
uint32_t g_settings_triggerPos;

//settings that can be changed via SPI and define the DAQ
uint32_t g_settings_datapoints;			// fixed at 1024 (max)
uint32_t g_settings_adc_time;			// 000 = 1.5 cycles to 111 = 601,5 cycles per ADC - must be set in ADC SMPR1 register
uint32_t g_settings_acquisition_speed;	// how many datapoints per second (defines timer settings)
uint32_t g_settings_enabledChannels;

//volatile uint32_t gSomething = 9;			//this would go into the data section... does it need to be copied in reset handler? it does not seem so...

int main( void )
{
	//some globals
	gState = STATE_STARTUP;
	//gErrState = ERRSTATE_NOERR;
	gLastCmd = 0;
	gTickCounter_ms = 0;
	gTim2Counter = 1;

	//these need to be changeable by commands
	g_settings_datapoints = 64;		//per channel
	g_settings_adc_time = 0x4; 		// 19.5 cycles
	g_settings_enabledChannels = CH1 | CH2;
	g_settings_enabledChannels = 0x3;

	g_settings_triggerMode = TRIG_SOFTWARE;
	g_settings_triggerLevel = 512;
	g_settings_triggerPos = 64;

	//clear normal SRAM for scope data
	for( int i = 0; i < 16; i++ )
	{
		setWord( 0x20009000 + i*4, 0 );
	}
	for( int i = 0; i < 1024; i++ )
	{
		setWord( 0x20000000 + i*4, 0 );
	}

	//setup interrupt handlers:
	setHandler_SPI1( mySPI1Handler );		//this seems to work, but somehow the interrupt never gets triggered...
	updateHandlers();
	//setHandler_TIM2( myTIM2Handler );		//just for testing


	CLOCK_init( SYSCLK_PLL );		//could also use HSI, but probably more stable as with HSE
	//CLOCK_init( SYSCLK_HSE );
	//CLOCK_start_PLL( SYSCLK_HSE );	//needed for fast ADC already started when using PLL

	uint32_t clkSpd = CLOCK_get_sysClk();		//this uint32_t seems to mess up the SPI communication... SOMETIMES
	SYSTICK_enable( clkSpd );				//enabled --> mySysTickHandler() gets called every ms


	GPIO_init();	//enables clocks

	SPI_init( 1 );	//enables clocks
	SPI_enable( 1, SPI_16BITSPERWORD );
	SPI_enable_interrupt( 1, SPI_RXNEI );
	SPI_send( SPIPACKET( RESP_ACK, DATA_NULL )  );	//load first acknowledge response (could do this within enable)

	DAQ12_setup();	//always configure both channels together
	//DAQ34_setup();

	//setWord( 0x20009014, 0xF0F0F0FF );

	gState = STATE_IDLE;
	gLastCmd = 0;
	gDataReady = 0;
	gDatapointsAcquired = 0;

	main_loop();

	return 0;
}

//DAQ stop:
// 1. stop timer
// 2. disarm ADC
// 3. clear DMA interrupt

//DAQ start:
// 1. reset DMA
// 2. arm ADC
// 3. start timer


// in case the trigger mode changes the handlers are updated:
void updateHandlers( void )
{
	if( g_settings_triggerMode == TRIG_SOFTWARE )
	{
		setHandler_DMA( 1, 1, myDMA1_transfer_complete_SWtrig );
		//setHandler_DMA( 2, ..., myDMA1_transfer_complete_SWtrig );
	}
	else
	{
		setHandler_DMA( 1, 1, myDMA1_HWtrig );
		//setHandler_DMA( 2, ..., myDMA1_transfer_complete_SWtrig );
		setHandler_ADC( 1, myADC_watchdog_handler );
		setHandler_ADC( 2, myADC_watchdog_handler );
		setHandler_ADC( 3, myADC_watchdog_handler );
		setHandler_ADC( 4, myADC_watchdog_handler );
	}
}

void updateConfig( void )
{

}

// for software trigger mode: called when DMA sequence is done! (do we need a second one for four channels?)
void myDMA1_transfer_complete_SWtrig( void )
{
	// stop the timer!
	//Immediately stop TIM2 (CEN=0) or clear EXTEN=0.
	//debug
	setWord( 0x20009030, 0xF );
	setWord( 0x2000903C, getWord( 0x2000903C ) + 1 );

	//clear interrupt
	DAQ12_stop();
	DMA_clear_interrupts( 1 );

	gDatapointsAcquired = g_settings_datapoints;
	gDataReady = g_settings_enabledChannels;
	setWord(0x20009034, gDataReady);

	gState = STATE_IDLE;
}

void myDMA2_transfer_complete_SWtrig( void )
{
	setWord( 0x20009038, getWord( 0x20009038 ) + 1 );
}

// for hardware trigger
void myDMA1_HWtrig( void )
{
	//half buffer and full buffer events
}

// for hardware trigger (watchdog) mode: called whenever a value is crossed... ---> should still continue for 1/2 DMA acquisitions
void myADC_watchdog_handler( void )
{
	//uint32_t remaining = DMA1->CH[0].CNDTR;
	//awd_position = BUFFER_SIZE - remaining;  // ← Position im Buffer
    //awd_triggered = true;
}

// not really needed - had that for testing
void myTIM2Handler( void )		//not ticking yet
{
	TIMER2_clear_interrupt();

	gTim2Counter++;
	setWord( 0x20009004, gTim2Counter );
}

// SPI handler fetches new SPI command and changes state accordingly to STATE_CMDRECEIVED
// CMD_ABORT overwrites that. Also STATE_FAST_TRANSFER is taking precedence
void mySPI1Handler( void )		// THE HANDLER has to be executed quickly: receive and send depending on current state
{
	uint16_t received = SPI_receive();
	gLastCmd = received >> 8;
	gLastData = received & 0xFF;

	//debug:
	setWord( 0x20009010, received );
	setWord( 0x20009014, gLastCmd );
	setWord( 0x20009018, gLastData );
	setWord( 0x2000901C, getWord( 0x2000901C ) + 1);	//to check if its working

	if( gLastCmd == CMD_ABORT )	//ignores current state
	{
		gState = STATE_ABORT;			//let
		SPI_send( SPIPACKET( RESP_ACK, DATA_NULL )  );
		return;
	}

	//act by state:
	if( gState == STATE_FAST_TRANSFER )	//just send data until done --> potentially "very fast" 30k transfers per second, when STM32 is running at 72kHz
	{
		SPI_send( (getWord( gDataStart + gDataIndex*4 ) >> gDataBitShift) );		//only sends 16 bits
		gDataIndex++;
		if( gDataIndex > gDataTransferSize )
		{
			gState = STATE_IDLE;
			gDataIndex = 0;

			//gDataReady = 0;	//should only unset correct bit
		}
		return;
	}
	else if( gState == STATE_IDLE )
	{
		/* in idle state we can reply immediately if its just a single reply */
		if( (gLastCmd == CMD_FFETCH) && !CHKBIT( gDataReady, gLastData - 1 ) )
		{
			SPI_send( SPIPACKET( RESP_ERR, gDataReady )  );
			return;
		}

		if( gLastCmd == CMD_DRDY )								//receive data ready request --> simply reply
		{
			SPI_send( SPIPACKET( RESP_ACK, gDataReady ) );
			return;
		}

		gState = STATE_CMDRECEIVED;			// change state machine
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
			gState = STATE_IDLE;
		}
		else if( gState == STATE_CMDRECEIVED )
		{
			switch( gLastCmd )	//only cmd
			{
				case CMD_DRDY:
					break;
				case CMD_FFETCH:		// which channel is defined by data. only one channel can be transferred at a  time, because the package size is 16 bits.
					//
					//1. check if data is ready
					//if( gDataReadyCh1 == 1 )
					gDataIndex = 0;
					gDataStart = 0x20000000;
					gDataBitShift = 0;

					if( (gLastData == 3) || (gLastData == 4) )
					{
						gDataStart = 0x20000400;		//offset
					}
					if( (gLastData == 2) || (gLastData == 4) )
					{
						gDataBitShift = 16;
					}

					gDataTransferSize = g_settings_datapoints;
					setWord( 0x20000000, gDataTransferSize + (gDataTransferSize << 16) );		//set length of remaing transfer into first bit.
					gState = STATE_FAST_TRANSFER;
					break;
				case CMD_ACQU:
					//prep data acqu
					gDataReady = 0;		//during acquisition no data is ready
					gDatapointsAcquired = 0;
					gState = STATE_ACQUIRE_DATA;
					DAQ12_start();	// always acquire data for 1 & 2 in parallel if enabled
					break;
				default:
					gState = STATE_IDLE;
					break;
			}
		}
		else if( gState == STATE_ACQUIRE_DATA )
		{
			//do nothing
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
