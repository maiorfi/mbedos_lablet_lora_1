typedef enum _protocol_states_enum {
    WAITING_START,
    WAITING_END,

} ProtocolStates;

typedef void (*host_protocol_notify_command_received_callback_t) ();

extern host_protocol_notify_command_received_callback_t host_protocol_notify_command_received_callback_instance;

void host_protocol_initialize(EventQueue* eventQueue);
void host_protocol_reset();

void host_protocol_fill_with_rx_buffer_dump(char* destBuffer, size_t destBufferSize);
void host_protocol_fill_with_tx_buffer_dump(char* destBuffer, uint8_t* txBuffer, size_t destBufferSize);

uint16_t host_protocol_get_latest_received_reply_payload();
uint16_t host_protocol_get_latest_received_request_payload();

bool host_protocol_should_i_reply_to_latest_received_request();
bool host_protocol_should_i_wait_for_reply_for_latest_sent_request();

void host_protocol_send_reply_command(uint16_t replyPayload);
void host_protocol_send_request_command(uint16_t requestPayload);

bool host_protocol_is_latest_received_reply_right();

void host_protocol_fill_create_request_buffer(uint8_t* buffer, uint16_t bufferSize, uint16_t argCounter);

bool host_protocol_is_latest_received_command_a_request();
bool host_protocol_is_latest_received_command_a_reply();
