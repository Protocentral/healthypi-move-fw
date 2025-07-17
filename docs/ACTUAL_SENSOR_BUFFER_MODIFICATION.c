/**
 * @file max32664c_async_optimized.c
 * @brief Actual implementation showing sensor buffer optimization
 * 
 * This demonstrates the real modification to max32664c_async.c
 * showing BEFORE and AFTER code with exact line changes needed.
 */

#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "sensor_buffer_pool.h"  // ADD THIS INCLUDE
#include "max32664c.h"

LOG_MODULE_REGISTER(max32664c_async, LOG_LEVEL_DBG);

/*
 * REAL MODIFICATION EXAMPLE
 * =========================
 * 
 * This shows the actual changes needed for max32664c_async_sample_fetch_scd()
 * in /drivers/sensor/max32664c/max32664c_async.c
 */

// CURRENT CODE (lines 30-70 in max32664c_async.c):
#if 0  // This is the ORIGINAL code
static int max32664c_async_sample_fetch_scd(const struct device *dev, uint8_t *chip_op_mode, uint8_t *scd_state)
{
    struct max32664c_data *data = dev->data;
    const struct max32664c_config *config = dev->config;

    uint8_t wr_buf[2] = {0x12, 0x01};
    static uint8_t buf[2048];  // <-- REMOVE THIS LINE (2KB static allocation)
    static int sample_len = 1;

    uint8_t hub_stat = max32664c_read_hub_status(dev);
    if (hub_stat & MAX32664C_HUB_STAT_DRDY_MASK)
    {
        int fifo_count = max32664c_get_fifo_count(dev);

        if (fifo_count > 8)
        {
            fifo_count = 8;
        }

        if (fifo_count > 0)
        {
            sample_len = 1;
            *chip_op_mode = data->op_mode;

            gpio_pin_set_dt(&config->mfio_gpio, 0);
            k_sleep(K_USEC(300));
            i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));

            i2c_read_dt(&config->i2c, buf, ((sample_len * fifo_count) + MAX32664C_SENSOR_DATA_OFFSET));
            k_sleep(K_USEC(300));
            gpio_pin_set_dt(&config->mfio_gpio, 1);

            for (int i = 0; i < fifo_count; i++)
            {
                uint8_t scd_state_val = (uint8_t)buf[(sample_len * i) + 0 + MAX32664C_SENSOR_DATA_OFFSET];
                *scd_state = scd_state_val;
            }
        }
    }
    
    return 0;  // <-- CHANGE THIS to return proper error codes
}
#endif

// NEW OPTIMIZED CODE:
static int max32664c_async_sample_fetch_scd(const struct device *dev, uint8_t *chip_op_mode, uint8_t *scd_state)
{
    struct max32664c_data *data = dev->data;
    const struct max32664c_config *config = dev->config;

    uint8_t wr_buf[2] = {0x12, 0x01};
    
    // ✅ ADD: Allocate buffer from shared pool instead of static
    uint8_t *buf = sensor_buffer_alloc();
    if (!buf) {
        LOG_ERR("Failed to allocate sensor buffer for SCD fetch");
        return -ENOMEM;
    }
    
    static int sample_len = 1;
    int ret = 0;  // ✅ ADD: Track return value for proper cleanup

    uint8_t hub_stat = max32664c_read_hub_status(dev);
    if (hub_stat & MAX32664C_HUB_STAT_DRDY_MASK)
    {
        int fifo_count = max32664c_get_fifo_count(dev);

        if (fifo_count > 8)
        {
            fifo_count = 8;
        }

        if (fifo_count > 0)
        {
            sample_len = 1;
            *chip_op_mode = data->op_mode;

            gpio_pin_set_dt(&config->mfio_gpio, 0);
            k_sleep(K_USEC(300));
            ret = i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));  // ✅ CHANGE: Check return value
            
            if (ret == 0) {  // ✅ ADD: Error handling
                ret = i2c_read_dt(&config->i2c, buf, ((sample_len * fifo_count) + MAX32664C_SENSOR_DATA_OFFSET));
            }
            
            k_sleep(K_USEC(300));
            gpio_pin_set_dt(&config->mfio_gpio, 1);

            if (ret == 0) {  // ✅ ADD: Only process data if I2C successful
                for (int i = 0; i < fifo_count; i++)
                {
                    uint8_t scd_state_val = (uint8_t)buf[(sample_len * i) + 0 + MAX32664C_SENSOR_DATA_OFFSET];
                    *scd_state = scd_state_val;
                }
            }
        }
    }
    
    sensor_buffer_free(buf);  // ✅ ADD: Always free buffer before return
    return ret;               // ✅ CHANGE: Return actual error code
}

