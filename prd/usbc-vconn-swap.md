# PRD: USB-C VCONN Swap (PE_VCS_*) State Machine

**Spec reference:** USB PD R3.2 V1.2 (Mar 2026), §7.13 (VCONN Swap AMS), §7.31.12 (VCONN Timers), §9.2.21 (VCONN Swap State Diagram).

**Status:** Draft — pending implementation. Blocked-by: none. Blocks: future EPR work, SOP'/SOP'' cable communication.

## Problem Statement

USB-C Power Delivery defines a VCONN Swap AMS that lets two PD ports exchange ownership of the VCONN supply that powers the cable plug (and any electronically-marked / active cable circuitry). Today the Zephyr USB-C stack does not implement any part of the Policy Engine VCONN Swap state machine:

- Outgoing VCONN_Swap: an application's Device Policy Manager (DPM) cannot ask the PE to send a `VCONN_Swap` message to its partner.
- Incoming VCONN_Swap: when the partner sends a `VCONN_Swap` control message, the PE has no handler for it and the message is silently dropped (or generates a Soft_Reset), which is a spec violation and forces the partner into recovery.
- The TC layer already turns VCONN on at `Attached.SRC` entry and off at exit (`usbc_tc_src_states.c:277,357`), but there is no mechanism to flip "who supplies VCONN" without dropping the connection. This blocks any subsequent feature that requires the non-default side to talk to the cable plug — most importantly EPR mode entry, which has to discover an EPR-capable cable from the side that is currently sourcing VCONN.

From a developer's perspective: I cannot ship a DRP product that interoperates correctly with chargers, docks, or active cables that send `VCONN_Swap`, and I cannot evolve the stack toward EPR or SOP'/SOP'' cable communication until VCONN Swap exists.

## Solution

Implement the eight `PE_VCS_*` states defined in spec §9.2.21 as a self-contained module that plugs into the existing Policy Engine SMF framework. Mirror the structure of the recently-landed Power Role Swap PR (`subsys/usb/usb_c/usbc_pe_prs_states.c`).

Refusal policy (departure from spec, agreed with user):
- If we are presently the VCONN Source, we **always Accept** an incoming `VCONN_Swap`. This satisfies the spec rule that a port currently sourcing VCONN Shall Not send Reject (§7.13).
- If we are not presently the VCONN Source, we consult the DPM via a single new policy check `CHECK_VCONN_SWAP`. True → Accept, false → Reject. **Wait is not implemented** in this PRD round.

Outgoing swap is triggered by a new DPM request value `REQUEST_PE_VCONN_SWAP` issued through the existing `usbc_request()` API.

VCONN tracking lives in a new `PE_FLAGS_VCONN_SOURCE` atomic flag bit in `struct policy_engine`. The TC layer sets it when it enables VCONN on `Attached.SRC` entry; the VCS states flip it on swap completion. PE-level driver work is limited to the existing `tcpc_set_vconn()` / `tcpc_vconn_discharge()` callbacks already present on every TCPC driver — no new driver ops.

Two new DPM notifications are added so the application can observe the resulting VCONN role: `VCONN_ROLE_IS_SOURCE` (our port is now supplying VCONN) and `VCONN_ROLE_IS_SINK` (our port is no longer supplying VCONN; the partner is). The pair mirrors the existing `POWER_ROLE_IS_SOURCE` / `POWER_ROLE_IS_SINK` convention.

## User Stories

