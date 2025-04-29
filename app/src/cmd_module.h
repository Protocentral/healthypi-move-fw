#pragma once

#define CES_CMDIF_PKT_START_1 0x0A
#define CES_CMDIF_PKT_START_2 0xFA
#define CES_CMDIF_PKT_STOP_1 0x00
#define CES_CMDIF_PKT_STOP_2 0x0B

enum cmdsm_state
{
    CMD_SM_STATE_INIT = 0,
    CMD_SM_STATE_SOF1_FOUND,
    CMD_SM_STATE_SOF2_FOUND,
    CMD_SM_STATE_PKTLEN_FOUND,
};

enum cmdsm_index
{
    CES_CMDIF_IND_LEN = 2,
    CES_CMDIF_IND_LEN_MSB,
    CES_CMDIF_IND_PKTTYPE,
    CES_CMDIF_PKT_OVERHEAD,
};

enum hpi_cmds
{
    HPI_CMD_GET_DEVICE_STATUS = 0x40,
    HPI_CMD_SET_DEVICE_TIME = 0x41,
    HPI_CMD_DEVICE_RESET = 0x42,
    HPI_CMD_PAIR_DEVICE = 0x43,
    HPI_CMD_UNPAIR_DEVICE = 0x44,
    HPI_CMD_PAIR_CHECK_PIN = 0x45,

    HPI_CMD_LOG_GET_INDEX = 0x50, //No arguments
    HPI_CMD_LOG_GET_FILE = 0x51,       //Needs session ID (uint16) as argument
    HPI_CMD_LOG_DELETE = 0x52,    //Needs session ID (uint16) as argument
    HPI_CMD_LOG_WIPE_ALL = 0x53,  //No arguments
    HPI_CMD_LOG_GET_COUNT = 0x54, //No arguments

    HPI_CMD_START_BPT_CAL = 0x60, //Needs Sys/Diastolic (as uint8/uint8) as argument
    
};

enum cmdif_pkt_type
{
    CES_CMDIF_TYPE_CMD = 0x01,
    CES_CMDIF_TYPE_DATA = 0x02,
    CES_CMDIF_TYPE_STATUS = 0x03,
    CES_CMDIF_TYPE_PROGRESS = 0x04,

    CES_CMDIF_TYPE_LOG_IDX = 0x05,
    CES_CMDIF_TYPE_CMD_RSP = 0x06,
};

enum ble_status
{
    BLE_STATUS_CONNECTED,
    BLE_STATUS_DISCONNECTED,
    BLE_STATUS_CONNECTING,
};

#define MAX_MSG_SIZE 32

struct hpi_cmd_data_obj_t
{
    uint8_t pkt_type;
    uint8_t data_len;
    uint8_t data[MAX_MSG_SIZE];
};

void cmdif_send_ble_data(uint8_t *m_data, uint8_t m_data_len);
void hpi_cmdif_send_ble_cmd_rsp(uint8_t m_cmd, uint16_t m_value);
void cmdif_send_ble_data_idx(uint8_t *m_data, uint8_t m_data_len);