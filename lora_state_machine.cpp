#include "mbed.h"

#define /*USE_SX1276_RADIO_MODULE*/ USE_SX1272_RADIO_MODULE

/* Set this flag to '1' to display debug messages on the console */
#define SX127x_DEBUG_ENABLED    1

#if defined USE_SX1272_RADIO_MODULE

    #include "sx1272-hal.h"
    #define sx127x_debug_if sx1272_debug_if
    #include "sx1272-debug.h"

    static SX1272MB2xAS Radio( NULL );

#elif defined USE_SX1276_RADIO_MODULE

    #include "sx1276-hal.h"
    #define sx127x_debug_if sx1276_debug_if
    #include "sx1276-debug.h"
    
    static SX1276MB1xAS Radio( NULL );

#else
    #error "Please specify a radio module"
#endif

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
static Timer s_state_timer;

Mutex lora_reply_cond_var_mutex;
ConditionVariable lora_reply_cond_var(lora_reply_cond_var_mutex);
LoraReplyOutcomes_t lora_reply_outcome;
uint16_t lora_reply_payload;

lora_notify_request_callback_t lora_state_machine_notify_request_callback;
lora_notify_request_and_get_reply_callback_t lora_state_machine_notify_request_and_get_reply_callback;

inline AppStates_t getState() { return State;}
AppStates_t setState(AppStates_t newState) { AppStates_t previousState=State; State=newState; s_state_timer.reset(); return previousState;}

inline void updateAndNotifyConditionOutcome(LoraReplyOutcomes_t outcome, uint16_t payload)
{
    lora_reply_cond_var_mutex.lock();
    lora_reply_outcome=outcome;
    lora_reply_payload=payload;
    lora_reply_cond_var.notify_all();
    lora_reply_cond_var_mutex.unlock();
}

void notify_request(uint8_t requestSourceAddress, uint16_t requestPayload)
{
    if(lora_state_machine_notify_request_callback) lora_state_machine_notify_request_callback(requestSourceAddress, requestPayload);
}

uint16_t notify_request_and_get_reply(uint8_t requestSourceAddress, uint16_t requestPayload)
{
    if(lora_state_machine_notify_request_and_get_reply_callback) return lora_state_machine_notify_request_and_get_reply_callback(requestSourceAddress, requestPayload);
    
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
        sx127x_debug_if( SX127x_DEBUG_ENABLED, "...(lora state-machine timeout, resetting to initial state)...\n" );

        setState(INITIAL);
    }

    uint16_t requestPayload;
    uint16_t replyPayload;
    uint8_t requestSourceAddress;
    uint8_t replyDestinationAddress;

    switch( getState() )
    {
        case INITIAL:

            //sx127x_debug_if( SX127x_DEBUG_ENABLED, "--- INITIAL STATE ---\n");

            Radio.Sleep();

            lora_protocol_reset();
            
            setState(RX_WAITING_FOR_REQUEST);
            
            Radio.Rx(RX_TIMEOUT_VALUE);

            break;

        case RX_WAITING_FOR_REQUEST:
            //sx127x_debug_if( SX127x_DEBUG_ENABLED, "...(waiting for request)...\n" );
            break;

        case RX_WAITING_FOR_REPLY:
            //sx127x_debug_if( SX127x_DEBUG_ENABLED, "...(waiting for reply)...\n" );
            break;

        case RX_DONE_RECEIVED_REQUEST:

            lora_protocol_fill_with_rx_buffer_dump(dumpBuffer, RADIO_MESSAGES_BUFFER_SIZE);

            sx127x_debug_if( SX127x_DEBUG_ENABLED, "*** LORA REQUEST RECEIVED : '%s' ***\n", dumpBuffer);
            
            if(!lora_protocol_is_latest_received_request_for_me())
            {
                sx127x_debug_if( SX127x_DEBUG_ENABLED, "...request is not for me\n");

                setState(INITIAL);
                
                break;
            }

            sx127x_debug_if( SX127x_DEBUG_ENABLED, "...REQUEST IS FOR ME...\n");

            requestSourceAddress = lora_protocol_get_latest_received_request_source_address();
            requestPayload = lora_protocol_get_latest_received_request_payload();

            if(!lora_protocol_should_i_reply_to_latest_received_request())
            {
                sx127x_debug_if( SX127x_DEBUG_ENABLED, "...but I should not reply\n");

                notify_request(requestSourceAddress, requestPayload);

                setState(INITIAL);
                
                break;
            }

            sx127x_debug_if( SX127x_DEBUG_ENABLED, "...AND I SHOULD REPLY...\n");

            replyPayload = notify_request_and_get_reply(requestSourceAddress, requestPayload);

            setState(TX_WAITING_FOR_REPLY_SENT);
            
            // Attesa di durata sufficiente per permettere a chi ha inviato la request di mettersi
            // in ascolto della reply
            wait_ms(REQUEST_REPLY_DELAY);

            // Send the REPLY frame
            lora_protocol_fill_create_reply_buffer(buffer, bufferSize, replyPayload);

            Radio.Send( buffer, bufferSize );

            break;

        case RX_DONE_RECEIVED_REPLY:

            lora_protocol_fill_with_rx_buffer_dump(dumpBuffer, RADIO_MESSAGES_BUFFER_SIZE);

            sx127x_debug_if( SX127x_DEBUG_ENABLED, "*** LORA REPLY RECEIVED ('%s') ***\n", dumpBuffer);
            
            if(!lora_protocol_is_latest_received_reply_for_me())
            {
                sx127x_debug_if( SX127x_DEBUG_ENABLED, "...reply is not for me, ignoring...\n");

                Radio.Sleep();
                Radio.Rx(RX_TIMEOUT_VALUE);
            }
            else
            {
                sx127x_debug_if( SX127x_DEBUG_ENABLED, "...REPLY IS FOR ME...\n");

                if(lora_protocol_is_latest_received_reply_right())
                {
                    sx127x_debug_if( SX127x_DEBUG_ENABLED, "...AND REPLY IS RIGHT\n");

                    replyPayload = lora_protocol_get_latest_received_reply_payload();

                    updateAndNotifyConditionOutcome(LORA_OUTCOME_REPLY_RIGHT, replyPayload);

                    setState(INITIAL);
                }
                else
                {
                    sx127x_debug_if( SX127x_DEBUG_ENABLED, "...BUT REPLY IS WRONG\n");

                    updateAndNotifyConditionOutcome(LORA_OUTCOME_REPLY_WRONG, 0);

                    setState(INITIAL);
                }
            }

            break;

        case TX_DONE_SENT_REQUEST:

            sx127x_debug_if( SX127x_DEBUG_ENABLED, "...lora request sent...\n" );

            Radio.Sleep();

            if(!lora_protocol_should_i_wait_for_reply_for_latest_sent_request())
            {
                sx127x_debug_if( SX127x_DEBUG_ENABLED, "...but I should not wait for reply\n" );

                updateAndNotifyConditionOutcome(LORA_OUTCOME_REPLY_NOT_NEEDED, 0);

                setState(INITIAL);

                break;
            }

            sx127x_debug_if( SX127x_DEBUG_ENABLED, "...waiting for reply...\n" );
            
            setState(RX_WAITING_FOR_REPLY);

            Radio.Rx(RX_TIMEOUT_VALUE);

            break;

        case TX_DONE_SENT_REPLY:

            sx127x_debug_if( SX127x_DEBUG_ENABLED, "...LORA REPLY SENT\n" ); 
           
            setState(INITIAL);
            
            break;

        case TX_WAITING_FOR_REQUEST_SENT:

            sx127x_debug_if( SX127x_DEBUG_ENABLED, "...waiting for request being sent...\n" ); 

            break;

        case TX_WAITING_FOR_REPLY_SENT:

            sx127x_debug_if( SX127x_DEBUG_ENABLED, "...waiting for reply being sent...\n" ); 

            break;
    }
}

