/*
 * Copyright (c) 2023 The Chromium OS Authors
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT zephyr_usb_c_pwrctrl

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>

#include "power_ctrl.h"

LOG_MODULE_REGISTER(power_ctrl, LOG_LEVEL_DBG);

#define PWM_FOR_15V 45000
#define PWM_FOR_9V  30000
#define PWM_FOR_5V  21500
#define PWM_FOR_0V  0

struct power_ctrl_config {
	struct gpio_dt_spec source_en;
	struct gpio_dt_spec vconn1_en;
	struct gpio_dt_spec vconn2_en;
	struct gpio_dt_spec dcdc_en;
	struct pwm_dt_spec pwm_ctl;
};

int vconn_ctrl_set(const struct device *dev, enum vconn_t v)
{
	const struct power_ctrl_config *cfg = dev->config;
	int ret;

	switch (v) {
	case VCONN_OFF:
		ret = gpio_pin_set_dt(&cfg->vconn1_en, 0);
		ret |= gpio_pin_set_dt(&cfg->vconn2_en, 0);
		break;
	case VCONN1_ON:
		ret = gpio_pin_set_dt(&cfg->vconn1_en, 1);
		ret |= gpio_pin_set_dt(&cfg->vconn2_en, 0);
		break;
	case VCONN2_ON:
		ret = gpio_pin_set_dt(&cfg->vconn1_en, 0);
		ret |= gpio_pin_set_dt(&cfg->vconn2_en, 1);
		break;
	default:
		LOG_ERR("Error: invalid VCONN state %d requested on device %s", v, dev->name);
		return -EINVAL;
	}

	if (ret != 0) {
		LOG_ERR("Error %d: failed to set VCONN state %d on device %s", ret, v, dev->name);
	}

	return ret;
}

int source_ctrl_set(const struct device *dev, enum source_t v)
{
	const struct power_ctrl_config *cfg = dev->config;
	uint32_t pwmv;
	int ret;

	switch (v) {
	case SOURCE_0V:
		pwmv = PWM_FOR_0V;
		break;
	case SOURCE_5V:
		pwmv = PWM_FOR_5V;
		break;
	case SOURCE_9V:
		pwmv = PWM_FOR_9V;
		break;
	case SOURCE_15V:
		pwmv = PWM_FOR_15V;
		break;
	default:
		LOG_ERR("Error: invalid source voltage %d requested on device %s", v, dev->name);
		return -EINVAL;
	}

	if (v == SOURCE_0V) {
		/* Disable Vbus: Disable source and DC-DC, then configure Vbus voltage */
		ret = gpio_pin_set_dt(&cfg->source_en, 0);
		if (ret != 0) {
			LOG_ERR("Error %d: failed to disable source on device %s", ret, dev->name);
			return ret;
		}

		ret = gpio_pin_set_dt(&cfg->dcdc_en, 0);
		if (ret != 0) {
			LOG_ERR("Error %d: failed to disable dcdc converter on device %s", ret,
				dev->name);
			return ret;
		}

		ret = pwm_set_pulse_dt(&cfg->pwm_ctl, pwmv);
		if (ret != 0) {
			LOG_ERR("Error %d: failed to set VBUS pulse on device %s", ret, dev->name);
			return ret;
		}
	} else {
		/* Enable Vbus: Enable DC-DC, configure Vbus voltage, then enable source */
		ret = gpio_pin_set_dt(&cfg->dcdc_en, 1);
		if (ret != 0) {
			LOG_ERR("Error %d: failed to enable dcdc converter on device %s", ret,
				dev->name);
			return ret;
		}

		ret = pwm_set_pulse_dt(&cfg->pwm_ctl, pwmv);
		if (ret != 0) {
			LOG_ERR("Error %d: failed to set VBUS pulse on device %s", ret, dev->name);
			return ret;
		}

		ret = gpio_pin_set_dt(&cfg->source_en, 1);
		if (ret != 0) {
			LOG_ERR("Error %d: failed to enable source on device %s", ret, dev->name);
			return ret;
		}
	}

	return 0;
}

static int power_ctrl_init(const struct device *dev)
{
	const struct power_ctrl_config *cfg = dev->config;
	int ret;

	if (!gpio_is_ready_dt(&cfg->source_en)) {
		LOG_ERR("Error: Source Enable device %s is not ready", cfg->source_en.port->name);
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&cfg->dcdc_en)) {
		LOG_ERR("Error: DCDC Enable device %s is not ready", cfg->dcdc_en.port->name);
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&cfg->vconn1_en)) {
		LOG_ERR("Error: VCONN1 Enable device %s is not ready", cfg->vconn1_en.port->name);
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&cfg->vconn2_en)) {
		LOG_ERR("Error: VCONN2 Enable device %s is not ready", cfg->vconn2_en.port->name);
		return -ENODEV;
	}

	if (!pwm_is_ready_dt(&cfg->pwm_ctl)) {
		LOG_ERR("Error: PWM CTL device is not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&cfg->source_en, GPIO_OUTPUT);
	if (ret != 0) {
		LOG_ERR("Error %d: failed to configure Source Enable device %s pin %d", ret,
			cfg->source_en.port->name, cfg->source_en.pin);
		return ret;
	}

	ret = gpio_pin_configure_dt(&cfg->dcdc_en, GPIO_OUTPUT);
	if (ret != 0) {
		LOG_ERR("Error %d: failed to configure DCDC Enable device %s pin %d", ret,
			cfg->dcdc_en.port->name, cfg->dcdc_en.pin);
		return ret;
	}

	ret = gpio_pin_configure_dt(&cfg->vconn1_en, GPIO_OUTPUT | GPIO_OPEN_DRAIN);
	if (ret != 0) {
		LOG_ERR("Error %d: failed to configure VCONN1 Enable device %s pin %d", ret,
			cfg->vconn1_en.port->name, cfg->vconn1_en.pin);
		return ret;
	}

	ret = gpio_pin_configure_dt(&cfg->vconn2_en, GPIO_OUTPUT | GPIO_OPEN_DRAIN);
	if (ret != 0) {
		LOG_ERR("Error %d: failed to configure VCONN2 Enable device %s pin %d", ret,
			cfg->vconn2_en.port->name, cfg->vconn2_en.pin);
		return ret;
	}

	/* Disable Vconn */
	ret = vconn_ctrl_set(dev, VCONN_OFF);
	if (ret != 0) {
		LOG_ERR("Error %d: failed to disable VCONN on device %s", ret, dev->name);
		return ret;
	}

	/* Disable Vbus */
	ret = source_ctrl_set(dev, SOURCE_0V);
	if (ret != 0) {
		LOG_ERR("Error %d: failed to set VBUS to 0V", ret);
		return ret;
	}

	return 0;
}

#define PWRCTRL_INIT(inst)                                                                         \
	static const struct power_ctrl_config power_ctrl_config_##inst = {                         \
		.source_en = GPIO_DT_SPEC_INST_GET(inst, source_en_gpios),                         \
		.vconn1_en = GPIO_DT_SPEC_INST_GET(inst, vconn1_en_gpios),                         \
		.vconn2_en = GPIO_DT_SPEC_INST_GET(inst, vconn2_en_gpios),                         \
		.dcdc_en = GPIO_DT_SPEC_INST_GET_OR(inst, dcdc_en_gpios, {0}),                     \
		.pwm_ctl = PWM_DT_SPEC_INST_GET_OR(inst, {0}),                                     \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, power_ctrl_init, NULL, NULL, &power_ctrl_config_##inst,        \
			      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

DT_INST_FOREACH_STATUS_OKAY(PWRCTRL_INIT)
