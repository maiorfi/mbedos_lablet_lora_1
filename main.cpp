#include "mbed.h"
#include "main.h"
#include "sx1272-hal.h"

#define SX1272_DEBUG

#include "sx1272-debug.h"

#include "lora_protocol_impl.h"
 
/* Set this flag to '1' to display debug messages on the console */
#define DEBUG_MESSAGE   1
 
#define RF_FREQUENCY                                    868000000 // Hz
#define TX_OUTPUT_POWER                                 14        // 14 dBm
  
#define LORA_BANDWIDTH                              1         // [0: 125 kHz,
                                                                //  1: 250 kHz,
                                                                //  2: 500 kHz,
                                                                //  3: Reserved]
#define LORA_SPREADING_FACTOR                       8         // [SF7..SF12]
#define LORA_CODINGRATE                             1         // [1: 4/5,
                                                                //  2: 4/6,
                                                                //  3: 4/7,
                                                                //  4: 4/8]
#define LORA_PREAMBLE_LENGTH                        8         // Same for Tx and Rx
#define LORA_SYMBOL_TIMEOUT                         5         // Symbols
#define LORA_FIX_LENGTH_PAYLOAD_ON                  false
#define LORA_FHSS_ENABLED                           false  
#define LORA_NB_SYMB_HOP                            4     
#define LORA_IQ_INVERSION_ON                        false
#define LORA_CRC_ENABLED                            true

// Communication parameters 
#define RX_TIMEOUT_VALUE                                1000      // in ms
#define TX_TIMEOUT_VALUE                                500      // in ms
#define RADIO_MESSAGES_BUFFER_SIZE                      32        // Define the payload size here

#define REQUEST_REPLY_DELAY                             150       // in ms

#define STATE_MACHINE_STALE_STATE_TIMEOUT               (RX_TIMEOUT_VALUE+500)      // in ms

#define EVENT_PROC_COMMUNICATION_CYCLE_INTERVAL         100       // in ms

 
static InterruptIn btn(BUTTON1);
 
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

static DigitalIn address_in_bit_0(PH_0, PullUp);
static DigitalIn address_in_bit_1(PH_1, PullUp);
 
/*!
 * Radio events function pointer
 */
static RadioEvents_t RadioEvents;
 
/*
 *  Global variables declarations
 */
static SX1272MB2xAS Radio( NULL );

static Thread s_thread_manage_communication;
static EventQueue s_eq_manage_communication;

static Timer s_state_timer;

inline AppStates_t getState() { return State;}
AppStates_t setState(AppStates_t newState) { AppStates_t previousState=State; State=newState; s_state_timer.reset(); return previousState;}