1. As a USB-C DRP product developer, I want my Zephyr port to respond to an incoming `VCONN_Swap` message with `Accept` and complete the handoff so that my port partner does not need to issue a Hard Reset.
2. As a USB-C application developer, I want to call `usbc_request(dev, REQUEST_PE_VCONN_SWAP)` to initiate an outgoing VCONN Swap so that my port can become the VCONN Source on demand.
3. As an application developer, I want the PE to handle the symmetric path automatically (we are VCONN Source vs we are not), so that I do not have to encode spec rules in my DPM.
4. As a compliance engineer, I want the PE to refuse the swap with `Reject` if my application has not opted-in to VCONN Swap support, so that we remain spec-compliant even on products that physically cannot source VCONN.
5. As a compliance engineer, I want the new VCONN Source to send `PS_RDY` within `tVconnSourceOn` (100–200 ms) of the `Accept` message GoodCRC EOP, so that the swap meets spec timing.
6. As a compliance engineer, I want the initial VCONN Source to cease sourcing VCONN within `tVconnSourceOff` of the `PS_RDY` GoodCRC EOP, so that the cable plug is not powered by two sources simultaneously.
7. As a compliance engineer, I want a `VconnOnTimer` timeout to trigger a Hard Reset, so that a stuck partner does not leave the port in a half-swapped state.
8. As an application developer, I want to be notified when our port becomes the VCONN Source so that I can start communicating with the cable plug.
9. As an application developer, I want to be notified when our port gives up VCONN Source so that I can stop scheduling SOP'/SOP'' traffic.
10. As a DRP developer, I want VCONN Swap to be supported from both `PE_SRC_Ready` and `PE_SNK_Ready`, so that the swap is decoupled from power role.
11. As a Type-C compliance engineer, I want the Rp/Rd resistors and the source-of-VBUS to remain unchanged across a VCONN Swap, so that the existing power contract is preserved.
12. As a sink-only product developer, I want VCONN Swap to be optional via Kconfig, so that my sink-only build does not pay code-size cost for a feature it cannot exercise.
13. As a maintainer, I want the VCS state machine code to live in its own translation unit, so that the existing `usbc_pe_common.c` does not grow further.
14. As a sample developer, I want the `samples/subsys/usb_c/drp` sample to opt into VCONN Swap so there is a working reference.
15. As a future EPR developer, I want the `PE_FLAGS_VCONN_SOURCE` flag to be the single source of truth for "are we VCONN Source", so that EPR cable discovery can branch on it without re-querying the TCPC.
16. As a developer adding hard-reset handling, I want VCS states to honor incoming Hard Reset transitions per spec §9.2.21 (back to `PE_SRC_Hard_Reset` or `PE_SNK_Hard_Reset`), so that error recovery is preserved.
17. As a DPM author, I want a clearly-named, single new policy check (`CHECK_VCONN_SWAP`) so that I do not have to wire multiple callbacks for one feature.
18. As a DPM author, I want the policy check to be consulted only when relevant (i.e., when we are not currently VCONN Source), so that I do not get spurious queries that I would have to answer "yes" to.
19. As a stack maintainer, I want the SOP'/SOP'' soft-reset requirement after a VCONN Swap (spec §8 cable-plug message-ID resync) called out as a follow-up rather than implemented now, since Zephyr does not yet emit SOP'/SOP'' traffic.
20. As a developer reading the code, I want each `PE_VCS_*` state to map 1:1 with the spec section number, so that I can cross-reference behavior to the spec without ambiguity.

## Implementation Decisions

### State machine

- Implement the eight `PE_VCS_*` states per spec §9.2.21:
  - `PE_VCS_SEND_SWAP` (§9.2.21.1)
  - `PE_VCS_EVALUATE_SWAP` (§9.2.21.2) — pass-through that always transitions to Accept or Reject based on the refusal policy below.
  - `PE_VCS_ACCEPT_SWAP` (§9.2.21.3)
  - `PE_VCS_REJECT_SWAP` (§9.2.21.4) — Reject only (no Wait); reachable only when we are not VCONN Source and DPM says unsupported.
  - `PE_VCS_WAIT_FOR_VCONN` (§9.2.21.5)
  - `PE_VCS_TURN_OFF_VCONN` (§9.2.21.6)
  - `PE_VCS_TURN_ON_VCONN` (§9.2.21.7)
  - `PE_VCS_SEND_PS_RDY` (§9.2.21.8)
- The optional `PE_VCS_Force_VCONN` (§9.2.21.9) is **out of scope** this round.
- VCS state code lives in a new file pair: `usbc_pe_vcs_states.c` and `usbc_pe_vcs_states_internal.h`, modeled on `usbc_pe_prs_states.c`.

### VCONN role tracking

- Add a new `PE_FLAGS_VCONN_SOURCE` bit to `enum pe_flags` in `usbc_pe_common_internal.h`.
- The TC source state machine sets this flag at the same point it currently calls `tcpc_set_vconn(tcpc, true)` (success path in `usbc_tc_src_states.c`).
- The TC clears it when it calls `tcpc_set_vconn(tcpc, false)` on Attached.SRC exit.
- VCS state machine flips it on completion of either direction.
- PE branches on this flag where the spec state diagram branches on "presently VCONN Source".