/*
 * EXACT CHANGES NEEDED FOR ALL 4 FUNCTIONS IN max32664c_async.c:
 * ==============================================================
 * 
 * 1. max32664c_async_sample_fetch_scd()     - line ~36:  static uint8_t buf[2048];
 * 2. max32664c_async_sample_fetch_ppg()     - line ~84:  static uint8_t buf[2048]; 
 * 3. max32664c_async_sample_fetch_raw()     - line ~126: static uint8_t buf[2048];
 * 4. max32664c_async_sample_fetch_algo()    - line ~189: static uint8_t buf[2048];
 * 
 * And 1 function in max32664d_async.c:
 * 5. max32664d_async_sample_fetch_algo()    - line ~17:  static uint8_t buf[2048];
 */

/*
 * SEARCH & REPLACE PATTERN:
 * ========================
 * 
 * FOR EACH FUNCTION:
 * 
 * FIND:
 *     static uint8_t buf[2048];
 * 
 * REPLACE WITH:
 *     uint8_t *buf = sensor_buffer_alloc();
 *     if (!buf) {
 *         LOG_ERR("Failed to allocate sensor buffer");
 *         return -ENOMEM;
 *     }
 * 
 * THEN ADD BEFORE EACH RETURN STATEMENT:
 *     sensor_buffer_free(buf);
 */

/*
 * VERIFICATION CHECKLIST:
 * ======================
 * 
 * After modification, verify each function:
 * ✅ Has sensor_buffer_alloc() call
 * ✅ Has NULL check with -ENOMEM return
 * ✅ Has sensor_buffer_free() before EVERY return path
 * ✅ Uses buf pointer same as before (no other changes to logic)
 * ✅ Includes sensor_buffer_pool.h at top of file
 * 
 * Build verification:
 * ✅ No compile errors
 * ✅ No static analysis warnings about memory leaks
 * ✅ RAM usage decreased by ~6KB in memory map
 */

/**
 * @brief Memory usage comparison table
 */
static const struct {
    const char *file;
    const char *function;
    uint16_t old_ram;
    uint16_t new_ram;
    uint16_t savings;
} memory_comparison[] = {
    {"max32664c_async.c", "max32664c_async_sample_fetch_scd",  2048, 0, 2048},
    {"max32664c_async.c", "max32664c_async_sample_fetch_ppg",  2048, 0, 2048},
    {"max32664c_async.c", "max32664c_async_sample_fetch_raw",  2048, 0, 2048},
    {"max32664c_async.c", "max32664c_async_sample_fetch_algo", 2048, 0, 2048},
    {"max32664d_async.c", "max32664d_async_sample_fetch_algo", 2048, 0, 2048},
    {"sensor_buffer_pool.c", "shared_pool", 0, 4096, -4096},
    // NET SAVINGS: 5 × 2048 - 4096 = 6,144 bytes
};

#define TOTAL_RAM_SAVINGS 6144
#define SAVINGS_PERCENTAGE 60  // (6144 / 10240) * 100

LOG_MODULE_REGISTER(sensor_optimization, LOG_LEVEL_INF);

static inline void log_optimization_results(void)
{
    LOG_INF("Sensor buffer optimization complete:");
    LOG_INF("- Static buffers removed: 5 × 2048 = %d bytes", 5 * 2048);
    LOG_INF("- Shared pool allocated: 2 × 2048 = %d bytes", 2 * 2048);
    LOG_INF("- Net RAM savings: %d bytes (%d%%)", TOTAL_RAM_SAVINGS, SAVINGS_PERCENTAGE);
    LOG_INF("- Risk level: LOW (graceful failure handling)");
}
