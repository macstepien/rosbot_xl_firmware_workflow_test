/**
 * @file UartLib.h
 * @author Maciej Kurcius
 * @brief 
 * @version 0.1
 * @date 2022-02-22
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#ifndef UartLib_H
#define UartLib_H

#include <Arduino.h>
#include <bsp.h>
#include <STM32FreeRTOS.h>

#define FRAME_START_BIT                 '<'
#define FRAME_STOP_BIT                  '>'
#define MAX_ARGS_SIZE                   32
#define RX_BUFF_CAPACITY                500
#define DEFAULT_TIMEOUT                 1
#define UART_H2B_ERR                    0xFFFF
#define UART_FRAME_DATA_LENGTH(arg)     (arg + 3) * 2 
#define UART_FRAME_LENGTH(arg)          UART_FRAME_DATA_LENGTH(arg) + 2               
#define UART_MAX_FRAME_DATA_LENGTH      UART_FRAME_DATA_LENGTH(MAX_ARGS_SIZE)

//extern variables
extern QueueHandle_t BatteryStateQueue;

typedef enum{
    ConversionOk    = 0,
    ConversionError = 1
}UartConvStatusTypeDef;


struct UartProtocolFrame{
    uint8_t cmd;
    uint8_t arg_size;
    uint8_t args[MAX_ARGS_SIZE];
    uint8_t check_sum;
};

class UartProtocolClass: public HardwareSerial{
    public:
    UartProtocolClass(uint32_t arg_rx, uint32_t arg_tx, uint32_t arg_baudrate, uint8_t arg_config);
    ~UartProtocolClass();
    void UartProtocolLoopHandler();
    void SendFrame(UartProtocolFrame arg_frame);
    void SendBuffer(uint8_t arg_size, uint8_t* arg_buffer);
    void SendBuffer(uint8_t arg_size, String arg_buffer);
    private:
    void ExecuteFrame();
    int8_t StreamParse();
    UartConvStatusTypeDef HexToByte(uint8_t* arg_byte_, uint8_t* arg_result);
    uint8_t* ByteToHex(uint8_t arg_byte, uint8_t* arg_buffer);
    uint8_t DecodeHex(uint8_t arg_byte);
    uint8_t EncodeHex(uint8_t arg_byte);
    UartProtocolFrame processed_frame_;
    uint8_t rx_buffer_[RX_BUFF_CAPACITY];
    volatile uint16_t rx_buffer_size_ = 0;
};

#endif /* UartLib_H */