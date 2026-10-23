ROOT := $(abspath .)
KERNEL := third_party/FreeRTOS-Kernel
CROSS ?= arm-none-eabi-
CC_ARM := $(CROSS)gcc
HOST_CC ?= clang
PYTHON ?= python3
QEMU ?= qemu-system-arm
INCLUDES := -Icore -Ifirmware -Ifirmware/freestanding -I$(KERNEL)/include -I$(KERNEL)/portable/GCC/ARM_CM3
ARMFLAGS := -mcpu=cortex-m3 -mthumb -std=c11 -O2 -g3 -ffreestanding -fno-builtin -ffunction-sections -fdata-sections -Wall -Wextra
APP := firmware/main.c firmware/bsp.c firmware/runtime.c firmware/startup.S core/protocol.c core/monitor.c
RTOS := $(KERNEL)/tasks.c $(KERNEL)/queue.c $(KERNEL)/list.c $(KERNEL)/portable/GCC/ARM_CM3/port.c $(KERNEL)/portable/MemMang/heap_4.c
HEADERS := $(wildcard core/*.h firmware/*.h firmware/freestanding/*.h)
.PHONY: all run debug test test-unit test-python test-integration demo clean check-tools
all: build/firmware.elf
build:
	mkdir -p $@
check-tools:
	@command -v $(CC_ARM) >/dev/null || (echo 'Missing ARM GCC; see README'; exit 1)
	@command -v $(QEMU) >/dev/null || (echo 'Missing QEMU; see README'; exit 1)
build/firmware.elf: $(APP) $(RTOS) $(HEADERS) firmware/linker.ld | build
	$(CC_ARM) $(ARMFLAGS) $(INCLUDES) $(APP) $(RTOS) -nostdlib -Wl,--gc-sections,-Map=build/firmware.map -Tfirmware/linker.ld -lgcc -o $@
	$(CROSS)size $@
run: all
	$(QEMU) -M mps2-an385 -display none -monitor none -serial stdio -kernel build/firmware.elf
debug: all
	$(QEMU) -M mps2-an385 -display none -monitor none -serial stdio -kernel build/firmware.elf -S -gdb tcp:127.0.0.1:3333
build/test-core: core/protocol.c core/monitor.c tests/test_core.c $(wildcard core/*.h) | build
	$(HOST_CC) -std=c11 -O1 -g -Wall -Wextra -Werror -Wconversion -Wpedantic -fsanitize=address,undefined -Icore core/protocol.c core/monitor.c tests/test_core.c -o $@
test-unit: build/test-core
	./build/test-core
test-python:
	$(PYTHON) -m unittest discover -s tests -p 'test_*.py' -v
test-integration: all
	$(PYTHON) tools/integration.py --qemu $(QEMU) --elf build/firmware.elf --output build/integration-results.json
test: test-unit test-python test-integration
demo: all
	$(PYTHON) tools/demo.py --qemu $(QEMU) --elf build/firmware.elf
clean:
	rm -rf build
