#include "mbed.h"
#include "main.h"
#include "sx1272-hal.h"
#include "sx1272-debug.h"
 
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
#define RX_TIMEOUT_VALUE                                2000      // in ms
#define TX_TIMEOUT_VALUE                                2000      // in ms
#define BUFFER_SIZE                                     32        // Define the payload size here

#define REQUEST_REPLY_DELAY                             250       // in ms

#define STATE_MACHINE_STALE_STATE_TIMEOUT               5000      // in ms

 
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

Ticker s_ticker;

static uint16_t Counter=0, LatestReceivedRequestCounter=0, LatestReceivedReplyCounter=0;
static uint8_t LatestReceivedRequestDestinationAddress=0, LatestReceivedRequestSourceAddress=0;
static uint8_t LatestReceivedReplyDestinationAddress=0, LatestReceivedReplySourceAddress=0;
static uint8_t MyAddress;

DigitalIn address_in_bit_0(PH_0, PullUp);
DigitalIn address_in_bit_1(PH_1, PullUp);
 
/*!
 * Radio events function pointer
 */
static RadioEvents_t RadioEvents;
 
/*
 *  Global variables declarations
 */
static SX1272MB2xAS Radio( NULL );
 
const uint8_t RequestMsg[] = "REQUEST-";
const uint8_t ReplyMsg[] = "REPLY-";
 
static uint16_t RxBufferSize = BUFFER_SIZE;
static uint8_t RxBuffer[BUFFER_SIZE];

static Thread s_thread_manage_communication;
static EventQueue s_eq_manage_communication;
//static Mutex s_state_mutex;

