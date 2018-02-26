void lora_state_machine_send_request(uint16_t argCounter, uint8_t argDestinationAddress);
int lora_state_machine_initialize(uint8_t myAddress, Thread* thread, EventQueue* eventQueue);
void lora_event_proc_communication_cycle();