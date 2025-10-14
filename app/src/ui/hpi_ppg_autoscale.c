#include <lvgl.h>
#include <math.h>

// Track last applied range to implement hysteresis
static int32_t last_applied_min = 0;
static int32_t last_applied_max = 65535;
static bool first_scale = true;

/* Reset autoscale state - call when screen is initialized */
void hpi_ppg_autoscale_reset(void)
{
    first_scale = true;
    last_applied_min = 0;
    last_applied_max = 65535;
}

void hpi_ppg_disp_do_set_scale_shared(lv_obj_t *chart, float *y_min_ppg, float *y_max_ppg, float *gx, int disp_window_size)
{
    // Trigger rescaling every 32 samples to reduce jitter
    if (*gx >= (disp_window_size / 4))
    {
        float fymin = *y_min_ppg;
        float fymax = *y_max_ppg;

        /* Detect sentinel/uninitialized values */
        const float sentinel_min = 10000.0f;
        const float sentinel_max = 0.0f;

        if (fymin == sentinel_min && fymax == sentinel_max)
        {
            /* No data observed yet - use default window */
            fymin = 2048.0f - 128.0f;
            fymax = 2048.0f + 128.0f;
        }

        int32_t min_v = (int32_t)floorf(fymin);
        int32_t max_v = (int32_t)ceilf(fymax);

        /* Expand range if extrema are equal or very tight */
        if (min_v >= max_v)
        {
            const int32_t center = 2048;
            const int32_t half_span = 128;
            min_v = center - half_span;
            max_v = center + half_span;
        }
        else if ((max_v - min_v) < 8)
        {
            int32_t center = (min_v + max_v) / 2;
            int32_t half = 16;
            min_v = center - half;
            max_v = center + half;
        }
        else
        {
            /* Add 10% padding for better visibility */
            int32_t range = max_v - min_v;
            int32_t padding = range / 10;
            if (padding < 8) padding = 8;
            min_v -= padding;
            max_v += padding;
        }

        // Apply hysteresis to reduce jitter - only update if change is significant
        bool should_update = first_scale;
        
        if (!first_scale)
        {
            int32_t current_range = last_applied_max - last_applied_min;
            int32_t new_range = max_v - min_v;
            
            if (current_range > 0)
            {
                float range_change_ratio = (float)abs(new_range - current_range) / (float)current_range;
                float min_shift = (float)abs(min_v - last_applied_min) / (float)current_range;
                float max_shift = (float)abs(max_v - last_applied_max) / (float)current_range;
                
                // Update if range changed by >15% OR min/max shifted by >20%
                if (range_change_ratio > 0.15f || min_shift > 0.20f || max_shift > 0.20f)
                {
                    should_update = true;
                }
            }
            else
            {
                should_update = true;
            }
        }
        
        if (should_update)
        {
            lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, min_v, max_v);
            last_applied_min = min_v;
            last_applied_max = max_v;
            first_scale = false;
        }

        /* Reset tracking values */
        *gx = 0;
        *y_max_ppg = sentinel_max;
        *y_min_ppg = sentinel_min;
    }
}
