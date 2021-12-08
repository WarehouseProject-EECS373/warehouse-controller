#pragma once

#include <os.h>

// share os with all
extern OS_t os;

//*****************************************************************/
// Custom Messages
//*****************************************************************/


typedef struct UartSmallPacketMessage_s
{
    Message_t base;
    uint8_t length;
    uint8_t payload[8];
} UartSmallPacketMessage_t;

typedef struct UartLargePacketMessage_s
{
    Message_t base;
    uint16_t length;
    uint16_t mem_key;
} UartLargePacketMessage_t;
typedef struct DispatchMessage_s

{
    Message_t base;
    uint8_t   bay_id;
    uint8_t   aisle_id;
    uint8_t   is_pickup;
} DispatchMessage_t;

typedef struct PropertyGetSetMessage_s
{
    Message_t base;
    uint16_t p_id;
    uint8_t value[4];
} PropertyGetSetMessage_t;

//*****************************************************************/
// MESSAGE IDs
//*****************************************************************/

// system
#define HEARTBEAT_MSG_ID   0x2

#define PROCESS_QR_BUFFER_MSG_ID 0x30
#define QR_READ_ERROR_MSG_ID     0x31

#define PUSH_BUTTON_PRESSED_MSG_ID          0x61

// comms subsystem
#define UART_SMALL_PACKET_MSG_ID    0x81
#define UART_LARGE_PACKET_MSG_ID    0x82
#define OS_DEBUG_MSG_ID             0x83

//*****************************************************************/
// Property Management
//*****************************************************************/

 
#define MSG_P_GET_ID            0x10
#define MSG_P_GET_RESPONSE_ID   0x11

#define MSG_P_SET_ID            0x20

#define GET_PROPERTY_MSG_ID     0x220
#define SET_PROPERTY_MSG_ID     0x221

#define GET_PROPERTY(var, vartype) do                       \
{                                                           \
    UartSmallPacketMessage_t msg;                           \
    msg.base.id = UART_SMALL_PACKET_MSG_ID;                 \
    msg.base.msg_size = sizeof(UartSmallPacketMessage_t);   \
    msg.payload[0] = MSG_P_GET_RESPONSE_ID;                 \
    msg.length = 5;                                         \
    vartype* start = (vartype*)(msg.payload + 1);           \
    *start = var;                                           \
    MsgQueuePut(&comms_ss_ao, &msg);                        \
} while(false);                                             \

#define SET_PROPERTY(msg, var, vartype) do {                \
     var = *((vartype*)(msg->value));                       \
} while (false);                                            \

#define GET_SET_PROPERTY(msg, var, vartype) do {            \
    if (GET_PROPERTY_MSG_ID == msg->base.id)                \
    {                                                       \
        GET_PROPERTY(var, vartype);                         \
    }                                                       \
    else if (SET_PROPERTY_MSG_ID == msg->base.id)           \
    {                                                       \
        SET_PROPERTY(msg, var, vartype);                    \
    }                                                       \
} while(false);                                             \


//*****************************************************************/
// Active Object Extern Declarations and Configuration
//*****************************************************************/

// message queue sizes
#define HEARTBEAT_QUEUE_SIZE     1
#define INPUT_CTL_SS_QUEUE_SIZE  4
#define COMMS_QUEUE_SIZE         8

#define WATCHDOG_AO_ID      0x0
#define INPUT_CTL_AO_ID     0x2
#define COMMS_AO_ID         0x3

ACTIVE_OBJECT_EXTERN(watchdog_ao, HEARTBEAT_QUEUE_SIZE)
ACTIVE_OBJECT_EXTERN(input_ctl_ss_ao, INPUT_CTL_SS_QUEUE_SIZE)
ACTIVE_OBJECT_EXTERN(comms_ss_ao, COMMS_QUEUE_SIZE)

