#include "mbed.h"

#include "lora_state_machine.h"
#include "host_state_machine.h"

static DigitalIn lora_address_in_bit_0(PH_0, PullUp);
static DigitalIn lora_address_in_bit_1(PH_1, PullUp);

static InterruptIn btn(BUTTON1);

#define LORA_EVENT_PROC_COMMUNICATION_CYCLE_INTERVAL         100       // in ms
#define HOST_EVENT_PROC_COMMUNICATION_CYCLE_INTERVAL         100       // in ms

static Thread s_thread_manage_lora_communication;
static EventQueue s_eq_manage_lora_communication;

static Thread s_thread_manage_host_communication;
static EventQueue s_eq_manage_host_communication;

#define SEND_LORA_REQUEST_TIMEOUT 3000 // in ms
#define SEND_HOST_REQUEST_TIMEOUT 3000 // in ms

// Main
static EventQueue s_eq_main;

// Variabili per demo
static uint8_t s_lora_MyAddress;

static uint16_t s_lora_Counter=-1;
static uint8_t s_lora_DestinationAddress=-1;

static uint16_t s_host_Counter=-1;
static uint8_t s_host_SourceAddress=-1;

static int s_toggler_wheel=-1;

#define MAX_DESTINATION_ADDRESS 4

static bool s_toggler;

LoraReplyOutcomes_t send_lora_request(uint16_t argCounter, uint8_t argDestinationAddress, uint16_t* outReplyPayload)
{
    Timer timer;

    lora_reply_cond_var_mutex.lock();

    timer.start();

    bool timedOut=false;
    uint32_t timeLeft=SEND_LORA_REQUEST_TIMEOUT;

    lora_reply_outcome=LORA_OUTCOME_PENDING;

    LoraReplyOutcomes_t outcome = lora_state_machine_send_request(argCounter, argDestinationAddress);

    if(outcome != LORA_OUTCOME_PENDING)
    {
        lora_reply_cond_var_mutex.unlock();
        return outcome;
    }

    do
    {
        timedOut = lora_reply_cond_var.wait_for(timeLeft);

        uint32_t elapsed = timer.read_ms();
        timeLeft = elapsed > SEND_LORA_REQUEST_TIMEOUT ? 0 : SEND_LORA_REQUEST_TIMEOUT - elapsed;

    } while(lora_reply_outcome == LORA_OUTCOME_PENDING && !timedOut);

    outcome=timedOut ? LORA_OUTCOME_TIMEOUT_STUCK : lora_reply_outcome;

    if(outcome==LORA_OUTCOME_REPLY_RIGHT) *outReplyPayload = lora_reply_payload;

    lora_reply_cond_var_mutex.unlock();

    return outcome;
}

HostReplyOutcomes_t send_host_request(uint16_t argCounter, uint8_t argSourceAddress, bool argRequiresReply, uint16_t* outReplyPayload)
{
    Timer timer;

    host_reply_cond_var_mutex.lock();

    timer.start();

    bool timedOut=false;
    uint32_t timeLeft=SEND_HOST_REQUEST_TIMEOUT;

    host_reply_outcome=HOST_OUTCOME_PENDING;

    HostReplyOutcomes_t outcome = host_state_machine_send_request(argCounter, argSourceAddress, argRequiresReply);

    if(outcome != HOST_OUTCOME_PENDING)
    {
        host_reply_cond_var_mutex.unlock();
        return outcome;
    }

    do
    {
        timedOut = host_reply_cond_var.wait_for(timeLeft);

        uint32_t elapsed = timer.read_ms();
        timeLeft = elapsed > SEND_HOST_REQUEST_TIMEOUT ? 0 : SEND_HOST_REQUEST_TIMEOUT - elapsed;

    } while(host_reply_outcome == HOST_OUTCOME_PENDING && !timedOut);

    outcome=timedOut ? HOST_OUTCOME_TIMEOUT_STUCK : host_reply_outcome;

    if(outcome==HOST_OUTCOME_REPLY_RIGHT) *outReplyPayload = host_reply_payload;

    host_reply_cond_var_mutex.unlock();

    return outcome;
}

