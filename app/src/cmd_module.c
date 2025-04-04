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
// #include "tdcs3.h"

// #define ESP_UART_DEVICE_NODE DT_ALIAS(esp_uart)

LOG_MODULE_REGISTER(hpi_cmd_module, LOG_LEVEL_DBG);
#define MAX_MSG_SIZE 32

#define CMDIF_BLE_UART_MAX_PKT_SIZE 128 // Max Packet Size in bytes

K_SEM_DEFINE(sem_ble_connected, 0, 1);
K_SEM_DEFINE(sem_ble_disconnected, 0, 1);

K_MSGQ_DEFINE(q_cmd_msg, sizeof(struct hpi_cmd_data_obj_t), 128, 4);

static volatile int ecs_rx_state = 0;

int cmd_pkt_len;
int cmd_pkt_pos_counter, cmd_pkt_data_counter;
int cmd_pkt_pkttype;
uint8_t ces_pkt_data_buffer[1000]; // = new char[1000];
volatile bool cmd_module_ble_connected = false;

extern int global_dev_status;

void hpi_decode_data_packet(uint8_t *in_pkt_buf, uint8_t pkt_len)
{
    uint8_t cmd_cmd_id = in_pkt_buf[0];

    LOG_DBG("Recd Command: %X", cmd_cmd_id);

    switch (cmd_cmd_id)
    {
    case HPI_CMD_GET_DEVICE_STATUS:
        LOG_DBG("Recd Get Device Status Command");
        // cmdif_send_ble_device_status_response();
        break;
    case HPI_CMD_SET_DEVICE_TIME:
        LOG_DBG("Recd Set Device Time Command");
        hw_rtc_set_time(in_pkt_buf[1], in_pkt_buf[2], in_pkt_buf[3], in_pkt_buf[4], in_pkt_buf[5], in_pkt_buf[6]);
        break;
    case HPI_CMD_DEVICE_RESET:
        LOG_DBG("Recd Reset Command");
        LOG_DBG("Rebooting...");
        k_sleep(K_MSEC(1000));
        sys_reboot(SYS_REBOOT_COLD);
        break;
    
    // File System Commands
    case HPI_CMD_LOG_GET_COUNT:
        printk("Recd Get Session Log Count Command\n");
        // cmdif_send_ble_command(HPI_CMD_GET_SESSION_LOG_COUNT);
        uint16_t log_count = log_get_count(in_pkt_buf[1]);
        //log_count=0x1234;
        hpi_cmdif_send_ble_cmd_rsp(HPI_CMD_LOG_GET_COUNT, log_count);
        //log_get_count();
        break;
    case HPI_CMD_LOG_GET_INDEX:
        printk("Recd Get Session Log Index Command\n");
        // cmdif_send_ble_command(HPI_CMD_GET_SESSION_LOG_INDEX);
        log_get_index(in_pkt_buf[1]);
        break;
    case HPI_CMD_LOG_GET_FILE:
        printk("Recd Get Session Log Command\n");
        uint16_t sess_id = (in_pkt_buf[2] | (in_pkt_buf[1] << 8));
        log_get(sess_id);
        break;
    case HPI_CMD_LOG_DELETE:
        printk("Recd Session Log Delete Command\n");
        log_delete((in_pkt_buf[1] | (in_pkt_buf[2] << 8)));
        break;
    case HPI_CMD_LOG_WIPE_ALL:
        printk("Recd Session Log Wipe Command\n");
        log_wipe_all();
        break;
    default:
        LOG_DBG("Recd Unknown Command");
        break;
    }
}

uint8_t data_pkt[256];

void cmdif_send_ble_data(uint8_t *m_data, uint8_t m_data_len)
{
    printk("Sending BLE Data: %d\n", m_data_len);

    data_pkt[0] = CES_CMDIF_TYPE_DATA;
    for (int i = 0; i < m_data_len; i++)
    {
        data_pkt[1 + i] = m_data[i];
    }
    hpi_ble_send_data(data_pkt, 1 + m_data_len);
}
// Send index of all session logs
void cmdif_send_ble_data_idx(uint8_t *m_data, uint8_t m_data_len)
{
    printk("Sending BLE Index Data: %d\n", m_data_len);
    uint8_t cmd_pkt[1 + m_data_len];

    cmd_pkt[0] = CES_CMDIF_TYPE_LOG_IDX;
    for (int i = 0; i < m_data_len; i++)
    {
        cmd_pkt[1 + i] = m_data[i];
        printk("%02X ", m_data[i]);
        //k_sleep(K_MSEC(10));
    }

    printk("\n");

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
