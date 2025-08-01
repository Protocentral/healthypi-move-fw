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


#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <stdio.h>
#include <math.h>
#include "algos.h"

int rear = -1;
int k = 0;
unsigned int array[HRV_LIMIT];
int min_hrv = 0;
int max_hrv = 0;
float mean_hrv;
float sdnn;
float rmssd;
float pnn;
int max_t = 0;
int min_t = 0;
bool ready_flag = false;

// hpi_computed_hrv_t hrv_calculated;

void calculate_hrv(int32_t heart_rate, int32_t *hrv_max, int32_t *hrv_min, float *mean, float *sdnn, float *pnn, float *rmssd, bool *hrv_ready_flag)
{
  k++;

  if (rear == HRV_LIMIT - 1)
  {
    for (int i = 0; i < (HRV_LIMIT - 1); i++)
    {
      array[i] = array[i + 1];
    }

    array[HRV_LIMIT - 1] = heart_rate;
  }
  else
  {
    rear++;
    array[rear] = heart_rate;
  }

  if (k >= HRV_LIMIT)
  {
    *hrv_max = calculate_hrvmax(array);
    *hrv_min = calculate_hrvmin(array);
    *mean = calculate_mean(array);
    *sdnn = calculate_sdnn(array);
    calculate_pnn_rmssd(array, pnn, rmssd);
    *hrv_ready_flag = true;
  }
}

int calculate_hrvmax(unsigned int array[])
{
  for (int i = 0; i < HRV_LIMIT; i++)
  {
    if (array[i] > max_t)
    {
      max_t = array[i];
    }
  }
  return max_t;
}

int calculate_hrvmin(unsigned int array[])
{
  min_t = max_hrv;
  for (int i = 0; i < HRV_LIMIT; i++)
  {
    if (array[i] < min_t)
    {
      min_t = array[i];
    }
  }
  return min_t;
}

float calculate_mean(unsigned int array[])
{
  int sum = 0;
  for (int i = 0; i < (HRV_LIMIT); i++)
  {
    sum = sum + array[i];
  }
  return ((float)sum) / HRV_LIMIT;
}

float calculate_sdnn(unsigned int array[])
{
  int sumsdnn = 0;
  int diff;

  for (int i = 0; i < (HRV_LIMIT); i++)
  {
    diff = (array[i] - (mean_hrv)) * (array[i] - (mean_hrv));
    sumsdnn = sumsdnn + diff;
  }
  return sqrt(sumsdnn / (HRV_LIMIT));
}

void calculate_pnn_rmssd(unsigned int array[], float *pnn50, float *rmssd)
{
  unsigned int pnn_rmssd[HRV_LIMIT];
  int count = 0;
  int sqsum = 0;

  for (int i = 0; i < (HRV_LIMIT - 2); i++)
  {
    pnn_rmssd[i] = abs(array[i + 1] - array[i]);
    sqsum = sqsum + (pnn_rmssd[i] * pnn_rmssd[i]);

    if (pnn_rmssd[i] > 50)
    {
      count = count + 1;
    }
  }
  *pnn50 = ((float)count / HRV_LIMIT);
  *rmssd = sqrt(sqsum / (HRV_LIMIT - 1));
}
