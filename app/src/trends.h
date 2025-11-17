/*
 * HealthyPi Move
 * 
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Author: Ashwin Whitchurch, Protocentral Electronics
 * Contact: ashwin@protocentral.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


#pragma once


struct hpi_hr_trend_point_t
{
    int64_t timestamp;
    uint16_t max;
    uint16_t min;
    uint16_t avg;
    uint16_t latest; 
};

struct hpi_hrv_trend_point_t
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