### DPM API additions

In `include/zephyr/usb_c/usbc.h`:
- Extend `enum usbc_policy_request_t` with `REQUEST_PE_VCONN_SWAP`.
- Extend `enum usbc_policy_check_t` with `CHECK_VCONN_SWAP`. Consulted only in `PE_VCS_EVALUATE_SWAP` and only when `PE_FLAGS_VCONN_SOURCE` is clear.
- Extend `enum usbc_policy_notify_t` with `VCONN_ROLE_IS_SOURCE` and `VCONN_ROLE_IS_SINK`. Emitted from PE when a swap completes.
- No new callback function-pointer types are added; the existing `policy_cb_check_t` and `policy_cb_notify_t` carry the new enum values.

### Driver-layer interaction

- VCS states call the existing `tcpc_set_vconn(tcpc, true/false)` and `tcpc_vconn_discharge(tcpc, true/false)` APIs already present in `include/zephyr/drivers/usb_c/usbc_tcpc.h`.
- No new TCPC driver ops are introduced. No driver code needs to change.
- The existing application-level `policy_cb_check` for `CHECK_VCONN_CONTROL` is unchanged; it gates whether the PE is allowed to touch VCONN at all (currently used by the TC layer).

### TC-layer coordination

- TC source-attach VCONN enable path stays as-is; it represents the *initial* VCONN Source assignment.
- On a successful VCONN Swap completion, PE owns VCONN enable/disable directly via TCPC ops, and the TC layer must not re-disable VCONN on a subsequent role swap as long as `PE_FLAGS_VCONN_SOURCE` is set. The PRS implementation already established the pattern of preserving stack state across role swaps (see PRS commit `55d5a876fcd` startup-entry change); VCS reuses that pattern at the TC↔PE boundary.

### Timers

- Add `pd_t_vconn_on` timer field to `struct policy_engine`, initialized to `tVconnSourceTimeout` max (200 ms per spec, used as `VconnOnTimer`).
- Started on entry to `PE_VCS_WAIT_FOR_VCONN`.
- Stopped when `PS_RDY` is received in `PE_VCS_WAIT_FOR_VCONN`.
- Timeout → hard-reset transition (`PE_SRC_HARD_RESET` or `PE_SNK_HARD_RESET` based on current power role).
- `tVconnSourceOff` is the partner's responsibility per spec; we do not enforce a local timer for it.
- If `PD_T_VCONN_SOURCE_TIMEOUT_MAX_MS` does not exist in `include/zephyr/drivers/usb_c/usbc_pd.h`, add it alongside the other `PD_T_*` constants.

### Refusal logic (Evaluate_Swap behavior)

```
on entry to PE_VCS_EVALUATE_SWAP:
    if PE_FLAGS_VCONN_SOURCE is set:
        transition to PE_VCS_ACCEPT_SWAP
    else:
        if policy_check(CHECK_VCONN_SWAP):
            transition to PE_VCS_ACCEPT_SWAP
        else:
            transition to PE_VCS_REJECT_SWAP
```

`PE_VCS_REJECT_SWAP` sends a `Reject` control message and transitions back to `PE_SRC_Ready` or `PE_SNK_Ready` based on current power role. No Wait path is implemented.

### Entry points

- Entered from `PE_SRC_Ready` / `PE_SNK_Ready` on:
  - DPM request `REQUEST_PE_VCONN_SWAP` → `PE_VCS_SEND_SWAP`.
  - Incoming `PD_CTRL_VCONN_SWAP` message → `PE_VCS_EVALUATE_SWAP`.
- Spec also allows entry from `PE_SRC_EPR_Mode_Discover_Cable` and `PE_SNK_EPR_Mode_Entry_Wait_For_Response`, but Zephyr has no EPR states today, so these entry edges are explicitly out of scope.

### Kconfig

- Add `CONFIG_USBC_CSM_VCONN_SWAP` (bool, default n) gated on at least one of `USBC_CSM_SUPPORTS_SINK` or `USBC_CSM_SUPPORTS_SOURCE`. When disabled:
  - VCS state entries are compiled out.
  - Incoming `PD_CTRL_VCONN_SWAP` responds with `Not_Supported`.

### Sample integration

