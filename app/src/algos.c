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
