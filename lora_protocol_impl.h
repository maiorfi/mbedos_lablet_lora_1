extern uint16_t Counter;
extern uint16_t LatestReceivedRequestCounter;
extern uint16_t LatestReceivedReplyCounter;

extern uint8_t LatestReceivedRequestDestinationAddress;
extern uint8_t LatestReceivedRequestSourceAddress;
extern uint8_t LatestReceivedReplyDestinationAddress;
extern uint8_t LatestReceivedReplySourceAddress;

void protocol_initialize(uint8_t myAddress);
void protocol_reset();

bool protocol_is_latest_received_request_for_me();
bool protocol_should_i_reply_to_latest_received_request();
bool protocol_is_latest_received_reply_for_me();
void protocol_fill_create_reply_buffer(uint8_t* buffer, uint16_t bufferSize);
bool protocol_is_latest_received_reply_right();
void protocol_fill_create_request_buffer(uint8_t* buffer, uint16_t bufferSize);
void protocol_process_received_data(uint8_t *payload, uint16_t size);
bool protocol_is_received_data_a_request();
void protocol_process_received_data_as_request();
bool protocol_is_received_data_a_reply();
void protocol_process_received_data_as_reply();

void protocol_fill_with_rx_buffer_dump(char* destBuffer, size_t destBufferSize);
void protocol_fill_with_tx_buffer_dump(char* destBuffer, uint8_t* txBuffer, size_t destBufferSize);