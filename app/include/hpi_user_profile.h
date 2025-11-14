/* User profile defaults and helpers */
#ifndef HPI_USER_PROFILE_H
#define HPI_USER_PROFILE_H

#include <stdint.h>

/* Get user profile values (height in cm, weight in kg, MET) */
uint16_t hpi_user_get_height_cm(void);
uint16_t hpi_user_get_weight_kg(void);
double hpi_user_get_met(void);

/* Calories calculation API */
uint16_t hpi_get_kcals_from_steps(uint16_t steps);

#endif /* HPI_USER_PROFILE_H */
