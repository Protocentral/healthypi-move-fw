#ifndef ACTIVITY_H
#define ACTIVITY_H

#include <stdbool.h>
#include <stdint.h>

#define ACT_RES_NONE        (-1)   /* no inference yet */
#define ACT_RES_UNAVAILABLE (-2)   /* model failed to load */

struct activity_result {
	int idx;               /* class index, or ACT_RES_* */
	const char *label;     /* NULL when idx < 0 */
	uint8_t conf_pct;      /* 0..100 */
	uint32_t infer_ms;     /* last ml_engine_invoke() duration */
    uint32_t seq;          /* increments once per new inference */
};

void activity_get_result(struct activity_result *r);

#endif