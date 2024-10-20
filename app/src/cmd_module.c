#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/reboot.h>

#include "cmd_module.h"
#include "hw_module.h"

#include "fs_module.h"
// #include "tdcs3.h"

//#define ESP_UART_DEVICE_NODE DT_ALIAS(esp_uart)

LOG_MODULE_REGISTER(hpi_cmd_module);
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
        //cmdif_send_ble_device_status_response();
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
    default:
        LOG_DBG("Recd Unknown Command");
        break;
    }
}

// TODO: implement BLE UART
void cmdif_send_ble_data(const char *in_data_buf, size_t in_data_len)
{
    uint8_t dataPacket[50];

    dataPacket[0] = CES_CMDIF_PKT_START_1;
    dataPacket[1] = CES_CMDIF_PKT_START_2;
    dataPacket[2] = in_data_len;
    dataPacket[3] = 0;
    dataPacket[4] = CES_CMDIF_TYPE_DATA;

    for (int i = 0; i < in_data_len; i++)
    {
        dataPacket[i + 5] = in_data_buf[i];
        //printk("Data %x: %d\n", i, in_data_buf[i]);
    }

    dataPacket[in_data_len + 5] = CES_CMDIF_PKT_STOP_1;
    dataPacket[in_data_len + 6] = CES_CMDIF_PKT_STOP_2;

    //printk("Sending UART data: %d\n", in_data_len);

    for (int i = 0; i < (in_data_len + 7); i++)
    {
        //uart_poll_out(esp_uart_dev, dataPacket[i]);
    }
}

void cmdif_send_ble_device_status_response(void)
{
    //cmdif_send_ble_status(WISER_CMD_GET_DEVICE_STATUS, global_dev_status);
}

void cmdif_send_ble_command(uint8_t m_cmd)
{
    LOG_DBG("Sending BLE Command: %X", m_cmd);
    uint8_t cmd_pkt[8];
    cmd_pkt[0] = CES_CMDIF_PKT_START_1;
    cmd_pkt[1] = CES_CMDIF_PKT_START_2;
    cmd_pkt[2] = 0x01;
    cmd_pkt[3] = 0x00;
    cmd_pkt[4] = CES_CMDIF_TYPE_CMD;
    cmd_pkt[5] = m_cmd;
    cmd_pkt[6] = CES_CMDIF_PKT_STOP_1;
    cmd_pkt[7] = CES_CMDIF_PKT_STOP_2;

    for (int i = 0; i < 8; i++)
    {
        //uart_poll_out(esp_uart_dev, cmd_pkt[i]);
    }
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

#define CMD_THREAD_STACKSIZE 1024
#define CMD_THREAD_PRIORITY 7

K_THREAD_DEFINE(cmd_thread_id, CMD_THREAD_STACKSIZE, cmd_thread, NULL, NULL, NULL, CMD_THREAD_PRIORITY, 0, 0);
