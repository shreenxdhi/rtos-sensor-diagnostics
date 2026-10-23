#ifndef BSP_H
#define BSP_H
#include <stdint.h>
void uart_init(void);
void uart_enable_rx_irq(void);
void uart_write(const char *s);
void uart_rx_byte(uint8_t b);
void UART0RX_Handler(void);
void fatal(const char *message);
#endif
