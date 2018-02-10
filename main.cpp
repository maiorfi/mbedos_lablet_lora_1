#include "mbed.h"
#include "main.h"
#include "sx1272-hal.h"
#include "sx1272-debug.h"
 
/* Set this flag to '1' to display debug messages on the console */
#define DEBUG_MESSAGE   1
 
#define RF_FREQUENCY                                    868000000 // Hz
#define TX_OUTPUT_POWER                                 14        // 14 dBm
  
#define LORA_BANDWIDTH                              2         // [0: 125 kHz,
                                                                //  1: 250 kHz,
                                                                //  2: 500 kHz,
                                                                //  3: Reserved]
#define LORA_SPREADING_FACTOR                       7         // [SF7..SF12]
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
#define RX_TIMEOUT_VALUE                                5000      // in ms
#define BUFFER_SIZE                                     16        // Define the payload size here
 
static InterruptIn btn(BUTTON1);
 
/*
 *  Global variables declarations
 */
typedef enum
{
    INITIAL,

    IDLE_RX_WAITING_FOR_REQUEST,
    RX_WAITING_FOR_REPLY,

    RX_DONE_RECEIVED_REQUEST,
    RX_DONE_RECEIVED_REPLY,
 
    TX_WAITING_FOR_REQUEST_SENT,
    TX_WAITING_FOR_REPLY_SENT,

    TX_DONE_SENT_REQUEST,
    TX_DONE_SENT_REPLY

} AppStates_t;
 
static AppStates_t State = INITIAL;

static uint16_t Counter=0, LatestReceivedCounter=0;
 
/*!
 * Radio events function pointer
 */
static RadioEvents_t RadioEvents;
 
/*
 *  Global variables declarations
 */
static SX1272MB2xAS Radio( NULL );
 
const uint8_t PingMsg[] = "REQUEST-";
const uint8_t PongMsg[] = "REPLY-";
 
static uint16_t RxBufferSize = BUFFER_SIZE;
static uint8_t RxBuffer[BUFFER_SIZE];

static int16_t RssiValue = 0.0;
static int8_t SnrValue = 0.0;

static Thread s_thread_manage_communication;
static EventQueue s_eq_manage_communication;
static Mutex s_state_mutex;

void event_proc_communication_cycle()
{
    AppStates_t currentState;
    uint16_t bufferSize=BUFFER_SIZE;
    uint8_t buffer[BUFFER_SIZE];

    uint16_t payloadLen;

    s_state_mutex.lock();
    currentState = State;
    s_state_mutex.unlock();

    switch( currentState )
    {
        case INITIAL:

            s_state_mutex.lock();
            State = IDLE_RX_WAITING_FOR_REQUEST;
            s_state_mutex.unlock();

            Radio.Rx(RX_TIMEOUT_VALUE);

            break;

        case IDLE_RX_WAITING_FOR_REQUEST:
            //sx1272_debug( "...(waiting for request)...\n" );
            break;

        case RX_WAITING_FOR_REPLY:
            sx1272_debug( "...(waiting for reply)...\n" );
            break;

        case RX_DONE_RECEIVED_REQUEST:
            
            s_state_mutex.lock();
            sx1272_debug( "*** REQUEST RECEIVED ('%s') ***\n", RxBuffer);
            s_state_mutex.unlock();

            // Attesa di durata sufficiente per permettere a chi ha inviato la request di mettersi
            // in ascolto della reply
            wait_ms(500);

            s_state_mutex.lock();
            State = TX_WAITING_FOR_REPLY_SENT;
            s_state_mutex.unlock();

            // Send the next REPLY frame
            sprintf((char*)buffer, "%s%d",(const char*)PongMsg,LatestReceivedCounter);
            
            payloadLen=strlen((const char*)buffer) + 1;

            if(payloadLen<bufferSize)
            {
                memset(buffer+payloadLen,0xFF,bufferSize-payloadLen);
            }

            Radio.Send( buffer, bufferSize );

            break;

        case RX_DONE_RECEIVED_REPLY:
            
            sx1272_debug( "*** REPLY RECEIVED ***\n\n" );

            s_state_mutex.lock();
            State = INITIAL;
            s_state_mutex.unlock();

            Radio.Sleep();

            break;

        case TX_DONE_SENT_REQUEST:

            sx1272_debug( "...request sent\n" ); 
           
            s_state_mutex.lock();
            State = RX_WAITING_FOR_REPLY;
            s_state_mutex.unlock();

            Radio.Rx(RX_TIMEOUT_VALUE);

            break;

        case TX_DONE_SENT_REPLY:

            sx1272_debug( "...reply sent\n" ); 
           
            s_state_mutex.lock();
            State = INITIAL;
            s_state_mutex.unlock();

            Radio.Sleep();

            break;

        case TX_WAITING_FOR_REQUEST_SENT:

            sx1272_debug( "...waiting for request being sent...\n" ); 

            break;

        case TX_WAITING_FOR_REPLY_SENT:

            sx1272_debug( "...waiting for reply being sent...\n" ); 

            break;
    }
}

