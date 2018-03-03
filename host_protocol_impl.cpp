#include "mbed.h"

#include <string>

#include "BufferedSerial.h"

#include "host_protocol_impl.h"

static Timer s_timer_1;

#define PROTOCOL_BUFFER_SIZE 32

BufferedSerial pc_buffered_serial(PB_10, PB_11);

static Thread s_thread_serial_worker;
static EventQueue s_eq_serial_worker;

static Thread s_thread_command_handler_worker;
static EventQueue* s_p_eq_command_handler_worker;

#define PROTOCOL_TIMEOUT_MS (50000)

static ProtocolStates current_protocol_state;
static std::string current_protocol_content;
static int current_protocol_timeout_event_id;

static std::string s_latest_received_command, s_latest_sent_command;

void event_proc_protocol_timeout_handler()
{
    current_protocol_content.clear();
    current_protocol_state = WAITING_START;

    printf("[HOST PROTOCOL_HANDLER - %d] TIMEOUT (%u ms), Stato Settato a 'WAITING_START'\n", s_timer_1.read_ms(), PROTOCOL_TIMEOUT_MS);
}

host_protocol_notify_command_received_callback_t host_protocol_notify_command_received_callback_instance;

void event_proc_command_handler(std::string *pcontent)
{
    if (pcontent->size() != 0) printf("[HOST COMMAND_HANDLER - %d] Ricevuto Comando: '%s'\n", s_timer_1.read_ms(), pcontent->c_str());

    s_latest_received_command = *pcontent;

    if(host_protocol_notify_command_received_callback_instance) host_protocol_notify_command_received_callback_instance();

    delete pcontent;
}

void event_proc_protocol_worker()
{
    while (pc_buffered_serial.readable())
    {
        char c = pc_buffered_serial.getc();

        if (c == '\r' || c == '\n')
            continue;

        //printf("[PROTOCOL_HANDLER - %d] Ricevuto '%c'\n", s_timer_1.read_ms(), c);

        switch (current_protocol_state)
        {
            case WAITING_START:
                
                switch (c)
                {
                    case '!':
                        current_protocol_content.clear();
                        current_protocol_state = WAITING_END;
                        if (current_protocol_timeout_event_id != 0)
                            s_eq_serial_worker.cancel(current_protocol_timeout_event_id);
                        current_protocol_timeout_event_id = s_eq_serial_worker.call_in(PROTOCOL_TIMEOUT_MS, event_proc_protocol_timeout_handler);

                        //printf("[PROTOCOL_HANDLER - %d] Stato Settato a 'WAITING_END'\n", s_timer_1.read_ms());
                        
                        break;
                }

                break;

            case WAITING_END:

                switch (c)
                {
                    case '!':
                        current_protocol_content.clear();
                        current_protocol_state = WAITING_END;
                        if (current_protocol_timeout_event_id != 0) s_eq_serial_worker.cancel(current_protocol_timeout_event_id);
                        current_protocol_timeout_event_id = s_eq_serial_worker.call_in(PROTOCOL_TIMEOUT_MS, event_proc_protocol_timeout_handler);

                        //printf("[PROTOCOL_HANDLER - %d] Stato Settato a 'WAITING_END'\n", s_timer_1.read_ms());

                        break;

                    case '#':
                        s_p_eq_command_handler_worker->call(event_proc_command_handler, new std::string(current_protocol_content));

                        current_protocol_content.clear();
                        current_protocol_state = WAITING_START;
                        if (current_protocol_timeout_event_id != 0) s_eq_serial_worker.cancel(current_protocol_timeout_event_id);
                        current_protocol_timeout_event_id = 0;

                        //printf("[PROTOCOL_HANDLER - %d] Stato Settato a 'WAITING_START'\n", s_timer_1.read_ms());

                        break;

                    default:
                        current_protocol_content.push_back(c);
                        break;
                }
            
                break;
        }
    }
}

void host_protocol_initialize(EventQueue* eventQueue)
{
    pc_buffered_serial.baud(115200);

    s_eq_serial_worker.call_every(100, event_proc_protocol_worker);
    s_thread_serial_worker.start(callback(&s_eq_serial_worker, &EventQueue::dispatch_forever));

    s_p_eq_command_handler_worker = eventQueue; 

    s_timer_1.start();
}

void host_protocol_reset()
{
}

void host_protocol_fill_with_rx_buffer_dump(char* destBuffer, size_t destBufferSize)
{
    sprintf(destBuffer, s_latest_received_command.c_str());
}

void host_protocol_fill_with_tx_buffer_dump(char* destBuffer, uint8_t* txBuffer, size_t destBufferSize)
{
    char srcBuffer[PROTOCOL_BUFFER_SIZE];
    memcpy(srcBuffer,txBuffer,PROTOCOL_BUFFER_SIZE);
    srcBuffer[PROTOCOL_BUFFER_SIZE-1]='\0';

    strcpy(destBuffer,srcBuffer);
}

uint16_t host_protocol_get_latest_received_reply_payload()
{
    return atoi(s_latest_received_command.c_str());
}

uint16_t host_protocol_get_latest_received_request_payload()
{
    return atoi(s_latest_received_command.c_str());
}

bool host_protocol_should_i_reply_to_latest_received_request()
{
    // TODO : implementare
    return host_protocol_get_latest_received_request_payload() % 2 == 0;
}

bool host_protocol_should_i_wait_for_reply_for_latest_sent_request()
{
    // TODO : implementare
    return host_protocol_get_latest_received_reply_payload() % 2 == 0;
}

void host_protocol_send_reply_command(uint16_t replyPayload)
{
    pc_buffered_serial.printf("^%u@", replyPayload);
}

void host_protocol_send_request_command(uint16_t requestPayload)
{
    pc_buffered_serial.printf("!%u#", requestPayload);
}

bool host_protocol_is_latest_received_reply_right()
{
    // TODO : implementare
    return true;
}

void host_protocol_fill_create_request_buffer(uint8_t* buffer, uint16_t bufferSize, uint16_t argCounter)
{
    sprintf((char*)buffer,"!%u#", argCounter);
}

bool host_protocol_is_latest_received_command_a_request()
{
    return s_latest_received_command[0]!='0';
}

bool host_protocol_is_latest_received_command_a_reply()
{
    return s_latest_received_command[0]=='0';
}
