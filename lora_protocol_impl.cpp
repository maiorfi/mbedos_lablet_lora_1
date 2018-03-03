#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "lora_protocol_impl.h"

#define lora_protocol_BUFFER_SIZE 32

static uint16_t RxBufferSize = lora_protocol_BUFFER_SIZE;
static uint8_t RxBuffer[lora_protocol_BUFFER_SIZE];

static uint8_t DestinationAddress=0;

static uint16_t Counter=0, LatestReceivedRequestCounter=0, LatestReceivedReplyCounter=0;
static uint8_t LatestReceivedRequestDestinationAddress=0, LatestReceivedRequestSourceAddress=0;
static uint8_t LatestReceivedReplyDestinationAddress=0, LatestReceivedReplySourceAddress=0;

static uint8_t MyAddress;

static const uint8_t RequestMsg[] = "REQUEST-";
static const uint8_t ReplyMsg[] = "REPLY-";

void lora_protocol_initialize(uint8_t myAddress)
{
    MyAddress=myAddress;
}

void lora_protocol_reset()
{
    memset(RxBuffer,0x00,lora_protocol_BUFFER_SIZE);
    RxBufferSize=0;
}

bool lora_protocol_is_latest_received_request_for_me()
{
    return LatestReceivedRequestDestinationAddress==MyAddress || LatestReceivedRequestDestinationAddress==0;
}

bool lora_protocol_should_i_reply_to_latest_received_request()
{
    return LatestReceivedRequestDestinationAddress==MyAddress;
}

bool lora_protocol_is_latest_received_reply_for_me()
{
    return LatestReceivedReplyDestinationAddress==MyAddress;
}

void lora_protocol_fill_create_reply_buffer(uint8_t* buffer, uint16_t bufferSize, uint16_t replyPayload)
{
    sprintf((char*)buffer, "%s%u|%u|%u",(const char*)ReplyMsg, replyPayload, MyAddress, LatestReceivedRequestSourceAddress);
}

bool lora_protocol_is_latest_received_reply_right()
{
    return LatestReceivedReplyCounter==Counter;
}

uint16_t lora_protocol_get_latest_received_reply_payload()
{
    return LatestReceivedReplyCounter;
}

uint8_t lora_protocol_get_latest_received_reply_source_address()
{
    return LatestReceivedReplySourceAddress;
}

uint16_t lora_protocol_get_latest_received_request_payload()
{
    return LatestReceivedRequestCounter;
}

uint8_t lora_protocol_get_latest_received_request_source_address()
{
    return LatestReceivedRequestSourceAddress;
}

void lora_protocol_fill_create_request_buffer(uint8_t* buffer, uint16_t bufferSize,
    uint16_t argCounter, uint8_t argDestinationAddress)
{
    Counter=argCounter;
    DestinationAddress=argDestinationAddress;

    sprintf((char*)buffer, "%s%u|%u|%u",(const char*)RequestMsg, Counter, MyAddress, DestinationAddress);
}

bool lora_protocol_should_i_wait_for_reply_for_latest_sent_request()
{
    return DestinationAddress!=0;
}

void lora_protocol_process_received_data(uint8_t *payload, uint16_t size)
{
    RxBufferSize = size;

    memcpy( RxBuffer, payload, size );
}

bool lora_protocol_is_received_data_a_request()
{
    return strncmp((const char*)RxBuffer, (const char*)RequestMsg, strlen((const char*)RequestMsg)) == 0;
}

void lora_protocol_process_received_data_as_request()
{
    char* dashPtr=NULL;
    char* pipePtr1=NULL;
    char* pipePtr2=NULL;

    dashPtr=strchr((const char*)RxBuffer,'-');

    if(dashPtr)
    {
        pipePtr1=strchr(dashPtr+1,'|');

        if(pipePtr1)
        {
            pipePtr2=strchr(pipePtr1+1,'|');
        }
    }
    
    if(dashPtr && pipePtr1 && pipePtr2)
    {
        *pipePtr1='\0';
        *pipePtr2='\0';
        LatestReceivedRequestCounter=atoi(dashPtr+1);
        LatestReceivedRequestSourceAddress=atoi(pipePtr1+1);
        LatestReceivedRequestDestinationAddress=atoi(pipePtr2+1);
        *pipePtr1='|';
        *pipePtr2='|';
    }
    else
    {
        LatestReceivedRequestCounter=0;
    }
}

bool lora_protocol_is_received_data_a_reply()
{
    return strncmp((const char*)RxBuffer, (const char*)ReplyMsg, strlen((const char*)ReplyMsg)) == 0;
}

void lora_protocol_process_received_data_as_reply()
{
    char* dashPtr=NULL;
    char* pipePtr1=NULL;
    char* pipePtr2=NULL;

    dashPtr=strchr((const char*)RxBuffer,'-');

    if(dashPtr)
    {
        pipePtr1=strchr(dashPtr+1,'|');

        if(pipePtr1)
        {
            pipePtr2=strchr(pipePtr1+1,'|');
        }
    }
    
    if(dashPtr && pipePtr1 && pipePtr2)
    {
        *pipePtr1='\0';
        *pipePtr2='\0';
        LatestReceivedReplyCounter=atoi(dashPtr+1);
        LatestReceivedReplySourceAddress=atoi(pipePtr1+1);
        LatestReceivedReplyDestinationAddress=atoi(pipePtr2+1);
        *pipePtr1='|';
        *pipePtr2='|';
    }
    else
    {
        LatestReceivedReplyCounter=0;
    }
}

void lora_protocol_fill_with_rx_buffer_dump(char* destBuffer, size_t destBufferSize)
{
    char srcBuffer[lora_protocol_BUFFER_SIZE];
    memcpy(srcBuffer,RxBuffer,lora_protocol_BUFFER_SIZE);
    srcBuffer[lora_protocol_BUFFER_SIZE-1]='\0';

    strcpy(destBuffer,srcBuffer);
}

void lora_protocol_fill_with_tx_buffer_dump(char* destBuffer, uint8_t* txBuffer, size_t destBufferSize)
{
    char srcBuffer[lora_protocol_BUFFER_SIZE];
    memcpy(srcBuffer,txBuffer,lora_protocol_BUFFER_SIZE);
    srcBuffer[lora_protocol_BUFFER_SIZE-1]='\0';

    strcpy(destBuffer,srcBuffer);
}