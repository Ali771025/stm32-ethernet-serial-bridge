#ifndef PLATFORM_DMA_MEMORY_H
#define PLATFORM_DMA_MEMORY_H

/* Keep compiler-specific DMA placement in this platform boundary. */
#if defined(__ICCARM__)
#define PLATFORM_DMA_BUFFER _Pragma("location=\".dma_buffer\"") \
                            _Pragma("data_alignment=32")
#elif defined(__GNUC__)
#define PLATFORM_DMA_BUFFER __attribute__((section(".dma_buffer"), aligned(32)))
#else
#error "Define aligned .dma_buffer placement for this compiler"
#endif

#endif
