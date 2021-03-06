#include "comms_subsystem.h"

#include <stdint.h>

#include <stm/stm32f4xx.h>
#include <stm32f4xx_hal.h>

#include "app_defs.h"

#include <os.h>
#include <os_mem.h>

#include <stcp.h>

#define UART_TX_TIMEOUT 10

#define UART_RX_BUFFER_SIZE 32

#define MESSAGE_ID_IDX 0
#define EXPECTED_BAY_EXIT_MSG_LENGTH 0

#define EXPECTED_GET_P_LENGTH       3
#define EXPECTED_SET_P_LENGTH       7


STCPEngine_t stcp_engine;

// NOTE: if we need a unique ID for each zumo
// have the warehouse controller assign IDs in
// the very beginning by sending some message
// instead of hardcoding it in source.

UART_HandleTypeDef uart_zigby_handle;

static volatile uint8_t rx_buffer[UART_RX_BUFFER_SIZE];
static volatile uint32_t rx_buffer_count = 0;

static void ProcessSmallMessage(UartSmallPacketMessage_t* msg);
static void ProcessLargeMessage(UartLargePacketMessage_t* msg);
static STCPStatus_t SendMessage(void* buffer, uint16_t length, void* instance_data);
static STCPStatus_t UnpackMessage(void* buffer, uint16_t length, void* instance_data);

__attribute__((__interrupt__)) extern void USART6_IRQHandler()
{
    OS_ISR_ENTER();

    // if RX buffer Not Empty (RXNE)
    if(__HAL_UART_GET_IT_SOURCE(&uart_zigby_handle, UART_IT_RXNE) != RESET)
    {
        // add byte in UART data register to rx buffer
        rx_buffer[rx_buffer_count++] = (uint8_t)(USART6->DR & 0xFF);
        
        if (rx_buffer_count == 1 && rx_buffer[0] != HEADER)
        {
            rx_buffer_count = 0;
        }

        // all incoming packets are terminated by 2 FOOTER characters
        if(rx_buffer_count > 2 && (UART_RX_BUFFER_SIZE <= rx_buffer_count || (rx_buffer[rx_buffer_count
         - 1] == FOOTER && rx_buffer[rx_buffer_count - 2] == FOOTER)))
        {
            // don't fill or modify buffer while unpacking it
            DISABLE_INTERRUPTS();
            StcpHandleMessage(&stcp_engine, (uint8_t*)rx_buffer, rx_buffer_count);
            ENABLE_INTERRUPTS();

            rx_buffer_count = 0;
        }
    }

    HAL_NVIC_ClearPendingIRQ(USART6_IRQn);

    OS_ISR_EXIT();
}

static STCPStatus_t UnpackMessage(void* buffer, uint16_t length, void* instance_data)
{
    uint8_t *payload = (uint8_t*)buffer;
    UNUSED(instance_data);
    if (ZUMO_BAY_EXIT_MSG_ID == payload[MESSAGE_ID_IDX])
    {
        if (EXPECTED_BAY_EXIT_MSG_LENGTH != length)
        {
            return STCP_STATUS_UNDEFINED_ERROR;
        }
        Message_t af_msg = {.id = AISLES_FREE_MSG_ID, .msg_size = sizeof(Message_t)};
        MsgQueuePut(&input_ctl_ss_ao, &af_msg);

    }

    return STCP_STATUS_SUCCESS;
}


extern void Comms_Init()
{
    STCPCallbacks_t callbacks = {.Send = SendMessage, .HandleMessage = UnpackMessage};
    stcp_engine.callbacks = callbacks;
    stcp_engine.instance_data = NULL;

    // make compiler happy, used as callbacks
    UNUSED(SendMessage);
    UNUSED(UnpackMessage);

    //UART zigby Init
    GPIO_InitTypeDef gpio_cfg;

    gpio_cfg.Pin = GPIO_PIN_11 | GPIO_PIN_12;
    gpio_cfg.Mode = GPIO_MODE_AF_PP;
    gpio_cfg.Pull = GPIO_PULLUP;
    gpio_cfg.Speed = GPIO_SPEED_FREQ_LOW;
    gpio_cfg.Alternate = GPIO_AF8_USART6;

    HAL_GPIO_Init(GPIOA, &gpio_cfg);

    uart_zigby_handle.Instance = USART6;
    uart_zigby_handle.Init.BaudRate = 115200;
    uart_zigby_handle.Init.Mode = UART_MODE_TX_RX;
    uart_zigby_handle.Init.WordLength = UART_WORDLENGTH_8B;
    uart_zigby_handle.Init.StopBits = UART_STOPBITS_1;
    uart_zigby_handle.Init.Parity = UART_PARITY_NONE;
    uart_zigby_handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;

    HAL_UART_Init(&uart_zigby_handle);

    HAL_NVIC_SetPriority(USART6_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USART6_IRQn);
    __HAL_UART_ENABLE_IT(&uart_zigby_handle, UART_IT_RXNE);

}

extern void CommsEventHandler(Message_t* msg)
{
    if(UART_SMALL_PACKET_MSG_ID == msg->id || OS_DEBUG_MSG_ID == msg->id)
    {
        ProcessSmallMessage((UartSmallPacketMessage_t*)msg);
    }
    else if(UART_LARGE_PACKET_MSG_ID == msg->id)
    {
        ProcessLargeMessage((UartLargePacketMessage_t*)msg);
    }
}

static void ProcessSmallMessage(UartSmallPacketMessage_t* msg)
{
    StcpWrite(&stcp_engine, msg->payload, msg->length);
}

static void ProcessLargeMessage(UartLargePacketMessage_t* msg)
{
    uint8_t* buffer = OSMemoryBlockGet(msg->mem_key);
    StcpWrite(&stcp_engine, buffer, msg->length);
    OSMemoryFreeBlock(msg->mem_key);
}

static STCPStatus_t SendMessage(void* buffer, uint16_t length, void* instance_data)
{
    UNUSED(instance_data);
    HAL_UART_Transmit(&uart_zigby_handle, (uint8_t*)buffer, length, UART_TX_TIMEOUT);

    return STCP_STATUS_SUCCESS;
}

