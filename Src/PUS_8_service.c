/*
 * PUS_8_service.c
 *
 *  Created on: 2024. gada 11. jūl.
 *      Author: Rūdolfs Arvīds Kalniņš <rakal@kth.se>
 */

#include <Device_State.h>
#include "cmsis_os.h"
#include "Space_Packet_Protocol.h"
#include "PUS.h"
#include "General_Functions.h"
#include "PUS_1_service.h"
#include "PUS_8_service.h"
#include "FRAM.h"

#define FPGA_MSG_PREMABLE_0     0xB5
#define FPGA_MSG_PREMABLE_1     0x43
#define FPGA_MSG_POSTAMBLE      0x0A

#define LANGMUIR_READBACK_PREMABLE_0    FPGA_MSG_PREMABLE_0
#define LANGMUIR_READBACK_PREMABLE_1    FPGA_MSG_PREMABLE_1
#define LANGMUIR_READBACK_POSTAMBLE     FPGA_MSG_POSTAMBLE

#define SCIENTIFIC_DATA_PREAMBLE        0x83
#define SC_DATA_MAX_SIZE                10000
#define SC_CB_PACKET_RAW_DATA_LEN       6 // 2 sequence counter bytes and 2 data bytes each probe.
#define SC_CB_PACKET_FULL_DATA_LEN      1 + SC_CB_PACKET_RAW_DATA_LEN  // 1 byte header

extern QueueHandle_t UART_OBC_Out_Queue;
extern UART_HandleTypeDef huart5;

extern osThreadId PUS_3_TaskHandle;

extern volatile uint8_t Sweep_Bias_Mode_Data[3072];
extern volatile uint16_t Sweep_Bias_Data_counter;
extern volatile uint16_t Old_Sweep_Bias_Data_counter;


// This queue is used to receive info from the UART handler task
QueueHandle_t PUS_8_Queue;

uint8_t UART_FPGA_Rx_Buffer[100];
uint8_t UART_FPGA_OBC_Tx_Buffer[100];

volatile uint8_t uart_tx_FPGA_done = 1;

bool PUS_8_check_FPGA_msg_format(uint8_t* msg, uint8_t msg_len) {
    bool result = false;
    if (msg[0] == LANGMUIR_READBACK_PREMABLE_0) {
        if (msg[1] == LANGMUIR_READBACK_PREMABLE_1) {
            if (msg[(msg_len - 1)] == LANGMUIR_READBACK_POSTAMBLE) {
                result = true;
            }
        }
    }
    return result;
}

void PUS_8_unpack_msg(uint8_t* data, PUS_8_msg_unpacked* pus8_msg_unpacked)
{
	uint8_t* 	data_interator = data;
	pus8_msg_unpacked->func_id = *data_interator++;
	pus8_msg_unpacked->N_args = *data_interator++;

	for(int i = 0; i < pus8_msg_unpacked->N_args; i++) {
		uint8_t arg_ID = *data_interator++;
		switch(arg_ID) {
			case PROBE_ID_ARG_ID:
				pus8_msg_unpacked->probe_ID = *data_interator++;
				break;
			case STEP_ID_ARG_ID:
				pus8_msg_unpacked->step_ID = *data_interator++;
				break;
			case VOL_LVL_ARG_ID:
				memcpy((uint8_t*)&pus8_msg_unpacked->voltage_level, data_interator, sizeof(pus8_msg_unpacked->voltage_level));
				data_interator += sizeof(pus8_msg_unpacked->voltage_level);
				break;
			case N_STEPS_ARG_ID:
				pus8_msg_unpacked->N_steps = *data_interator++;
				break;
			case N_SKIP_ARG_ID:
				memcpy((uint8_t*)&pus8_msg_unpacked->N_skip, data_interator, sizeof(pus8_msg_unpacked->N_skip));
				data_interator += sizeof(pus8_msg_unpacked->N_skip);
				break;
			case N_F_ARG_ID:
				memcpy((uint8_t*)&pus8_msg_unpacked->N_f, data_interator, sizeof(pus8_msg_unpacked->N_f));
				data_interator += sizeof(pus8_msg_unpacked->N_f);
				break;
			case N_POINTS_ARG_ID:
				memcpy((uint8_t*)&pus8_msg_unpacked->N_points, data_interator, sizeof(pus8_msg_unpacked->N_points));
				data_interator += sizeof(pus8_msg_unpacked->N_points);
				break;
			case GS_TARGET_ARG_ID:
				// "sizeof" cannot be used here as .target is an enum type whose length cannot specified(at least in <C23).
				// If "sizeof" is used, it returns 4, which is incorrect as the target is only a single byte value.
				// This bug does not cause problems if the target argument is the last one in the message but would
				// mess up the alignment if it is in the beginning or middle of the message.
				memcpy((uint8_t*)&pus8_msg_unpacked->target, data_interator, 1);
				data_interator += 1;
				break;
			case N_SAMPLES_PER_STEP_ARG_ID:
				memcpy((uint8_t*)&pus8_msg_unpacked->N_samples_per_step, data_interator, sizeof(pus8_msg_unpacked->N_samples_per_step));
				data_interator += sizeof(pus8_msg_unpacked->N_samples_per_step);
				break;
			case FRAM_TABLE_ID_ARG_ID:
				memcpy((uint8_t*)&pus8_msg_unpacked->FRAM_Table_ID, data_interator, sizeof(pus8_msg_unpacked->FRAM_Table_ID));
				data_interator += sizeof(pus8_msg_unpacked->FRAM_Table_ID);
				break;
			default:
				break;
		}
	}
}

