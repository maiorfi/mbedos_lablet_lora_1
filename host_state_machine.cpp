#include "mbed.h"

#include "host_protocol_impl.h"
#include "host_state_machine.h"

#define WAIT_FOR_REPLY_TIMEOUT                          (2000)      // in ms
#define STATE_MACHINE_STALE_STATE_TIMEOUT               (WAIT_FOR_REPLY_TIMEOUT+500)      // in ms

#define HOST_MESSAGES_BUFFER_SIZE 32

/*
 *  Global variables declarations
 */
typedef enum
{
    INITIAL,

    RX_WAITING_FOR_REQUEST,
    RX_WAITING_FOR_REPLY,

    RX_DONE_RECEIVED_REQUEST,
    RX_DONE_RECEIVED_REPLY,

    TX_DONE_SENT_REQUEST,
    TX_DONE_SENT_REPLY

} HostAppStates_t;
 
static HostAppStates_t State = INITIAL;

static Timer s_state_timer;

static Timer s_wait_for_reply_timer;
 
/*
 *  Global variables declarations
 */

Mutex host_reply_cond_var_mutex;
ConditionVariable host_reply_cond_var(host_reply_cond_var_mutex);
HostReplyOutcomes_t host_reply_outcome;
uint16_t host_reply_payload;

host_notify_request_callback_t host_state_machine_notify_request_callback;
host_notify_request_and_get_reply_callback_t host_state_machine_notify_request_and_get_reply_callback;

static inline HostAppStates_t getState() { return State;}
static HostAppStates_t setState(HostAppStates_t newState) { HostAppStates_t previousState=State; State=newState; s_state_timer.reset(); return previousState;}

static inline void updateAndNotifyConditionOutcome(HostReplyOutcomes_t outcome, uint16_t payload)
{
    host_reply_cond_var_mutex.lock();
    host_reply_outcome=outcome;
    host_reply_payload=payload;
    host_reply_cond_var.notify_all();
    host_reply_cond_var_mutex.unlock();
}

static void notify_request(uint8_t requestSourceAddress, uint16_t requestPayload)
{
    if(host_state_machine_notify_request_callback) host_state_machine_notify_request_callback(requestSourceAddress, requestPayload);
}

static uint16_t notify_request_and_get_reply(uint8_t requestSourceAddress, uint16_t requestPayload)
{
    if(host_state_machine_notify_request_and_get_reply_callback) return host_state_machine_notify_request_and_get_reply_callback(requestSourceAddress, requestPayload);
    
    return 0;
}

