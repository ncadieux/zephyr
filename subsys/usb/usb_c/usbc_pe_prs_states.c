/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/smf.h>
#include <zephyr/usb_c/usbc.h>
#include <zephyr/drivers/usb_c/usbc_pd.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(usbc_stack, CONFIG_USBC_STACK_LOG_LEVEL);

#include "usbc_pe_common_internal.h"
#include "usbc_pe_prs_states_internal.h"
#include "usbc_tc_common_internal.h"
#include "usbc_stack.h"
#include <zephyr/drivers/usb_c/usbc_ppc.h>

/**
 * @brief PE_PRS_Send_Swap Entry state
 *	  NOTE: 9.2.20.3.7. PE_PRS_SRC_SNK_Send_Swap State
 *		9.2.20.4.7. PE_PRS_SNK_SRC_Send_Swap State
 */
void pe_prs_send_swap_entry(void *obj)
{
	struct policy_engine *pe = (struct policy_engine *)obj;
	const struct device *dev = pe->dev;

	LOG_INF("PE_PRS_Send_Swap");

	/*
	 * PE_PRS_SRC_SNK_Send_Swap (9.2.20.3.7) / PE_PRS_SNK_SRC_Send_Swap (9.2.20.4.7)
	 * On entry the Policy Engine Shall Request the Protocol Layer to send a PR_Swap
	 * Message and Shall start the SenderResponseTimer.
	 */
	pe_send_ctrl_msg(dev, PD_PACKET_SOP, PD_CTRL_PR_SWAP);
}

/**
 * @brief PE_PRS_Send_Swap Run state
 *	  NOTE: Sender Response Timer is handled in super state.
 */
enum smf_state_result pe_prs_send_swap_run(void *obj)
{
	struct policy_engine *pe = (struct policy_engine *)obj;
	const struct device *dev = pe->dev;
	struct usbc_port_data *data = dev->data;
	struct protocol_layer_rx_t *prl_rx = data->prl_rx;

	/*
	 * The Policy Engine Shall transition to the PE_SRC_Ready / PE_SNK_Ready State when:
	 *	- A Reject Message is received.
	 *	- Or a Wait Message is received.
	 *	- Or the SenderResponseTimer times out.
	 *
	 * The Policy Engine Shall transition to the PE_PRS_SRC_SNK_Transition_to_off /
	 * PE_PRS_SNK_SRC_Transition_to_off State when:
	 *	- An Accept Message is received.
	 *
	 * On exit the Policy Engine Shall stop the SenderResponseTimer.
	 */
	if (atomic_test_and_clear_bit(pe->flags, PE_FLAGS_TX_COMPLETE)) {
		/* Wait for Accept, Reject or Wait, handled by PE_SENDER_RESPONSE_PARENT */
	} else if (atomic_test_and_clear_bit(pe->flags, PE_FLAGS_MSG_RECEIVED)) {
		union pd_header header = prl_rx->emsg.header;

		if (received_control_message(dev, header, PD_CTRL_REJECT)) {
			/*
			 * Inform Device Policy Manager that the swap was rejected and
			 * return to the ready state.
			 */
			policy_notify(dev, MSG_REJECTED_RECEIVED);
			/* Transition to the Ready state */
			pe_set_ready_state(dev);
			return SMF_EVENT_PROPAGATE;
		} else if (received_control_message(dev, header, PD_CTRL_WAIT)) {
			/*
			 * Inform Device Policy Manager that the swap needs to wait and
			 * return to the ready state.
			 */
			if (policy_wait_notify(dev, WAIT_POWER_ROLE_SWAP)) {
				atomic_set_bit(pe->flags, PE_FLAGS_WAIT_POWER_ROLE_SWAP);
				usbc_timer_start(&pe->pd_t_wait_to_resend);
			}
			/* Transition to the Ready state */
			pe_set_ready_state(dev);
			return SMF_EVENT_PROPAGATE;
		} else if (received_control_message(dev, header, PD_CTRL_ACCEPT)) {
			/* Lock TC attached state until PR_Swap is complete. */
			tc_pr_swap_start(dev);
			/* Transition to PE_PRS_Transition_to_off state */
			pe_set_state(dev, PE_PRS_TRANSITION_TO_OFF);
			return SMF_EVENT_PROPAGATE;
		}
	} else if (atomic_test_and_clear_bit(pe->flags, PE_FLAGS_MSG_DISCARDED)) {
		/*
		 * Inform Device Policy Manager that the swap was discarded and
		 * return to the ready state.
		 */
		policy_notify(dev, MSG_DISCARDED);
		/* Transition to the Ready state */
		pe_set_ready_state(dev);
		return SMF_EVENT_PROPAGATE;
	}

