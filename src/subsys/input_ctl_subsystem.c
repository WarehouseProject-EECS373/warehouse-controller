#include "input_ctl_subsystem.h"

#include <stm32f4xx_hal.h>
#include <stm/stm32f4xx.h>
#include <stdlib.h>
#include <string.h>

#include <os.h>
#include <os_msg.h>

#include "app_defs.h"




// only allow button to be pressed once every 100ms
#define USER_BUTTON_DEBOUNCE_TIME 100

#define USER_BUTTON_PORT GPIOC
#define USER_BUTTON_PIN  GPIO_PIN_13
#define UART_RX_BUFFER_SIZE  24//need length of 1 digit qr code read


#define DEPOSIT 0
#define FETCH 1
#define MSG_DISPATCH_ID 1

#define LED_PORT GPIOB 
#define LED_DROPOFF_PIN GPIO_PIN_13
#define LED_PICKUP_PIN GPIO_PIN_14
#define LED_READ_ERROR_PIN GPIO_PIN_15
#define READ_ERROR_INDICATOR_PERIOD 2000

#define NUM_BAYS 5
#define NUM_AISLES 2
#define BAYS_IN_AISLE_1 2
#define BAYS_IN_AISLE_2 3

static TimedEventSimple_t read_error_periodic_event;
static Message_t          read_error_periodic_msg = {.id = QR_READ_ERROR_MSG_ID,
                                                      .msg_size = sizeof(Message_t)};

static uint8_t ready_to_dispatch = 1;
static uint8_t controller_state = DEPOSIT;

static volatile uint8_t rx_buffer[UART_RX_BUFFER_SIZE];
static volatile uint32_t rx_buffer_count = 0;

static uint32_t qr_count = 0;
static uint8_t qr_input[UART_RX_BUFFER_SIZE];

/*
static QR_t QR_code_queue[30];
static uint8_t QR_size = 0;
static void QR_push(uint8_t code)
{
    QR_code_queue[QR_size].qr = code;
    QR_code_queue[QR_size].state = controller_state;
    ++QR_size;
}
static void QR_pop_and_dispatch(uint8_t code)
{
    //do something with front;
    ++QR_size;
}
*/

UART_HandleTypeDef uart_qr_handle;

static Package_t Bay_Array[NUM_BAYS];
static uint8_t QR_Array[10] = {99,99,99,99,99,99,99,99,99,99};
static uint8_t bays_occupied = 0;

static uint8_t get_dispatch_bay(uint8_t code);
static uint8_t get_fetch_bay(uint8_t code);
static void dispatch_fetch();
static uint8_t check_reading();
static void error();

static void error()
{
    HAL_GPIO_WritePin(LED_PORT, LED_READ_ERROR_PIN, GPIO_PIN_SET);
    TimedEventSimpleCreate(&read_error_periodic_event, &input_ctl_ss_ao, &read_error_periodic_msg,
                                            READ_ERROR_INDICATOR_PERIOD, TIMED_EVENT_SINGLE_TYPE);
    SchedulerAddTimedEvent(&read_error_periodic_event);
}

static uint8_t check_reading()
{
    uint8_t flag = 0;
    
    if (qr_count < 10 || qr_count > 11)
    {
        flag = 1;
    }

    if (!flag)
    {
        if (!((qr_input[5] - qr_input[4]) == 0 && (qr_input[5] - qr_input[6]) == 0))
        {
            flag = 1;
        }
    }

    if (!flag)
    {
        for(uint8_t i = 4; i < 7; ++i)
        {
            if (qr_input[i] < 49 || qr_input[i] > 54)
            {
                flag = 1; 
            }
        }
    }

    if ((bays_occupied == NUM_BAYS && controller_state == DEPOSIT) || (bays_occupied == 0 && controller_state == FETCH))
    {
        flag = 1;
    }
    
    if (flag)
    {
        error();
        return 0;
    }
    else
    {
        return 1;
    }
}