void host_event_proc_communication_cycle()
{
    int elapsed_ms=s_state_timer.read_ms();

    if(elapsed_ms > STATE_MACHINE_STALE_STATE_TIMEOUT)
    {
        //printf("...(host state-machine timeout, resetting to initial state)...\n" );

        setState(INITIAL);
    }

    uint16_t requestPayload;
    uint16_t replyPayload;
    uint8_t requestSourceAddress;
    uint8_t replyDestinationAddress;

    char dumpBuffer[HOST_MESSAGES_BUFFER_SIZE];

    uint16_t bufferSize=HOST_MESSAGES_BUFFER_SIZE;
    uint8_t buffer[HOST_MESSAGES_BUFFER_SIZE];

    switch( getState() )
    {
        case INITIAL:

            //printf("--- HOST INITIAL STATE ---\n");

            host_protocol_reset();
            
            setState(RX_WAITING_FOR_REQUEST);

            break;

        case RX_WAITING_FOR_REQUEST:
            //printf("...(waiting for host request)...\n" );
            break;

        case RX_WAITING_FOR_REPLY:
            //printf("...(waiting for host reply)...\n" );

            if(s_wait_for_reply_timer.read_ms() > WAIT_FOR_REPLY_TIMEOUT)
            {
                printf("...(timeout waiting for host reply)...\n" );

                updateAndNotifyConditionOutcome(HOST_OUTCOME_WAITING_FOR_REPLY_TIMEOUT, 0);

                setState(INITIAL);
            }

            break;

        case RX_DONE_RECEIVED_REQUEST:

            host_protocol_fill_with_rx_buffer_dump(dumpBuffer, HOST_MESSAGES_BUFFER_SIZE);

            printf("*** HOST REQUEST RECEIVED : '%s' ***\n", dumpBuffer);
            
            requestSourceAddress = host_protocol_get_latest_received_request_source_address();
            requestPayload = host_protocol_get_latest_received_request_payload();

            if(!host_protocol_should_i_reply_to_latest_received_request())
            {
                printf("...but I should not reply to host\n");

                notify_request(requestSourceAddress, requestPayload);

                setState(INITIAL);
                
                break;
            }

            printf("...AND I SHOULD REPLY TO HOST...\n");

            replyPayload = notify_request_and_get_reply(requestSourceAddress, requestPayload);
            
            // Send the REPLY frame
            host_protocol_fill_create_reply_buffer(buffer, bufferSize, replyPayload, requestSourceAddress);
            host_protocol_send_reply_command(buffer, bufferSize);

            setState(TX_DONE_SENT_REPLY);

            break;

        case RX_DONE_RECEIVED_REPLY:

            host_protocol_fill_with_rx_buffer_dump(dumpBuffer, HOST_MESSAGES_BUFFER_SIZE);

            printf("*** HOST REPLY RECEIVED ('%s') ***\n", dumpBuffer);
            
            if(host_protocol_is_latest_received_reply_right())
            {
                printf("...AND HOST REPLY IS RIGHT\n");

                replyPayload = host_protocol_get_latest_received_reply_payload();

                updateAndNotifyConditionOutcome(HOST_OUTCOME_REPLY_RIGHT, replyPayload);

                setState(INITIAL);
            }
            else
            {
                printf("...BUT HOST REPLY IS WRONG\n");

                updateAndNotifyConditionOutcome(HOST_OUTCOME_REPLY_WRONG, 0);

                setState(INITIAL);
            }

            break;

        case TX_DONE_SENT_REQUEST:

            printf("...host request sent...\n" );

            if(!host_protocol_should_i_wait_for_reply_for_latest_sent_request())
            {
                printf("...but I should not wait for host reply\n" );

                updateAndNotifyConditionOutcome(HOST_OUTCOME_REPLY_NOT_NEEDED, 0);

                setState(INITIAL);

                break;
            }

            printf("...waiting for host reply...\n" );
            
            setState(RX_WAITING_FOR_REPLY);

            s_wait_for_reply_timer.reset();

            break;

        case TX_DONE_SENT_REPLY:

            printf("...REPLY SENT TO HOST\n" ); 
           
            setState(INITIAL);
            
            break;
    }
}

HostReplyOutcomes_t host_state_machine_send_request(uint16_t argCounter, uint8_t argLoraDestinationAddress, bool argRequiresReply)
{
    uint16_t bufferSize=HOST_MESSAGES_BUFFER_SIZE;
    uint8_t buffer[HOST_MESSAGES_BUFFER_SIZE];

    if(getState() != RX_WAITING_FOR_REQUEST) return HOST_OUTCOME_INVALID_STATE;
    
    // Send the REQUEST frame
    host_protocol_fill_create_request_buffer(buffer, bufferSize, argCounter, argLoraDestinationAddress, argRequiresReply);
    
    char dumpBuffer[HOST_MESSAGES_BUFFER_SIZE];

    host_protocol_fill_with_tx_buffer_dump(dumpBuffer, buffer, HOST_MESSAGES_BUFFER_SIZE);

    printf("*** HOST SEND REQUEST : '%s' ***\n", dumpBuffer);
    
    host_protocol_send_request_command(buffer, bufferSize);

    setState(TX_DONE_SENT_REQUEST);

    return HOST_OUTCOME_PENDING;
}

void notify_command_received_callback()
{
    if(getState() == RX_WAITING_FOR_REQUEST && host_protocol_is_latest_received_command_a_request())
    {
        printf("...host request rx done...\n" );

        setState(RX_DONE_RECEIVED_REQUEST);
    }
    else if(getState() == RX_WAITING_FOR_REPLY && host_protocol_is_latest_received_command_a_reply())
    { 
        printf("...host reply rx done...\n" );

        setState(RX_DONE_RECEIVED_REPLY);
    }
    else // ricezione valida, ma arrivata in uno stato non previsto
    {   
        char dumpBuffer[HOST_MESSAGES_BUFFER_SIZE];

        host_protocol_fill_with_rx_buffer_dump(dumpBuffer, HOST_MESSAGES_BUFFER_SIZE);
        
        printf("...valid but unexpected host rx done ('%s'), ignoring...\n", dumpBuffer);
    }
}

int host_state_machine_initialize(EventQueue* eventQueue)
{
    host_protocol_initialize(eventQueue);

    host_protocol_notify_command_received_callback_instance = notify_command_received_callback;

    s_state_timer.start();
    s_wait_for_reply_timer.start();

    return 0;
}