LoraReplyOutcomes_t lora_state_machine_send_request(uint16_t argCounter, uint8_t argDestinationAddress, bool argRequiresReply)
{
    uint16_t bufferSize=RADIO_MESSAGES_BUFFER_SIZE;
    uint8_t buffer[RADIO_MESSAGES_BUFFER_SIZE];

    if(getState() != RX_WAITING_FOR_REQUEST) return LORA_OUTCOME_INVALID_STATE;

    // Send the REQUEST frame
    lora_protocol_fill_create_request_buffer(buffer, bufferSize, argCounter, argDestinationAddress, argRequiresReply);

    char dumpBuffer[RADIO_MESSAGES_BUFFER_SIZE];

    lora_protocol_fill_with_tx_buffer_dump(dumpBuffer, buffer, RADIO_MESSAGES_BUFFER_SIZE);

    sx127x_debug_if( SX127x_DEBUG_ENABLED, "*** LORA SEND REQUEST : '%s' ***\n", dumpBuffer);

    setState(TX_WAITING_FOR_REQUEST_SENT);

    Radio.Send( buffer, bufferSize );

    return LORA_OUTCOME_PENDING;
}

void OnTxDone( void )
{
    sx127x_debug_if( SX127x_DEBUG_ENABLED, "> OnTxDone\n" );

    if(getState() == TX_WAITING_FOR_REQUEST_SENT)
    {
        sx127x_debug_if( SX127x_DEBUG_ENABLED, "...request tx done...\n" );

        setState(TX_DONE_SENT_REQUEST);
    }
    else if (getState() == TX_WAITING_FOR_REPLY_SENT)
    {
        sx127x_debug_if( SX127x_DEBUG_ENABLED, "...reply tx done...\n" );

        setState(TX_DONE_SENT_REPLY);
    }
}
 
