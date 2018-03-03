#include "mbed.h"

#include "BufferedSerial.h"

#include "host_protocol_impl.h"

static Timer s_timer_1;

#define PROTOCOL_BUFFER_SIZE 32
#define PROTOCOL_PROC_COMMUNICATION_CYCLE_INTERVAL 20
#define PROTOCOL_UART_BAUD_RATE 115200

BufferedSerial pc_buffered_serial(PB_10, PB_11);

static Thread s_thread_serial_worker;
static EventQueue s_eq_serial_worker;

static EventQueue* s_p_eq_command_handler_worker;

#define PROTOCOL_TIMEOUT_MS (1000)

static ProtocolStates current_protocol_state;
static std::string current_protocol_content;
static int current_protocol_timeout_event_id;

static std::string s_latest_received_command, s_latest_sent_command;

static std::vector<std::string> s_latest_received_vector, s_latest_sent_vector;

void event_proc_protocol_timeout_handler()
{
    current_protocol_content.clear();
    current_protocol_state = WAITING_START;

    //printf("[HOST PROTOCOL_HANDLER - %d] TIMEOUT (%u ms), Stato Settato a 'WAITING_START'\n", s_timer_1.read_ms(), PROTOCOL_TIMEOUT_MS);
}

host_protocol_notify_command_received_callback_t host_protocol_notify_command_received_callback_instance;

void event_proc_command_handler(std::string *pcontent)
{
    //if (pcontent->size() != 0) printf("[HOST COMMAND_HANDLER - %d] Ricevuto Comando: '%s'\n", s_timer_1.read_ms(), pcontent->c_str());

    s_latest_received_command = *pcontent;

    split(s_latest_received_command.c_str(), s_latest_received_vector, '|');

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
                        if (current_protocol_timeout_event_id != 0) s_eq_serial_worker.cancel(current_protocol_timeout_event_id);
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
    pc_buffered_serial.baud(PROTOCOL_UART_BAUD_RATE);

    s_eq_serial_worker.call_every(PROTOCOL_PROC_COMMUNICATION_CYCLE_INTERVAL, event_proc_protocol_worker);
    s_thread_serial_worker.start(callback(&s_eq_serial_worker, &EventQueue::dispatch_forever));

    s_p_eq_command_handler_worker = eventQueue; 

    s_timer_1.start();
}

void host_protocol_reset()
{
    current_protocol_content.clear();
    current_protocol_state = WAITING_START;
    if (current_protocol_timeout_event_id != 0) s_eq_serial_worker.cancel(current_protocol_timeout_event_id);
    current_protocol_timeout_event_id = 0;
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
    return atoi(s_latest_received_vector[2].c_str());
}

uint8_t host_protocol_get_latest_received_reply_source_address()
{
    return atoi(s_latest_received_vector[1].c_str());
}

uint16_t host_protocol_get_latest_received_request_payload()
{
    return atoi(s_latest_received_vector[2].c_str());
}

uint8_t host_protocol_get_latest_received_request_destination_address()
{
    return atoi(s_latest_received_vector[1].c_str());
}

uint16_t host_protocol_get_latest_sent_request_payload()
{
    return atoi(s_latest_sent_command.c_str());
}

bool host_protocol_should_i_reply_to_latest_received_request()
{
    return s_latest_received_vector[0].compare("Q")==0;
}

bool host_protocol_should_i_wait_for_reply_for_latest_sent_request()
{
    return s_latest_sent_vector[0].compare("Q")==0;
}

void host_protocol_send_request_command(uint8_t* buffer, uint16_t bufferSize)
{
    pc_buffered_serial.write(buffer, strlen((const char*)buffer));

    s_latest_sent_command=(const char*)buffer;

    s_latest_sent_command=s_latest_sent_command.substr(1,strlen((const char*)buffer)-2);

    split(s_latest_sent_command.c_str(), s_latest_sent_vector, '|');
}

void host_protocol_send_reply_command(uint8_t* buffer, uint16_t bufferSize)
{
    pc_buffered_serial.write(buffer, strlen((const char*)buffer));

    s_latest_sent_command=(const char*)buffer;

    s_latest_sent_command=s_latest_sent_command.substr(1,strlen((const char*)buffer)-2);

    split(s_latest_sent_command.c_str(), s_latest_sent_vector, '|');
}

void host_protocol_fill_create_request_buffer(uint8_t* buffer, uint16_t bufferSize, uint16_t argPayload, uint8_t argSourceAddress, bool argRequiresReply)
{
    sprintf((char*)buffer,"^%s|%u|%u@", argRequiresReply ? "Q" : "C", argSourceAddress, argPayload);
}

void host_protocol_fill_create_reply_buffer(uint8_t* buffer, uint16_t bufferSize, uint16_t argPayload, uint8_t argDestinationAddress)
{
    sprintf((char*)buffer,"^R|%u|%u@", argDestinationAddress, argPayload);
}

bool host_protocol_is_latest_received_command_a_request()
{
    return s_latest_received_vector[0]=="Q" || s_latest_received_vector[0]=="C";
}

bool host_protocol_is_latest_received_command_a_reply()
{
    return s_latest_received_vector[0]=="R";
}

bool host_protocol_is_latest_received_reply_right()
{
    return atoi(s_latest_received_vector[2].c_str()) >= 0;
}

void split(const char *str, std::vector<std::string>& v, char c = ' ')
{
    v.clear();

    do
    {
        const char *begin = str;

        while(*str != c && *str)
            str++;

        v.push_back(std::string(begin, str));

    } while (0 != *str++);
}