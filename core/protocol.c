/*
 * ILITEK Touch IC driver
 *
 * Copyright (C) 2011 ILI Technology Corporation.
 *
 * Author: Dicky Chiang <dicky_chiang@ilitek.com>
 * Based on TDD v7.0 implemented by Mstar & ILITEK
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */
#include "../common.h"
#include "protocol.h"

struct protocol_cmd_list *protocol = NULL;

static void config_protocol_v5_cmd(void)
{
    if(protocol->minor == 0x0)
    {
        protocol->func_ctrl_len = 2;
        
        protocol->sense_ctrl[0] = 0x1;
        protocol->sense_ctrl[1] = 0x0;
    
        protocol->sleep_ctrl[0] = 0x2;
        protocol->sleep_ctrl[1] = 0x0;
    
        protocol->glove_ctrl[0] = 0x6;
        protocol->glove_ctrl[1] = 0x0;
    
        protocol->stylus_ctrl[0] = 0x7;
        protocol->stylus_ctrl[1] = 0x0;
    
        protocol->tp_scan_mode[0] = 0x8;
        protocol->tp_scan_mode[1] = 0x0;
    
        protocol->lpwg_ctrl[0] = 0xA;
        protocol->lpwg_ctrl[1] = 0x0;
    
        protocol->gesture_ctrl[0] = 0xB;
        protocol->gesture_ctrl[1] = 0x3F;
    
        protocol->phone_cover_ctrl[0] = 0xC;
        protocol->phone_cover_ctrl[1] = 0x0;
    
        protocol->finger_sense_ctrl[0] = 0xF;
        protocol->finger_sense_ctrl[1] = 0x0;

        protocol->phone_cover_window[0] = 0xD;
    
        /* Non support on v5.0 */
        // protocol->proximity_ctrl[0] = 0x10;
        // protocol->proximity_ctrl[1] = 0x0;
    
        // protocol->plug_ctrl[0] = 0x11;
        // protocol->plug_ctrl[1] = 0x0;
    }
    else if(protocol->minor == 0x1)
    {
        protocol->func_ctrl_len = 3;
        
        protocol->sense_ctrl[0] = 0x1;
        protocol->sense_ctrl[1] = 0x1;
        protocol->sense_ctrl[2] = 0x0;
    
        protocol->sleep_ctrl[0] = 0x1;
        protocol->sleep_ctrl[1] = 0x2;
        protocol->sleep_ctrl[2] = 0x0;
    
        protocol->glove_ctrl[0] = 0x1;
        protocol->glove_ctrl[1] = 0x6;
        protocol->glove_ctrl[2] = 0x0;
    
        protocol->stylus_ctrl[0] = 0x1;
        protocol->stylus_ctrl[1] = 0x7;
        protocol->stylus_ctrl[2] = 0x0;
    
        protocol->tp_scan_mode[0] = 0x1;
        protocol->tp_scan_mode[1] = 0x8;
        protocol->tp_scan_mode[2] = 0x0;
    
        protocol->lpwg_ctrl[0] = 0x1;
        protocol->lpwg_ctrl[1] = 0xA;
        protocol->lpwg_ctrl[2] = 0x0;
    
        protocol->gesture_ctrl[0] = 0x1;
        protocol->gesture_ctrl[1] = 0xB;
        protocol->gesture_ctrl[2] = 0x3F;
    
        protocol->phone_cover_ctrl[0] = 0x1;
        protocol->phone_cover_ctrl[1] = 0xC;
        protocol->phone_cover_ctrl[2] = 0x0;
    
        protocol->finger_sense_ctrl[0] = 0x1;
        protocol->finger_sense_ctrl[1] = 0xF;
        protocol->finger_sense_ctrl[2] = 0x0;
    
        protocol->proximity_ctrl[0] = 0x1;
        protocol->proximity_ctrl[1] = 0x10;
        protocol->proximity_ctrl[2] = 0x0;
    
        protocol->plug_ctrl[0] = 0x1;
        protocol->plug_ctrl[1] = 0x11;
        protocol->plug_ctrl[2] = 0x0;

        protocol->phone_cover_window[0] = 0xE;
    }

    protocol->fw_ver_len = 4;
    protocol->pro_ver_len = 3;
    protocol->tp_info_len = 14;
    protocol->key_info_len = 30;
    protocol->core_ver_len = 5;
    protocol->window_len = 8;

    protocol->cmd_read_ctrl     = P5_0_READ_DATA_CTRL;
    protocol->cmd_get_tp_info   = P5_0_GET_TP_INFORMATION;
    protocol->cmd_get_key_info  = P5_0_GET_KEY_INFORMATION;
    protocol->cmd_get_fw_ver    = P5_0_GET_FIRMWARE_VERSION;
    protocol->cmd_get_pro_ver   = P5_0_GET_PROTOCOL_VERSION;
    protocol->cmd_get_core_ver  = P5_0_GET_CORE_VERSION;
    protocol->cmd_mode_ctrl     = P5_0_MODE_CONTROL;
    protocol->cmd_cdc_busy      = P5_0_CDC_BUSY_STATE;
    protocol->cmd_i2cuart       = P5_0_I2C_UART;

    protocol->unknow_mode       = P5_0_FIRMWARE_UNKNOWN_MODE;
    protocol->demo_mode         = P5_0_FIRMWARE_DEMO_MODE;
    protocol->debug_mode        = P5_0_FIRMWARE_DEBUG_MODE;
    protocol->test_mode         = P5_0_FIRMWARE_TEST_MODE;
    protocol->i2cuart_mode      = P5_0_FIRMWARE_I2CUART_MODE;

    protocol->demo_pid          = P5_0_DEMO_PACKET_ID;
    protocol->debug_pid         = P5_0_DEBUG_PACKET_ID;
    protocol->test_pid          = P5_0_TEST_PACKET_ID;
    protocol->i2cuart_pid       = P5_0_I2CUART_PACKET_ID;
    protocol->ges_pid           = P5_0_GESTURE_PACKET_ID;

    protocol->demo_len          = P5_0_DEMO_MODE_PACKET_LENGTH;
    protocol->debug_len         = P5_0_DEBUG_MODE_PACKET_LENGTH;
    protocol->test_len          = P5_0_TEST_MODE_PACKET_LENGTH;
}

int core_protocol_init(uint8_t major, uint8_t minor)
{
    if(protocol == NULL)
    {
        protocol = kzalloc(sizeof(*protocol), GFP_KERNEL);
        if(ERR_ALLOC_MEM(protocol))
        {
            DBG_ERR("Failed to allocate protocol mem");
            return -ENOMEM;
        }
    }

    protocol->major = major;
    protocol->minor = minor;

    DBG_INFO("major = %d, minor = %d", protocol->major, protocol->minor);

    if(protocol->major == 0x5)
    {
        config_protocol_v5_cmd(); 
    }
    else
    {
        DBG_ERR("Doesn't support this verions of protocol");
        return -1;
    }

    return 0;
}

void core_protocol_remove(void)
{
    DBG_INFO("Remove core-protocol memebers");

    if(protocol != NULL)
    {
        kfree(protocol);
        protocol = NULL;
    }
}