static void dispatch_fetch()
{
    if (!check_reading())
    {
        return;
    }
    
    char num[] = {(char)qr_input[5], '\n'}; 
    uint8_t qr_code = (uint8_t)atoi(num);
    if (DEPOSIT == controller_state)
    {
        //TODO: change to dispatch deposit msg
        
        //uint8_t bay = get_dispatch_bay(qr_code);
        uint8_t bay = qr_code - 1;
        if (bay == 99)
        {
            error();
            return;
        }
        UartSmallPacketMessage_t dmsg;
        dmsg.base.id = UART_SMALL_PACKET_MSG_ID;
        dmsg.base.msg_size = sizeof(UartSmallPacketMessage_t);
        dmsg.length = 4;
        dmsg.payload[0] = MSG_DISPATCH_ID;
        dmsg.payload[1] = Bay_Array[bay].aisle_id;
        dmsg.payload[2] = Bay_Array[bay].bay_id;
        dmsg.payload[3] = 0;
        if (ready_to_dispatch)
        {
            //ready_to_dispatch = 0;
            MsgQueuePut(&comms_ss_ao, &dmsg);
        }
        else
        {
            ;//add deposit message to queue
        }
    }
    else
    {
        uint8_t bay = qr_code - 1;
        //uint8_t bay = get_fetch_bay(qr_code);
        if (bay == 99)
        {
            error();
            return;
        }
        UartSmallPacketMessage_t dmsg;
        dmsg.base.id = UART_SMALL_PACKET_MSG_ID;
        dmsg.base.msg_size = sizeof(UartSmallPacketMessage_t);
        dmsg.length = 4;
        dmsg.payload[0] = MSG_DISPATCH_ID;
        dmsg.payload[1] = Bay_Array[bay].aisle_id;
        dmsg.payload[2] = Bay_Array[bay].bay_id;
        dmsg.payload[3] = 1;
        if (ready_to_dispatch)
        {
            //ready_to_dispatch = 0;
            MsgQueuePut(&comms_ss_ao, &dmsg);
        }
        else
        {
            ;//add fetch msg to queue
        }

    }

}

static uint8_t get_dispatch_bay(uint8_t code)
{
    if (QR_Array[code] != 99 && Bay_Array[QR_Array[code]].occupied)
    {
        return 99;
    }
    uint8_t bay = 0;
    ++bays_occupied;

    while (Bay_Array[bay].occupied)
    {
        ++bay;
    }
    
    Bay_Array[bay].occupied = 1;
    QR_Array[code] = bay;
    
    return bay;
}

static uint8_t get_fetch_bay(uint8_t code)
{
    if (QR_Array[code] != 99 && !Bay_Array[QR_Array[code]].occupied)
    {
        return 99;
    }
    --bays_occupied;
    uint8_t bay_idx = QR_Array[code];
    Bay_Array[bay_idx].occupied = 0;
    return bay_idx;
}

static void Bay_Array_Init()
{
    uint8_t aisle = 1;
    uint8_t bay_array_idx = 0;
    uint8_t bay;
    uint8_t num_bays;
    
    while(aisle <= NUM_AISLES)
    {
        if (aisle == 1)
        {
            num_bays = BAYS_IN_AISLE_1;
        }
        else
        {
            num_bays = BAYS_IN_AISLE_2;
        }
        for (uint8_t i = 0; i < num_bays; ++i)
        {
            if (num_bays == BAYS_IN_AISLE_1)
            {
                bay = 1 + i*2;
            }
            else
            {
                bay = i*2;
            }
            Package_t package = {.occupied = 0, .aisle_id = aisle, .bay_id = bay};
            Bay_Array[bay_array_idx] = package;
            ++bay_array_idx;
        }
        ++aisle;
    }

}