void event_proc_send_data_through_lora()
{
    s_lora_Counter++;
    s_lora_DestinationAddress++;

    if(s_lora_DestinationAddress==s_lora_MyAddress) s_lora_DestinationAddress++;
    if(s_lora_DestinationAddress>MAX_DESTINATION_ADDRESS) s_lora_DestinationAddress=0;

    printf("\n\n___________ LORA BEGIN %d -> %d ___________\n", s_lora_Counter, s_lora_DestinationAddress);

    uint16_t outReplyPayload=0xFFFF;

    int outcome = send_lora_request(s_lora_Counter, s_lora_DestinationAddress, &outReplyPayload);

    printf("__________ LORA END %d (0x%X) __________\n", outcome, outReplyPayload);
}

void event_proc_send_data_to_host()
{
    s_host_Counter++;
    s_host_SourceAddress++;
    s_toggler_wheel++;

    if(s_host_SourceAddress == s_lora_MyAddress) s_host_SourceAddress++;
    if(s_host_SourceAddress > MAX_DESTINATION_ADDRESS) s_host_SourceAddress=0;
    if(s_toggler_wheel > 6) s_toggler_wheel=0;
    

    printf("\n\n___________ HOST BEGIN %d ___________\n", s_host_Counter);

    uint16_t outReplyPayload=0xFFFF;

    int outcome = send_host_request(s_host_Counter, s_host_SourceAddress, s_toggler_wheel!=0, &outReplyPayload);

    printf("__________ HOST END %d (0x%X) __________\n", outcome, outReplyPayload);
}

void btn_interrupt_handler()
{
    /*if(s_toggler)*/ s_eq_main.call(event_proc_send_data_through_lora);
    /*else s_eq_main.call(event_proc_send_data_to_host);

    s_toggler=!s_toggler;*/
}

/*
void on_lora_state_machine_notify_request_callback(uint8_t requestSourceAddress, uint16_t requestPayload)
{
    printf("lora_state_machine_notify_request_payload_callback: Source=%u, Payload=%u\n", requestSourceAddress, requestPayload);
}

uint16_t loraGetReplyPayloadForRequestPayload(uint16_t requestPayload)
{
    return requestPayload;
}

uint16_t on_lora_state_machine_notify_request_and_get_reply_callback(uint8_t requestSourceAddress, uint16_t requestPayload)
{
    uint16_t replyPayload = loraGetReplyPayloadForRequestPayload(requestPayload);

    printf("lora_state_machine_notify_request_and_get_reply_callback: Source=%u, Payload=%u...returning %u\n", requestSourceAddress, requestPayload, replyPayload);

    return replyPayload;
}

void on_host_state_machine_notify_request_callback(uint8_t requestLoraDestinationAddress, uint16_t requestPayload)
{
    printf("host_state_machine_notify_request_payload_callback: LoraTargetAddress=%u, Payload=%u\n", requestLoraDestinationAddress, requestPayload);
}

uint16_t hostGetReplyPayloadForRequestPayload(uint16_t requestPayload)
{
    return requestPayload;
}

uint16_t on_host_state_machine_notify_request_and_get_reply_callback(uint8_t requestLoraDestinationAddress, uint16_t requestPayload)
{
    uint16_t replyPayload = hostGetReplyPayloadForRequestPayload(requestPayload);;
    
    printf("host_state_machine_notify_request_payload_and_get_reply_payload_callback: LoraTargetAddress=%u, Payload=%u...returning %u\n", requestLoraDestinationAddress, requestPayload, replyPayload);

    return replyPayload;
}*/

void on_lora_state_machine_notify_request_callback(uint8_t requestSourceAddress, uint16_t requestPayload)
{
    printf("<<< COMMAND RECEIVED through LORA channel: Source=%u, Payload=%u\n", requestSourceAddress, requestPayload);

    uint16_t outReplyPayload=0xFFFF;

    int outcome = send_host_request(requestPayload, requestSourceAddress, false, &outReplyPayload);

    printf(">>> COMMAND SENT to HOST: Outcome=%d, ReplyPayload=%u\n", outcome, outReplyPayload);
}

