#pragma once

#define NUM_HOURS 24
#define MAX_POINTS_PER_HOUR 60
#define MAX_POINTS_SPO2_PER_HOUR 10

struct hpi_hr_trend_point_t
{
    int64_t timestamp;
    uint16_t max;
    uint16_t min;
    uint16_t avg;
    uint16_t latest; 
};

struct hpi_trend_point_t
{
    int64_t timestamp;
    uint16_t max;
    uint16_t min;
    uint16_t avg;
    uint16_t latest; 
};

struct hpi_spo2_trend_point_t
{
    int64_t timestamp;
    uint16_t max;
    uint16_t min;
    uint16_t avg;
    uint16_t latest; 
};

struct hpi_hourly_trend_point_t
{
    uint8_t hour_no;
    uint16_t max;
    uint16_t min;
    uint16_t avg;
    uint16_t latest; 
};

/*
struct hpi_spo2_hourly_trend_point_t
{
    uint8_t hour_no;
    uint8_t spo2_max;
    uint8_t spo2_min;
    uint8_t spo2_avg;
    uint8_t spo2_latest; 
};
*/


struct hpi_hr_trend_day_t
{
    struct hpi_hr_trend_point_t hr_points[1440];
    uint32_t time_last_update;
};

int hpi_trend_load_hr_day_trend(struct hpi_hourly_trend_point_t *hr_hourly_trend_points, int *num_points);
void hpi_trend_wr_hr_point_to_file(struct hpi_hr_trend_point_t m_hr_trend_point, int64_t day_ts);
void hpi_trend_wr_spo2_point_to_file(struct hpi_spo2_trend_point_t m_spo2_trend_point, int64_t day_ts);
int hpi_trend_load_spo2_day_trend(struct hpi_hourly_trend_point_t *spo2_hourly_trend_points, int *num_points);