	return SMF_EVENT_PROPAGATE;
}

/**
 * @brief PE_PRS_Evaluate_Swap Entry state
 *	  NOTE: 9.2.20.3.2. PE_PRS_SRC_SNK_Evaluate_Swap State
 *		9.2.20.3.3. PE_PRS_SRC_SNK_Accept_Swap State (embedded)
 *		9.2.20.3.8. PE_PRS_SRC_SNK_Reject_Swap State (embedded)
 *		9.2.20.4.2. PE_PRS_SNK_SRC_Evaluate_Swap State
 *		9.2.20.4.3. PE_PRS_SNK_SRC_Accept_Swap State (embedded)
 *		9.2.20.4.8. PE_PRS_SNK_SRC_Reject_Swap State (embedded)
 */
void pe_prs_evaluate_swap_entry(void *obj)
{
	struct policy_engine *pe = (struct policy_engine *)obj;
	const struct device *dev = pe->dev;

	LOG_INF("PE_PRS_Evaluate_Swap");

	/*
	 * PE_PRS_SRC_SNK_Evaluate_Swap (9.2.20.3.2) / PE_PRS_SNK_SRC_Evaluate_Swap (9.2.20.4.2)
	 * On entry the Policy Engine Shall ask the Device Policy Manager whether a Power Role
	 * Swap can be made.
	 */
	enum usbc_policy_check_t check = (pe->power_role == TC_ROLE_SOURCE)
						 ? CHECK_POWER_ROLE_SWAP_TO_SINK
						 : CHECK_POWER_ROLE_SWAP_TO_SOURCE;

	if (policy_check(dev, check)) {
		/*
		 * PE_PRS_SRC_SNK_Accept_Swap (9.2.20.3.3) / PE_PRS_SNK_SRC_Accept_Swap (9.2.20.4.3)
		 * On entry the Policy Engine Shall Request the Protocol Layer to send an Accept
		 * Message.
		 */
		pe_send_ctrl_msg(dev, PD_PACKET_SOP, PD_CTRL_ACCEPT);
	} else {
		/*
		 * PE_PRS_SRC_SNK_Reject_Swap (9.2.20.3.8) / PE_PRS_SNK_SRC_Reject_Swap (9.2.20.4.8)
		 * On entry the Policy Engine Shall Request the Protocol Layer to send a Reject
		 * Message if the Device is unable to perform a Power Role Swap at this time.
		 */
		pe_send_ctrl_msg(dev, PD_PACKET_SOP, PD_CTRL_REJECT);
	}
}

/**
 * @brief PE_PRS_Evaluate_Swap Run state
 */
enum smf_state_result pe_prs_evaluate_swap_run(void *obj)
{
	struct policy_engine *pe = (struct policy_engine *)obj;
	const struct device *dev = pe->dev;
	struct usbc_port_data *data = dev->data;
	struct protocol_layer_tx_t *prl_tx = data->prl_tx;
	struct protocol_layer_rx_t *prl_rx = data->prl_rx;