void event_proc_send_data()
{
    uint16_t bufferSize=BUFFER_SIZE;
    uint8_t buffer[BUFFER_SIZE];

    AppStates_t currentState;

    s_state_mutex.lock();
    currentState=State;
    s_state_mutex.unlock();

    if(currentState!=IDLE_RX_WAITING_FOR_REQUEST) return;

    Counter++;

    sx1272_debug( "\n*** SENDING NEW REQUEST : ('%s%d') ***\n",(const char*)PingMsg, Counter );

    // Send the next REQUEST frame
    sprintf((char*)buffer, "%s%d",(const char*)PingMsg, Counter);
    
    uint16_t payloadLen=strlen((const char*)buffer)+1;

    if(payloadLen<bufferSize) memset(buffer+payloadLen,0xFF,bufferSize-payloadLen);

    Radio.Send( buffer, bufferSize );

    s_state_mutex.lock();
    State=TX_WAITING_FOR_REQUEST_SENT;
    s_state_mutex.unlock();
}

void btn_interrupt_handler()
{
    s_eq_manage_communication.call(event_proc_send_data);
}
 
void OnTxDone( void )
{
    sx1272_debug_if( DEBUG_MESSAGE, "> OnTxDone\n" );

    s_state_mutex.lock();

    if(State==TX_WAITING_FOR_REQUEST_SENT)
    {
        sx1272_debug( "...request tx done...\n" );

        State = TX_DONE_SENT_REQUEST;
    }
    else if (TX_WAITING_FOR_REPLY_SENT)
    {
        sx1272_debug( "...reply tx done...\n" );

        State = TX_DONE_SENT_REPLY;
    }

    s_state_mutex.unlock();    
}
 
void OnRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr )
{
    sx1272_debug_if( DEBUG_MESSAGE, "> OnRxDone (RSSI:%d, SNR:%d): %s\n", RssiValue, SnrValue, (const char*)payload);

    s_state_mutex.lock();

    RssiValue = rssi;
    SnrValue = snr;

    RxBufferSize = size;

    if( size > 0 )
    {
        memcpy( RxBuffer, payload, size );

        if(State==IDLE_RX_WAITING_FOR_REQUEST && strncmp((const char*)RxBuffer, (const char*)PingMsg, strlen((const char*)PingMsg)) == 0)
        {
            sx1272_debug( "...request rx done...\n" );

            const char* dashPtr=strchr((const char*)RxBuffer,'-');

            if(dashPtr && dashPtr-((const char*)RxBuffer)>0)
            {
                LatestReceivedCounter=atoi(dashPtr+1);
            }
            else
            {
                LatestReceivedCounter=0;
            }

            State = RX_DONE_RECEIVED_REQUEST;
        }
        else if(State==RX_WAITING_FOR_REPLY && strncmp((const char*)RxBuffer, (const char*)PongMsg, strlen((const char*)PongMsg)) == 0)
        { 
            sx1272_debug( "...reply rx done...\n" );

            uint16_t replyCounter=0;

            const char* dashPtr=strchr((const char*)RxBuffer,'-');

            if(dashPtr && dashPtr-((const char*)RxBuffer)>0)
            {
                replyCounter=atoi(dashPtr+1);
            }

            if(replyCounter==Counter)
            {
                sx1272_debug( "...REQUEST<->REPLY MATCH...\n" );
            }            
            else
            {
                sx1272_debug( "...REQUEST<->REPLY MISMATCH (%d<->%d)...\n", Counter, replyCounter);
            }

            State = RX_DONE_RECEIVED_REPLY;
        }
        else // valid reception but neither a REQUEST nor a REPLY message
        {   
            sx1272_debug( "...valid but unexpected rx done ('%s'), resetting to idle state...\n", (const char*)RxBuffer);

            State = IDLE_RX_WAITING_FOR_REQUEST;

            Radio.Rx(RX_TIMEOUT_VALUE);
        }
    }

    s_state_mutex.unlock();

}
 