void OnRxDone( uint8_t* payload, uint16_t size, int16_t rssi, int8_t snr )
{
    sx127x_debug_if( SX127x_DEBUG_ENABLED, "> OnRxDone (RSSI:%d, SNR:%d): %s (len: %d) \n", rssi, snr, (const char*)payload, size);

    if( size == 0 ) return;
    
    lora_protocol_process_received_data(payload, size);

    if(getState() == RX_WAITING_FOR_REQUEST && lora_protocol_is_received_data_a_request())
    {
        sx127x_debug_if( SX127x_DEBUG_ENABLED, "...request rx done...\n" );

        lora_protocol_process_received_data_as_request();

        setState(RX_DONE_RECEIVED_REQUEST);
    }
    else if(getState() == RX_WAITING_FOR_REPLY && lora_protocol_is_received_data_a_reply())
    { 
        sx127x_debug_if( SX127x_DEBUG_ENABLED, "...reply rx done...\n" );

        lora_protocol_process_received_data_as_reply();

        setState(RX_DONE_RECEIVED_REPLY);
    }
    else // ricezione valida, ma arrivata in uno stato non previsto
    {   
        char dumpBuffer[RADIO_MESSAGES_BUFFER_SIZE];

        lora_protocol_fill_with_rx_buffer_dump(dumpBuffer, RADIO_MESSAGES_BUFFER_SIZE);
        
        sx127x_debug_if( SX127x_DEBUG_ENABLED, "...valid but unexpected rx done ('%s'), ignoring...\n", dumpBuffer);

        Radio.Sleep();
        Radio.Rx(RX_TIMEOUT_VALUE);
    }
}
 
void OnTxTimeout( void )
{
    sx127x_debug_if( SX127x_DEBUG_ENABLED, "> OnTxTimeout\n" );

    if(getState() == TX_WAITING_FOR_REQUEST_SENT)
    {
        updateAndNotifyConditionOutcome(LORA_OUTCOME_TIMEOUT_WAITING_FOR_REQUEST_SENT, 0);
    }
    else if(getState() == TX_WAITING_FOR_REPLY_SENT)
    {
        updateAndNotifyConditionOutcome(LORA_OUTCOME_TIMEOUT_WAITING_FOR_REPLY_SENT, 0);
    }
    
    setState(INITIAL);
}
 
void OnRxTimeout( void )
{
    // sx127x_debug_if( SX127x_DEBUG_ENABLED, "> OnRxTimeout\n" );

    if(getState() != RX_WAITING_FOR_REQUEST && getState() != RX_WAITING_FOR_REPLY) return;

    if(getState() == RX_WAITING_FOR_REQUEST)
    {
        // sx127x_debug_if( getState() == RX_WAITING_FOR_REQUEST, "...rx timeout while waiting for request: restarting for request...\n" );
    }
    else if(getState() == RX_WAITING_FOR_REPLY)
    {
        sx127x_debug_if(SX127x_DEBUG_ENABLED , "...rx TIMEOUT while WAITING for REPLY: restarting waiting for request...\n" );

        updateAndNotifyConditionOutcome(LORA_OUTCOME_WAITING_FOR_REPLY_TIMEOUT, 0);
    }

    Radio.Sleep();

    setState(RX_WAITING_FOR_REQUEST);
    
    Radio.Rx(RX_TIMEOUT_VALUE);
}
 
void OnRxError( void )
{
    sx127x_debug_if( SX127x_DEBUG_ENABLED, "> OnRxError\n" );

    setState(INITIAL);
    
    sx127x_debug_if( SX127x_DEBUG_ENABLED, "...rx error: resetting state to idle...\n" );
}

int lora_state_machine_initialize(uint8_t myAddress, EventQueue* eventQueue)
{
    lora_protocol_initialize(myAddress);

    // Initialize Radio driver

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
        sx127x_debug_if( SX127x_DEBUG_ENABLED, "Radio could not be detected!\n");
        return -1;
    }
 
    #if defined USE_SX1272_RADIO_MODULE

        sx127x_debug_if( ( SX127x_DEBUG_ENABLED & ( Radio.DetectBoardType( ) == SX1272MB2XAS ) ), " > Board Type: SX1272MB2xAS <\n" );

    #elif defined USE_SX1276_RADIO_MODULE
    
        sx127x_debug_if( ( SX127x_DEBUG_ENABLED & ( Radio.DetectBoardType( ) == SX1276MB1LAS ) ), "\n\r > Board Type: SX1276MB1LAS < \n\r" );
        sx127x_debug_if( ( SX127x_DEBUG_ENABLED & ( Radio.DetectBoardType( ) == SX1276MB1MAS ) ), "\n\r > Board Type: SX1276MB1MAS < \n\r" );
        sx127x_debug_if( ( SX127x_DEBUG_ENABLED & ( Radio.DetectBoardType( ) == RFM95_SX1276 ) ), "\n\r > Board Type: RFM95_SX1276 < \n\r" );
        sx127x_debug_if( ( SX127x_DEBUG_ENABLED & ( Radio.DetectBoardType( ) == MURATA_SX1276 ) ), "\n\r > Board Type: MURATA_SX1276 < \n\r" );
    
    #endif

    Radio.SetChannel( RF_FREQUENCY ); 
 
    sx127x_debug_if( LORA_FHSS_ENABLED, " > LORA FHSS Mode <\n" );
    sx127x_debug_if( !LORA_FHSS_ENABLED, " > LORA Mode <\n" );
 
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