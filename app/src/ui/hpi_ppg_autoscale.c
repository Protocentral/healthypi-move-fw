#include <lvgl.h>
#include <math.h>

void hpi_ppg_disp_do_set_scale_shared(lv_obj_t *chart, float *y_min_ppg, float *y_max_ppg, float *gx, int disp_window_size)
{
    if (*gx >= (disp_window_size / 4))
    {
        /* Defensive copies */
        float fymin = *y_min_ppg;
        float fymax = *y_max_ppg;

        /* Detect sentinel/uninitialized values used elsewhere in code */
        const float sentinel_min = 10000.0f;
        const float sentinel_max = 0.0f;

        if (fymin == sentinel_min && fymax == sentinel_max)
        {
            /* Nothing meaningful observed yet - use a sensible default window */
            fymin = 2048.0f - 128.0f;
            fymax = 2048.0f + 128.0f;
        }

        int32_t min_v = (int32_t)floorf(fymin);
        int32_t max_v = (int32_t)ceilf(fymax);

        /* If extrema are equal or very tight, expand around center to avoid flat-line at top */
        if (min_v >= max_v)
        {
            const int32_t center = 2048;
            const int32_t half_span = 128; /* default +/- span */
            min_v = center - half_span;
            max_v = center + half_span;
        }
        else if ((max_v - min_v) < 8)
        {
            /* Expand a small range to a minimum span to keep waveform visible */
            int32_t center = (min_v + max_v) / 2;
            int32_t half = 16;
            min_v = center - half;
            max_v = center + half;
        }

    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, min_v, max_v);

        /* Reset tracking values */
        *gx = 0;
        *y_max_ppg = sentinel_max;
        *y_min_ppg = sentinel_min;
    }
}
