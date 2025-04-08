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
K_MSGQ_DEFINE(q_cmd_msg, sizeof(struct hpi_cmd_data_obj_t), 128, 4);

int cmd_pkt_len;
int cmd_pkt_pos_counter, cmd_pkt_data_counter;
int cmd_pkt_pkttype;
uint8_t ces_pkt_data_buffer[1000]; // = new char[1000];
volatile bool cmd_module_ble_connected = false;
uint8_t data_pkt_buffer[256];

// Externs
extern int global_dev_status;

void hpi_decode_data_packet(uint8_t *in_pkt_buf, uint8_t pkt_len)
{
    uint8_t cmd_cmd_id = in_pkt_buf[0];

    LOG_DBG("Recd Command: %X", cmd_cmd_id);

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

    // File System Commands
    case HPI_CMD_LOG_GET_COUNT:
        LOG_DBG("RX CMD Get Log Count");
        uint16_t log_count = log_get_count(in_pkt_buf[1]);
        hpi_cmdif_send_ble_cmd_rsp(HPI_CMD_LOG_GET_COUNT, log_count);
        break;
    case HPI_CMD_LOG_GET_INDEX:
        LOG_DBG("RX CMD Get Index");
        log_get_index(in_pkt_buf[1]);
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
        log_wipe_all();
        break;
    default:
        LOG_DBG("RX CMD Unknown");
        break;
    }
}

void cmdif_send_ble_data(uint8_t *m_data, uint8_t m_data_len)
{
    LOG_DBG("Sending BLE Data: %d", m_data_len);

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
    LOG_DBG("Sending BLE Index Data: %d", m_data_len);
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

void hpi_cmdif_send_ble_cmd_rsp(uint8_t m_cmd, uint16_t m_value)
{
    printk("Sending BLE Command Response: %X %X\n", m_cmd, m_value);
    uint8_t cmd_pkt[4];

    cmd_pkt[0] = CES_CMDIF_TYPE_CMD_RSP;
    cmd_pkt[1] = m_cmd;
    cmd_pkt[2] = (uint8_t)(m_value & 0x00FF);
    cmd_pkt[3] = (uint8_t)((m_value >> 8) & 0x00FF);

    hpi_ble_send_data(cmd_pkt, 4);
}

void cmd_thread(void)
{
    LOG_DBG("CMD Thread starting");

    struct hpi_cmd_data_obj_t rx_cmd_data_obj;

    for (;;)
    {
        k_msgq_get(&q_cmd_msg, &rx_cmd_data_obj, K_FOREVER);

        LOG_DBG("Recd BLE Packet len: %d", rx_cmd_data_obj.data_len);
        for (int i = 0; i < rx_cmd_data_obj.data_len; i++)
        {
            printk("%02X ", rx_cmd_data_obj.data[i]);
        }
        printk("\n");
        hpi_decode_data_packet(rx_cmd_data_obj.data, rx_cmd_data_obj.data_len);

        k_sleep(K_MSEC(1000));
    }
}

#define CMD_THREAD_STACKSIZE 2048
#define CMD_THREAD_PRIORITY 7

K_THREAD_DEFINE(cmd_thread_id, CMD_THREAD_STACKSIZE, cmd_thread, NULL, NULL, NULL, CMD_THREAD_PRIORITY, 0, 0);