void event_proc_communication_cycle()
{
    uint16_t bufferSize=RADIO_MESSAGES_BUFFER_SIZE;
    uint8_t buffer[RADIO_MESSAGES_BUFFER_SIZE];

    char dumpBuffer[RADIO_MESSAGES_BUFFER_SIZE];

    int elapsed_ms=s_state_timer.read_ms();

    if(elapsed_ms > STATE_MACHINE_STALE_STATE_TIMEOUT)
    {
        sx1272_debug_if( DEBUG_MESSAGE, "...(state-machine timeout, resetting to initial state)...\n" );

        setState(INITIAL);
    }

    switch( getState() )
    {
        case INITIAL:

            sx1272_debug_if( DEBUG_MESSAGE, "--- INITIAL STATE ---\n");

            Radio.Sleep();

            protocol_reset();
            
            setState(RX_WAITING_FOR_REQUEST);
            
            Radio.Rx(RX_TIMEOUT_VALUE);

            break;

        case RX_WAITING_FOR_REQUEST:
            //sx1272_debug_if( DEBUG_MESSAGE, "...(waiting for request)...\n" );
            break;

        case RX_WAITING_FOR_REPLY:
            //sx1272_debug_if( DEBUG_MESSAGE, "...(waiting for reply)...\n" );
            break;

        case RX_DONE_RECEIVED_REQUEST:

            protocol_fill_with_rx_buffer_dump(dumpBuffer, RADIO_MESSAGES_BUFFER_SIZE);

            sx1272_debug_if( DEBUG_MESSAGE, "*** REQUEST RECEIVED : '%s' ***\n", dumpBuffer);
            
            if(!protocol_is_latest_received_request_for_me())
            {
                sx1272_debug_if( DEBUG_MESSAGE, "...request is not for me\n");

                setState(INITIAL);
                
                break;
            }

            sx1272_debug_if( DEBUG_MESSAGE, "...REQUEST IS FOR ME...\n");

            if(!protocol_should_i_reply_to_latest_received_request())
            {
                sx1272_debug_if( DEBUG_MESSAGE, "...but I should not reply\n");

                setState(INITIAL);
                
                break;
            }

            sx1272_debug_if( DEBUG_MESSAGE, "...AND I SHOULD REPLY...\n");

            setState(TX_WAITING_FOR_REPLY_SENT);
            
            // Attesa di durata sufficiente per permettere a chi ha inviato la request di mettersi
            // in ascolto della reply
            wait_ms(REQUEST_REPLY_DELAY);

            // Send the REPLY frame
            protocol_fill_create_reply_buffer(buffer, bufferSize);

            Radio.Send( buffer, bufferSize );

            break;

        case RX_DONE_RECEIVED_REPLY:

            protocol_fill_with_rx_buffer_dump(dumpBuffer, RADIO_MESSAGES_BUFFER_SIZE);

            sx1272_debug_if( DEBUG_MESSAGE, "*** REPLY RECEIVED ('%s') ***\n", dumpBuffer);
            
            if(!protocol_is_latest_received_reply_for_me())
            {
                sx1272_debug_if( DEBUG_MESSAGE, "...reply is not for me\n");
            }
            else
            {
                sx1272_debug_if( DEBUG_MESSAGE, "...REPLY IS FOR ME...\n");

                if(protocol_is_latest_received_reply_right())
                {
                    sx1272_debug_if( DEBUG_MESSAGE, "...AND REPLY IS RIGHT\n");
                }
                else
                {
                    sx1272_debug_if( DEBUG_MESSAGE, "...BUT REPLY IS WRONG (reply doesn't mach request)\n");
                }
            }

            setState(INITIAL);
            
            break;

        case TX_DONE_SENT_REQUEST:

            sx1272_debug_if( DEBUG_MESSAGE, "...request sent...\n" );

            Radio.Sleep();

            if(!protocol_should_i_wait_for_reply_for_latest_sent_request())
            {
                sx1272_debug_if( DEBUG_MESSAGE, "...but I should not wait for reply\n" );

                setState(INITIAL);

                break;
            }

            sx1272_debug_if( DEBUG_MESSAGE, "...waiting for reply...\n" );
            
            setState(RX_WAITING_FOR_REPLY);

            Radio.Rx(RX_TIMEOUT_VALUE);

            break;

        case TX_DONE_SENT_REPLY:

            sx1272_debug_if( DEBUG_MESSAGE, "...REPLY SENT\n" ); 
           
            setState(INITIAL);
            
            break;

        case TX_WAITING_FOR_REQUEST_SENT:

            sx1272_debug_if( DEBUG_MESSAGE, "...waiting for request being sent...\n" ); 

            break;

        case TX_WAITING_FOR_REPLY_SENT:

            sx1272_debug_if( DEBUG_MESSAGE, "...waiting for reply being sent...\n" ); 

            break;
    }
}

void event_proc_send_data()
{
    uint16_t bufferSize=RADIO_MESSAGES_BUFFER_SIZE;
    uint8_t buffer[RADIO_MESSAGES_BUFFER_SIZE];

    if(getState() != RX_WAITING_FOR_REQUEST) return;

    // Send the REQUEST frame
    protocol_fill_create_request_buffer(buffer, bufferSize);

    char dumpBuffer[RADIO_MESSAGES_BUFFER_SIZE];

    protocol_fill_with_tx_buffer_dump(dumpBuffer, buffer, RADIO_MESSAGES_BUFFER_SIZE);

    sx1272_debug_if( DEBUG_MESSAGE, "\n*** SENDING NEW REQUEST : '%s' ***\n", dumpBuffer);

    setState(TX_WAITING_FOR_REQUEST_SENT);

    Radio.Send( buffer, bufferSize );
}

void btn_interrupt_handler()
{
    s_eq_manage_communication.call(event_proc_send_data);
}
 
void OnTxDone( void )
{
    sx1272_debug_if( DEBUG_MESSAGE, "> OnTxDone\n" );

    if(getState() == TX_WAITING_FOR_REQUEST_SENT)
    {
        sx1272_debug_if( DEBUG_MESSAGE, "...request tx done...\n" );

        setState(TX_DONE_SENT_REQUEST);
    }
    else if (getState() == TX_WAITING_FOR_REPLY_SENT)
    {
        sx1272_debug_if( DEBUG_MESSAGE, "...reply tx done...\n" );

        setState(TX_DONE_SENT_REPLY);
    }
}
 
