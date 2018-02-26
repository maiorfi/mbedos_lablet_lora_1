void lora_state_machine_send_data();
int lora_state_machine_initialize(uint8_t myAddress, Thread* thread, EventQueue* eventQueue);
void lora_event_proc_communication_cycle();