typedef enum
{
    OUTCOME_PENDING=-1,
    OUTCOME_WAITING_FOR_REPLY_TIMEOUT=-2,
    OUTCOME_REPLY_WRONG=-3,
    OUTCOME_TIMEOUT_WAITING_FOR_REQUEST_SENT=-4,
    OUTCOME_TIMEOUT_WAITING_FOR_REPLY_SENT=-5,
    OUTCOME_INVALID_STATE=-6,
    OUTCOME_TIMEOUT_STUCK=-10,
    OUTCOME_REPLY_RIGHT=1,
    OUTCOME_REPLY_NOT_NEEDED=0,

} ReplyOutcomes_t;

typedef void (*notify_request_payload_callback_t)(uint16_t);
typedef uint16_t (*notify_request_payload_and_get_reply_payload_callback_t)(uint16_t);

extern Mutex lora_reply_cond_var_mutex;
extern ConditionVariable lora_reply_cond_var;
extern ReplyOutcomes_t lora_reply_outcome;
extern uint16_t lora_reply_payload;

extern notify_request_payload_callback_t lora_state_machine_notify_request_payload_callback;
extern notify_request_payload_and_get_reply_payload_callback_t lora_state_machine_notify_request_payload_and_get_reply_payload_callback;

int lora_state_machine_initialize(uint8_t myAddress, EventQueue* eventQueue);
ReplyOutcomes_t lora_state_machine_send_request(uint16_t argCounter, uint8_t argDestinationAddress);
void lora_event_proc_communication_cycle();
