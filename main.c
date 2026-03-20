#include "main.h"

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
		STATE_FAST_TRANSFER_DONE,
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



//COMMANDS FROM PI:
#define CMD_ECHO  0x53		// 01010011		for debugging
#define CMD_ABORT 0x5F		// 01011111
#define CMD_QUERY 0x50 		// 01010000
#define CMD_STATE 0x5A		// ...

//per channel: last 2 bits
//settings
#define CMD_SET_TB 		0x71	// 01110001		settings, set timebase	--> full duration
#define CMD_SET_TRIG 	0x72	// 01110010		settings, set trigger
#define CMD_SET_MODE 	0x73	// 01110011		settings set trigger mode (circular buffer or software trigger)

//data acquisition:
#define CMD_DRDY 	0x60		// 01100000	-- ask if data is ready return 4bits for 4 channels
#define CMD_ACQU 	0x61 		// 01100001 -- tell to acquire data --> run circular buffer until either HW or SW trigger fires
#define CMD_FETCH 	0x62		// 01100010 -- in HW mode fetch data if trigger occured (afterwards re-arm trigger), in SW mode fetch last data batch (afterwards re-arm circular ACQ?)
//optional
#define CMD_STOP 	0x63		// 01100011 -- stop circular buffer and wait (stop TIM2)



//--> in software trigger mode: stops until data is fetched or ACQU is sent again
//--> in HW trigger mode: just transfers last data

static inline uint16_t SPIPACKET(uint8_t cmd, uint8_t data)
{
    return ((uint16_t)cmd << 8) | (uint16_t)data;
}

static inline uint16_t SPIDATAPACKET( uint16_t data )
{
    return data & 0x7FFF;
}

//state machine stuff
uint8_t gLastCmdSPI;
uint8_t gLastDataSPI;
uint32_t gState;


// data transfer globals (from _daq.c)
extern uint32_t gDataReady;	//binary encoding 0b1111 for all four channels, 0b0001 for channel 1
extern uint32_t gDataStart;		//channel 1 & 2 --> 0x20000000, ...
extern uint32_t gDataBitShift;		//channel1: 0 channel2: 16
extern uint32_t gDataTransferSize;	//number of datapoints
extern uint32_t gDataBufferLength;
extern uint32_t gDataIndex; 		//points to current datapoint during fast transfer
extern uint32_t gDataCounter;		//start with 0 and count up to gDataTransferSize
extern uint32_t gDatapointsAcquired;

//settings to be set via cmds
extern uint32_t g_settings_triggerMode;		//software or hardware (channel, v-level, t-position)
extern uint32_t g_settings_triggerLevel;
extern uint32_t g_settings_triggerPos;

//settings that can be changed via SPI and define the DAQ
extern uint32_t g_settings_datapoints;			// fixed at 1024 (max)
extern uint32_t g_settings_adc_time;			// 000 = 1.5 cycles to 111 = 601,5 cycles per ADC - must be set in ADC SMPR1 register
extern uint32_t g_settings_acquisition_speed;	// how many datapoints per second (defines timer settings)
extern uint32_t g_settings_enabledChannels;


//counter variables for testing
uint32_t gTickCounter_ms;
uint32_t gTim2Counter;


//volatile uint32_t gSomething = 9;			//this would go into the data section... does it need to be copied in reset handler? it does not seem so...

int main( void )
{
	//some globals
	gState = STATE_STARTUP;
	//gErrState = ERRSTATE_NOERR;
	gLastCmdSPI = 0;
	gTickCounter_ms = 0;
	gTim2Counter = 1;


	//these need to be changeable by commands
	g_settings_datapoints = 512;		//per channel	--> buffer = 4*512 = 2048
	g_settings_adc_time = 0x4; 		// 19.5 cycles
	g_settings_enabledChannels = CH1;// | CH2;
	//g_settings_enabledChannels = 0x3;

	g_settings_triggerMode = TRIG_SOFTWARE;
	g_settings_triggerLevel = 512;
	g_settings_triggerPos = 4;

	//clear normal SRAM for scope data
	for( int i = 0; i < 16; i++ )
	{
		setWord( 0x20009000 + i*4, 0 );
	}
	for( int i = 0; i < 4096; i++ )
	{
		setWord( 0x20000000 + i*4, 0 );
	}

	//setup interrupt handlers:
	setHandler_SPI1( mySPI1Handler );		//this seems to work, but somehow the interrupt never gets triggered...
	//other handlers are set in DAQ
	//setHandler_TIM2( myTIM2Handler );		//just for testing

	CLOCK_init( SYSCLK_PLL );		//could also use HSI, but probably more stable as with HSE
	//CLOCK_init( SYSCLK_HSE );
	//CLOCK_start_PLL( SYSCLK_HSE );	//needed for fast ADC already started when using PLL

//	uint32_t clkSpd = CLOCK_get_sysClk();		//this uint32_t seems to mess up the SPI communication... SOMETIMES
//	SYSTICK_enable( clkSpd );				//enabled --> mySysTickHandler() gets called every ms

	GPIO_init();	//enables GPIO clocks

	SPI_init( 1 );	//enables SPI clocks
	SPI_enable( 1, SPI_16BITSPERWORD );
	SPI_enable_interrupt( 1, SPI_RXNEI );
	SPI_send( SPIPACKET( RESP_ACK, DATA_NULL )  );	//load first acknowledge response (could do this within enable)

	DAQ12_setup();	//always configure both channels together
	//DAQ34_setup();

	//initialisation is done
	gState = STATE_IDLE;

	main_loop();

	return 0;
}


