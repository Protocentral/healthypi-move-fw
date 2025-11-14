/* Day stats module implementation */
#include "day_stats_module.h"

/* Legacy global user profile variables (kept for backward compatibility).
 * Some UI code references these symbols directly (e.g. settings UI),
 * so provide them here and have the module use them.
 */
uint16_t m_user_height = 170; /* cm */
uint16_t m_user_weight = 70;  /* kg */
double m_user_met = 3.5;

/* Internal aliases in cm/kg for the module API */
#define m_user_height_cm (m_user_height)
#define m_user_weight_kg (m_user_weight)

static uint32_t g_steps = 0;
static uint32_t g_active_time_s = 0;

uint16_t hpi_get_kcals_from_steps(uint16_t steps)
{
    double _m_time = (((m_user_height_cm / 100.000) * 0.414 * steps) / 4800.000) * 60.000; // Assuming speed of 4.8 km/h
    double _m_kcals = (_m_time * m_user_met * 3.500 * m_user_weight_kg) / 200.0;
    return (uint16_t)_m_kcals;
}

uint16_t day_stats_get_user_height_cm(void)
{
    return m_user_height_cm;
}

uint16_t day_stats_get_user_weight_kg(void)
{
    return m_user_weight_kg;
}

double day_stats_get_user_met(void)
{
    return m_user_met;
}

void day_stats_set_steps(uint32_t steps)
{
    g_steps = steps;
}

uint32_t day_stats_get_steps(void)
{
    return g_steps;
}

void day_stats_set_active_time_s(uint32_t s)
{
    g_active_time_s = s;
}

uint32_t day_stats_get_active_time_s(void)
{
    return g_active_time_s;
}
