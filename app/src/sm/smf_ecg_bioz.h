/**
 * @file smf_ecg_bioz.h
 * @brief Header for ECG/BioZ state machine functions
 * 
 * This file contains public function declarations for the ECG/BioZ state machine
 * that can be used by other modules.
 */

#ifndef SMF_ECG_BIOZ_H_
#define SMF_ECG_BIOZ_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reconfigure ECG leads when hand worn setting changes
 * 
 * This function reconfigures the ECG lead polarity based on the current
 * hand worn setting. It only applies the configuration if ECG is currently active.
 * 
 * @note This function is thread-safe and can be called from UI callbacks.
 */
void reconfigure_ecg_leads_for_hand_worn(void);

#ifdef __cplusplus
}
#endif

#endif /* SMF_ECG_BIOZ_H_ */
