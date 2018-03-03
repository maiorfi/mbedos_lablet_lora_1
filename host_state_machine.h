typedef enum
{
    HOST_OUTCOME_PENDING=-1,
    HOST_OUTCOME_WAITING_FOR_REPLY_TIMEOUT=-2,
    HOST_OUTCOME_REPLY_WRONG=-3,
    HOST_OUTCOME_TIMEOUT_WAITING_FOR_REQUEST_SENT=-4,
    HOST_OUTCOME_TIMEOUT_WAITING_FOR_REPLY_SENT=-5,
    HOST_OUTCOME_INVALID_STATE=-6,
    HOST_OUTCOME_TIMEOUT_STUCK=-10,
    HOST_OUTCOME_REPLY_RIGHT=1,
    HOST_OUTCOME_REPLY_NOT_NEEDED=0,

} HostReplyOutcomes_t;

typedef void (*host_notify_request_callback_t)(uint8_t, uint16_t);
typedef uint16_t (*host_notify_request_and_get_reply_callback_t)(uint8_t, uint16_t);

extern Mutex host_reply_cond_var_mutex;
extern ConditionVariable host_reply_cond_var;
extern HostReplyOutcomes_t host_reply_outcome;
extern uint16_t host_reply_payload;

extern host_notify_request_callback_t host_state_machine_notify_request_callback;
extern host_notify_request_and_get_reply_callback_t host_state_machine_notify_request_and_get_reply_callback;

int host_state_machine_initialize(EventQueue* eventQueue);
HostReplyOutcomes_t host_state_machine_send_request(uint16_t argCounter, uint8_t argSourceAddress, bool argRequiresReply);
void host_event_proc_communication_cycle();