	/*
	 * PE_PRS_SRC_SNK_Accept_Swap (9.2.20.3.3) / PE_PRS_SNK_SRC_Accept_Swap (9.2.20.4.3)
	 * The Policy Engine Shall transition to the PE_PRS_SRC_SNK_Transition_to_off State when:
	 *	- The Accept Message has been sent.
	 *
	 * PE_PRS_SRC_SNK_Reject_Swap (9.2.20.3.8) / PE_PRS_SNK_SRC_Reject_Swap (9.2.20.4.8)
	 * The Policy Engine Shall transition to the PE_SRC_Ready / PE_SNK_Ready State when:
	 *	- The Reject or Wait Message has been sent.
	 */
	if (atomic_test_and_clear_bit(pe->flags, PE_FLAGS_TX_COMPLETE)) {
		/* Power Role Swap Accepted */
		if (prl_tx->msg_type == PD_CTRL_ACCEPT) {
			/* Lock TC attached state until PR_Swap is complete. */
			tc_pr_swap_start(dev);
			/* Transition to PE_PRS_Transition_to_off state */
			pe_set_state(dev, PE_PRS_TRANSITION_TO_OFF);
			return SMF_EVENT_PROPAGATE;
		}
		/* Power Role Swap Rejected */
		/* Transition to the Ready state */
		pe_set_ready_state(dev);
		return SMF_EVENT_PROPAGATE;
	} else if (atomic_test_and_clear_bit(pe->flags, PE_FLAGS_MSG_DISCARDED)) {
		policy_notify(dev, MSG_DISCARDED);
		pe_send_soft_reset(dev, prl_rx->emsg.type);
		return SMF_EVENT_PROPAGATE;
	}

	return SMF_EVENT_PROPAGATE;
}

/**
 * @brief PE_PRS_Transition_to_off Entry state
 *	  NOTE: 9.2.20.3.4. PE_PRS_SRC_SNK_Transition_to_off State
 *		9.2.20.4.4. PE_PRS_SNK_SRC_Transition_to_off State
 */
void pe_prs_transition_to_off_entry(void *obj)
{
	struct policy_engine *pe = (struct policy_engine *)obj;
	const struct device *dev = pe->dev;
	struct usbc_port_data *data = dev->data;
	int ret;

	LOG_INF("PE_PRS_Transition_to_off");

	if (pe->power_role == TC_ROLE_SINK) {
		/*
		 * On entry to the PE_PRS_SNK_SRC_Transition_to_off State the Policy Engine Shall
		 * initialize and run the PSSourceOffTimer and then Request the Device Policy
		 * Manager to turn off the Sink.
		 */
		ret = tcpc_set_snk_ctrl(data->tcpc, false);
		if (ret < 0 && ret != -ENOSYS) {
			LOG_ERR("PRS: Couldn't disable TCPC sink path: %d", ret);
		}

		/* Disable the VBUS sink path of the PPC */
		if (data->ppc != NULL) {
			ret = ppc_set_snk_ctrl(data->ppc, false);

			if (ret < 0 && ret != -ENOSYS) {
				LOG_ERR("PRS: Couldn't disable PPC sink");
			}
		}

		usbc_timer_start(&pe->pd_t_source_off);
	} else {
		/*
		 * On entry to the PE_PRS_SRC_SNK_Transition_to_off State the Policy Engine Shall
		 * Request the Device Policy Manager to turn off the Source.
		 */
		if (usbc_policy_src_en(dev, data->tcpc, false) != 0) {
			LOG_ERR("PRS: Couldn't disable VBUS source");
		}

		/* Disable the VBUS sourcing by the PPC */
		if (data->ppc != NULL) {
			ret = ppc_set_src_ctrl(data->ppc, false);

			if (ret < 0 && ret != -ENOSYS) {
				LOG_ERR("PRS: Couldn't disable PPC source");
			}
		}

		/* Bound the wait for VBUS to discharge to vSafe0V */
		usbc_timer_start(&pe->pd_t_source_off);
	}
}

/**
 * @brief PE_PRS_Transition_to_off Run state
 */
enum smf_state_result pe_prs_transition_to_off_run(void *obj)
{
	struct policy_engine *pe = (struct policy_engine *)obj;
	const struct device *dev = pe->dev;
	struct usbc_port_data *data = dev->data;