void event_proc_communication_cycle()
{
    AppStates_t currentState;

    static Timer s_state_timer;
    static AppStates_t previousState=INITIAL;
    static int stateChangeTimeStamp;

    uint16_t bufferSize=BUFFER_SIZE;
    uint8_t buffer[BUFFER_SIZE];

    uint16_t payloadLen;

    //s_state_mutex.lock();
    currentState = State;
    //s_state_mutex.unlock();

    if(previousState!=currentState)
    {
        stateChangeTimeStamp=s_state_timer.read_ms();
        previousState=currentState;
    }
    else
    {
        if(s_state_timer.read_ms()-stateChangeTimeStamp > STATE_MACHINE_STALE_STATE_TIMEOUT)
        {
            sx1272_debug_if( DEBUG_MESSAGE, "...(state-machine timeout, resetting to initial state)...\n" );

            //s_state_mutex.lock();
            currentState=State=INITIAL;
            //s_state_mutex.unlock();
        }
    }

    switch( currentState )
    {
        case INITIAL:

            Radio.Sleep();

            //s_state_mutex.lock();
            memset(RxBuffer,0x00,BUFFER_SIZE);
            RxBufferSize=0;
            State = IDLE_RX_WAITING_FOR_REQUEST;
            //s_state_mutex.unlock();

            Radio.Rx(RX_TIMEOUT_VALUE);

            break;

        case IDLE_RX_WAITING_FOR_REQUEST:
            //sx1272_debug_if( DEBUG_MESSAGE, "...(waiting for request)...\n" );
            break;

        case RX_WAITING_FOR_REPLY:
            sx1272_debug_if( DEBUG_MESSAGE, "...(waiting for reply)...\n" );
            break;

        case RX_DONE_RECEIVED_REQUEST:

            uint8_t latestReceivedRequestDestinationAddress, latestReceivedRequestSourceAddress;
            uint16_t latestReceivedRequestCounter;
            
            //s_state_mutex.lock();
            sx1272_debug_if( DEBUG_MESSAGE, "*** REQUEST RECEIVED ('%s') ***\n", RxBuffer);
            latestReceivedRequestDestinationAddress=LatestReceivedRequestDestinationAddress;
            latestReceivedRequestSourceAddress=LatestReceivedRequestSourceAddress;
            latestReceivedRequestCounter=LatestReceivedRequestCounter;
            //s_state_mutex.unlock();

            if(latestReceivedRequestDestinationAddress!=MyAddress)
            {
                sx1272_debug_if( DEBUG_MESSAGE, "...request is not for me...\n");

                //s_state_mutex.lock();
                State = INITIAL;
                //s_state_mutex.unlock();
                
                break;
            }

            sx1272_debug_if( DEBUG_MESSAGE, "...REQUEST IS FOR ME...\n");

            //s_state_mutex.lock();
            State = TX_WAITING_FOR_REPLY_SENT;
            //s_state_mutex.unlock();

            // Attesa di durata sufficiente per permettere a chi ha inviato la request di mettersi
            // in ascolto della reply
            wait_ms(REQUEST_REPLY_DELAY);

            // Send the next REPLY frame
            sprintf((char*)buffer, "%s%u|%u|%u",(const char*)ReplyMsg, latestReceivedRequestCounter, MyAddress, latestReceivedRequestSourceAddress);
            
            payloadLen=strlen((const char*)buffer) + 1;

            if(payloadLen<bufferSize)
            {
                memset(buffer+payloadLen,0xFF,bufferSize-payloadLen);
            }

            Radio.Send( buffer, bufferSize );

            break;

        case RX_DONE_RECEIVED_REPLY:
            
            uint8_t latestReceivedReplyDestinationAddress, latestReceivedReplySourceAddress;
            uint16_t latestReceivedReplyCounter, counter;
            
            //s_state_mutex.lock();
            sx1272_debug_if( DEBUG_MESSAGE, "*** REPLY RECEIVED ('%s') ***\n", RxBuffer);
            latestReceivedReplyDestinationAddress=LatestReceivedReplyDestinationAddress;
            latestReceivedReplySourceAddress=LatestReceivedReplySourceAddress;
            latestReceivedReplyCounter=LatestReceivedReplyCounter;
            counter=Counter;
            //s_state_mutex.unlock();

            if(latestReceivedReplyDestinationAddress!=MyAddress)
            {
                sx1272_debug_if( DEBUG_MESSAGE, "...reply is not for me...\n");
            }
            else
            {
                sx1272_debug_if( DEBUG_MESSAGE, "...REPLY IS FOR ME...\n");

                if(latestReceivedReplyCounter==counter)
                {
                    sx1272_debug_if( DEBUG_MESSAGE, "...AND REPLY IS RIGHT\n");
                }
                else
                {
                    sx1272_debug_if( DEBUG_MESSAGE, "...BUT REPLY IS WRONG (REQUEST: %u, REPLY: %u)\n",counter, latestReceivedReplyCounter);
                }
            }

            //s_state_mutex.lock();
            State = INITIAL;
            //s_state_mutex.unlock();

            break;

        case TX_DONE_SENT_REQUEST:

            sx1272_debug_if( DEBUG_MESSAGE, "...request sent\n" );

            Radio.Sleep();
           
            //s_state_mutex.lock();
            State = RX_WAITING_FOR_REPLY;
            //s_state_mutex.unlock();

            Radio.Rx(RX_TIMEOUT_VALUE);

            break;

        case TX_DONE_SENT_REPLY:

            sx1272_debug_if( DEBUG_MESSAGE, "...REPLY SENT\n" ); 
           
            //s_state_mutex.lock();
            State = INITIAL;
            //s_state_mutex.unlock();

            break;

        case TX_WAITING_FOR_REQUEST_SENT:

            sx1272_debug_if( DEBUG_MESSAGE, "...waiting for request being sent...\n" ); 

            break;

        case TX_WAITING_FOR_REPLY_SENT:

            sx1272_debug_if( DEBUG_MESSAGE, "...waiting for reply being sent...\n" ); 

            break;
    }
}

static uint8_t DestinationAddress=0;
#define MAX_DESTINATION_ADDRESS 4

