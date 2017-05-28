/*
* FreeRTOS_Test.c
*
* Created: 26/10/2016 13:55:41
* Author : IHA
*/

#include <avr/sfr_defs.h>
#include <avr/io.h>
#include <avr/interrupt.h>

// FfreeRTOS Includes
#include <FreeRTOS.h>
#include <task.h>
#include <timers.h>
#include <queue.h>
#include <semphr.h>

//CNC 20170525 Added to have booleans
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "src/board/board.h"

static const uint8_t _COM_RX_QUEUE_LENGTH = 30;

static QueueHandle_t _received_chars_queue = NULL;	//CNC 20172505
static SemaphoreHandle_t  xMutexReceivedData = NULL;
static bool run_process_matrix = false;

// frame_buf contains a bit pattern for each column in the display
static uint16_t frame_buf[14] = {0, 0, 28, 62, 126, 254, 508, 254, 126, 62, 28, 0, 0, 0};


//-----------------------------------------
/*void another_task(void *pvParameters)
{
// The parameters are not used
( void ) pvParameters;

#if (configUSE_APPLICATION_TASK_TAG == 1)
// Set task no to be used for tracing with R2R-Network
vTaskSetApplicationTaskTag( NULL, ( void * ) 2 );
#endif

BaseType_t result = 0;
uint8_t byte;

while(1)
{
result = xQueueReceive(_x_com_received_chars_queue, &byte, 1000L);

if (result) {
com_send_bytes(&byte, 1);
}else {
com_send_bytes((uint8_t*)"TO", 2);
}
}
}*/


//-----------------------------------------
/*void startup_task(void *pvParameters)
{
// The parameters are not used
( void ) pvParameters;

#if (configUSE_APPLICATION_TASK_TAG == 1)
// Set task no to be used for tracing with R2R-Network
vTaskSetApplicationTaskTag( NULL, ( void * ) 1 );
#endif

_x_com_received_chars_queue = xQueueCreate( _COM_RX_QUEUE_LENGTH, ( unsigned portBASE_TYPE ) sizeof( uint8_t ) );
init_com(_x_com_received_chars_queue);

// Initialise Mutex
xMutex = xSemaphoreCreateMutex();

// Initialization of tasks etc. can be done here
BaseType_t t1 = xTaskCreate(another_task, (const char *)"Another", configMINIMAL_STACK_SIZE, (void *)NULL, tskIDLE_PRIORITY, NULL);


// Lets send a start message to the console
com_send_bytes((uint8_t *)"Then we Start!\n", 15);

while(1)
{
vTaskDelay( 1000 );
}
}*/


bool correctArrowKey(char data_received[])
{
	switch (data_received[0])
	{
		case 65  : return true;		//A
		case 97  : return true;		//a
		case 87  : return true;		//W
		case 119 : return true;		//w
		case 68 : return true;		//D
		case 100 : return true;		//d
		case 83 : return true;		//S
		case 115 : return true;		//s
		default  : return false;

	}
	
}

/************************************************************************/
/* CNC 20172505                                                         */
/************************************************************************/

void waitForHello(void *pvParameters){

	//The parameters are not used
	( void ) pvParameters;

	#if (configUSE_APPLICATION_TASK_TAG == 1)
	// Set task no to be used for tracing with R2R-Network
	vTaskSetApplicationTaskTag( NULL, ( void * ) 1 );
	#endif
	

	//Variables
	//BaseType_t received_data = 0;
	
	uint8_t data[] = "";
	uint8_t response[] = { 0x4f, 0x4b };	//OK

	_received_chars_queue = xQueueCreate(_COM_RX_QUEUE_LENGTH, (unsigned portBASE_TYPE) sizeof (uint8_t));
	init_com(_received_chars_queue);	//Initialize comPort at the board

	vTaskDelay(500);
	while (1) {

		vTaskDelay(500);
		//Check until we receive something - Waiting for a number 1.
		if (xQueueReceive(_received_chars_queue, &data, ( TickType_t ) 10)) {
			
			com_send_bytes(data, 1);

			if (data[0] == 49){		//If it's equal as 1
				//Send first confirmation message to the console.
				
				com_send_bytes((uint8_t *)" Ready to receive - Reading...\n", 32);
				
				vTaskDelay(500);

				while (1) {
					if (xQueueReceive(_received_chars_queue, &data,  ( TickType_t ) 10)){
						if (correctArrowKey(&data) == true) {
							com_send_bytes((uint8_t *)" Key: ", 6);
							com_send_bytes((uint8_t *)data, 1);
							
							xMutexReceivedData = xSemaphoreCreateMutex();
							if (xSemaphoreTake(xMutexReceivedData, (TickType_t) 10 ) == pdTRUE) {
								run_process_matrix = 1;
								xSemaphoreGive(xMutexReceivedData);
							} else if (data[0] == 32) {
							com_send_bytes((uint8_t *)"Pause mode\n", 12);
							} else {
							com_send_bytes((uint8_t *)"Not a valid key\n", 16);
							}
						}
					}
				}
			} else {
			com_send_bytes((uint8_t *)"Not a valid key - Press 1 to play\n", 35);
			vTaskDelay(500);
			}
		}
	}
}


void processMatrix(void* pvParameters) {
	
	//The parameters are not used
	( void ) pvParameters;
	
	if (run_process_matrix == 1) {
		com_send_bytes((uint8_t *)"Processing matrix...!\n", 22);
	}
}

// Prepare shift register setting SER = 1
void prepare_shiftregister()
{
	// Set SER to 1
	PORTD |= _BV(PORTD2);
}

// clock shift-register
void clock_shift_register_and_prepare_for_next_col()
{
	// one SCK pulse
	PORTD |= _BV(PORTD5);
	PORTD &= ~_BV(PORTD5);
	
	// one RCK pulse
	PORTD |= _BV(PORTD4);
	PORTD &= ~_BV(PORTD4);
	
	// Set SER to 0 - for next column
	PORTD &= ~_BV(PORTD2);
}

// Load column value for column to show
void load_col_value(uint16_t col_value)
{
	PORTA = ~(col_value & 0xFF);
	
	// Manipulate only with PB0 and PB1
	PORTB |= 0x03;
	PORTB &= ~((col_value >> 8) & 0x03);
}

//-----------------------------------------
void handle_display(void)
{
	static uint8_t col = 0;
	
	if (col == 0)
	{
		prepare_shiftregister();
	}
	
	load_col_value(frame_buf[col]);
	
	clock_shift_register_and_prepare_for_next_col();
	
	// count column up - prepare for next
	col++;
	if (col > 13)
	{
		col = 0;
	}
}

//-----------------------------------------
void vApplicationIdleHook( void )
{
	//
}

//-----------------------------------------
int main(void)
{
	
	init_board();
	
	// Shift register Enable output (G=0)
	PORTD &= ~_BV(PORTD6);
	
	//Create task to blink gpio
	BaseType_t taskWaitForHello = xTaskCreate(waitForHello, (const char *)"Wait for hello", configMINIMAL_STACK_SIZE, (void *)NULL, tskIDLE_PRIORITY, NULL);

	BaseType_t taskProcessMatrix = xTaskCreate(processMatrix, (const char *)"Process Matrix", configMINIMAL_STACK_SIZE, (void *) NULL, tskIDLE_PRIORITY, NULL);

	// Start the display handler timer
	init_display_timer(handle_display);
	
	sei();
	
	//Start the scheduler
	vTaskStartScheduler();
	
	//Should never reach here
	while (1)
	{
	}
}