uint16_t on_lora_state_machine_notify_request_and_get_reply_callback(uint8_t requestSourceAddress, uint16_t requestPayload)
{
    printf("<<< QUERY RECEIVED through LORA channel: Source=%u, Payload=%u\n", requestSourceAddress, requestPayload);

    uint16_t outReplyPayload=0xFFFF;

    int outcome = send_host_request(requestPayload, requestSourceAddress, true, &outReplyPayload);

    printf(">>> QUERY SENT to HOST: Outcome=%d, RETURNING ReplyPayload=%u\n", outcome, outReplyPayload);

    return outReplyPayload;
}

void on_host_state_machine_notify_request_callback(uint8_t requestLoraDestinationAddress, uint16_t requestPayload)
{
    printf("<<< COMMAND RECEIVED from HOST: LoraTargetAddress=%u, Payload=%u\n", requestLoraDestinationAddress, requestPayload);

    uint16_t outReplyPayload=0xFFFF;

    int outcome = send_lora_request(requestPayload, requestLoraDestinationAddress, &outReplyPayload);

    printf(">>> COMMAND SENT to LORA node: Outcome=%d, ReplyPayload=%u\n", outcome, outReplyPayload);
}

uint16_t on_host_state_machine_notify_request_and_get_reply_callback(uint8_t requestLoraDestinationAddress, uint16_t requestPayload)
{
   printf("<<< QUERY RECEIVED from HOST: LoraTargetAddress=%u, Payload=%u\n", requestLoraDestinationAddress, requestPayload);

    uint16_t outReplyPayload=0xFFFF;

    int outcome = send_lora_request(requestPayload, requestLoraDestinationAddress, &outReplyPayload);

    printf(">>> QUERY SENT to LORA node: Outcome=%d, RETURNING ReplyPayload=%u\n", outcome, outReplyPayload);

    return outReplyPayload;
}
 
int main( void ) 
{
    printf("LoRa Request/Reply Demo Application (blue button to send a new LoRa request)\n");

    s_lora_MyAddress = 1 + (lora_address_in_bit_0.read() ? 0 : 1) +  (lora_address_in_bit_1.read() ? 0 : 2);

    printf("\n\n ------------------------\n");
    printf("|   MY LORA ADDRESS: %u   |\n", s_lora_MyAddress);
    printf(" ------------------------\n\n");
    
    s_eq_manage_lora_communication.call_every(LORA_EVENT_PROC_COMMUNICATION_CYCLE_INTERVAL, lora_event_proc_communication_cycle);
    s_eq_manage_host_communication.call_every(HOST_EVENT_PROC_COMMUNICATION_CYCLE_INTERVAL, host_event_proc_communication_cycle);

    btn.fall(&btn_interrupt_handler);

    int lora_init_ret_val=lora_state_machine_initialize(s_lora_MyAddress, &s_eq_manage_lora_communication);
    
    if(lora_init_ret_val!=0)
    {
        printf("LoRa initialization FAILED!");

        return lora_init_ret_val;
    }

    int host_init_ret_val=host_state_machine_initialize( &s_eq_manage_host_communication);

    if(host_init_ret_val!=0)
    {
        printf("Host initialization FAILED!");

        return host_init_ret_val;
    }

    lora_state_machine_notify_request_callback = on_lora_state_machine_notify_request_callback;
    lora_state_machine_notify_request_and_get_reply_callback = on_lora_state_machine_notify_request_and_get_reply_callback;

    host_state_machine_notify_request_callback = on_host_state_machine_notify_request_callback;
    host_state_machine_notify_request_and_get_reply_callback = on_host_state_machine_notify_request_and_get_reply_callback;

    s_thread_manage_lora_communication.start(callback(&s_eq_manage_lora_communication, &EventQueue::dispatch_forever));
    s_thread_manage_host_communication.start(callback(&s_eq_manage_host_communication, &EventQueue::dispatch_forever));

    s_eq_main.dispatch_forever();
}