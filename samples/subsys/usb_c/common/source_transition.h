/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SAMPLES_SUBSYS_USBC_SOURCE_TRANSITION_H_
#define ZEPHYR_SAMPLES_SUBSYS_USBC_SOURCE_TRANSITION_H_

#include <zephyr/kernel.h>

#include "power_ctrl.h"

/* All operations run in thread context and serialize access to the power controller. */
struct source_transition {
	struct k_mutex lock;
	const struct device *pwrctrl;
	const struct device *vbus;
	enum source_t target;
	int64_t deadline;
	int64_t in_range_since;
	bool pending;
	bool active;
	bool ready;
};

/* Initialize before starting the USB-C stack. */
void source_transition_init(struct source_transition *transition, const struct device *pwrctrl,
			    const struct device *vbus);

/* Replace the current request and invalidate its completion. */
void source_transition_request(struct source_transition *transition, enum source_t target);

/* Cancel polling and queued programming without changing the current hardware setting. */
void source_transition_cancel(struct source_transition *transition);

/* Return true only after the current request has completed. */
bool source_transition_is_ready(struct source_transition *transition);

/* Advance from the application loop; return a negative error once on transition failure. */
int source_transition_poll(struct source_transition *transition);

#endif /* ZEPHYR_SAMPLES_SUBSYS_USBC_SOURCE_TRANSITION_H_ */
