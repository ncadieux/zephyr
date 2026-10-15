/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include "source_transition.h"

static struct source_transition transition;
static int set_result;
static int measure_result;
static int set_calls;
static int measure_calls;
static enum source_t applied;
static bool block_measurement;
static int poll_result;

K_SEM_DEFINE(measuring, 0, 1);
K_SEM_DEFINE(release_measurement, 0, 1);
K_SEM_DEFINE(requesting, 0, 1);
K_THREAD_STACK_DEFINE(poll_stack, 1024);
K_THREAD_STACK_DEFINE(request_stack, 1024);
static struct k_thread poll_thread;
static struct k_thread request_thread;

int source_ctrl_set(const struct device *dev, enum source_t target)
{
	ARG_UNUSED(dev);
	set_calls++;
	applied = target;
	return set_result;
}

int source_is_ps_ready(const struct device *dev, enum source_t target)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(target);
	measure_calls++;
	if (block_measurement) {
		k_sem_give(&measuring);
		zassert_ok(k_sem_take(&release_measurement, K_SECONDS(1)));
	}
	return measure_result;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	source_transition_init(&transition, NULL, NULL);
	set_result = 0;
	measure_result = 0;
	set_calls = 0;
	measure_calls = 0;
	applied = SOURCE_0V;
	block_measurement = false;
}

ZTEST(source_transition, test_settling_and_replacement)
{
	source_transition_request(&transition, SOURCE_5V);
	measure_result = 1;
	zassert_ok(source_transition_poll(&transition));
	zassert_false(source_transition_is_ready(&transition));
	k_msleep(10);
	zassert_ok(source_transition_poll(&transition));
	zassert_true(source_transition_is_ready(&transition));
	zassert_equal(set_calls, 1);

	source_transition_request(&transition, SOURCE_15V);
	zassert_false(source_transition_is_ready(&transition));
	measure_result = 0;
	zassert_ok(source_transition_poll(&transition));
	zassert_equal(applied, SOURCE_15V);
	zassert_false(source_transition_is_ready(&transition));
}

ZTEST(source_transition, test_out_of_range_restarts_settling)
{
	source_transition_request(&transition, SOURCE_9V);
	measure_result = 1;
	zassert_ok(source_transition_poll(&transition));
	k_msleep(10);
	measure_result = 0;
	zassert_ok(source_transition_poll(&transition));
	measure_result = 1;
	zassert_ok(source_transition_poll(&transition));
	zassert_false(source_transition_is_ready(&transition));
}

ZTEST(source_transition, test_programming_failure)
{
	source_transition_request(&transition, SOURCE_9V);
	set_result = -EIO;
	zassert_equal(source_transition_poll(&transition), -EIO);
	zassert_equal(measure_calls, 0);
	zassert_false(source_transition_is_ready(&transition));
	zassert_ok(source_transition_poll(&transition));
	zassert_equal(set_calls, 1);
}

ZTEST(source_transition, test_measurement_failure)
{
	source_transition_request(&transition, SOURCE_9V);
	measure_result = -EIO;
	zassert_equal(source_transition_poll(&transition), -EIO);
	zassert_false(source_transition_is_ready(&transition));
	zassert_ok(source_transition_poll(&transition));
	zassert_equal(measure_calls, 1);
}

ZTEST(source_transition, test_timeout)
{
	source_transition_request(&transition, SOURCE_15V);
	zassert_ok(source_transition_poll(&transition));
	k_msleep(410);
	/* Even an in-range voltage after the deadline must not report success. */
	measure_result = 1;
	zassert_equal(source_transition_poll(&transition), -ETIMEDOUT);
	zassert_false(source_transition_is_ready(&transition));
	zassert_ok(source_transition_poll(&transition));
}

ZTEST(source_transition, test_cancel_and_shutdown_replace_pending_request)
{
	source_transition_request(&transition, SOURCE_15V);
	source_transition_cancel(&transition);
	zassert_ok(source_transition_poll(&transition));
	zassert_equal(set_calls, 0);
	zassert_false(source_transition_is_ready(&transition));

	source_transition_request(&transition, SOURCE_15V);
	source_transition_request(&transition, SOURCE_0V);
	zassert_ok(source_transition_poll(&transition));
	zassert_equal(applied, SOURCE_0V);
	zassert_equal(set_calls, 1);
}

static void poll_entry(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	poll_result = source_transition_poll(&transition);
}

static void request_entry(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	k_sem_give(&requesting);
	source_transition_request(&transition, SOURCE_15V);
}

ZTEST(source_transition, test_replace_during_measurement)
{
	source_transition_request(&transition, SOURCE_5V);
	measure_result = 1;
	zassert_ok(source_transition_poll(&transition));
	k_msleep(10);
	block_measurement = true;

	k_thread_create(&poll_thread, poll_stack, K_THREAD_STACK_SIZEOF(poll_stack),
			poll_entry, NULL, NULL, NULL, 1, 0, K_NO_WAIT);
	zassert_ok(k_sem_take(&measuring, K_SECONDS(1)));
	k_thread_create(&request_thread, request_stack, K_THREAD_STACK_SIZEOF(request_stack),
			request_entry, NULL, NULL, NULL, 1, 0, K_NO_WAIT);
	zassert_ok(k_sem_take(&requesting, K_SECONDS(1)));
	k_sem_give(&release_measurement);
	zassert_ok(k_thread_join(&poll_thread, K_SECONDS(1)));
	zassert_ok(k_thread_join(&request_thread, K_SECONDS(1)));
	zassert_ok(poll_result);

	/* Completion of the old ADC read cannot mark the replacement request ready. */
	zassert_false(source_transition_is_ready(&transition));
	zassert_equal(applied, SOURCE_5V);
	block_measurement = false;
	measure_result = 0;
	zassert_ok(source_transition_poll(&transition));
	zassert_equal(applied, SOURCE_15V);
	zassert_false(source_transition_is_ready(&transition));
}

ZTEST_SUITE(source_transition, NULL, NULL, before, NULL, NULL);
