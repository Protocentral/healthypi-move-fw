/*
 * HealthyPi Move
 * 
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Author: Ashwin Whitchurch, Protocentral Electronics
 * Contact: ashwin@protocentral.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/reboot.h>

#include "cmd_module.h"
#include "hw_module.h"
#include "ble_module.h"
#include "fs_module.h"
#include "log_module.h"

LOG_MODULE_REGISTER(hpi_cmd_module, LOG_LEVEL_DBG);

#define MAX_MSG_SIZE 32
K_MSGQ_DEFINE(q_cmd_msg, sizeof(struct hpi_cmd_data_obj_t), 64, 4);  // Reduced from 128 to 64 messages

int cmd_pkt_len;
int cmd_pkt_pos_counter, cmd_pkt_data_counter;
int cmd_pkt_pkttype;
uint8_t ces_pkt_data_buffer[512]; // Reduced from 1000 to 512 bytes - sufficient for command packets
volatile bool cmd_module_ble_connected = false;
uint8_t data_pkt_buffer[128]; // Reduced from 256 to 128 bytes - adequate for BLE MTU size
uint8_t log_type=0;

// Externs
extern int global_dev_status;

extern struct k_sem sem_bpt_enter_mode_cal;
extern struct k_sem sem_bpt_cal_start;
extern struct k_sem sem_bpt_exit_mode_cal;

void hpi_decode_data_packet(uint8_t *in_pkt_buf, uint8_t pkt_len)
{
    uint8_t cmd_cmd_id = in_pkt_buf[0];
    uint8_t recording_type =0;

    LOG_DBG("RX Command: %X Len: %d", cmd_cmd_id, pkt_len);

    switch (cmd_cmd_id)
    {
    case HPI_CMD_GET_DEVICE_STATUS:
        LOG_DBG("RX CMD Get Device Status");
        break;
    case HPI_CMD_SET_DEVICE_TIME:
        LOG_DBG("RX CMD Set Time");
        hw_rtc_set_time(in_pkt_buf[1], in_pkt_buf[2], in_pkt_buf[3], in_pkt_buf[4], in_pkt_buf[5], in_pkt_buf[6]);
        break;
    case HPI_CMD_DEVICE_RESET:
        LOG_DBG("RX CMD Reboot");
        LOG_DBG("Rebooting...");
        k_sleep(K_MSEC(1000));
        sys_reboot(SYS_REBOOT_COLD);
        break;
    case HPI_CMD_BPT_SEL_CAL_MODE:
        LOG_DBG("RX CMD Select BPT Cal Mode");
        k_sem_give(&sem_bpt_enter_mode_cal);
        break;
    case HPI_CMD_START_BPT_CAL_START:
        uint8_t bpt_cal_index = in_pkt_buf[3];
        uint8_t bpt_cal_value_sys = in_pkt_buf[1];
        uint8_t bpt_cal_value_dia = in_pkt_buf[2];
        LOG_DBG("RX CMD Start Cal Index: %d Sys: %d Dia: %d", bpt_cal_index, bpt_cal_value_sys, bpt_cal_value_dia);
        hpi_bpt_set_cal_vals(bpt_cal_index, bpt_cal_value_sys, bpt_cal_value_dia);        
        k_sem_give(&sem_bpt_cal_start);
        break;
    case HPI_CMD_BPT_EXIT_CAL_MODE:
        LOG_DBG("RX CMD Exit BPT Cal Mode");
        k_sem_give(&sem_bpt_exit_mode_cal);
        break;
    // File System Commands
    case HPI_CMD_LOG_GET_COUNT:
        LOG_DBG("RX CMD Get Log Count");
        uint16_t log_count = log_get_count(in_pkt_buf[1]);
        log_type = in_pkt_buf[1];
        hpi_cmdif_send_count_rsp(HPI_CMD_LOG_GET_COUNT, log_type, log_count);
        break;
    case HPI_CMD_LOG_GET_INDEX:
        LOG_DBG("RX CMD Get Index: %d", in_pkt_buf[1]);
        log_type = in_pkt_buf[1];
        log_get_index(log_type);
        break;
    case HPI_CMD_LOG_GET_FILE:
        LOG_DBG("RX CMD Get Log");
        uint8_t log_type = in_pkt_buf[1];
        int64_t log_id_int64 = 0;
        for (int i = 0; i < 8; i++)
        {
            log_id_int64 |= ((int64_t)in_pkt_buf[2 + i] << (8 * i));
        }
        log_get(log_type, log_id_int64);
        break;
    case HPI_CMD_LOG_DELETE:
        LOG_DBG("RX CMD Log delete");
        log_delete((in_pkt_buf[1] | (in_pkt_buf[2] << 8)));
        break;
    case HPI_CMD_LOG_WIPE_ALL:
        LOG_DBG("RX CMD Log Wipe");
        log_wipe_trends();
        break;
    // Recording Commands
    case HPI_CMD_RECORDING_COUNT:
        LOG_DBG("RX CMD Recording Count");
        recording_type = in_pkt_buf[1];
        uint16_t recording_count = log_get_count(recording_type);
        hpi_cmdif_send_count_rsp(HPI_CMD_RECORDING_COUNT, recording_type, recording_count);
        break;
    case HPI_CMD_RECORDING_INDEX:
        LOG_DBG("RX CMD Recording Index");
        recording_type = in_pkt_buf[1];
        log_get_index(recording_type);
        break;
    case HPI_CMD_RECORDING_FETCH_FILE:
        LOG_DBG("RX CMD Recording Fetch File");
        recording_type = in_pkt_buf[1];
        int64_t recording_id_int64 = 0;
        for (int i = 0; i < 8; i++)
        {
            recording_id_int64 |= ((int64_t)in_pkt_buf[2 + i] << (8 * i));
        }
        log_get(recording_type, recording_id_int64);
        break;
    case HPI_CMD_RECORDING_DELETE:
        LOG_DBG("RX CMD Recording Delete");
        recording_type = in_pkt_buf[1];
        log_delete(recording_type);
        break;
    case HPI_CMD_RECORDING_WIPE_ALL:
        LOG_DBG("RX CMD Recording Wipe Records");
        log_wipe_records();
        break;
    default:
        LOG_DBG("RX CMD Unknown");
        break;
    }
}

void cmdif_send_ble_data(uint8_t *m_data, uint8_t m_data_len)
{
    //LOG_DBG("Sending BLE Data: %d", m_data_len);

    data_pkt_buffer[0] = CES_CMDIF_TYPE_DATA;
    for (int i = 0; i < m_data_len; i++)
    {
        data_pkt_buffer[1 + i] = m_data[i];
    }
    hpi_ble_send_data(data_pkt_buffer, 1 + m_data_len);
}

// Send index of all logs
void cmdif_send_ble_data_idx(uint8_t *m_data, uint8_t m_data_len)
{
    //LOG_DBG("Sending BLE Index Data: %d", m_data_len);
    uint8_t cmd_pkt[1 + m_data_len];

    cmd_pkt[0] = CES_CMDIF_TYPE_LOG_IDX;
    for (int i = 0; i < m_data_len; i++)
    {
        cmd_pkt[1 + i] = m_data[i];
        // printk("%02X ", m_data[i]);
    }
    // printk("\n");

    hpi_ble_send_data(cmd_pkt, 1 + m_data_len);
}

void hpi_cmdif_send_count_rsp(uint8_t m_cmd, uint8_t m_log_type, uint16_t m_value)
{
    LOG_DBG("Sending BLE Command Response: %X %X\n", m_cmd, m_value);
    uint8_t cmd_pkt[5];

    cmd_pkt[0] = CES_CMDIF_TYPE_CMD_RSP;
    cmd_pkt[1] = m_cmd;
    cmd_pkt[2] = m_log_type;
    cmd_pkt[3] = (uint8_t)(m_value & 0x00FF);
    cmd_pkt[4] = (uint8_t)((m_value >> 8) & 0x00FF);

    hpi_ble_send_data(cmd_pkt, 5);
}

void cmd_thread(void)
{
    LOG_DBG("CMD Thread starting");

    struct hpi_cmd_data_obj_t rx_cmd_data_obj;

    for (;;)
    {
        k_msgq_get(&q_cmd_msg, &rx_cmd_data_obj, K_FOREVER);

        /*LOG_DBG("Recd BLE Packet len: %d", rx_cmd_data_obj.data_len);
        for (int i = 0; i < rx_cmd_data_obj.data_len; i++)
        {
            printk("%02X ", rx_cmd_data_obj.data[i]);
        }
        printk("\n");
        */
        hpi_decode_data_packet(rx_cmd_data_obj.data, rx_cmd_data_obj.data_len);

        k_sleep(K_MSEC(1000));
    }
}

#define CMD_THREAD_STACKSIZE 2048  // Reduced from 3072 - adequate for command processing
#define CMD_THREAD_PRIORITY 7

K_THREAD_DEFINE(cmd_thread_id, CMD_THREAD_STACKSIZE, cmd_thread, NULL, NULL, NULL, CMD_THREAD_PRIORITY, 0, 0);
