
//#include "main.h"
#include "Space_Packet_Protocol.h"

#define FRAM_I2C_ADDR 0xA0
#define FRAM_I2C_ADDR_READ 0xA1

#define FRAM_BOOT_CNT 0x0000
#define FRAM_FFU_ID 0x0002
#define FRAM_UNIT_ID 0x0003
#define FRAM_GS_ID_FPGA 0x0004
#define FRAM_GS_ID_UC 0x0005
#define FRAM_VHF_TIME_SLOT 0x0006

#define FRAM_SWEEP_TABLE_SECTION_START 0x0FC0
#define FRAM_SWEEP_TABLE_FOOTER_SIZE    8 // bytes
#define FRAM_SWEEP_TABLE_SIZE   512 + FRAM_SWEEP_TABLE_FOOTER_SIZE  // bytes


#define FRAM_BOOT_METADATA      FRAM_SWEEP_TABLE_SECTION_START
#define FRAM_SWEEP_TABLE_0      FRAM_SWEEP_TABLE_SECTION_START + (FRAM_SWEEP_TABLE_SIZE * 1)
#define FRAM_SWEEP_TABLE_1      FRAM_SWEEP_TABLE_SECTION_START + (FRAM_SWEEP_TABLE_SIZE * 2)
#define FRAM_SWEEP_TABLE_2      FRAM_SWEEP_TABLE_SECTION_START + (FRAM_SWEEP_TABLE_SIZE * 3)
#define FRAM_SWEEP_TABLE_3      FRAM_SWEEP_TABLE_SECTION_START + (FRAM_SWEEP_TABLE_SIZE * 4)
#define FRAM_SWEEP_TABLE_4      FRAM_SWEEP_TABLE_SECTION_START + (FRAM_SWEEP_TABLE_SIZE * 5)
#define FRAM_SWEEP_TABLE_5      FRAM_SWEEP_TABLE_SECTION_START + (FRAM_SWEEP_TABLE_SIZE * 6)
#define FRAM_SWEEP_TABLE_6      FRAM_SWEEP_TABLE_SECTION_START + (FRAM_SWEEP_TABLE_SIZE * 7)


#define FRAM_FINAL_ADDRESS 0x1FFF

#define METADATA_SIZE 7

extern I2C_HandleTypeDef hi2c4;

typedef enum {
	BOOT_NEW_IMAGE = 0,
	BOOTED_OK
} Boot_Feedback;

typedef enum {
	NO = 0,
	YES = 1
} New_Metadata;

typedef enum {
	NO_BOOT_ERROR = 0,
	CORRUPTED_METADATA_ERROR = 1,
	HARDWARE_RESET_ERROR = 2,
	BOOT_FAILURE_PREVIOUS_IMAGE_ERROR = 3,
	BOOT_FAILURE_CURRENT_IMAGE_ERROR = 4,
}Error_Code;

// The metadata structre that is save into FRAM. 7 bytes long in total
typedef struct{
	uint16_t crc;
	uint8_t new_metadata;
	uint8_t boot_feedback;
	uint8_t image_index;
	int8_t boot_counter;
	uint8_t error_code;
} Metadata_Struct;

HAL_StatusTypeDef writeFRAM_DMA(uint16_t addr, uint8_t* data, uint32_t size);
HAL_StatusTypeDef readFRAM_DMA(uint16_t addr, uint8_t* buf, uint32_t size);
HAL_StatusTypeDef writeFRAM(uint16_t addr, uint8_t* data, uint32_t size);
HAL_StatusTypeDef readFRAM(uint16_t addr, uint8_t* buf, uint32_t size);

SPP_error save_sweep_table_value_FRAM(uint8_t table_id, uint8_t step_id, uint16_t value);
uint16_t read_sweep_table_value_FRAM(uint8_t table_id, uint8_t step_id);

void unpack_metadata(Metadata_Struct* metadata, uint8_t* metadata_raw);
void pack_metadata(Metadata_Struct* metadata, uint8_t* metadata_raw);