- `samples/subsys/usb_c/drp` opts in to `CONFIG_USBC_CSM_VCONN_SWAP=y`.
- Sample's DPM returns `true` from the new `CHECK_VCONN_SWAP` check.
- Sample logs the two new notifications so behavior is observable on the console.
- Sink and Source samples are left untouched.

## Testing Decisions

Good tests for this PRD exercise the state machine through its public surface: DPM requests in, DPM notifications and PD messages out. They do not poke private flags or call internal SMF setters. They run on `native_sim` using the existing USB-C test harness in `tests/subsys/usb_c/`.

Modules to test:

- VCS state machine end-to-end (outgoing path): given a port partner, when DPM issues `REQUEST_PE_VCONN_SWAP`, then `VCONN_Swap` is sent and on `Accept` + `PS_RDY` exchange the appropriate notify fires and `PE_FLAGS_VCONN_SOURCE` reflects the new role.
- VCS state machine end-to-end (incoming path, VCONN Source side): given the port is VCONN Source, when `VCONN_Swap` is received, then `Accept` is sent, `VconnOnTimer` is started, `PS_RDY` is received within timeout, VCONN is turned off, and the notification fires.
- VCS state machine end-to-end (incoming path, not VCONN Source, DPM supports): when `VCONN_Swap` is received and DPM returns true for `CHECK_VCONN_SWAP`, then `Accept` is sent, VCONN is turned on, `PS_RDY` is sent.
- VCS refusal (incoming path, not VCONN Source, DPM unsupported): when `VCONN_Swap` is received and DPM returns false, then `Reject` is sent and the port returns to Ready unchanged.
- VconnOnTimer expiry: when `PS_RDY` is not received within `tVconnSourceTimeout`, then the state machine transitions to `PE_SRC_Hard_Reset` or `PE_SNK_Hard_Reset` per current role.
- DPM request rejected when feature disabled: when `CONFIG_USBC_CSM_VCONN_SWAP=n`, then `REQUEST_PE_VCONN_SWAP` is a no-op and incoming `VCONN_Swap` is responded to with `Not_Supported`.
- Kconfig integration build: `west twister -p native_sim -T tests/subsys/usb_c --build-only` passes both with VCS enabled and disabled.

Prior art: the PRS PR (`subsys: usb_c: pe: add Power Role Swap state machine` on `feature/usbc-power-role-swap`) added equivalent test coverage; mirror its `testcase.yaml` and per-state test layout.

## Out of Scope

- `PE_VCS_Force_VCONN` (spec §9.2.21.9, marked Optional). May be added in a follow-up.
- Wait responses to `VCONN_Swap`. Spec allows them but we always Accept or Reject.
- The R3.2 V1.1 backwards-compat rule (spec §7.13.1) that forces Accept-instead-of-Wait for older partners — irrelevant because we never send Wait.
- EPR-mode entry edges (`PE_SRC_EPR_Mode_Discover_Cable` and `PE_SNK_EPR_Mode_Entry_Wait_For_Response`). Re-evaluated when EPR lands.
- SOP'/SOP'' cable-plug soft reset following a successful swap (spec §8 cable plug message-ID resync). Zephyr does not emit SOP'/SOP'' traffic today; tracked separately.
- `PE_VCS_Force_VCONN` path and the related `Not_Supported` reception handling in `PE_VCS_SEND_SWAP`.
- VCS-aware behavior in the TC error-recovery path beyond the standard Hard Reset transition.

## Further Notes

- The TCPC VCONN driver-level memory-corruption fix (`cf8197b7d1d`) and the explicit `set_vconn` success check (`793d29d23f3`) are already in main. The TCPC API is ready.
- This PRD assumes the Power Role Swap PR has merged. If PRS lands after VCS, integration testing should cover the PRS-VCS interaction (VCONN role survives a power role swap; PRS does not touch `PE_FLAGS_VCONN_SOURCE`).
- For future EPR work, when `PE_SRC_EPR_Mode_Discover_Cable` is added it should jump directly into `PE_VCS_SEND_SWAP` if the source is not yet VCONN Source. The current `PE_VCS_SEND_SWAP` already returns to either Ready state, so adding EPR re-entry is a one-line spec-state addition.
- The `WAIT_VCONN_SWAP` value already in `enum usbc_policy_wait_t` (`usbc.h:163`) becomes dead code under this PRD. Decision: leave it in place to avoid an API-breaking change; revisit when Wait support is added.
