/* Day stats module - centralized daily stats and helper APIs for Today screen */
#ifndef DAY_STATS_MODULE_H
#define DAY_STATS_MODULE_H

#include <stdint.h>

/* Calculate kcals from steps using user profile defaults
 * Mirrors previous implementation in smf_display.c
 */
uint16_t hpi_get_kcals_from_steps(uint16_t steps);

/* Optional getters/setters for day stats (future use) */
void day_stats_set_steps(uint32_t steps);
uint32_t day_stats_get_steps(void);
void day_stats_set_active_time_s(uint32_t s);
uint32_t day_stats_get_active_time_s(void);

/* Basic user profile getters folded into day_stats module for now */
uint16_t day_stats_get_user_height_cm(void);
uint16_t day_stats_get_user_weight_kg(void);
double day_stats_get_user_met(void);

#endif /* DAY_STATS_MODULE_H */
