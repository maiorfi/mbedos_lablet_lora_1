typedef enum
{
    OUTCOME_UNDEFINED=-1,
    OUTCOME_WAITING_FOR_REPLY_TIMEOUT=-2,
    OUTCOME_REPLY_WRONG=-3,
    OUTCOME_TIMEOUT_WAITING_FOR_REQUEST_SENT=-4,
    OUTCOME_TIMEOUT_WAITING_FOR_REPLY_SENT=-5,
    OUTCOME_TIMEOUT_GENERAL=-10,
    OUTCOME_REPLY_RIGHT=1,
    OUTCOME_REPLY_NOT_NEEDED=0,

} RequestOutcomes_t;

int lora_state_machine_initialize(uint8_t myAddress, Thread* thread, EventQueue* eventQueue);
void lora_state_machine_send_request(uint16_t argCounter, uint8_t argDestinationAddress);
void lora_event_proc_communication_cycle();