void PUS_8_copy_table_FRAM_to_FPGA(uint8_t fram_table_id, uint8_t fpga_table_id) {

    uint8_t msg[64] = {0};
	msg[0] = FPGA_MSG_PREMABLE_0;
	msg[1] = FPGA_MSG_PREMABLE_1;
	msg[2] = FPGA_SET_SWT_VOL_LVL;
	msg[3] = fpga_table_id;
	msg[7] = FPGA_MSG_POSTAMBLE;

    for(int i = 0; i < 256; i++) {
        uint8_t step_id = i;
        uint16_t value = read_sweep_table_value_FRAM(fram_table_id, step_id);

		msg[4] = step_id;
		msg[5] = ((uint8_t*)(&value))[0];
		msg[6] = ((uint8_t*)(&value))[1];

		if (HAL_UART_Transmit(&huart5, msg, 8, 100)!= HAL_OK) {
			HAL_GPIO_WritePin(GPIOB, LED4_Pin|LED3_Pin, GPIO_PIN_SET);
		}
		osDelay(5);
    }
}


SPP_error PUS_8_perform_function(SPP_header_t* SPP_h, PUS_TC_header_t* PUS_TC_h , PUS_8_msg_unpacked* pus8_msg_unpacked)
{
	switch(pus8_msg_unpacked->func_id)
	{
		case FPGA_SET_SWT_VOL_LVL:
		{
			if(pus8_msg_unpacked->target == TARGET_uC)
			{
				save_sweep_table_value_FRAM(pus8_msg_unpacked->probe_ID,
											pus8_msg_unpacked->step_ID,
											pus8_msg_unpacked->voltage_level);
			}
			else if (pus8_msg_unpacked->target == TARGET_FPGA)
			{
				uint8_t msg[64] = {0};
				uint8_t msg_cnt = 0;

				msg[msg_cnt++] = FPGA_MSG_PREMABLE_0;
				msg[msg_cnt++] = FPGA_MSG_PREMABLE_1;
				msg[msg_cnt++] = FPGA_SET_SWT_VOL_LVL;
				msg[msg_cnt++] = pus8_msg_unpacked->probe_ID;
				msg[msg_cnt++] = pus8_msg_unpacked->step_ID;
				msg[msg_cnt++] = ((uint8_t*)(&pus8_msg_unpacked->voltage_level))[0];
				msg[msg_cnt++] = ((uint8_t*)(&pus8_msg_unpacked->voltage_level))[1];

				msg[msg_cnt++] = FPGA_MSG_POSTAMBLE;

				if (HAL_UART_Transmit(&huart5, msg, msg_cnt, 100)!= HAL_OK) {
					HAL_GPIO_WritePin(GPIOB, LED4_Pin|LED3_Pin, GPIO_PIN_SET);
				}

			}
			break;
		}

		case FPGA_GET_SWT_VOL_LVL:
		{
			if(pus8_msg_unpacked->target == TARGET_uC)
			{
				uint16_t step_voltage = read_sweep_table_value_FRAM(pus8_msg_unpacked->probe_ID,
																	pus8_msg_unpacked->step_ID);
				UART_OUT_OBC_msg msg = {0};

				msg.PUS_HEADER_PRESENT	= 1;
				msg.PUS_SOURCE_ID 		= PUS_TC_h->source_id;
				msg.SERVICE_ID			= FUNCTION_MANAGEMNET_ID;
				msg.SUBTYPE_ID			= FM_PERFORM_FUNCTION;
				msg.TM_data[0] = pus8_msg_unpacked->target;
				msg.TM_data[1] = pus8_msg_unpacked->probe_ID;
				msg.TM_data[2] = pus8_msg_unpacked->step_ID;

				memcpy(msg.TM_data + 3*sizeof(uint8_t), &step_voltage, sizeof(uint16_t));
				msg.TM_data_len			= 5;

				xQueueSend(UART_OBC_Out_Queue, &msg, portMAX_DELAY);
			}
			else if(pus8_msg_unpacked->target == TARGET_FPGA)
			{
				uint8_t msg[64] = {0};
				uint8_t msg_cnt = 0;

				msg[msg_cnt++] = FPGA_MSG_PREMABLE_0;
				msg[msg_cnt++] = FPGA_MSG_PREMABLE_1;
				msg[msg_cnt++] = FPGA_GET_SWT_VOL_LVL;
				msg[msg_cnt++] = pus8_msg_unpacked->probe_ID;
				msg[msg_cnt++] = pus8_msg_unpacked->step_ID;
				msg[msg_cnt++] = FPGA_MSG_POSTAMBLE;

				memset(UART_FPGA_Rx_Buffer, 0, sizeof(UART_FPGA_Rx_Buffer));
				memset(UART_FPGA_OBC_Tx_Buffer, 0, sizeof(UART_FPGA_OBC_Tx_Buffer));

				UART_FPGA_OBC_Tx_Buffer[0] = FPGA_GET_SWT_VOL_LVL;
				UART_FPGA_OBC_Tx_Buffer[1] = pus8_msg_unpacked->probe_ID;
				UART_FPGA_OBC_Tx_Buffer[2] = pus8_msg_unpacked->step_ID;

//				HAL_UART_Receive_DMA(&huart5, UART_FPGA_Rx_Buffer, 2 + 9 + 1); // receiving a 16 bit value for the voltage

				if (HAL_UART_Transmit(&huart5, msg, msg_cnt, 100)!= HAL_OK) {
					HAL_GPIO_WritePin(GPIOB, LED4_Pin|LED3_Pin, GPIO_PIN_SET);
				}
			}
			break;
		}

		case FPGA_SWT_ACTIVATE_SWEEP:
		{
//			Current_Global_Device_State = CB_MODE;
			uint8_t msg[64] = {0};
			uint8_t msg_cnt = 0;
			msg[msg_cnt++] = FPGA_MSG_PREMABLE_0;
			msg[msg_cnt++] = FPGA_MSG_PREMABLE_1;
			msg[msg_cnt++] = FPGA_SWT_ACTIVATE_SWEEP;
			msg[msg_cnt++] = FPGA_MSG_POSTAMBLE;

			Sweep_Bias_Data_counter = 0;
			Old_Sweep_Bias_Data_counter = 1;

			memset(Sweep_Bias_Mode_Data, 0, sizeof(Sweep_Bias_Mode_Data));

			if (HAL_UART_Transmit(&huart5, msg, msg_cnt, 100)!= HAL_OK) {
				HAL_GPIO_WritePin(GPIOB, LED4_Pin|LED3_Pin, GPIO_PIN_SET);
			}

			// VERY IMPORTANT TO CLEAR THE INTERRUPTS
			__HAL_GPIO_EXTI_CLEAR_IT(FPGA_BUF_INT_Pin);
			NVIC_ClearPendingIRQ(EXTI9_5_IRQn);

			HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

			break;
		}

		case FPGA_EN_CB_MODE:
		{
			Current_Global_Device_State = CB_MODE;
//			vTaskSuspend(PUS_3_TaskHandle);

			uint8_t msg[64] = {0};
			uint8_t msg_cnt = 0;

			msg[msg_cnt++] = FPGA_MSG_PREMABLE_0;
			msg[msg_cnt++] = FPGA_MSG_PREMABLE_1;
			msg[msg_cnt++] = FPGA_EN_CB_MODE;
			msg[msg_cnt++] = FPGA_MSG_POSTAMBLE;

			memset(UART_FPGA_Rx_Buffer, 0, sizeof(UART_FPGA_Rx_Buffer));
			memset(UART_FPGA_OBC_Tx_Buffer, 0, sizeof(UART_FPGA_OBC_Tx_Buffer));

			UART_FPGA_OBC_Tx_Buffer[0] = FPGA_EN_CB_MODE;

//			HAL_UART_Receive_DMA(&huart5, UART_FPGA_Rx_Buffer, 2 + 9 + 1);

			if (HAL_UART_Transmit(&huart5, msg, msg_cnt, 100)!= HAL_OK) {
				HAL_GPIO_WritePin(GPIOB, LED4_Pin|LED3_Pin, GPIO_PIN_SET);
			}

			break;
		}
		case FPGA_DIS_CB_MODE:
		{
			Current_Global_Device_State = NORMAL_MODE;
//			vTaskResume(PUS_3_TaskHandle);

			uint8_t msg[64] = {0};
			uint8_t msg_cnt = 0;

			msg[msg_cnt++] = FPGA_MSG_PREMABLE_0;
			msg[msg_cnt++] = FPGA_MSG_PREMABLE_1;
			msg[msg_cnt++] = FPGA_DIS_CB_MODE;
			msg[msg_cnt++] = FPGA_MSG_POSTAMBLE;

			if (HAL_UART_Transmit(&huart5, msg, msg_cnt, 100)!= HAL_OK) {
				HAL_GPIO_WritePin(GPIOB, LED4_Pin|LED3_Pin, GPIO_PIN_SET);
			}

			break;
		}

		case FPGA_SET_CB_VOL_LVL:
		{
			uint8_t msg[64] = {0};
			uint8_t msg_cnt = 0;

			msg[msg_cnt++] = FPGA_MSG_PREMABLE_0;
			msg[msg_cnt++] = FPGA_MSG_PREMABLE_1;
			msg[msg_cnt++] = FPGA_SET_CB_VOL_LVL;
			msg[msg_cnt++] = pus8_msg_unpacked->probe_ID;
			msg[msg_cnt++] = ((uint8_t*)(&pus8_msg_unpacked->voltage_level))[0];
			msg[msg_cnt++] = ((uint8_t*)(&pus8_msg_unpacked->voltage_level))[1];
			msg[msg_cnt++] = FPGA_MSG_POSTAMBLE;

			if (HAL_UART_Transmit(&huart5, msg, msg_cnt, 100)!= HAL_OK) {
				HAL_GPIO_WritePin(GPIOB, LED4_Pin|LED3_Pin, GPIO_PIN_SET);
			}
			break;
		}

		case FPGA_GET_CB_VOL_LVL:
		{
			uint8_t msg[64] = {0};
			uint8_t msg_cnt = 0;

			msg[msg_cnt++] = FPGA_MSG_PREMABLE_0;
			msg[msg_cnt++] = FPGA_MSG_PREMABLE_1;
			msg[msg_cnt++] = FPGA_GET_CB_VOL_LVL;
			msg[msg_cnt++] = pus8_msg_unpacked->probe_ID;
			msg[msg_cnt++] = FPGA_MSG_POSTAMBLE;

			memset(UART_FPGA_Rx_Buffer, 0, sizeof(UART_FPGA_Rx_Buffer));
			memset(UART_FPGA_OBC_Tx_Buffer, 0, sizeof(UART_FPGA_OBC_Tx_Buffer));

			UART_FPGA_OBC_Tx_Buffer[0] = FPGA_GET_CB_VOL_LVL;
			UART_FPGA_OBC_Tx_Buffer[1] = pus8_msg_unpacked->probe_ID;

//			HAL_UART_Receive_DMA(&huart5, UART_FPGA_Rx_Buffer, 2 + 9 + 1); // receiving a 16 bit value for the voltage

			if (HAL_UART_Transmit(&huart5, msg, msg_cnt, 100)!= HAL_OK) {
				HAL_GPIO_WritePin(GPIOB, LED4_Pin|LED3_Pin, GPIO_PIN_SET);
			}

			break;
		}

		case FPGA_SET_SWT_STEPS:
		{
			uint8_t msg[64] = {0};
			uint8_t msg_cnt = 0;

			msg[msg_cnt++] = FPGA_MSG_PREMABLE_0;
			msg[msg_cnt++] = FPGA_MSG_PREMABLE_1;
			msg[msg_cnt++] = FPGA_SET_SWT_STEPS;
			msg[msg_cnt++] = pus8_msg_unpacked->N_steps;
			msg[msg_cnt++] = FPGA_MSG_POSTAMBLE;

			if (HAL_UART_Transmit(&huart5, msg, msg_cnt, 100)!= HAL_OK) {
				HAL_GPIO_WritePin(GPIOB, LED4_Pin|LED3_Pin, GPIO_PIN_SET);
			}
			break;
		}

		case FPGA_GET_SWT_STEPS:
		{
			uint8_t msg[64] = {0};
			uint8_t msg_cnt = 0;

			msg[msg_cnt++] = FPGA_MSG_PREMABLE_0;
			msg[msg_cnt++] = FPGA_MSG_PREMABLE_1;
			msg[msg_cnt++] = FPGA_GET_SWT_STEPS;
			msg[msg_cnt++] = FPGA_MSG_POSTAMBLE;

			memset(UART_FPGA_Rx_Buffer, 0, sizeof(UART_FPGA_Rx_Buffer));
			memset(UART_FPGA_OBC_Tx_Buffer, 0, sizeof(UART_FPGA_OBC_Tx_Buffer));

			UART_FPGA_OBC_Tx_Buffer[0] = FPGA_GET_SWT_STEPS;

//			HAL_UART_Receive_DMA(&huart5, UART_FPGA_Rx_Buffer, 2 + 9 + 1); // receiving a 16 bit value for the voltage

			if (HAL_UART_Transmit(&huart5, msg, msg_cnt, 100)!= HAL_OK) {
				HAL_GPIO_WritePin(GPIOB, LED4_Pin|LED3_Pin, GPIO_PIN_SET);
			}

			break;
		}

		case FPGA_SET_SWT_SAMPLES_PER_STEP:
		{
			uint8_t msg[64] = {0};
			uint8_t msg_cnt = 0;

			msg[msg_cnt++] = FPGA_MSG_PREMABLE_0;
			msg[msg_cnt++] = FPGA_MSG_PREMABLE_1;
			msg[msg_cnt++] = FPGA_SET_SWT_SAMPLES_PER_STEP;
			msg[msg_cnt++] = ((uint8_t*)(&pus8_msg_unpacked->N_samples_per_step))[0];
			msg[msg_cnt++] = ((uint8_t*)(&pus8_msg_unpacked->N_samples_per_step))[1];
			msg[msg_cnt++] = FPGA_MSG_POSTAMBLE;

			if (HAL_UART_Transmit(&huart5, msg, msg_cnt, 100)!= HAL_OK) {
				HAL_GPIO_WritePin(GPIOB, LED4_Pin|LED3_Pin, GPIO_PIN_SET);
			}
			break;
		}

		case FPGA_GET_SWT_SAMPLES_PER_STEP:
		{
			uint8_t msg[64] = {0};
			uint8_t msg_cnt = 0;

			msg[msg_cnt++] = FPGA_MSG_PREMABLE_0;
			msg[msg_cnt++] = FPGA_MSG_PREMABLE_1;
			msg[msg_cnt++] = FPGA_GET_SWT_SAMPLES_PER_STEP;
			msg[msg_cnt++] = FPGA_MSG_POSTAMBLE;

			memset(UART_FPGA_Rx_Buffer, 0, sizeof(UART_FPGA_Rx_Buffer));
			memset(UART_FPGA_OBC_Tx_Buffer, 0, sizeof(UART_FPGA_OBC_Tx_Buffer));

			UART_FPGA_OBC_Tx_Buffer[0] = FPGA_GET_SWT_SAMPLES_PER_STEP;

//			HAL_UART_Receive_DMA(&huart5, UART_FPGA_Rx_Buffer, 2 + 9 + 1); // receiving a 16 bit value for the voltage

			if (HAL_UART_Transmit(&huart5, msg, msg_cnt, 100)!= HAL_OK) {
				HAL_GPIO_WritePin(GPIOB, LED4_Pin|LED3_Pin, GPIO_PIN_SET);
			}

			break;
		}

		case FPGA_SET_SWT_SAMPLE_SKIP:
		{
			uint8_t msg[64] = {0};
			uint8_t msg_cnt = 0;

			msg[msg_cnt++] = FPGA_MSG_PREMABLE_0;
			msg[msg_cnt++] = FPGA_MSG_PREMABLE_1;
			msg[msg_cnt++] = FPGA_SET_SWT_SAMPLE_SKIP;
			msg[msg_cnt++] = ((uint8_t*)(&pus8_msg_unpacked->N_skip))[0];
			msg[msg_cnt++] = ((uint8_t*)(&pus8_msg_unpacked->N_skip))[1];
			msg[msg_cnt++] = FPGA_MSG_POSTAMBLE;

			if (HAL_UART_Transmit(&huart5, msg, msg_cnt, 100)!= HAL_OK) {
				HAL_GPIO_WritePin(GPIOB, LED4_Pin|LED3_Pin, GPIO_PIN_SET);
			}
			break;
		}

		case FPGA_GET_SWT_SAMPLE_SKIP:
		{
			uint8_t msg[64] = {0};
			uint8_t msg_cnt = 0;

			msg[msg_cnt++] = FPGA_MSG_PREMABLE_0;
			msg[msg_cnt++] = FPGA_MSG_PREMABLE_1;
			msg[msg_cnt++] = FPGA_GET_SWT_SAMPLE_SKIP;
			msg[msg_cnt++] = FPGA_MSG_POSTAMBLE;

			memset(UART_FPGA_Rx_Buffer, 0, sizeof(UART_FPGA_Rx_Buffer));
			memset(UART_FPGA_OBC_Tx_Buffer, 0, sizeof(UART_FPGA_OBC_Tx_Buffer));

			UART_FPGA_OBC_Tx_Buffer[0] = FPGA_GET_SWT_SAMPLE_SKIP;

//			HAL_UART_Receive_DMA(&huart5, UART_FPGA_Rx_Buffer, 2 + 9 + 1); // receiving a 16 bit value for the voltage

			if (HAL_UART_Transmit(&huart5, msg, msg_cnt, 100)!= HAL_OK) {
				HAL_GPIO_WritePin(GPIOB, LED4_Pin|LED3_Pin, GPIO_PIN_SET);
			}

			break;
		}

		case FPGA_SET_SWT_SAMPLES_PER_POINT:
		{
			uint8_t msg[64] = {0};
			uint8_t msg_cnt = 0;

			msg[msg_cnt++] = FPGA_MSG_PREMABLE_0;
			msg[msg_cnt++] = FPGA_MSG_PREMABLE_1;
			msg[msg_cnt++] = FPGA_SET_SWT_SAMPLES_PER_POINT;
			msg[msg_cnt++] = ((uint8_t*)(&pus8_msg_unpacked->N_f))[0];
			msg[msg_cnt++] = ((uint8_t*)(&pus8_msg_unpacked->N_f))[1];
			msg[msg_cnt++] = FPGA_MSG_POSTAMBLE;

			if (HAL_UART_Transmit(&huart5, msg, msg_cnt, 100)!= HAL_OK) {
				HAL_GPIO_WritePin(GPIOB, LED4_Pin|LED3_Pin, GPIO_PIN_SET);
			}
			break;
		}

		case FPGA_GET_SWT_SAMPLES_PER_POINT:
		{
			uint8_t msg[64] = {0};
			uint8_t msg_cnt = 0;

			msg[msg_cnt++] = FPGA_MSG_PREMABLE_0;
			msg[msg_cnt++] = FPGA_MSG_PREMABLE_1;
			msg[msg_cnt++] = FPGA_GET_SWT_SAMPLES_PER_POINT;
			msg[msg_cnt++] = FPGA_MSG_POSTAMBLE;

			memset(UART_FPGA_Rx_Buffer, 0, sizeof(UART_FPGA_Rx_Buffer));
			memset(UART_FPGA_OBC_Tx_Buffer, 0, sizeof(UART_FPGA_OBC_Tx_Buffer));

			UART_FPGA_OBC_Tx_Buffer[0] = FPGA_GET_SWT_SAMPLES_PER_POINT;

//			HAL_UART_Receive_DMA(&huart5, UART_FPGA_Rx_Buffer, 2 + 9 + 1); // receiving a 16 bit value for the voltage

			if (HAL_UART_Transmit(&huart5, msg, msg_cnt, 100)!= HAL_OK) {
				HAL_GPIO_WritePin(GPIOB, LED4_Pin|LED3_Pin, GPIO_PIN_SET);
			}

			break;
		}

		case FPGA_SET_SWT_NPOINTS:
		{
			uint8_t msg[64] = {0};
			uint8_t msg_cnt = 0;

			msg[msg_cnt++] = FPGA_MSG_PREMABLE_0;
			msg[msg_cnt++] = FPGA_MSG_PREMABLE_1;
			msg[msg_cnt++] = FPGA_SET_SWT_NPOINTS;
			msg[msg_cnt++] = ((uint8_t*)(&pus8_msg_unpacked->N_points))[0];
			msg[msg_cnt++] = ((uint8_t*)(&pus8_msg_unpacked->N_points))[1];
			msg[msg_cnt++] = FPGA_MSG_POSTAMBLE;

			if (HAL_UART_Transmit(&huart5, msg, msg_cnt, 100)!= HAL_OK) {
				HAL_GPIO_WritePin(GPIOB, LED4_Pin|LED3_Pin, GPIO_PIN_SET);
			}
			break;
		}

		case FPGA_GET_SWT_NPOINTS:
		{
			uint8_t msg[64] = {0};
			uint8_t msg_cnt = 0;

			msg[msg_cnt++] = FPGA_MSG_PREMABLE_0;
			msg[msg_cnt++] = FPGA_MSG_PREMABLE_1;
			msg[msg_cnt++] = FPGA_GET_SWT_NPOINTS;
			msg[msg_cnt++] = FPGA_MSG_POSTAMBLE;

			memset(UART_FPGA_Rx_Buffer, 0, sizeof(UART_FPGA_Rx_Buffer));
			memset(UART_FPGA_OBC_Tx_Buffer, 0, sizeof(UART_FPGA_OBC_Tx_Buffer));

			UART_FPGA_OBC_Tx_Buffer[0] = FPGA_GET_SWT_NPOINTS;

//			HAL_UART_Receive_DMA(&huart5, UART_FPGA_Rx_Buffer, 2 + 9 + 1); // receiving a 16 bit value for the voltage

			if (HAL_UART_Transmit(&huart5, msg, msg_cnt, 100)!= HAL_OK) {
				HAL_GPIO_WritePin(GPIOB, LED4_Pin|LED3_Pin, GPIO_PIN_SET);
			}

			break;
		}

		case CPY_TABLE_FRAM_TO_FPGA:
		{
			uint8_t FRAM_Table_ID = pus8_msg_unpacked->FRAM_Table_ID;
			uint8_t FPGA_Table_ID = pus8_msg_unpacked->probe_ID;
			PUS_8_copy_table_FRAM_to_FPGA(FRAM_Table_ID, FPGA_Table_ID);
			break;
		}

		default:
			break;
	}

	return SPP_OK;
}