void OnRxDone( uint8_t* payload, uint16_t size, int16_t rssi, int8_t snr )
{
    sx1272_debug_if( DEBUG_MESSAGE, "> OnRxDone (RSSI:%d, SNR:%d): %s (len: %d) \n", rssi, snr, (const char*)payload, size);

    if( size == 0 ) return;
    
    protocol_process_received_data(payload, size);

    if(getState() == RX_WAITING_FOR_REQUEST && protocol_is_received_data_a_request())
    {
        sx1272_debug_if( DEBUG_MESSAGE, "...request rx done...\n" );

        protocol_process_received_data_as_request();

        setState(RX_DONE_RECEIVED_REQUEST);
    }
    else if(getState() == RX_WAITING_FOR_REPLY && protocol_is_received_data_a_reply())
    { 
        sx1272_debug_if( DEBUG_MESSAGE, "...reply rx done...\n" );

        protocol_process_received_data_as_reply();

        setState(RX_DONE_RECEIVED_REPLY);
    }
    else // ricezione valida, ma arrivata in uno stato non previsto
    {   
        char dumpBuffer[RADIO_MESSAGES_BUFFER_SIZE];

        protocol_fill_with_rx_buffer_dump(dumpBuffer, RADIO_MESSAGES_BUFFER_SIZE);
        
        sx1272_debug_if( DEBUG_MESSAGE, "...valid but unexpected rx done ('%s'), ignoring...\n", dumpBuffer);

        Radio.Sleep();
        Radio.Rx(RX_TIMEOUT_VALUE);
    }
}
 
void OnTxTimeout( void )
{
    sx1272_debug_if( DEBUG_MESSAGE, "> OnTxTimeout\n" );

    Radio.Sleep();

    setState(RX_WAITING_FOR_REQUEST);
    
    Radio.Rx(RX_TIMEOUT_VALUE);
}
 
void OnRxTimeout( void )
{
    // sx1272_debug_if( DEBUG_MESSAGE, "> OnRxTimeout\n" );

    if(getState() != RX_WAITING_FOR_REQUEST && getState() != RX_WAITING_FOR_REPLY) return;

    // sx1272_debug_if( getState() == RX_WAITING_FOR_REQUEST, "...rx timeout while waiting for request: restarting for request...\n" );

    sx1272_debug_if( getState() == RX_WAITING_FOR_REPLY, "...rx TIMEOUT while WAITING for REPLY: restarting waiting for request...\n" );

    Radio.Sleep();

    setState(RX_WAITING_FOR_REQUEST);
    
    Radio.Rx(RX_TIMEOUT_VALUE);
}
 
void OnRxError( void )
{
    sx1272_debug_if( DEBUG_MESSAGE, "> OnRxError\n" );

    setState(INITIAL);;
    
    sx1272_debug_if( DEBUG_MESSAGE, "...rx error: resetting state to idle...\n" );
}

int main( void ) 
{
    sx1272_debug_if( DEBUG_MESSAGE,"LoRa Request/Reply Demo Application (blue button to send a new request)\n");

    uint8_t myAddress;

    myAddress = 1 + (address_in_bit_0.read() ? 0 : 1) +  (address_in_bit_1.read() ? 0 : 2);

    sx1272_debug_if( DEBUG_MESSAGE,"\n\n---------------------\n");
    sx1272_debug_if( DEBUG_MESSAGE,"|   MY_ADDRESS: %u   |\n", myAddress);
    sx1272_debug_if( DEBUG_MESSAGE,"---------------------\n\n");

    protocol_initialize(myAddress);
 
    // Initialize Radio driver

    Radio.assign_events_queue_thread(&s_thread_manage_communication);
    Radio.assign_events_queue(&s_eq_manage_communication);

    RadioEvents.TxDone = OnTxDone;
    RadioEvents.RxDone = OnRxDone;
    RadioEvents.RxError = OnRxError;
    RadioEvents.TxTimeout = OnTxTimeout;
    RadioEvents.RxTimeout = OnRxTimeout;

    Radio.Init( &RadioEvents );
 
    // verify the connection with the board
    while( Radio.Read( REG_VERSION ) == 0x00  )
    {
        sx1272_debug_if( DEBUG_MESSAGE, "Radio could not be detected!\n");
        return -1;
    }
 
    sx1272_debug_if( ( DEBUG_MESSAGE & ( Radio.DetectBoardType( ) == SX1272MB2XAS ) ), " > Board Type: SX1272MB2xAS <\n" );
 
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

    s_eq_manage_communication.call_every(EVENT_PROC_COMMUNICATION_CYCLE_INTERVAL, event_proc_communication_cycle);

    btn.fall(&btn_interrupt_handler);

    s_state_timer.start();

    s_thread_manage_communication.start(callback(&s_eq_manage_communication, &EventQueue::dispatch_forever));
}