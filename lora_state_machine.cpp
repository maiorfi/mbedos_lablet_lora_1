#include "mbed.h"

#include "sx1272-hal.h"

/* Set this flag to '1' to display debug messages on the console */
#define SX1272_DEBUG_ENABLED    1
#include "sx1272-debug.h"

#include "lora_protocol_impl.h"

#include "lora_config.h"

#include "lora_events_callbacks.h"

#include "lora_state_machine.h"

#define REQUEST_REPLY_DELAY                             150       // in ms
#define STATE_MACHINE_STALE_STATE_TIMEOUT               (RX_TIMEOUT_VALUE+500)      // in ms

#define RADIO_MESSAGES_BUFFER_SIZE                      32

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
 
    TX_WAITING_FOR_REQUEST_SENT,
    TX_WAITING_FOR_REPLY_SENT,

    TX_DONE_SENT_REQUEST,
    TX_DONE_SENT_REPLY

} AppStates_t;
 
static AppStates_t State = INITIAL;
 
/*!
 * Radio events function pointer
 */
static RadioEvents_t RadioEvents;
 
/*
 *  Global variables declarations
 */
static SX1272MB2xAS Radio( NULL );

static Timer s_state_timer;

Mutex lora_reply_cond_var_mutex;
ConditionVariable lora_reply_cond_var(lora_reply_cond_var_mutex);
ReplyOutcomes_t lora_reply_outcome;
uint16_t lora_reply_payload;

notify_request_payload_callback_t lora_state_machine_notify_request_payload_callback;
notify_request_payload_and_get_reply_payload_callback_t lora_state_machine_notify_request_payload_and_get_reply_payload_callback;

inline AppStates_t getState() { return State;}
AppStates_t setState(AppStates_t newState) { AppStates_t previousState=State; State=newState; s_state_timer.reset(); return previousState;}

inline void updateAndNotifyConditionOutcome(ReplyOutcomes_t outcome, uint16_t payload)
{
    lora_reply_cond_var_mutex.lock();
    lora_reply_outcome=outcome;
    lora_reply_payload=payload;
    lora_reply_cond_var.notify_all();
    lora_reply_cond_var_mutex.unlock();
}

void notify_request_payload(uint16_t requestPayload)
{
    if(lora_state_machine_notify_request_payload_callback) lora_state_machine_notify_request_payload_callback(requestPayload);
}

uint16_t notify_request_payload_and_get_reply_payload(uint16_t requestPayload)
{
    if(lora_state_machine_notify_request_payload_and_get_reply_payload_callback) return lora_state_machine_notify_request_payload_and_get_reply_payload_callback(requestPayload);
    
    return 0;
}