void OnTxTimeout( void )
{
    uint16_t bufferSize=BUFFER_SIZE;
    uint8_t buffer[BUFFER_SIZE];
    
    sx1272_debug_if( DEBUG_MESSAGE, "> OnTxTimeout\n" );

    s_state_mutex.lock();
    
    /*if(State==TX_WAITING_FOR_REQUEST_SENT)
    {
        sx1272_debug( "...tx timeout while waiting for request: re-sending request...\n" );

        // Send the next REQUEST frame
        sprintf((char*)buffer, "%s%d",(const char*)PingMsg, ++Counter);
        
        uint16_t payloadLen=strlen((const char*)buffer)+1;

        if(payloadLen<bufferSize) memset(buffer+payloadLen,0xFF,bufferSize-payloadLen);

        Radio.Send( buffer, bufferSize );
    }
    else if (TX_WAITING_FOR_REPLY_SENT)
    {
        sx1272_debug( "...tx timeout while waiting for reply: re-sending reply...\n" );

        // Send the next REPLY frame
        sprintf((char*)buffer, "%s%d",(const char*)PongMsg,LatestReceivedCounter);
        
        uint16_t payloadLen=strlen((const char*)buffer) + 1;

        if(payloadLen<bufferSize)
        {
            memset(buffer+payloadLen,0xFF,bufferSize-payloadLen);
        }

        Radio.Send( buffer, bufferSize );
    }*/

    State=IDLE_RX_WAITING_FOR_REQUEST;
    
    s_state_mutex.unlock();

    Radio.Rx(RX_TIMEOUT_VALUE);
}
 
void OnRxTimeout( void )
{
    sx1272_debug_if( DEBUG_MESSAGE, "> OnRxTimeout\n" );

    AppStates_t currentState;

    s_state_mutex.lock();
    currentState=State;
    s_state_mutex.unlock();

    if(currentState!=IDLE_RX_WAITING_FOR_REQUEST && currentState!=RX_WAITING_FOR_REPLY) return;

    s_state_mutex.lock();
    memset(RxBuffer,0x00,BUFFER_SIZE);
    RxBufferSize=0;
    State = IDLE_RX_WAITING_FOR_REQUEST;
    s_state_mutex.unlock();

    sx1272_debug_if( DEBUG_MESSAGE, "...rx timeout: resetting state to idle...\n" );

    Radio.Rx(RX_TIMEOUT_VALUE);
}
 
void OnRxError( void )
{
    sx1272_debug_if( DEBUG_MESSAGE, "> OnRxError\n" );

    s_state_mutex.lock();
    memset(RxBuffer,0x00,BUFFER_SIZE);
    RxBufferSize=0;
    State = INITIAL;;
    s_state_mutex.unlock();

    sx1272_debug_if( DEBUG_MESSAGE, "...rx error: resetting state to idle...\n" );

    Radio.Sleep();
}

int main( void ) 
{
    sx1272_debug("LoRa Request/Reply Demo Application (blue button to send acked message)");
 
    // Initialize Radio driver

    RadioEvents.TxDone = OnTxDone;
    RadioEvents.RxDone = OnRxDone;
    RadioEvents.RxError = OnRxError;
    RadioEvents.TxTimeout = OnTxTimeout;
    RadioEvents.RxTimeout = OnRxTimeout;
    Radio.Init( &RadioEvents );
 
    // verify the connection with the board
    while( Radio.Read( REG_VERSION ) == 0x00  )
    {
        sx1272_debug( "Radio could not be detected!\n", NULL );
        wait( 1 );
    }
 
    sx1272_debug_if( ( DEBUG_MESSAGE & ( Radio.DetectBoardType( ) == SX1272MB2XAS ) ), " > Board Type: SX1272MB2xAS <\n" );
 
    Radio.SetChannel( RF_FREQUENCY ); 
 
    sx1272_debug_if( LORA_FHSS_ENABLED, " > LORA FHSS Mode <\n" );
    sx1272_debug_if( !LORA_FHSS_ENABLED, " > LORA Mode <\n" );
 
    Radio.SetTxConfig( MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                         LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                         LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                         LORA_CRC_ENABLED, LORA_FHSS_ENABLED, LORA_NB_SYMB_HOP,
                         LORA_IQ_INVERSION_ON, 2000 );
 
    Radio.SetRxConfig( MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                         LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                         LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON, 0,
                         LORA_CRC_ENABLED, LORA_FHSS_ENABLED, LORA_NB_SYMB_HOP,
                         LORA_IQ_INVERSION_ON, true );
 
    Radio.Sleep();

    s_eq_manage_communication.call_every(100, event_proc_communication_cycle);

    btn.fall(&btn_interrupt_handler);

    s_thread_manage_communication.start(callback(&s_eq_manage_communication, &EventQueue::dispatch_forever));
}