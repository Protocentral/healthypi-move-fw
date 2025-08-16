/*
 * Suggested optimizations for MAX32664C driver
 * These enhancements improve RTIO streaming performance and reduce I2C bus contention
 * 
 * (c) 2024 Protocentral Electronics
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MAX32664C_OPTIMIZATIONS_H
#define MAX32664C_OPTIMIZATIONS_H

/* Suggested Kconfig options to add to your driver */

/* 
 * CONFIG_MAX32664C_RTIO_BUFFER_COUNT
 * Number of RTIO buffers to pre-allocate for streaming
 * Default: 8, Range: 4-16
 */
#ifndef CONFIG_MAX32664C_RTIO_BUFFER_COUNT
#define CONFIG_MAX32664C_RTIO_BUFFER_COUNT 8
#endif

/*
 * CONFIG_MAX32664C_BATCH_SIZE
 * Maximum number of samples to process in a single interrupt
 * Default: 8, Range: 1-16
 */
#ifndef CONFIG_MAX32664C_BATCH_SIZE  
#define CONFIG_MAX32664C_BATCH_SIZE 8
#endif

/*
 * CONFIG_MAX32664C_STREAM_PRIORITY
 * Thread priority for streaming operations
 * Default: 6 (higher than normal app threads)
 */
#ifndef CONFIG_MAX32664C_STREAM_PRIORITY
#define CONFIG_MAX32664C_STREAM_PRIORITY 6
#endif

/* Enhanced data structures for optimized streaming */

/**
 * @brief Enhanced streaming context for the driver
 */
struct max32664c_stream_ctx {
    struct rtio_iodev_sqe *pending_sqe;
    struct k_work_delayable stream_work;
    struct k_mutex stream_mutex;
    bool stream_active;
    uint32_t sample_count;
    uint64_t last_timestamp;
    uint8_t error_count;
};

/**
 * @brief Optimized buffer management structure
 */
struct max32664c_buffer_mgr {
    void *buffers[CONFIG_MAX32664C_RTIO_BUFFER_COUNT];
    uint8_t buffer_states[CONFIG_MAX32664C_RTIO_BUFFER_COUNT];
    struct k_mutex buffer_mutex;
    uint8_t next_free;
    uint8_t in_use_count;
};

/* Function prototypes for optimizations */

/**
 * @brief Initialize optimized streaming context
 */
int max32664c_stream_ctx_init(const struct device *dev);

/**
 * @brief Optimized MFIO interrupt handler with batching
 */
void max32664c_optimized_irq_handler(const struct device *dev);

/**
 * @brief Batch process multiple samples to reduce overhead
 */
int max32664c_batch_process_samples(const struct device *dev, 
                                   uint8_t *buffer, uint32_t sample_count);

/**
 * @brief Advanced buffer pool management
 */
void *max32664c_get_buffer(struct max32664c_buffer_mgr *mgr);
void max32664c_release_buffer(struct max32664c_buffer_mgr *mgr, void *buffer);

/**
 * @brief I2C bus arbitration helper
 */
int max32664c_acquire_i2c_bus(const struct device *dev, k_timeout_t timeout);
void max32664c_release_i2c_bus(const struct device *dev);

/**
 * @brief Power-aware streaming control
 */
int max32664c_set_stream_rate(const struct device *dev, uint32_t rate_hz);
int max32664c_adaptive_streaming(const struct device *dev, bool enable);

#endif /* MAX32664C_OPTIMIZATIONS_H */
