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

//Our variables
static QueueHandle_t _received_chars_queue = NULL;
static SemaphoreHandle_t  xMutexReceivedData = NULL;

static int start_game = 1;
static int run_process_matrix = 0;

// frame_buf contains a bit pattern for each column in the display
static uint16_t frame_buf[14] = {0, 0, 28, 62, 126, 254, 508, 254, 126, 62, 28, 0, 0, 0};



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

void processMatrix(void* pvParameters) {
	
	//The parameters are not used
	( void ) pvParameters;
	
	while (1) {
		//See if we can obtain the semaphore. If the semaphore is not available, wait 10 ticks to see if it becomes free.
		if (xSemaphoreTake(xMutexReceivedData, (TickType_t) 0) == pdTRUE) {
			//We were able to obtain the semaphore and can now access the shared resource.
			com_send_bytes((uint8_t *)"Processing matrix\n", 19);
			vTaskDelay(50);
			
			// We have finished accessing the shared resource. Release the semaphore.
			xSemaphoreGive(xMutexReceivedData);

		}
	}
}

void waitForKeyPress(void *pvParameters){

	//The parameters are not used
	( void ) pvParameters;

	//Variables
	uint8_t data[] = "";
	
	_received_chars_queue = xQueueCreate(_COM_RX_QUEUE_LENGTH, (unsigned portBASE_TYPE) sizeof (uint8_t));
	init_com(_received_chars_queue);	//Initialize comPort at the board


	//Semaphore
	xMutexReceivedData = xSemaphoreCreateMutex();
	//Take semaphore before everything happens
	//xSemaphoreTake(xMutexReceivedData, (TickType_t) 0);

	#if (configUSE_APPLICATION_TASK_TAG == 1)
	// Set task no to be used for tracing with R2R-Network
	vTaskSetApplicationTaskTag( NULL, ( void * ) 1 );
	#endif

	while (start_game == 1) {
		//Check until we receive something - Waiting for a number 1.
		if (xQueueReceive(_received_chars_queue, &data, ( TickType_t ) 10)) {
			com_send_bytes(data, 1);

			if (data[0] == 49){		//If it's equal as 1
				//Send first confirmation message to the console.
				com_send_bytes((uint8_t *)" Ready to receive - Reading...\n", 32);
				vTaskDelay(50);
				start_game = 0;
				} else {
				com_send_bytes((uint8_t *)"Not a valid key - Press 1 to play\n", 35);
				vTaskDelay(100);
			}
		}
	}

	init_com(_received_chars_queue);	//Initialize comPort at the board
	
	while (1) {
		xSemaphoreTake(xMutexReceivedData, (TickType_t) 0 );
		if (xQueueReceive(_received_chars_queue, &data,  ( TickType_t ) 10)){
			if (correctArrowKey(&data) == true) {
				com_send_bytes((uint8_t *)" Key: ", 6);
				com_send_bytes((uint8_t *)data, 1);
				
				
				//if (xSemaphoreTake(xMutexReceivedData, (TickType_t) 0 ) == pdTRUE) {
				run_process_matrix = 1;
				xSemaphoreGive(xMutexReceivedData);
				//}
				} else if (data[0] == 32) {
				com_send_bytes((uint8_t *)"Pause mode\n", 12);
				vTaskDelay(10);
				} else {
				com_send_bytes((uint8_t *)"Not a valid key\n", 16);
				vTaskDelay(10);
			}
		}
	}
	BaseType_t taskProcessMatrix = xTaskCreate(processMatrix, (const char *)"Process Matrix", configMINIMAL_STACK_SIZE, (void *) NULL, tskIDLE_PRIORITY, NULL);
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


//Don't delete or program crashes..
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

	//Create task to check key press
	BaseType_t taskWaitForKeyPress = xTaskCreate(waitForKeyPress, (const char *)"Wait for key press", configMINIMAL_STACK_SIZE, (void *)NULL, tskIDLE_PRIORITY, NULL);

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