	/*
	 * PE_PRS_SNK_SRC_Transition_to_off (9.2.20.4.4)
	 * The Policy Engine Shall transition to the ErrorRecovery State when:
	 *	- The PSSourceOffTimer times out.
	 * The Policy Engine Shall transition to the PE_PRS_SNK_SRC_Assert_Rp State when:
	 *	- A PS_RDY Message is received.
	 *
	 * PE_PRS_SRC_SNK_Transition_to_off (9.2.20.3.4)
	 * The Policy Engine Shall transition to the PE_PRS_SRC_SNK_Assert_Rd State when:
	 *	- The Device Policy Manager indicates that the Source has been turned off.
	 *	  NOTE: VBUS reaching vSafe0V is used as the indication that the Source is off,
	 *	  so that no application callback is required to complete the swap. The
	 *	  PSSourceOffTimer bounds the wait in case VBUS never discharges.
	 */
	if (pe->power_role == TC_ROLE_SINK) {
		if (usbc_timer_expired(&pe->pd_t_source_off)) {
			usbc_timer_stop(&pe->pd_t_source_off);
			usbc_request(dev, REQUEST_TC_ERROR_RECOVERY);
		} else if (atomic_test_and_clear_bit(pe->flags, PE_FLAGS_MSG_RECEIVED)) {
			union pd_header header = data->prl_rx->emsg.header;

			if (received_control_message(dev, header, PD_CTRL_PS_RDY)) {
				usbc_timer_stop(&pe->pd_t_source_off);
				/* Transition to PE_PRS_Assert_CC state */
				pe_set_state(dev, PE_PRS_ASSERT_CC);
				return SMF_EVENT_PROPAGATE;
			}
		}
	} else {
		if (usbc_vbus_check_level(data->vbus, TC_VBUS_SAFE0V)) {
			usbc_timer_stop(&pe->pd_t_source_off);
			/* Transition to PE_PRS_Assert_CC state */
			pe_set_state(dev, PE_PRS_ASSERT_CC);
			return SMF_EVENT_PROPAGATE;
		} else if (usbc_timer_expired(&pe->pd_t_source_off)) {
			usbc_timer_stop(&pe->pd_t_source_off);
			usbc_request(dev, REQUEST_TC_ERROR_RECOVERY);
		}
	}

	return SMF_EVENT_PROPAGATE;
}

/**
 * @brief PE_PRS_Assert_CC Entry state
 *	  NOTE: 9.2.20.3.5. PE_PRS_SRC_SNK_Assert_Rd State
 *		9.2.20.4.5. PE_PRS_SNK_SRC_Assert_Rp State
 */
void pe_prs_assert_cc_entry(void *obj)
{
	struct policy_engine *pe = (struct policy_engine *)obj;
	const struct device *dev = pe->dev;
	struct usbc_port_data *data = dev->data;

	LOG_INF("PE_PRS_Assert_CC");

	if (pe->power_role == TC_ROLE_SINK) {
		/*
		 * On entry to the PE_PRS_SNK_SRC_Assert_Rp State the Policy Engine Shall Request
		 * the Device Policy Manager to change the resistor asserted on the CC wire from Rd
		 * to Rp.
		 */
		enum tc_rp_value rp = TC_RP_USB;

		if (data->policy_cb_get_src_rp) {
			data->policy_cb_get_src_rp(dev, &rp);
		}
		tcpc_select_rp_value(data->tcpc, rp);
		tcpc_set_cc(data->tcpc, rp);
	} else {
		/*
		 * On entry to the PE_PRS_SRC_SNK_Assert_Rd State the Policy Engine Shall Request
		 * the Device Policy Manager to change the resistor on the CC wire from Rp to Rd.
		 */
		tcpc_set_cc(data->tcpc, TC_CC_RD);
	}
}

/**
 * @brief PE_PRS_Assert_CC Run state
 */
enum smf_state_result pe_prs_assert_cc_run(void *obj)
{
	struct policy_engine *pe = (struct policy_engine *)obj;
	const struct device *dev = pe->dev;

