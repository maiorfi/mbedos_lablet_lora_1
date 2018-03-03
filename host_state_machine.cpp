#include "mbed.h"

#include "host_protocol_impl.h"
#include "host_state_machine.h"

#define STATE_MACHINE_STALE_STATE_TIMEOUT               (60000)      // in ms

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
 
/*
 *  Global variables declarations
 */

Mutex host_reply_cond_var_mutex;
ConditionVariable host_reply_cond_var(host_reply_cond_var_mutex);
HostReplyOutcomes_t host_reply_outcome;
uint16_t host_reply_payload;

host_notify_request_payload_callback_t host_state_machine_notify_request_payload_callback;
host_notify_request_payload_and_get_reply_payload_callback_t host_state_machine_notify_request_payload_and_get_reply_payload_callback;

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

static void notify_request_payload(uint16_t requestPayload)
{
    if(host_state_machine_notify_request_payload_callback) host_state_machine_notify_request_payload_callback(requestPayload);
}

static uint16_t notify_request_payload_and_get_reply_payload(uint16_t requestPayload)
{
    if(host_state_machine_notify_request_payload_and_get_reply_payload_callback) return host_state_machine_notify_request_payload_and_get_reply_payload_callback(requestPayload);
    
    return 0;
}

void host_event_proc_communication_cycle()
{
    int elapsed_ms=s_state_timer.read_ms();

    if(elapsed_ms > STATE_MACHINE_STALE_STATE_TIMEOUT)
    {
        printf("...(host state-machine timeout, resetting to initial state)...\n" );

        setState(INITIAL);
    }

    uint16_t requestPayload;
    uint16_t replyPayload;

    char dumpBuffer[HOST_MESSAGES_BUFFER_SIZE];

    switch( getState() )
    {
        case INITIAL:

            printf("--- HOST INITIAL STATE ---\n");

            host_protocol_reset();
            
            setState(RX_WAITING_FOR_REQUEST);

            break;

        case RX_WAITING_FOR_REQUEST:
            //printf("...(waiting for request)...\n" );
            break;

        case RX_WAITING_FOR_REPLY:
            //printf("...(waiting for reply)...\n" );
            break;

        case RX_DONE_RECEIVED_REQUEST:

            host_protocol_fill_with_rx_buffer_dump(dumpBuffer, HOST_MESSAGES_BUFFER_SIZE);

            printf("*** HOST REQUEST RECEIVED : '%s' ***\n", dumpBuffer);
            
            requestPayload = host_protocol_get_latest_received_request_payload();

            if(!host_protocol_should_i_reply_to_latest_received_request())
            {
                printf("...but I should not reply to host\n");

                notify_request_payload(requestPayload);

                setState(INITIAL);
                
                break;
            }

            printf("...AND I SHOULD REPLY TO HOST...\n");

            replyPayload = notify_request_payload_and_get_reply_payload(requestPayload);
            
            // Send the REPLY frame
            host_protocol_send_reply_command(replyPayload);

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

            break;

        case TX_DONE_SENT_REPLY:

            printf("...REPLY SENT TO HOST\n" ); 
           
            setState(INITIAL);
            
            break;
    }
}

void host_state_machine_send_request(uint16_t argCounter)
{
    uint16_t bufferSize=HOST_MESSAGES_BUFFER_SIZE;
    uint8_t buffer[HOST_MESSAGES_BUFFER_SIZE];

    if(getState() != RX_WAITING_FOR_REQUEST) return;

    // Send the REQUEST frame
    host_protocol_fill_create_request_buffer(buffer, bufferSize, argCounter);

    char dumpBuffer[HOST_MESSAGES_BUFFER_SIZE];

    host_protocol_fill_with_tx_buffer_dump(dumpBuffer, buffer, HOST_MESSAGES_BUFFER_SIZE);

    printf("\n*** SEND HOST REQUEST : '%s' ***\n", dumpBuffer);

    host_protocol_send_request_command(argCounter);

    setState(TX_DONE_SENT_REQUEST);
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

    return 0;
}