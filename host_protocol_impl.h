#include <string>
#include <vector>

typedef enum _protocol_states_enum {
    WAITING_START,
    WAITING_END,

} ProtocolStates;

typedef void (*host_protocol_notify_command_received_callback_t) ();

extern host_protocol_notify_command_received_callback_t host_protocol_notify_command_received_callback_instance;

void host_protocol_initialize(EventQueue* eventQueue);
void host_protocol_reset();

uint16_t host_protocol_get_latest_received_reply_payload();
uint8_t host_protocol_get_latest_received_reply_source_address();

uint16_t host_protocol_get_latest_received_request_payload();
uint8_t host_protocol_get_latest_received_request_source_address();

bool host_protocol_should_i_reply_to_latest_received_request();
bool host_protocol_should_i_wait_for_reply_for_latest_sent_request();

void host_protocol_send_reply_command(uint8_t* buffer, uint16_t bufferSize);
void host_protocol_send_request_command(uint8_t* buffer, uint16_t bufferSize);

bool host_protocol_is_latest_received_reply_right();

void host_protocol_fill_create_request_buffer(uint8_t* buffer, uint16_t bufferSize, uint16_t argCounter, uint8_t argSourceAddress, bool argRequiresReply);
void host_protocol_fill_create_reply_buffer(uint8_t* buffer, uint16_t bufferSize, uint16_t argPayload, uint8_t argDestinationAddress);

bool host_protocol_is_latest_received_command_a_request();
bool host_protocol_is_latest_received_command_a_reply();

void host_protocol_fill_with_rx_buffer_dump(char* destBuffer, size_t destBufferSize);
void host_protocol_fill_with_tx_buffer_dump(char* destBuffer, uint8_t* txBuffer, size_t destBufferSize);

void split(const char *str, std::vector<std::string>& v, char c);