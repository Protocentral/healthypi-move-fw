#pragma once


struct hpi_hr_trend_point_t
{
    int64_t timestamp;
    uint16_t max;
    uint16_t min;
    uint16_t avg;
    uint16_t latest; 
};

struct hpi_temp_trend_point_t
{
    int64_t timestamp;
    uint16_t max;
    uint16_t min;
    uint16_t avg;
    uint16_t latest;
};

#define HPI_TREND_POINT_SIZE 16

struct hpi_log_index_t
{
    int64_t start_time;
    uint32_t log_file_length;
    uint8_t log_type;
};

#define HPI_FILE_IDX_SIZE 13

struct hpi_hourly_trend_point_t
{
    uint8_t hour_no;
    uint16_t max;
    uint16_t min;
    uint16_t avg;
    uint16_t latest; 
};

struct hpi_minutely_trend_point_t
{
    uint8_t minute_no;
    uint16_t max;
    uint16_t min;
    uint16_t avg;
    uint16_t latest; 
};

enum trend_type
{
    TREND_HR,
    TREND_SPO2,
    TREND_TEMP,
    TREND_BPT,
};

int hpi_trend_load_trend(struct hpi_hourly_trend_point_t *hourly_trend_points, struct hpi_minutely_trend_point_t *minute_trend_points, int *num_points, enum trend_type m_trend_type);
