/*
 * HealthyPi Move — Health Store: type registry table (H1)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * The single source of truth for metric metadata (units, scale, aggregation
 * class, HealthKit / Health Connect mappings). Served verbatim over the TYPES
 * MCUmgr command so clients are self-describing. See hpi_hs_types.h + the frozen
 * contract in docs/HPI_HS_API.md — keep this table and that doc in lockstep.
 */

#include <zephyr/sys/util.h>
#include "health/hpi_hs_types.h"

#define D  HPI_HS_CLASS_DISCRETE
#define C  HPI_HS_CLASS_CUMULATIVE
#define E  HPI_HS_CLASS_EVENT

static const struct hpi_hs_type_info s_types[] = {
    /* id                     key             unit    scale cls  derived hk                                  hc                             */
    {HPI_HS_T_HR,            "hr",           "bpm",    1,   D, false, "heartRate",                    "HeartRateRecord"},
    {HPI_HS_T_RESTING_HR,    "resting_hr",   "bpm",    1,   D, true,  "restingHeartRate",             "RestingHeartRateRecord"},
    {HPI_HS_T_ECG_HR,        "ecg_hr",       "bpm",    1,   E, false, "heartRate",                    "HeartRateRecord"},
    {HPI_HS_T_HR_MIN,        "hr_min",       "bpm",    1,   D, true,  "heartRate",                    "HeartRateRecord"},
    {HPI_HS_T_HR_MAX,        "hr_max",       "bpm",    1,   D, true,  "heartRate",                    "HeartRateRecord"},
    {HPI_HS_T_SPO2,          "spo2",         "%",      1,   D, false, "oxygenSaturation",             "OxygenSaturationRecord"},
    {HPI_HS_T_SKIN_TEMP,     "skin_temp",    "degC",   100, D, false, "bodyTemperature",              "SkinTemperatureRecord"},
    {HPI_HS_T_SKIN_TEMP_DEV, "skin_temp_dev","degC",   100, D, true,  "appleSleepingWristTemperature", ""},
    {HPI_HS_T_SKIN_TEMP_CNT, "skin_temp_cnt","count",  1,   D, true,  "",                             ""},
    {HPI_HS_T_BP_SYS,        "bp_sys",       "mmHg",   1,   E, false, "bloodPressureSystolic",        "BloodPressureRecord"},
    {HPI_HS_T_BP_DIA,        "bp_dia",       "mmHg",   1,   E, false, "bloodPressureDiastolic",       "BloodPressureRecord"},
    {HPI_HS_T_STEPS,         "steps",        "count",  1,   C, false, "stepCount",                    "StepsRecord"},
    {HPI_HS_T_ACTIVE_ENERGY, "active_energy","kcal",   1,   C, true,  "activeEnergyBurned",           "ActiveCaloriesBurnedRecord"},
    {HPI_HS_T_HRV_SDNN,      "hrv_sdnn",     "ms",     10,  D, false, "heartRateVariabilitySDNN",     ""},
    {HPI_HS_T_HRV_RMSSD,     "hrv_rmssd",    "ms",     10,  D, false, "",                             "HeartRateVariabilityRmssdRecord"},
    {HPI_HS_T_HRV_LFHF,      "hrv_lfhf",     "ratio",  100, D, true,  "",                             ""},
    {HPI_HS_T_HRV_MEAN_RR,   "hrv_mean_rr",  "ms",     1,   D, true,  "",                             ""},
    {HPI_HS_T_HRV_COVERAGE,  "hrv_coverage", "%",      1,   D, true,  "",                             ""},
    {HPI_HS_T_HRV_NPAIRS,     "hrv_npairs",   "count",  1,   D, true,  "",                             ""},
    {HPI_HS_T_HRV_NBEATS,     "hrv_nbeats",   "count",  1,   D, true,  "",                             ""},
    {HPI_HS_T_EDA_SCL,       "eda_scl",      "uS",     100, D, false, "",                             ""},
    {HPI_HS_T_EDA_SCR_RATE,  "eda_scr_rate", "/min",   1,   D, false, "",                             ""},
    {HPI_HS_T_STRESS_EDA,    "stress_eda",   "index",  1,   D, true,  "",                             ""},
    {HPI_HS_T_STRESS_HRV,    "stress_hrv",   "index",  1,   D, true,  "",                             ""},
};

#undef D
#undef C
#undef E

const struct hpi_hs_type_info *hpi_hs_type_lookup(uint8_t type)
{
    for (size_t i = 0; i < ARRAY_SIZE(s_types); i++) {
        if (s_types[i].id == type) {
            return &s_types[i];
        }
    }
    return NULL;
}

size_t hpi_hs_type_count(void)
{
    return ARRAY_SIZE(s_types);
}

const struct hpi_hs_type_info *hpi_hs_type_at(size_t index)
{
    return (index < ARRAY_SIZE(s_types)) ? &s_types[index] : NULL;
}
