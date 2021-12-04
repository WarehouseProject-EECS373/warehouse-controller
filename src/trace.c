#include "trace.h"

#include <os.h>
#include <os_mem.h>

#include "app_defs.h"

// debug message configuration
#define MSG_OS_TRACE_ID        0x2
#define MSG_OS_TRACE_SIZE      7

#ifdef OS_TRACE_ENABLED
extern void DebugPrint(uint8_t ao_id, uint32_t msg_id, uint8_t is_queue)
{
    // don't log debug messages (will create an infinite loop)
    if (OS_DEBUG_MSG_ID == msg_id)
    {
        return;
    }

    UartSmallPacketMessage_t debug_msg;

    // create and send debug message packet to comms event handler
    debug_msg.base.id = OS_DEBUG_MSG_ID;
    debug_msg.base.msg_size = sizeof(UartSmallPacketMessage_t);
    debug_msg.length = MSG_OS_TRACE_SIZE;

    debug_msg.payload[0] = MSG_OS_TRACE_ID;
    debug_msg.payload[1] = ao_id;
    debug_msg.payload[2] = is_queue;

   uint32_t *id_start = (uint32_t*)(debug_msg.payload + 3);
   *id_start = msg_id;

   MsgQueuePut(&comms_ss_ao, &debug_msg);
}
#endif