__attribute__((__interrupt__)) extern void EXTI15_10_IRQHandler(void)
{
    // button debouncing
    static bool time_set = false;
    static uint32_t last_time = 0;

    OS_ISR_ENTER(&os);

    // if triggered by GPIO_13
    if(__HAL_GPIO_EXTI_GET_FLAG(USER_BUTTON_PIN) != RESET)
    {
        // check debounce
        if(!time_set || USER_BUTTON_DEBOUNCE_TIME < (OSGetTime() - last_time))
        {
            if (controller_state == DEPOSIT)
            {
                controller_state = FETCH;
                HAL_GPIO_WritePin(LED_PORT, LED_DROPOFF_PIN, GPIO_PIN_RESET);
                HAL_GPIO_WritePin(LED_PORT, LED_PICKUP_PIN, GPIO_PIN_SET);
            }
            else
            {
                controller_state = DEPOSIT;
                HAL_GPIO_WritePin(LED_PORT, LED_DROPOFF_PIN, GPIO_PIN_SET);
                HAL_GPIO_WritePin(LED_PORT, LED_PICKUP_PIN, GPIO_PIN_RESET);
            }

        }

        // must clear interrupt
        __HAL_GPIO_EXTI_CLEAR_IT(USER_BUTTON_PIN);
    }

    // always exit to tail chain scheduler
    OS_ISR_EXIT(&os);
}
__attribute__((__interrupt__)) extern void USART1_IRQHandler()
{
    OS_ISR_ENTER();

    // if RX buffer Not Empty (RXNE)
    if(__HAL_UART_GET_IT_SOURCE(&uart_qr_handle, UART_IT_RXNE) != RESET)
    {
        // add byte in UART data register to rx buffer
        rx_buffer[rx_buffer_count++] = (uint8_t)(USART1->DR & 0xFF);
        
        // don't fill or modify buffer while unpacking it
        if (rx_buffer[rx_buffer_count - 1] == 10 || rx_buffer_count >= 15)
        {
            DISABLE_INTERRUPTS();
            for (uint8_t i = 0; i < 15; ++i)
            {
                qr_input[i] = rx_buffer[i];
            }
            Message_t pqrmsg = {.id = PROCESS_QR_BUFFER_MSG_ID, .msg_size = sizeof(Message_t)};
            MsgQueuePut(&input_ctl_ss_ao, &pqrmsg);
            ENABLE_INTERRUPTS();
            qr_count = rx_buffer_count;
            rx_buffer_count = 0;
        }

    }

    HAL_NVIC_ClearPendingIRQ(USART1_IRQn);

    OS_ISR_EXIT();
}


extern void InputEventHandler(Message_t* msg)
{
    if(msg->id == PROCESS_QR_BUFFER_MSG_ID)
    {
        dispatch_fetch();
    }

    else if(msg->id == QR_READ_ERROR_MSG_ID)
    {
        HAL_GPIO_WritePin(LED_PORT, LED_READ_ERROR_PIN, GPIO_PIN_RESET);
    }
    else if(msg->id == AISLES_FREE_MSG_ID)
    {
        ready_to_dispatch = 1;
    }
}

extern void ITCTL_Init()
{
    UNUSED(get_fetch_bay);
    UNUSED(get_dispatch_bay);
    // initialize blue user button interrupt
    GPIO_InitTypeDef gpio_cfg;
    gpio_cfg.Pin = USER_BUTTON_PIN;
    gpio_cfg.Mode = GPIO_MODE_IT_FALLING;
    gpio_cfg.Pull = GPIO_NOPULL;
    gpio_cfg.Speed = GPIO_SPEED_FREQ_HIGH;

    HAL_GPIO_Init(USER_BUTTON_PORT, &gpio_cfg);

    // set blue user push button interrupt priority
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

    // initialize indicator LEDs
    gpio_cfg.Pin = LED_DROPOFF_PIN | LED_PICKUP_PIN | LED_READ_ERROR_PIN;
    gpio_cfg.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_cfg.Pull = GPIO_NOPULL;
    gpio_cfg.Speed = GPIO_SPEED_LOW;

    HAL_GPIO_Init(LED_PORT, &gpio_cfg);
    HAL_GPIO_WritePin(LED_PORT, LED_DROPOFF_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LED_PORT, LED_PICKUP_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_PORT, LED_READ_ERROR_PIN, GPIO_PIN_RESET);



    // UART qr Init
    gpio_cfg.Pin = GPIO_PIN_9|GPIO_PIN_10;
    gpio_cfg.Mode = GPIO_MODE_AF_PP;
    gpio_cfg.Pull = GPIO_NOPULL;
    gpio_cfg.Speed = GPIO_SPEED_FREQ_LOW;
    gpio_cfg.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &gpio_cfg);



    uart_qr_handle.Instance = USART1;
    uart_qr_handle.Init.BaudRate = 9600;
    uart_qr_handle.Init.WordLength = UART_WORDLENGTH_8B;
    uart_qr_handle.Init.StopBits = UART_STOPBITS_1;
    uart_qr_handle.Init.Parity = UART_PARITY_NONE;
    uart_qr_handle.Init.Mode = UART_MODE_TX_RX;
    uart_qr_handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    uart_qr_handle.Init.OverSampling = UART_OVERSAMPLING_16;

    HAL_UART_Init(&uart_qr_handle);
    HAL_NVIC_SetPriority(USART1_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    __HAL_UART_ENABLE_IT(&uart_qr_handle, UART_IT_RXNE);
    
    Bay_Array_Init();
}