void lora_event_proc_communication_cycle()
{
    uint16_t bufferSize=RADIO_MESSAGES_BUFFER_SIZE;
    uint8_t buffer[RADIO_MESSAGES_BUFFER_SIZE];

    char dumpBuffer[RADIO_MESSAGES_BUFFER_SIZE];

    int elapsed_ms=s_state_timer.read_ms();

    if(elapsed_ms > STATE_MACHINE_STALE_STATE_TIMEOUT)
    {
        sx1272_debug_if( SX1272_DEBUG_ENABLED, "...(state-machine timeout, resetting to initial state)...\n" );

        setState(INITIAL);
    }

    uint16_t requestPayload;
    uint16_t replyPayload;

    switch( getState() )
    {
        case INITIAL:

            //sx1272_debug_if( SX1272_DEBUG_ENABLED, "--- INITIAL STATE ---\n");

            Radio.Sleep();

            protocol_reset();
            
            setState(RX_WAITING_FOR_REQUEST);
            
            Radio.Rx(RX_TIMEOUT_VALUE);

            break;

        case RX_WAITING_FOR_REQUEST:
            //sx1272_debug_if( SX1272_DEBUG_ENABLED, "...(waiting for request)...\n" );
            break;

        case RX_WAITING_FOR_REPLY:
            //sx1272_debug_if( SX1272_DEBUG_ENABLED, "...(waiting for reply)...\n" );
            break;

        case RX_DONE_RECEIVED_REQUEST:

            protocol_fill_with_rx_buffer_dump(dumpBuffer, RADIO_MESSAGES_BUFFER_SIZE);

            sx1272_debug_if( SX1272_DEBUG_ENABLED, "*** REQUEST RECEIVED : '%s' ***\n", dumpBuffer);
            
            if(!protocol_is_latest_received_request_for_me())
            {
                sx1272_debug_if( SX1272_DEBUG_ENABLED, "...request is not for me\n");

                setState(INITIAL);
                
                break;
            }

            sx1272_debug_if( SX1272_DEBUG_ENABLED, "...REQUEST IS FOR ME...\n");

            requestPayload = protocol_get_latest_received_request_payload();

            if(!protocol_should_i_reply_to_latest_received_request())
            {
                sx1272_debug_if( SX1272_DEBUG_ENABLED, "...but I should not reply\n");

                notify_request_payload(requestPayload);

                setState(INITIAL);
                
                break;
            }

            sx1272_debug_if( SX1272_DEBUG_ENABLED, "...AND I SHOULD REPLY...\n");

            replyPayload = notify_request_payload_and_get_reply_payload(requestPayload);

            setState(TX_WAITING_FOR_REPLY_SENT);
            
            // Attesa di durata sufficiente per permettere a chi ha inviato la request di mettersi
            // in ascolto della reply
            wait_ms(REQUEST_REPLY_DELAY);

            // Send the REPLY frame
            protocol_fill_create_reply_buffer(buffer, bufferSize, replyPayload);

            Radio.Send( buffer, bufferSize );

            break;

        case RX_DONE_RECEIVED_REPLY:

            protocol_fill_with_rx_buffer_dump(dumpBuffer, RADIO_MESSAGES_BUFFER_SIZE);

            sx1272_debug_if( SX1272_DEBUG_ENABLED, "*** REPLY RECEIVED ('%s') ***\n", dumpBuffer);
            
            if(!protocol_is_latest_received_reply_for_me())
            {
                sx1272_debug_if( SX1272_DEBUG_ENABLED, "...reply is not for me, ignoring...\n");

                Radio.Sleep();
                Radio.Rx(RX_TIMEOUT_VALUE);
            }
            else
            {
                sx1272_debug_if( SX1272_DEBUG_ENABLED, "...REPLY IS FOR ME...\n");

                if(protocol_is_latest_received_reply_right())
                {
                    sx1272_debug_if( SX1272_DEBUG_ENABLED, "...AND REPLY IS RIGHT\n");

                    replyPayload = protocol_get_latest_received_reply_payload();

                    updateAndNotifyConditionOutcome(OUTCOME_REPLY_RIGHT, replyPayload);

                    setState(INITIAL);
                }
                else
                {
                    sx1272_debug_if( SX1272_DEBUG_ENABLED, "...BUT REPLY IS WRONG\n");

                    updateAndNotifyConditionOutcome(OUTCOME_REPLY_WRONG, 0);

                    setState(INITIAL);
                }
            }

            break;

        case TX_DONE_SENT_REQUEST:

            sx1272_debug_if( SX1272_DEBUG_ENABLED, "...request sent...\n" );

            Radio.Sleep();

            if(!protocol_should_i_wait_for_reply_for_latest_sent_request())
            {
                sx1272_debug_if( SX1272_DEBUG_ENABLED, "...but I should not wait for reply\n" );

                updateAndNotifyConditionOutcome(OUTCOME_REPLY_NOT_NEEDED, 0);

                setState(INITIAL);

                break;
            }

            sx1272_debug_if( SX1272_DEBUG_ENABLED, "...waiting for reply...\n" );
            
            setState(RX_WAITING_FOR_REPLY);

            Radio.Rx(RX_TIMEOUT_VALUE);

            break;

        case TX_DONE_SENT_REPLY:

            sx1272_debug_if( SX1272_DEBUG_ENABLED, "...REPLY SENT\n" ); 
           
            setState(INITIAL);
            
            break;

        case TX_WAITING_FOR_REQUEST_SENT:

            sx1272_debug_if( SX1272_DEBUG_ENABLED, "...waiting for request being sent...\n" ); 

            break;

        case TX_WAITING_FOR_REPLY_SENT:

            sx1272_debug_if( SX1272_DEBUG_ENABLED, "...waiting for reply being sent...\n" ); 

            break;
    }
}

