/*
 * device_state.h
 *
 *  Created on: 2024. gada 13. jūl.
 *      Author: Rūdolfs Arvīds Kalniņš <rakal@kth.se>
 */

#ifndef DEVICE_STATE_H_
#define DEVICE_STATE_H_


typedef enum {
    NORMAL_MODE = 1,
    IDLE_MODE   = 2,
    OFF_MODE    = 3,
    UPDATE_MODE = 4,
	CB_MODE = 5,
	SB_MODE = 6,
} DeviceState;

typedef enum {
        SET_DEV_STATE_NORMAL,
        SET_DEV_STATE_IDLE,
        SET_DEV_STATE_REBOOT,
        SET_DEV_STATE_UPDATE,
        SET_DEV_STATE_SWAP_IMAGE,
} DevState_Func_ID_t;

extern DeviceState Current_Global_Device_State;

void set_device_state(DeviceState state);

#endif /* DEVICE_STATE_H_ */
