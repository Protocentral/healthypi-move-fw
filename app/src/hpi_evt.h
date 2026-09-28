/*
 * HealthyPi Move
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Cross-module event signalling (P2 full migration).
 *
 * Replaces the former web of cross-module k_sem "signal" semaphores between the
 * UI screens, data_module, hw_module and the sensor SMFs / display with per-
 * domain k_event objects. Each signal is a named bit; producers k_event_post()
 * a bit, consumers peek/clear it with hpi_evt_consume() (which mirrors the old
 * k_sem_take(K_NO_WAIT) semantics: returns true and clears the bit if set).
 *
 * Note: this replaces the *signalling* primitive only. The tick-based SMFs
 * (ECG/finger) keep their periodic run loop for their time-based states; they
 * simply peek these events each tick instead of taking semaphores.
 */

#ifndef HPI_EVT_H
#define HPI_EVT_H

#include <zephyr/kernel.h>
#include <stdbool.h>
#include <stdint.h>

/* ---- ECG / BioZ / HRV / GSR domain -------------------------------------- */
extern struct k_event ecg_evt;

/* Inputs: UI / data_module / hw_module -> ECG SMF */
#define EVT_ECG_START        BIT(0)
#define EVT_ECG_CANCEL       BIT(1)
#define EVT_GSR_START        BIT(2)
#define EVT_GSR_CANCEL       BIT(3)
/* BIT(4)/BIT(5) free — were EVT_HRV_START/EVT_HRV_CANCEL (ECG HRV eval removed) */
#define EVT_ECG_COMPLETE     BIT(6)
#define EVT_GSR_COMPLETE     BIT(7)
/* Outputs: ECG SMF / data_module -> display */
#define EVT_ECG_LEAD_ON      BIT(8)
#define EVT_ECG_LEAD_OFF     BIT(9)
#define EVT_ECG_LEAD_TIMEOUT BIT(10)
#define EVT_GSR_LEAD_ON      BIT(11)
#define EVT_GSR_LEAD_OFF     BIT(12)
/* BIT(13) free — was EVT_HRV_COMPLETE (ECG HRV eval removed) */
#define EVT_ECG_RESET        BIT(14)
#define EVT_GSR_RESET        BIT(15)
/* hw_module -> ECG SMF: one-shot boot handshake ("MAX30001 is up"). Kept
 * distinct from EVT_ECG_START so a sensor bring-up can never masquerade as a
 * user "start measurement" and spuriously arm the ECG SMF. */
#define EVT_ECG_HW_READY     BIT(16)
/* ECG SMF -> display: leads came off mid-measurement and the spot check was
 * aborted; display shows a transient "leads off" warning toast. */
#define EVT_ECG_LEADOFF_ABORT BIT(17)

/* Sentinel progress_timer values (in struct hpi_ecg_status_t) the ECG SMF
 * publishes for UI-only phases that fall outside the stabilize/record countdown
 * range: WAITING for initial contact ("place fingers"), and LEADS-OFF during a
 * measurement ("reconnect" warning shown during the abort grace period). */
#define HPI_ECG_UI_WAIT_LEADS  0xFFFF
#define HPI_ECG_UI_LEADS_OFF   0xFFFE

/* ---- Finger PPG (BPT / SpO2 spot-check) domain -------------------------- */
extern struct k_event fi_evt;

/* Inputs: UI / hw_module -> finger SMF (legacy BLE cmd_module removed) */
#define EVT_FI_SM_START         BIT(0)  /* hw init handshake */
#define EVT_BPT_EST_START       BIT(1)
#define EVT_BPT_CAL_START       BIT(2)
#define EVT_BPT_ENTER_CAL       BIT(3)
#define EVT_BPT_EXIT_CAL        BIT(4)
#define EVT_FI_SPO2_START       BIT(5)
#define EVT_FI_BPT_EST_CANCEL   BIT(6)
#define EVT_FI_SPO2_CANCEL      BIT(7)
#define EVT_FI_BPT_CAL_CANCEL   BIT(8)  /* consumed by finger SMF and display */
/* Output: finger SMF -> display */
#define EVT_FI_CONTACT_TIMEOUT  BIT(9)

/* ---- Wrist one-shot SpO2 domain ----------------------------------------- */
extern struct k_event spo2_evt;

#define EVT_SPO2_START   BIT(0)  /* UI -> wrist SpO2 control thread */
#define EVT_SPO2_STOP    BIT(1)  /* decode workqueue -> control thread */
#define EVT_SPO2_CANCEL  BIT(2)  /* UI -> control thread */

/* ---- Wrist PPG wear state ------------------------------------------------ */
/* BMI323 any-motion -> wrist PPG SMF: wakes it from OFF_SKIN. */
void hpi_ppg_wrist_notify_motion(void);

/*
 * Peek-and-consume a set of event bits, non-blocking. Returns true if any of
 * the requested bits was set (and clears exactly those bits), else false.
 * Drop-in replacement for `k_sem_take(&sem, K_NO_WAIT) == 0`.
 */
static inline bool hpi_evt_consume(struct k_event *ev, uint32_t bits)
{
	if (k_event_wait(ev, bits, false, K_NO_WAIT) & bits) {
		k_event_clear(ev, bits);
		return true;
	}
	return false;
}

#endif /* HPI_EVT_H */