// Function Management PUS service 8
SPP_error PUS_8_handle_FM_TC(SPP_header_t* SPP_header , PUS_TC_header_t* PUS_TC_header, uint8_t* data) {

	if (Current_Global_Device_State != NORMAL_MODE && Current_Global_Device_State != CB_MODE) {
		return UNDEFINED_ERROR;
	}
	if (SPP_header == NULL || PUS_TC_header == NULL) {
		return UNDEFINED_ERROR;
	}

	switch (PUS_TC_header->message_subtype_id) {
		case FM_PERFORM_FUNCTION:
			PUS_1_send_succ_acc(SPP_header, PUS_TC_header);
			break;
		default:
			PUS_1_send_fail_acc(SPP_header, PUS_TC_header);
			return SPP_UNHANDLED_PUS_ID;  // Invalid message subtype
	}

	PUS_8_msg pus8_msg_to_send;
	pus8_msg_to_send.SPP_header = *SPP_header;
	pus8_msg_to_send.PUS_TC_header = *PUS_TC_header;
	memcpy(pus8_msg_to_send.data, data, PUS_8_MAX_DATA_LEN);

	if (xQueueSend(PUS_8_Queue, &pus8_msg_to_send, 0) != pdPASS) {
		PUS_1_send_fail_start(SPP_header, PUS_TC_header);
	}

    return SPP_OK;
}
