#include "mbed.h"

#include "lora_state_machine.h"

static DigitalIn lora_address_in_bit_0(PH_0, PullUp);
static DigitalIn lora_address_in_bit_1(PH_1, PullUp);

static InterruptIn btn(BUTTON1);

#define LORA_EVENT_PROC_COMMUNICATION_CYCLE_INTERVAL         100       // in ms

static Thread s_thread_manage_lora_communication;
static EventQueue s_eq_manage_lora_communication;

// Variabili per demo
static uint8_t s_lora_MyAddress;

static uint16_t s_lora_Counter=0;
static uint8_t s_lora_DestinationAddress=0;

#define MAX_DESTINATION_ADDRESS 4

void event_proc_send_data_through_lora()
{
    s_lora_Counter++;
    s_lora_DestinationAddress++;

    if(s_lora_DestinationAddress==s_lora_MyAddress) s_lora_DestinationAddress++;
    if(s_lora_DestinationAddress>MAX_DESTINATION_ADDRESS) s_lora_DestinationAddress=0;

    lora_state_machine_send_request(s_lora_Counter, s_lora_DestinationAddress);
}

void btn_interrupt_handler()
{
    s_eq_manage_lora_communication.call(event_proc_send_data_through_lora);
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

    if(lora_init_ret_val!=0)
    {
        printf("LoRa initialization FAILED!");

        return lora_init_ret_val;
    }

    s_thread_manage_lora_communication.start(callback(&s_eq_manage_lora_communication, &EventQueue::dispatch_forever));
}