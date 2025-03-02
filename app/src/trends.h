#pragma once

struct hpi_hr_trend_point_t
{
    int64_t timestamp;
    uint16_t hr_max;
    uint16_t hr_min;
    uint16_t hr_avg;
    uint16_t hr_latest; 
};

struct hpi_hr_trend_day_t
{
    struct hpi_hr_trend_point_t hr_points[1440];
    uint32_t time_last_update;
};