void event_proc_send_data()
{
    uint16_t bufferSize=BUFFER_SIZE;
    uint8_t buffer[BUFFER_SIZE];

    AppStates_t currentState;

    uint16_t counter;
    uint8_t destinationAddress;

    //s_state_mutex.lock();
    currentState=State;
    destinationAddress=DestinationAddress;
    counter=++Counter;
    //s_state_mutex.unlock();

    if(currentState!=IDLE_RX_WAITING_FOR_REQUEST) return;

    sx1272_debug_if( DEBUG_MESSAGE, "\n*** SENDING NEW REQUEST : ('%s%u|%u|%u') ***\n",(const char*)RequestMsg, counter, MyAddress, destinationAddress );

    // Send the next REQUEST frame
    sprintf((char*)buffer, "%s%u|%u|%u",(const char*)RequestMsg, counter, MyAddress, destinationAddress);
    
    uint16_t payloadLen=strlen((const char*)buffer)+1;

    if(payloadLen<bufferSize)
    {
        memset(buffer+payloadLen,0xFF,bufferSize-payloadLen);
    }

    Radio.Send( buffer, bufferSize );

    //s_state_mutex.lock();
    DestinationAddress++;
    if(DestinationAddress==MyAddress) DestinationAddress++;
    if(DestinationAddress>MAX_DESTINATION_ADDRESS) DestinationAddress=0;
    State=TX_WAITING_FOR_REQUEST_SENT;
    //s_state_mutex.unlock();
}

void btn_interrupt_handler()
{
    s_eq_manage_communication.call(event_proc_send_data);
}
 
void OnTxDone( void )
{
    sx1272_debug_if( DEBUG_MESSAGE, "> OnTxDone\n" );

    //s_state_mutex.lock();

    if(State==TX_WAITING_FOR_REQUEST_SENT)
    {
        sx1272_debug_if( DEBUG_MESSAGE, "...request tx done...\n" );

        State = TX_DONE_SENT_REQUEST;
    }
    else if (TX_WAITING_FOR_REPLY_SENT)
    {
        sx1272_debug_if( DEBUG_MESSAGE, "...reply tx done...\n" );

        State = TX_DONE_SENT_REPLY;
    }

    //s_state_mutex.unlock();    
}
 
void OnRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr )
{
    sx1272_debug_if( DEBUG_MESSAGE, "> OnRxDone (RSSI:%d, SNR:%d): %s (len: %d) \n", rssi, snr, (const char*)payload, size);

    //s_state_mutex.lock();

    RxBufferSize = size;

    if( size > 0 )
    {
        memcpy( RxBuffer, payload, size );

        if(State==IDLE_RX_WAITING_FOR_REQUEST && strncmp((const char*)RxBuffer, (const char*)RequestMsg, strlen((const char*)RequestMsg)) == 0)
        {
            sx1272_debug_if( DEBUG_MESSAGE, "...request rx done...\n" );

            char* dashPtr=NULL;
            char* pipePtr1=NULL;
            char* pipePtr2=NULL;

            dashPtr=strchr((const char*)RxBuffer,'-');

            if(dashPtr)
            {
                pipePtr1=strchr(dashPtr+1,'|');

                if(pipePtr1)
                {
                    pipePtr2=strchr(pipePtr1+1,'|');
                }
            }
            
            if(dashPtr && pipePtr1 && pipePtr2)
            {
                *pipePtr1='\0';
                *pipePtr2='\0';
                LatestReceivedRequestCounter=atoi(dashPtr+1);
                LatestReceivedRequestSourceAddress=atoi(pipePtr1+1);
                LatestReceivedRequestDestinationAddress=atoi(pipePtr2+1);
                *pipePtr1='|';
                *pipePtr2='|';
            }
            else
            {
                LatestReceivedRequestCounter=0;
            }

            State = RX_DONE_RECEIVED_REQUEST;
        }
        else if(State==RX_WAITING_FOR_REPLY && strncmp((const char*)RxBuffer, (const char*)ReplyMsg, strlen((const char*)ReplyMsg)) == 0)
        { 
            sx1272_debug_if( DEBUG_MESSAGE, "...reply rx done...\n" );

            char* dashPtr=NULL;
            char* pipePtr1=NULL;
            char* pipePtr2=NULL;

            dashPtr=strchr((const char*)RxBuffer,'-');

            if(dashPtr)
            {
                pipePtr1=strchr(dashPtr+1,'|');

                if(pipePtr1)
                {
                    pipePtr2=strchr(pipePtr1+1,'|');
                }
            }
            
            if(dashPtr && pipePtr1 && pipePtr2)
            {
                *pipePtr1='\0';
                *pipePtr2='\0';
                LatestReceivedReplyCounter=atoi(dashPtr+1);
                LatestReceivedReplySourceAddress=atoi(pipePtr1+1);
                LatestReceivedReplyDestinationAddress=atoi(pipePtr2+1);
                *pipePtr1='|';
                *pipePtr2='|';
            }
            else
            {
                LatestReceivedReplyCounter=0;
            }

            State = RX_DONE_RECEIVED_REPLY;
        }
        else // ricezione valida, ma arrivata in uno stato non previsto
        {   
            sx1272_debug_if( DEBUG_MESSAGE, "...valid but unexpected rx done ('%s'), resetting to idle state...\n", (const char*)RxBuffer);
            
            State = INITIAL;
        }
    }

    //s_state_mutex.unlock();
}
 
