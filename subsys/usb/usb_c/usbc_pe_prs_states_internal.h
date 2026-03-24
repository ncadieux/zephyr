/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_USBC_PE_PRS_STATES_INTERNAL_H_
#define ZEPHYR_SUBSYS_USBC_PE_PRS_STATES_INTERNAL_H_

/**
 * @brief PE_PRS_Send_Swap Entry state
 *	  NOTE: 9.2.20.3.7. PE_PRS_SRC_SNK_Send_Swap State
 *		9.2.20.4.7. PE_PRS_SNK_SRC_Send_Swap State
 */
void pe_prs_send_swap_entry(void *obj);
enum smf_state_result pe_prs_send_swap_run(void *obj);

/**
 * @brief PE_PRS_Evaluate_Swap Entry state
 *	  NOTE: 9.2.20.3.2. PE_PRS_SRC_SNK_Evaluate_Swap State
 *		9.2.20.3.3. PE_PRS_SRC_SNK_Accept_Swap State (embedded)
 *		9.2.20.3.8. PE_PRS_SRC_SNK_Reject_Swap State (embedded)
 *		9.2.20.4.2. PE_PRS_SNK_SRC_Evaluate_Swap State
 *		9.2.20.4.3. PE_PRS_SNK_SRC_Accept_Swap State (embedded)
 *		9.2.20.4.8. PE_PRS_SNK_SRC_Reject_Swap State (embedded)
 */
void pe_prs_evaluate_swap_entry(void *obj);
enum smf_state_result pe_prs_evaluate_swap_run(void *obj);

/**
 * @brief PE_PRS_Transition_to_off Entry state
 *	  NOTE: 9.2.20.3.4. PE_PRS_SRC_SNK_Transition_to_off State
 *		9.2.20.4.4. PE_PRS_SNK_SRC_Transition_to_off State
 */
void pe_prs_transition_to_off_entry(void *obj);
enum smf_state_result pe_prs_transition_to_off_run(void *obj);

/**
 * @brief PE_PRS_Assert_CC Entry state
 *	  NOTE: 9.2.20.3.5. PE_PRS_SRC_SNK_Assert_Rd State
 *		9.2.20.4.5. PE_PRS_SNK_SRC_Assert_Rp State
 */
void pe_prs_assert_cc_entry(void *obj);
enum smf_state_result pe_prs_assert_cc_run(void *obj);

/**
 * @brief PE_PRS_Source_on Entry state
 *	  NOTE: 9.2.20.4.6. PE_PRS_SNK_SRC_Source_on State
 */
void pe_prs_source_on_entry(void *obj);
enum smf_state_result pe_prs_source_on_run(void *obj);

/**
 * @brief PE_PRS_Wait_Source_on Entry state
 *	  NOTE: 9.2.20.3.6. PE_PRS_SRC_SNK_Wait_Source_on State
 */
void pe_prs_wait_source_on_entry(void *obj);
enum smf_state_result pe_prs_wait_source_on_run(void *obj);

#endif /* ZEPHYR_SUBSYS_USBC_PE_PRS_STATES_INTERNAL_H_ */
