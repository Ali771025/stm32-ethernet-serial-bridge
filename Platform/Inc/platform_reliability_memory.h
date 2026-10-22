#ifndef PLATFORM_RELIABILITY_MEMORY_H
#define PLATFORM_RELIABILITY_MEMORY_H

/* CPU-only retry/reorder storage in cacheable AXI SRAM. Never use this
 * section for Ethernet or UART DMA buffers. */
#if defined(__ICCARM__)
#define PLATFORM_RELIABILITY_WINDOW \
  _Pragma("location=\".bridge_window\"") _Pragma("data_alignment=32")
#elif defined(__GNUC__)
#define PLATFORM_RELIABILITY_WINDOW \
  __attribute__((section(".bridge_window"), aligned(32)))
#else
#error "Define .bridge_window placement for this compiler"
#endif

#endif /* PLATFORM_RELIABILITY_MEMORY_H */
