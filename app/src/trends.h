#pragma once

#define NUM_HOURS 24
#define MAX_POINTS_PER_HOUR 60

struct hpi_hr_trend_point_t
{
    int64_t timestamp;
    uint16_t hr_max;
    uint16_t hr_min;
    uint16_t hr_avg;
    uint16_t hr_latest; 
};

struct hpi_hourly_trend_point_t
{
    uint8_t hour_no;
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

void hpi_trend_load_day_trend(struct hpi_hourly_trend_point_t *hr_hourly_trend_points, int *num_points);
void hpi_trend_wr_hr_point_to_file(struct hpi_hr_trend_point_t m_hr_trend_point, int64_t day_ts);