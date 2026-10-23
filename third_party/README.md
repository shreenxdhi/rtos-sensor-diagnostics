# FreeRTOS source provenance

- Upstream: https://github.com/FreeRTOS/FreeRTOS-Kernel
- Release tag: `V11.1.0`
- Commit: `dbf70559b27d39c1fdb68dfb9a32140b6a6777a0`
- License: MIT, retained in `FreeRTOS-Kernel/LICENSE.md`

This repository vendors an unmodified build-relevant subset: `tasks.c`,
`queue.c`, `list.c`, public `include/` headers, `portable/GCC/ARM_CM3/`, and
`portable/MemMang/heap_4.c`. Other ports, examples, and unused kernel modules
are omitted. The upstream Git metadata is not included. Application, BSP,
startup/linker, runtime, Python tools, tests, and documentation are separate
from the upstream kernel.