void OnTxTimeout( void )
{
    sx1272_debug_if( DEBUG_MESSAGE, "> OnTxTimeout\n" );

    Radio.Sleep();

    //s_state_mutex.lock();
    State=IDLE_RX_WAITING_FOR_REQUEST;
    //s_state_mutex.unlock();

    Radio.Rx(RX_TIMEOUT_VALUE);
}
 
void OnRxTimeout( void )
{
    sx1272_debug_if( DEBUG_MESSAGE, "> OnRxTimeout\n" );

    AppStates_t currentState;

    //s_state_mutex.lock();
    currentState=State;
    //s_state_mutex.unlock();

    if(currentState!=IDLE_RX_WAITING_FOR_REQUEST && currentState!=RX_WAITING_FOR_REPLY) return;

    sx1272_debug_if( DEBUG_MESSAGE, "...rx timeout: resetting state to idle...\n" );

    //s_state_mutex.lock();
    State = IDLE_RX_WAITING_FOR_REQUEST;
    //s_state_mutex.unlock();

    Radio.Sleep();
    Radio.Rx(RX_TIMEOUT_VALUE);
}
 
void OnRxError( void )
{
    sx1272_debug_if( DEBUG_MESSAGE, "> OnRxError\n" );

    //s_state_mutex.lock();
    State = INITIAL;;
    //s_state_mutex.unlock();

    sx1272_debug_if( DEBUG_MESSAGE, "...rx error: resetting state to idle...\n" );
}

int main( void ) 
{
    sx1272_debug_if( DEBUG_MESSAGE,"LoRa Request/Reply Demo Application (blue button to send acked message)\n");

    MyAddress=1 + (address_in_bit_0.read() ? 0 : 1) +  (address_in_bit_1.read() ? 0 : 2);

    sx1272_debug_if( DEBUG_MESSAGE,"\n\n---------------------\n");
    sx1272_debug_if( DEBUG_MESSAGE,"|   MY_ADDRESS: %u   |\n", MyAddress);
    sx1272_debug_if( DEBUG_MESSAGE,"---------------------\n\n");
 
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
        sx1272_debug_if( DEBUG_MESSAGE, "Radio could not be detected!\n", NULL );
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
                         LORA_IQ_INVERSION_ON, TX_TIMEOUT_VALUE );
 
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