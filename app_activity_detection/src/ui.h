#ifndef UI_H
#define UI_H

#include <zephyr/kernel.h>
#include <stdbool.h>

/* ---- boot screen API (called from boot.c, any thread) ---- */
void ui_boot_add(const char *text, bool ok, bool show_status);
void ui_start(void);                            /* display rail is on: start the UI thread */
bool ui_wait_boot_screen(k_timeout_t timeout);  /* true once the boot screen is up */
void ui_boot_complete(void);                    /* UI moves on to the home screen */

/* ---- implemented in home_screen.c, called on the UI thread only ---- */
void home_screen_create(void);

#endif