/****** INTERRUPT HANDLERS: ******/

// not really needed anymore - had that for testing - TIM2 triggers the ADCs on its own. now handler needed
void myTIM2Handler( void )		//not ticking yet
{
	TIMER2_clear_interrupt();

	gTim2Counter++;
	setWord( 0x20009004, gTim2Counter );
}

// SPI handler fetches new SPI command and changes state accordingly to STATE_CMDRECEIVED
// CMD_ABORT overwrites that. Also STATE_FAST_TRANSFER is taking precedence
void mySPI1Handler( void )		// THE HANDLER has to be executed fast: receive and send depending on current state
{
	uint16_t received = SPI_receive();
	gLastCmdSPI = received >> 8;
	gLastDataSPI = received & 0xFF;

	//debug:
	setWord( 0x20009010, received );
	setWord( 0x20009014, gLastCmdSPI );
	setWord( 0x20009018, gLastDataSPI );
	setWord( 0x2000901C, getWord( 0x2000901C ) + 1);	//to check if its working

	if( gLastCmdSPI == CMD_ABORT )	//ignores current state
	{
		gState = STATE_ABORT;			//let
		SPI_send( SPIPACKET( RESP_ACK, DATA_NULL )  );
		return;
	}

	// act by state:
	if( gState == STATE_FAST_TRANSFER )	//just send data until done --> potentially "very fast" 30k transfers per second, when STM32 is running at 72kHz
	{
		if( gDataCounter != 0 )		//starts sending at currectly configured datapoint
		{
			SPI_send( (getWord( gDataStart + gDataIndex*4 ) >> gDataBitShift) );		//only sends 16 bits
			//SPI_send( gDataIndex );
			gDataCounter++;
			gDataIndex++;
			if( gDataIndex >= gDataBufferLength )		//double check offsets here!
			{
				gDataIndex = 1;							//jump back to beginning of data array (not to the size... )
			}
			if( gDataCounter > gDataTransferSize )		//counts the actual number of datapoints only + the datasize transfer (therefore > not >=)
			{
				DAQ_currentFetchDone();		//unsets correct bit for data transfer
				gState = STATE_FAST_TRANSFER_DONE;	//--> checks in main loop if the DAQ needs to be resumed
			}
		}
		else
		{
			//no offset --> sizeof data array
			SPI_send( (getWord( gDataStart ) >> gDataBitShift) );	//sends the size of the array
			gDataCounter++;
		}
		return;
	}
	else if( gState == STATE_IDLE )
	{
		/* in idle state we can reply immediately if its just a single reply */
		if( (gLastCmdSPI == CMD_FETCH) && !CHKBIT( gDataReady, gLastDataSPI - 1 ) )		//-->
		{
			SPI_send( SPIPACKET( RESP_ERR, gDataReady )  );
			return;
		}
		else if( gLastCmdSPI == CMD_DRDY )								//receive data ready request --> simply reply
		{
			SPI_send( SPIPACKET( RESP_ACK, gDataReady ) );
			return;
		}
		else if( gLastCmdSPI == CMD_STATE )			//for debugging pointless, because it only works within STATE_IDLE
		{
			SPI_send( SPIPACKET( RESP_ACK, gState ) );
			return;
		}

		gState = STATE_CMDRECEIVED;			// change state machine
		SPI_send( SPIPACKET( RESP_ACK, DATA_NULL )  );		//if it is asked for data we should check data ready here??
		return;
	}
	else
	{
		SPI_send( SPIPACKET( RESP_WAIT, gState )  );	//send current state along for debugging
	}
}

void main_loop( void )
{
	int running = 1;
	while( running )
	{
		setWord( 0x2000900C, getWord(0x2000900C) + 1 );
		setWord( 0x20009008, gState );
		if( gState == STATE_ABORT )
		{
			gDataReady = 0;		//during acquisition no data is ready
			gDatapointsAcquired = 0;
			DAQ12_start();	// always acquire data for 1 & 2 in parallel if enabled
			//maybe restart here?
			gState = STATE_IDLE;
		}
		else if( gState == STATE_FAST_TRANSFER_DONE )
		{
			if( (gDataReady == 0) && ( g_settings_triggerMode == TRIG_SOFTWARE) )
			{
				//in software trigger mode, resume DAQ if all data has been transferred
				DAQ12_resume();
			}
			gState = STATE_IDLE;
		}
		else if( gState == STATE_CMDRECEIVED )
		{
			switch( gLastCmdSPI )	//only cmd
			{
				case CMD_DRDY:
					break;
				case CMD_FETCH:		// which channel is defined by data. only one channel can be transferred at a  time, because the package size is 16 bits.
					//in hardware mode or auto mode --> fetch the most recent data if available
					//should already be paused??

					//in software mode --> pause acquisition and transfer data
					DAQ12_pause();
					//check if data is ready ---> was already done

					DAQ_prepFetch( gLastDataSPI );	//preps params for fast fetch state. gLastDataSPI contains requested channel number
					gState = STATE_FAST_TRANSFER;
					break;
				case CMD_ACQU:		//always restarts data acquisition
					//prep data acqu
					gDataReady = 0;		//during acquisition no data is ready
					gDatapointsAcquired = 0;
					DAQ12_start();	// always acquire data for 1 & 2 in parallel if enabled

					gState = STATE_IDLE;
					break;
				case CMD_STOP:		//always pauses acquisition
					//
					DAQ12_pause();

					gState = STATE_IDLE;
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
