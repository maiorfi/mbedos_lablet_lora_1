typedef enum
{
    LORA_OUTCOME_PENDING=-1,
    LORA_OUTCOME_WAITING_FOR_REPLY_TIMEOUT=-2,
    LORA_OUTCOME_REPLY_WRONG=-3,
    LORA_OUTCOME_TIMEOUT_WAITING_FOR_REQUEST_SENT=-4,
    LORA_OUTCOME_TIMEOUT_WAITING_FOR_REPLY_SENT=-5,
    LORA_OUTCOME_INVALID_STATE=-6,
    LORA_OUTCOME_TIMEOUT_STUCK=-10,
    LORA_OUTCOME_REPLY_RIGHT=1,
    LORA_OUTCOME_REPLY_NOT_NEEDED=0,

} LoraReplyOutcomes_t;

typedef void (*lora_notify_request_callback_t)(uint8_t, uint16_t);
typedef uint16_t (*lora_notify_request_and_get_reply_callback_t)(uint8_t, uint16_t);

extern Mutex lora_reply_cond_var_mutex;
extern ConditionVariable lora_reply_cond_var;
extern LoraReplyOutcomes_t lora_reply_outcome;
extern uint16_t lora_reply_payload;

extern lora_notify_request_callback_t lora_state_machine_notify_request_callback;
extern lora_notify_request_and_get_reply_callback_t lora_state_machine_notify_request_and_get_reply_callback;

int lora_state_machine_initialize(uint8_t myAddress, EventQueue* eventQueue);
LoraReplyOutcomes_t lora_state_machine_send_request(uint16_t argCounter, uint8_t argDestinationAddress, bool argRequiresReply);
void lora_event_proc_communication_cycle();