void lora_state_machine_send_request(uint16_t argCounter, uint8_t argDestinationAddress)
{
    uint16_t bufferSize=RADIO_MESSAGES_BUFFER_SIZE;
    uint8_t buffer[RADIO_MESSAGES_BUFFER_SIZE];

    if(getState() != RX_WAITING_FOR_REQUEST) return;

    // Send the REQUEST frame
    protocol_fill_create_request_buffer(buffer, bufferSize, argCounter, argDestinationAddress);

    char dumpBuffer[RADIO_MESSAGES_BUFFER_SIZE];

    protocol_fill_with_tx_buffer_dump(dumpBuffer, buffer, RADIO_MESSAGES_BUFFER_SIZE);

    sx1272_debug_if( SX1272_DEBUG_ENABLED, "\n*** SEND REQUEST : '%s' ***\n", dumpBuffer);

    setState(TX_WAITING_FOR_REQUEST_SENT);

    Radio.Send( buffer, bufferSize );
}

void OnTxDone( void )
{
    sx1272_debug_if( SX1272_DEBUG_ENABLED, "> OnTxDone\n" );

    if(getState() == TX_WAITING_FOR_REQUEST_SENT)
    {
        sx1272_debug_if( SX1272_DEBUG_ENABLED, "...request tx done...\n" );

        setState(TX_DONE_SENT_REQUEST);
    }
    else if (getState() == TX_WAITING_FOR_REPLY_SENT)
    {
        sx1272_debug_if( SX1272_DEBUG_ENABLED, "...reply tx done...\n" );

        setState(TX_DONE_SENT_REPLY);
    }
}
 
void OnRxDone( uint8_t* payload, uint16_t size, int16_t rssi, int8_t snr )
{
    sx1272_debug_if( SX1272_DEBUG_ENABLED, "> OnRxDone (RSSI:%d, SNR:%d): %s (len: %d) \n", rssi, snr, (const char*)payload, size);

    if( size == 0 ) return;
    
    protocol_process_received_data(payload, size);

    if(getState() == RX_WAITING_FOR_REQUEST && protocol_is_received_data_a_request())
    {
        sx1272_debug_if( SX1272_DEBUG_ENABLED, "...request rx done...\n" );

        protocol_process_received_data_as_request();

        setState(RX_DONE_RECEIVED_REQUEST);
    }
    else if(getState() == RX_WAITING_FOR_REPLY && protocol_is_received_data_a_reply())
    { 
        sx1272_debug_if( SX1272_DEBUG_ENABLED, "...reply rx done...\n" );

        protocol_process_received_data_as_reply();

        setState(RX_DONE_RECEIVED_REPLY);
    }
    else // ricezione valida, ma arrivata in uno stato non previsto
    {   
        char dumpBuffer[RADIO_MESSAGES_BUFFER_SIZE];

        protocol_fill_with_rx_buffer_dump(dumpBuffer, RADIO_MESSAGES_BUFFER_SIZE);
        
        sx1272_debug_if( SX1272_DEBUG_ENABLED, "...valid but unexpected rx done ('%s'), ignoring...\n", dumpBuffer);

        Radio.Sleep();
        Radio.Rx(RX_TIMEOUT_VALUE);
    }
}
 
