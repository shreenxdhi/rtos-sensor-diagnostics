#include "bsp.h"
#include "FreeRTOS.h"
#include "task.h"
#define REG32(a) (*(volatile uint32_t *)(a))
#define UART_BASE 0x40004000u
#define DATA REG32(UART_BASE)
#define STATE REG32(UART_BASE + 4u)
#define CTRL REG32(UART_BASE + 8u)
#define INTSTATUS REG32(UART_BASE + 12u)
#define BAUDDIV REG32(UART_BASE + 16u)
void uart_init(void) { CTRL = 0; BAUDDIV = 217; CTRL = 3; }
void uart_enable_rx_irq(void) {

    (*(volatile uint8_t *)0xe000e400u) = 0x60;
    INTSTATUS = 2u;
    REG32(0xe000e100u) = 1u;
    CTRL = 0x0bu;
}
void UART0RX_Handler(void) {
    INTSTATUS = 2u;
    if (STATE & 2u) uart_rx_byte((uint8_t)DATA);
}
void uart_write(const char *s) {
    while (*s) {

        uint32_t budget = 1000000;
        while ((STATE & 1u) && --budget) { }
        if (!budget) return;
        DATA = (uint8_t)*s++;
    }
}
void fatal(const char *message) {
    __asm volatile("cpsid i");
    uart_write("{\"event\":\"fatal\",\"reason\":\"");
    uart_write(message); uart_write("\"}\n");
    for (;;) __asm volatile("wfi");
}
void app_assert(const char *file, int line) { (void)file; (void)line; fatal("assert"); }
void vApplicationMallocFailedHook(void) { fatal("heap_exhausted"); }
void vApplicationStackOverflowHook(TaskHandle_t t, char *name) {
    (void)t; (void)name; fatal("stack_overflow");
}
void Default_Handler(void) { fatal("unexpected_exception"); }
