#include "mbed.h"

#include "lora_state_machine.h"

static DigitalIn lora_address_in_bit_0(PH_0, PullUp);
static DigitalIn lora_address_in_bit_1(PH_1, PullUp);

static InterruptIn btn(BUTTON1);

#define LORA_EVENT_PROC_COMMUNICATION_CYCLE_INTERVAL         100       // in ms

static Thread s_thread_manage_lora_communication;
static EventQueue s_eq_manage_lora_communication;

// Main
static EventQueue s_eq_main;

// Variabili per demo
static uint8_t s_lora_MyAddress;

static uint16_t s_lora_Counter=0;
static uint8_t s_lora_DestinationAddress=0;

#define MAX_DESTINATION_ADDRESS 4

#define SEND_LORA_REQUEST_TIMEOUT 3000 // in ms

ReplyOutcomes_t send_lora_request(uint16_t argCounter, uint8_t argDestinationAddress, uint16_t* outReplyPayload)
{
    ReplyOutcomes_t outcome=OUTCOME_UNDEFINED;
    Timer timer;

    lora_reply_cond_var_mutex.lock();

    timer.start();

    bool timedOut=false;
    uint32_t timeLeft=SEND_LORA_REQUEST_TIMEOUT;

    lora_reply_outcome=OUTCOME_UNDEFINED;

    lora_state_machine_send_request(argCounter, argDestinationAddress);

    do
    {
        timedOut = lora_reply_cond_var.wait_for(timeLeft);

        uint32_t elapsed = timer.read_ms();
        timeLeft = elapsed > SEND_LORA_REQUEST_TIMEOUT ? 0 : SEND_LORA_REQUEST_TIMEOUT - elapsed;

    } while(lora_reply_outcome == OUTCOME_UNDEFINED && !timedOut);

    outcome=timedOut ? OUTCOME_TIMEOUT_GENERAL : lora_reply_outcome;

    if(outcome==OUTCOME_REPLY_RIGHT) *outReplyPayload = lora_reply_payload;

    lora_reply_cond_var_mutex.unlock();

    return outcome;
}

void event_proc_send_data_through_lora()
{
    s_lora_Counter++;
    s_lora_DestinationAddress++;

    if(s_lora_DestinationAddress==s_lora_MyAddress) s_lora_DestinationAddress++;
    if(s_lora_DestinationAddress>MAX_DESTINATION_ADDRESS) s_lora_DestinationAddress=0;

    printf("\n\n___________ BEGIN %d -> %d ___________\n", s_lora_Counter, s_lora_DestinationAddress);

    uint16_t outReplyPayload=0xFFFF;

    int outcome = send_lora_request(s_lora_Counter, s_lora_DestinationAddress, &outReplyPayload);

    printf("__________   END %d (0x%X)  __________\n", outcome, outReplyPayload);
}

void btn_interrupt_handler()
{
    s_eq_main.call(event_proc_send_data_through_lora);
}

void lora_state_machine_notify_request_payload_callback_instance(uint16_t requestPayload)
{
    printf("lora_state_machine_notify_request_payload_callback_instance: %u\n", requestPayload);
}

uint16_t lora_state_machine_notify_request_payload_and_get_reply_payload_callback_instance(uint16_t requestPayload)
{
    printf("lora_state_machine_notify_request_payload_and_get_reply_payload_callback_instance: %u...returning %u\n", requestPayload, requestPayload);

    return requestPayload;
}
 
int main( void ) 
{
    printf("LoRa Request/Reply Demo Application (blue button to send a new LoRa request)\n");

    s_lora_MyAddress = 1 + (lora_address_in_bit_0.read() ? 0 : 1) +  (lora_address_in_bit_1.read() ? 0 : 2);

    printf("\n\n ------------------------\n");
    printf("|   MY LORA ADDRESS: %u   |\n", s_lora_MyAddress);
    printf(" ------------------------\n\n");
    
    s_eq_manage_lora_communication.call_every(LORA_EVENT_PROC_COMMUNICATION_CYCLE_INTERVAL, lora_event_proc_communication_cycle);

    btn.fall(&btn_interrupt_handler);

    int lora_init_ret_val=lora_state_machine_initialize(s_lora_MyAddress, &s_thread_manage_lora_communication, &s_eq_manage_lora_communication);

    lora_state_machine_notify_request_payload_callback = lora_state_machine_notify_request_payload_callback_instance;
    lora_state_machine_notify_request_payload_and_get_reply_payload_callback = lora_state_machine_notify_request_payload_and_get_reply_payload_callback_instance;

    if(lora_init_ret_val!=0)
    {
        printf("LoRa initialization FAILED!");

        return lora_init_ret_val;
    }

    s_thread_manage_lora_communication.start(callback(&s_eq_manage_lora_communication, &EventQueue::dispatch_forever));

    s_eq_main.dispatch_forever();
}