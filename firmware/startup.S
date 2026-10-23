.syntax unified
.cpu cortex-m3
.thumb
.section .isr_vector,"a",%progbits
.global vectors
vectors:
    .word _stack_top
    .word Reset_Handler
    .word Default_Handler
    .word Default_Handler
    .word Default_Handler
    .word Default_Handler
    .word Default_Handler
    .word 0,0,0,0
    .word SVC_Handler
    .word Default_Handler
    .word 0
    .word PendSV_Handler
    .word SysTick_Handler
    .word UART0RX_Handler
    .rept 31
    .word Default_Handler
    .endr
.section .text.Reset_Handler,"ax",%progbits
.thumb_func
.global Reset_Handler
Reset_Handler:
    ldr r0, =_data_load
    ldr r1, =_data_start
    ldr r2, =_data_end
1:  cmp r1, r2
    bcs 2f
    ldr r3, [r0], #4
    str r3, [r1], #4
    b 1b
2:  ldr r1, =_bss_start
    ldr r2, =_bss_end
    movs r3, #0
3:  cmp r1, r2
    bcs 4f
    str r3, [r1], #4
    b 3b
4:  bl main
    b .