	/*
	 * PE_PRS_SNK_SRC_Assert_Rp (9.2.20.4.5)
	 * The Policy Engine Shall transition to the PE_PRS_SNK_SRC_Source_on State when:
	 *	- The Device Policy Manager indicates that Rd is asserted.
	 *
	 * PE_PRS_SRC_SNK_Assert_Rd (9.2.20.3.5)
	 * The Policy Engine Shall transition to the PE_PRS_SRC_SNK_Wait_Source_on State when:
	 *	- The Device Policy Manager indicates that Rd is asserted.
	 */
	if (pe->power_role == TC_ROLE_SINK) {
		/* Transition to PE_PRS_Source_on state */
		pe_set_state(dev, PE_PRS_SOURCE_ON);
	} else {
		/* Transition to PE_PRS_Wait_Source_on state */
		pe_set_state(dev, PE_PRS_WAIT_SOURCE_ON);
	}

	return SMF_EVENT_PROPAGATE;
}

/**
 * @brief PE_PRS_Source_on Entry state
 *	  NOTE: 9.2.20.4.6. PE_PRS_SNK_SRC_Source_on State
 */
void pe_prs_source_on_entry(void *obj)
{
	struct policy_engine *pe = (struct policy_engine *)obj;
	const struct device *dev = pe->dev;
	struct usbc_port_data *data = dev->data;

	LOG_INF("PE_PRS_Source_on");

	/*
	 * On entry to the PE_PRS_SNK_SRC_Source_on State the Policy Engine Shall Request the
	 * Device Policy Manager to turn on the Source.
	 */
	if (usbc_policy_src_en(dev, data->tcpc, true) != 0) {
		LOG_ERR("PRS: Couldn't enable VBUS source");
	}

	/* Enable the VBUS sourcing by the PPC */
	if (data->ppc != NULL) {
		int ret = ppc_set_src_ctrl(data->ppc, true);

		if (ret < 0 && ret != -ENOSYS) {
			LOG_ERR("PRS: Couldn't enable PPC source");
		}
	}

	pe->submachine = SM_WAIT_FOR_PS_RDY;
}

/**
 * @brief PE_PRS_Source_on Run state
 */
enum smf_state_result pe_prs_source_on_run(void *obj)
{
	struct policy_engine *pe = (struct policy_engine *)obj;
	const struct device *dev = pe->dev;
	struct usbc_port_data *data = dev->data;

	/*
	 * The Policy Engine Shall transition to the PE_SRC_Startup State when:
	 *	- The Source Port VBUS is at vSafe5V.
	 * The Policy Engine Shall transition to the ErrorRecovery State when:
	 *	- The PS_RDY Message is not sent after retries (a GoodCRC Message has not been
	 *	  received). A Soft Reset Shall Not be initiated in this case.
	 * On exit from the PE_PRS_SNK_SRC_Source_on State the Policy Engine Shall send a PS_RDY
	 * Message.
	 */
	if (pe->submachine == SM_WAIT_FOR_PS_RDY) {
		if (usbc_vbus_check_level(data->vbus, TC_VBUS_PRESENT)) {
			pe->power_role = TC_ROLE_SOURCE;
			tcpc_set_roles(data->tcpc, pe->power_role, pe->data_role);
			policy_notify(dev, POWER_ROLE_IS_SOURCE);

			pe_send_ctrl_msg(dev, PD_PACKET_SOP, PD_CTRL_PS_RDY);
			pe->submachine = SM_WAIT_FOR_TX;
		}
	} else if (pe->submachine == SM_WAIT_FOR_TX) {
		if (atomic_test_and_clear_bit(pe->flags, PE_FLAGS_TX_COMPLETE)) {
			/* Transition TC to the new role and unlock disconnect detection */
			tc_pr_swap_transition(dev, TC_ATTACHED_SRC_STATE);
			/* Transition to PE_SRC_Startup state */
			pe_set_state(dev, PE_SRC_STARTUP);
			return SMF_EVENT_PROPAGATE;
		} else if (atomic_test_and_clear_bit(pe->flags, PE_FLAGS_MSG_XMIT_ERROR)) {
			usbc_request(dev, REQUEST_TC_ERROR_RECOVERY);
		}
	}