void OnTxTimeout( void )
{
    sx1272_debug_if( SX1272_DEBUG_ENABLED, "> OnTxTimeout\n" );

    if(getState() == TX_WAITING_FOR_REQUEST_SENT)
    {
        updateAndNotifyConditionOutcome(OUTCOME_TIMEOUT_WAITING_FOR_REQUEST_SENT, 0);
    }
    else if(getState() == TX_WAITING_FOR_REPLY_SENT)
    {
        updateAndNotifyConditionOutcome(OUTCOME_TIMEOUT_WAITING_FOR_REPLY_SENT, 0);
    }
    
    setState(INITIAL);
}
 
void OnRxTimeout( void )
{
    // sx1272_debug_if( SX1272_DEBUG_ENABLED, "> OnRxTimeout\n" );

    if(getState() != RX_WAITING_FOR_REQUEST && getState() != RX_WAITING_FOR_REPLY) return;

    if(getState() == RX_WAITING_FOR_REQUEST)
    {
        // sx1272_debug_if( getState() == RX_WAITING_FOR_REQUEST, "...rx timeout while waiting for request: restarting for request...\n" );
    }
    else if(getState() == RX_WAITING_FOR_REPLY)
    {
        sx1272_debug_if(SX1272_DEBUG_ENABLED , "...rx TIMEOUT while WAITING for REPLY: restarting waiting for request...\n" );

        updateAndNotifyConditionOutcome(OUTCOME_WAITING_FOR_REPLY_TIMEOUT, 0);
    }

    Radio.Sleep();

    setState(RX_WAITING_FOR_REQUEST);
    
    Radio.Rx(RX_TIMEOUT_VALUE);
}
 
void OnRxError( void )
{
    sx1272_debug_if( SX1272_DEBUG_ENABLED, "> OnRxError\n" );

    setState(INITIAL);
    
    sx1272_debug_if( SX1272_DEBUG_ENABLED, "...rx error: resetting state to idle...\n" );
}

int lora_state_machine_initialize(uint8_t myAddress, Thread* thread, EventQueue* eventQueue)
{
    protocol_initialize(myAddress);

    // Initialize Radio driver

    Radio.assign_events_queue_thread(thread);
    Radio.assign_events_queue(eventQueue);

    RadioEvents.TxDone = OnTxDone;
    RadioEvents.RxDone = OnRxDone;
    RadioEvents.RxError = OnRxError;
    RadioEvents.TxTimeout = OnTxTimeout;
    RadioEvents.RxTimeout = OnRxTimeout;

    Radio.Init( &RadioEvents );
 
    // verify the connection with the board
    while( Radio.Read( REG_VERSION ) == 0x00  )
    {
        sx1272_debug_if( SX1272_DEBUG_ENABLED, "Radio could not be detected!\n");
        return -1;
    }
 
    sx1272_debug_if( ( SX1272_DEBUG_ENABLED & ( Radio.DetectBoardType( ) == SX1272MB2XAS ) ), " > Board Type: SX1272MB2xAS <\n" );
 
    Radio.SetChannel( RF_FREQUENCY ); 
 
    sx1272_debug_if( LORA_FHSS_ENABLED, " > LORA FHSS Mode <\n" );
    sx1272_debug_if( !LORA_FHSS_ENABLED, " > LORA Mode <\n" );
 
    Radio.SetTxConfig( MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                         LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                         LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                         LORA_CRC_ENABLED, LORA_FHSS_ENABLED, LORA_NB_SYMB_HOP,
                         LORA_IQ_INVERSION_ON, TX_TIMEOUT_VALUE );
 
    Radio.SetRxConfig( MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                         LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                         LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON, 0,
                         LORA_CRC_ENABLED, LORA_FHSS_ENABLED, LORA_NB_SYMB_HOP,
                         LORA_IQ_INVERSION_ON, true );
 
    Radio.Sleep();

    s_state_timer.start();

    return 0;
}