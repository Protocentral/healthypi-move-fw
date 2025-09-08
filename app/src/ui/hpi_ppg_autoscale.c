#include <lvgl.h>
#include <math.h>

void hpi_ppg_disp_do_set_scale_shared(lv_obj_t *chart, float *y_min_ppg, float *y_max_ppg, float *gx, int disp_window_size)
{
    if (*gx >= (disp_window_size / 4))
    {
        int32_t min_v = (int32_t)floorf(*y_min_ppg);
        int32_t max_v = (int32_t)ceilf(*y_max_ppg);
        if (min_v >= max_v)
        {
            const int32_t center = 2048;
            const int32_t half_span = 128; /* default +/- span */
            min_v = center - half_span;
            max_v = center + half_span;
        }

        lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, min_v, max_v);

        *gx = 0;
        *y_max_ppg = 0;
        *y_min_ppg = 10000;
    }
}