	return SMF_EVENT_PROPAGATE;
}

/**
 * @brief PE_PRS_Wait_Source_on Entry state
 *	  NOTE: 9.2.20.3.6. PE_PRS_SRC_SNK_Wait_Source_on State
 */
void pe_prs_wait_source_on_entry(void *obj)
{
	struct policy_engine *pe = (struct policy_engine *)obj;
	const struct device *dev = pe->dev;

	LOG_INF("PE_PRS_Wait_Source_on");

	/*
	 * On entry to the PE_PRS_SRC_SNK_Wait_Source_on State the Policy Engine Shall Request the
	 * Protocol Layer to send a PS_RDY Message and Shall start the PSSourceOnTimer.
	 */
	pe_send_ctrl_msg(dev, PD_PACKET_SOP, PD_CTRL_PS_RDY);
	usbc_timer_start(&pe->pd_t_source_on);
	pe->submachine = SM_WAIT_FOR_TX;
}

/**
 * @brief PE_PRS_Wait_Source_on Run state
 */
enum smf_state_result pe_prs_wait_source_on_run(void *obj)
{
	struct policy_engine *pe = (struct policy_engine *)obj;
	const struct device *dev = pe->dev;
	struct usbc_port_data *data = dev->data;

	/*
	 * The Policy Engine Shall transition to the PE_SNK_Startup when:
	 *	- A PS_RDY Message is received indicating that the remote Source is now supplying
	 *	  power.
	 * The Policy Engine Shall transition to the ErrorRecovery State when:
	 *	- The PSSourceOnTimer times out or
	 *	- The PS_RDY Message is not sent after retries (a GoodCRC Message has not been
	 *	  received).
	 * Note: A Soft Reset Shall Not be initiated in this case.
	 * On exit from the PE_PRS_SRC_SNK_Wait_Source_on State the Policy Engine Shall stop the
	 * PSSourceOnTimer.
	 */
	if (pe->submachine == SM_WAIT_FOR_TX) {
		if (atomic_test_and_clear_bit(pe->flags, PE_FLAGS_TX_COMPLETE)) {
			pe->submachine = SM_WAIT_FOR_RX;
			return SMF_EVENT_PROPAGATE;
		} else if (atomic_test_and_clear_bit(pe->flags, PE_FLAGS_MSG_XMIT_ERROR)) {
			usbc_timer_stop(&pe->pd_t_source_on);
			usbc_request(dev, REQUEST_TC_ERROR_RECOVERY);
		}
	} else if (pe->submachine == SM_WAIT_FOR_RX) {
		if (usbc_timer_expired(&pe->pd_t_source_on)) {
			usbc_timer_stop(&pe->pd_t_source_on);
			usbc_request(dev, REQUEST_TC_ERROR_RECOVERY);
		} else if (atomic_test_and_clear_bit(pe->flags, PE_FLAGS_MSG_RECEIVED)) {
			union pd_header header = data->prl_rx->emsg.header;

			if (received_control_message(dev, header, PD_CTRL_PS_RDY)) {
				usbc_timer_stop(&pe->pd_t_source_on);

				pe->power_role = TC_ROLE_SINK;
				tcpc_set_roles(data->tcpc, pe->power_role, pe->data_role);
				policy_notify(dev, POWER_ROLE_IS_SINK);

				/* Transition TC to the new role and unlock disconnect detection */
				tc_pr_swap_transition(dev, TC_ATTACHED_SNK_STATE);
				/* Transition to PE_SNK_Startup state */
				pe_set_state(dev, PE_SNK_STARTUP);
				return SMF_EVENT_PROPAGATE;
			}
		}
	}

	return SMF_EVENT_PROPAGATE;
}
