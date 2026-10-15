/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/usb_c/usbc_pd.h>

#include "source_transition.h"

/* Leave recovery margin before the minimum SPR sink transition timeout. */
#define SOURCE_TRANSITION_TIMEOUT_MS 400
/* Require in-range observations separated by at least one application polling interval. */
#define SOURCE_SETTLE_MS 10

void source_transition_init(struct source_transition *transition, const struct device *pwrctrl,
			    const struct device *vbus)
{
	k_mutex_init(&transition->lock);
	transition->pwrctrl = pwrctrl;
	transition->vbus = vbus;
	transition->pending = false;
	transition->active = false;
	transition->ready = false;
}

void source_transition_request(struct source_transition *transition, enum source_t target)
{
	k_mutex_lock(&transition->lock, K_FOREVER);
	transition->target = target;
	transition->deadline = k_uptime_get() + (target == SOURCE_0V ? PD_T_SAFE_0V_MAX_MS :
					       SOURCE_TRANSITION_TIMEOUT_MS);
	transition->in_range_since = -1;
	transition->pending = true;
	transition->active = true;
	transition->ready = false;
	k_mutex_unlock(&transition->lock);
}

void source_transition_cancel(struct source_transition *transition)
{
	k_mutex_lock(&transition->lock, K_FOREVER);
	transition->pending = false;
	transition->active = false;
	transition->ready = false;
	k_mutex_unlock(&transition->lock);
}

bool source_transition_is_ready(struct source_transition *transition)
{
	bool ready;

	k_mutex_lock(&transition->lock, K_FOREVER);
	ready = transition->ready;
	k_mutex_unlock(&transition->lock);

	return ready;
}

int source_transition_poll(struct source_transition *transition)
{
	int64_t now;
	int ret = 0;

	/* Keep request replacement, programming, measurement and completion serialized. */
	k_mutex_lock(&transition->lock, K_FOREVER);
	if (!transition->active) {
		goto out;
	}

	if (k_uptime_get() >= transition->deadline) {
		ret = -ETIMEDOUT;
		goto failed;
	}

	if (transition->pending) {
		transition->pending = false;
		ret = source_ctrl_set(transition->pwrctrl, transition->target);
		if (ret != 0) {
			goto failed;
		}
	}

	ret = source_is_ps_ready(transition->vbus, transition->target);
	if (ret < 0) {
		goto failed;
	}

	now = k_uptime_get();
	if (now >= transition->deadline) {
		ret = -ETIMEDOUT;
		goto failed;
	}

	if (ret == 0) {
		transition->in_range_since = -1;
	} else if (transition->in_range_since < 0) {
		transition->in_range_since = now;
	} else if (now - transition->in_range_since >= SOURCE_SETTLE_MS) {
		transition->active = false;
		transition->ready = true;
	}
	ret = 0;
	goto out;

failed:
	transition->pending = false;
	transition->active = false;
	transition->ready = false;
out:
	k_mutex_unlock(&transition->lock);
	return ret;
}
