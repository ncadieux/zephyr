# PRD: USB-C Dual-Role Policy Engine States (PE_DR_*)

**Spec reference:** USB PD R3.2 V1.2 (Mar 2026), §9.2.20.7–.10 (base cap exchanges), §9.2.20.15–.16 (Source Info). §6.3.7–.8, §6.3.23, §6.4.10 cover the corresponding messages.

**Status:** Draft — pending implementation. Blocked-by: none for this scope. Blocks: extended-cap variants (need PRL Extended Messages support, tracked as a separate future PRD).

## Problem Statement

When two USB-C ports are connected and at least one is dual-role-capable, either side may legitimately ask the other for caps from either direction:

- A source can be asked for its sink caps (the partner wants to know how much we could draw if we became sink).
- A sink can be asked for its source caps (the partner wants to know how much we could supply if we became source).
- Either role may want a `Source_Info` exchange so policy decisions reflect actual port capability and present capability.

Today the Zephyr Policy Engine has:
- `PE_SNK_GET_SOURCE_CAP` — sink asks partner for source caps. Functionally correct for the sink-role case; name is sink-specific even though the same operation is meaningful from a source role too.
- `PE_GET_SINK_CAP` — called only from the source path (`usbc_pe_src_states.c:58`) to ask partner for sink caps. Source-side correct, but the doxygen comment incorrectly cross-references `PE_DR_SNK_Get_Sink_Cap` (a different spec state).
- `PE_SNK_GIVE_SINK_CAP` — sink replies to incoming `Get_Sink_Cap`. Sink-specific name; the same operation is needed from a source role too.
- `PE_SRC_SEND_CAPABILITIES` — source sends `Source_Capabilities` either unsolicited at startup / post-PRS, or in response to inbound `Get_Source_Cap`. Stays as the source's inbound-`Get_Source_Cap` handler.

What is missing relative to spec §9.2.20:
- No state for a **source** to reply when it receives `Get_Sink_Cap` (`PE_DR_SRC_Give_Sink_Cap`, §9.2.20.8). Source today has no handler — silently fails or generates Soft_Reset.
- No state for a **sink** to reply when it receives `Get_Source_Cap` (§9.2.20.10). Sink today has no handler.
- No path for a **source** to send `Get_Source_Cap` to ask partner for its source caps (would be entered from `PE_SRC_Ready`). DRP applications cannot probe partner source capability.
- No `Get_Source_Info` / `Source_Info` handling at all (spec §6.3.23, §6.4.10). The DPM has no way to query the partner's port type, maximum capabilities, and present capabilities.
- The control-message-type constant `PD_CTRL_GET_SOURCE_INFO` (value `10111b` = 23) and the data-message-type constant `PD_DATA_SOURCE_INFO` (value `01011b` = 11) are not defined in `usbc_pd.h`.

From a developer's perspective: I am building a Zephyr DRP product and I cannot interrogate or be interrogated about caps in the non-default direction, which limits the breadth of policy decisions my DPM can make.

## Solution

Add three new `PE_DR_*` Policy Engine states and rename three existing ones to a consolidated dual-role-aware shape. The four `*_Cap_Ext` variants (§9.2.20.11–.14) and any EPR variants are deferred until PRL gains Extended Messages support.

The implementations of mechanically-identical operations are collapsed into single role-agnostic states even where the spec calls them out under role-specific names — this matches the user directive "we do not need to follow the specification exactly if it does not make sense to do so" and the existing Zephyr PE pattern of single states reused across roles (e.g., `PE_SOFT_RESET`).

**Three renames** (private symbol changes, no public API break):

| Old name | New name | Behavior change |
|----------|----------|-----------------|
| `PE_GET_SINK_CAP` | `PE_DR_GET_SINK_CAP` | Same wire behavior. Now also enterable from `PE_SNK_READY` via the existing `REQUEST_GET_SNK_CAPS` DPM request — covers spec §9.2.20.9 by collapsing. |
| `PE_SNK_GET_SOURCE_CAP` | `PE_DR_GET_SOURCE_CAP` | Same wire behavior. Now also enterable from `PE_SRC_READY` via the existing `REQUEST_PE_GET_SRC_CAPS` DPM request — covers spec §9.2.20.7 by collapsing. |
| `PE_SNK_GIVE_SINK_CAP` | `PE_DR_GIVE_SINK_CAP` | Same wire behavior. Now also enterable from `PE_SRC_READY` on inbound `Get_Sink_Cap` — covers spec §9.2.20.8 by collapsing. |

**Three new states:**

| Enum value | Spec § | Direction | Role(s) | Entered from |
|------------|--------|-----------|---------|--------------|
| `PE_DR_SNK_GIVE_SOURCE_CAP` | §9.2.20.10 | inbound reply | sink only | `PE_SNK_READY` on `Get_Source_Cap` receive. Source role keeps using existing `PE_SRC_SEND_CAPABILITIES` for inbound `Get_Source_Cap` — that path is unchanged. |
| `PE_DR_GET_SOURCE_INFO` | §9.2.20.15 generalised | outgoing | either | `PE_SRC_READY` or `PE_SNK_READY` via new `REQUEST_PE_GET_SRC_INFO` DPM request |
| `PE_DR_GIVE_SOURCE_INFO` | §9.2.20.16 generalised | inbound reply | either | `PE_SRC_READY` or `PE_SNK_READY` on `Get_Source_Info` receive |

A new DPM request `REQUEST_PE_GET_SRC_INFO` triggers the outgoing `Get_Source_Info`. A new DPM callback `policy_cb_get_source_info` supplies the two Source Info Data Objects (SIDO1 / SIDO2 per spec Tables 6.29 and 6.30) when the partner sends `Get_Source_Info` to us. A new DPM callback `policy_cb_set_partner_source_info` delivers the partner's response back to the application.

`PE_DR_SNK_GIVE_SOURCE_CAP` stays sink-name-prefixed because there is no symmetric source-side state to merge with — the source's inbound-`Get_Source_Cap` path already exists as `PE_SRC_SEND_CAPABILITIES` and is intentionally left alone.

## User Stories

1. As a DRP application developer, I want my port to reply to an incoming `Get_Source_Cap` while operating as a sink, so that my partner can include me in its policy decisions about which side should be the source.
2. As a DRP application developer, I want my port to reply to an incoming `Get_Sink_Cap` while operating as a source, so that my partner knows how much power I could accept if a power role swap happened.
3. As a DRP application developer, I want to ask my partner for its source capabilities while my port is currently a source, so that I can make an informed decision about initiating a Power Role Swap. The DPM request value is the existing `REQUEST_PE_GET_SRC_CAPS`.
4. As a DRP application developer, I want to ask my partner for its sink capabilities while my port is currently a sink, so that I can decide whether becoming a source would benefit the partner. The DPM request value is the existing `REQUEST_GET_SNK_CAPS`.
5. As a USB-C policy developer, I want to send a `Get_Source_Info` to my partner so I can read its port type, maximum capabilities, and present capabilities in a single message exchange.
6. As a USB-C policy developer, I want my port to reply to an incoming `Get_Source_Info` with my own SIDO1 and SIDO2 contents, so that my partner can run the same logic against my port.
7. As an application developer, I want to register a single callback (`policy_cb_get_source_info`) that the PE invokes when a `Get_Source_Info` arrives, so that I do not have to assemble a Source_Info message from scratch.
8. As an application developer, I want the existing `policy_cb_set_partner_snk_cap` callback (renamed from `policy_cb_set_port_partner_snk_cap` for consistency) to fire for the renamed `PE_DR_GET_SINK_CAP` flow regardless of role, so that the partner's sink caps land in the same place whether our port asks as source or sink.
9. As an application developer, I want a new `policy_cb_set_partner_src_cap` (mirror of the sink-cap one) to receive the partner's source caps when our port asks for them via `PE_DR_GET_SOURCE_CAP`, so that the data flow is symmetric.
10. As an application developer, I want a new `policy_cb_set_partner_source_info` to receive the partner's SIDO1/SIDO2 after a `Get_Source_Info` round-trip, so that the data is not lost.
11. As a USB-C developer, I want `REQUEST_PE_GET_SRC_INFO` exposed through the existing `usbc_request()` API, so that DPM-initiated Source_Info round-trips use the same trigger pattern as other DPM requests.
12. As a USB-C developer, I want `PD_CTRL_GET_SOURCE_INFO = 23` and `PD_DATA_SOURCE_INFO = 11` defined in the public `usbc_pd.h`, so that other layers (TCPC drivers, applications, tests) can name them.
13. As a maintainer, I want the new DR state code to live in a new translation unit (`usbc_pe_dr_states.c`) parallel to `usbc_pe_prs_states.c` and the future `usbc_pe_vcs_states.c`, so that the PE source files stay focused.
14. As a maintainer, I want the consolidated `PE_DR_GET_*` and `PE_DR_GIVE_SINK_CAP` states to remember which `Ready` state they were entered from, so that timeouts and successful completion return to the correct power-role-aware `Ready` rather than always returning to one side.
15. As a stack maintainer, I want `PE_SRC_SEND_CAPABILITIES` left alone — it continues to handle both the unsolicited startup/post-PRS send and the inbound-`Get_Source_Cap` reply for the source role.
16. As a stack maintainer, I want the `SenderResponseTimer` from `usbc_pe_common.c` reused for the outgoing DR states, so that we do not introduce a parallel timer infrastructure.
17. As a sample developer, I want the `samples/subsys/usb_c/drp` sample to register `policy_cb_get_source_info` returning sensible defaults so that the sample exercises the full DR matrix.
18. As a tester, I want each new state covered by a positive (caps received) and a timeout-negative test, so that regressions are detected before they ship.
19. As a future EPR developer, I want the four `*_Cap_Ext` variants explicitly tracked as out of scope and gated behind the future PRL extended-message work, so that scope creep does not slow this PRD.
20. As a DPM developer, I want `Get_Source_Info` exchanges to be optional — if I do not register the callback, the PE responds `Not_Supported` rather than crashing, so that I can adopt the feature incrementally.
21. As a code reviewer, I want each new state's docstring to cite its spec section number, so that the state machine remains audit-traceable to the spec. Renamed states cite both their basic-flow section (e.g., §9.2.19 for sink-role use) and the corresponding DR-state section (e.g., §9.2.20.8 for source-role use).
22. As a stack maintainer, I want the three renames documented as private symbol changes in the migration notes for the next Zephyr release, so that downstream forks know to update.
23. As a developer hooking inbound `Get_Sink_Cap` while sourcing, I want `PE_DR_GIVE_SINK_CAP` to call the existing `policy_get_snk_cap` callback regardless of role, so that I do not need to register a new callback for the same data.
24. As a developer hooking inbound `Get_Source_Cap` while sinking, I want `PE_DR_SNK_GIVE_SOURCE_CAP` to call the existing `policy_cb_get_src_caps` callback to fetch our source PDOs, so that I do not need to register a new callback for the same data.

## Implementation Decisions

### State machine

Six total states involved — three renames and three new entries — added/updated in `enum usbc_pe_state` in `usbc_pe_common_internal.h`:

| Enum value | Spec § | Role(s) | Trigger | Sent | Expected response | DPM hook |
|------------|--------|---------|---------|------|-------------------|----------|
| `PE_DR_GET_SINK_CAP` (renamed) | §9.2.18 + §9.2.20.9 | source or sink | `REQUEST_GET_SNK_CAPS` | `Get_Sink_Cap` (ctl 8) | `Sink_Capabilities` | existing (renamed) `policy_cb_set_partner_snk_cap` |
| `PE_DR_GET_SOURCE_CAP` (renamed) | §9.2.19 + §9.2.20.7 | source or sink | `REQUEST_PE_GET_SRC_CAPS` | `Get_Source_Cap` (ctl 7) | `Source_Capabilities` | new `policy_cb_set_partner_src_cap` |
| `PE_DR_GIVE_SINK_CAP` (renamed) | §9.2.19 + §9.2.20.8 | source or sink | inbound `Get_Sink_Cap` | `Sink_Capabilities` (data 4) | (TX-only) | reads `policy_get_snk_cap` |
| `PE_DR_SNK_GIVE_SOURCE_CAP` (new) | §9.2.20.10 | sink only | inbound `Get_Source_Cap` | `Source_Capabilities` (data 1) | (TX-only) | reads `policy_cb_get_src_caps` |
| `PE_DR_GET_SOURCE_INFO` (new) | §9.2.20.15 generalised | source or sink | `REQUEST_PE_GET_SRC_INFO` | `Get_Source_Info` (ctl 23, new) | `Source_Info` (data 11, new) | new `policy_cb_set_partner_source_info` |
| `PE_DR_GIVE_SOURCE_INFO` (new) | §9.2.20.16 generalised | source or sink | inbound `Get_Source_Info` | `Source_Info` (data 11, new) | (TX-only) | reads new `policy_cb_get_source_info`; if unregistered → `Not_Supported` |

### File layout

- New: `subsys/usb/usb_c/usbc_pe_dr_states.c` and `usbc_pe_dr_states_internal.h` — contain the implementations of the three new states and the moved/wrapped bodies of the three renamed states so that the role-agnostic logic is in one place.
- Modified: `subsys/usb/usb_c/usbc_pe_common.c` — `PE_GET_SINK_CAP` enum reference and its `SMF_CREATE_STATE` entry rename to `PE_DR_GET_SINK_CAP`. If the implementation body is moved to the new translation unit, the SMF table entry references the moved functions.
- Modified: `subsys/usb/usb_c/usbc_pe_common_internal.h` — rename enum values; add new enum values.
- Modified: `subsys/usb/usb_c/usbc_pe_snk_states.c` — rename `PE_SNK_GET_SOURCE_CAP` → `PE_DR_GET_SOURCE_CAP` and `PE_SNK_GIVE_SINK_CAP` → `PE_DR_GIVE_SINK_CAP` everywhere (including the state's entry/run function names if we keep them in this file, or move them into `usbc_pe_dr_states.c`). `pe_snk_ready_run` adds two cases: `PD_CTRL_GET_SOURCE_CAP` → `PE_DR_SNK_GIVE_SOURCE_CAP`, `PD_CTRL_GET_SOURCE_INFO` → `PE_DR_GIVE_SOURCE_INFO`.
- Modified: `subsys/usb/usb_c/usbc_pe_src_states.c` — `source_dpm_requests()` uses the new enum names. `pe_src_ready_run` adds two cases: `PD_CTRL_GET_SINK_CAP` → `PE_DR_GIVE_SINK_CAP`, `PD_CTRL_GET_SOURCE_INFO` → `PE_DR_GIVE_SOURCE_INFO`. The existing `PD_CTRL_GET_SOURCE_CAP` case continues to point at `PE_SRC_SEND_CAPABILITIES` (unchanged).
- Modified: `include/zephyr/usb_c/usbc.h`, `subsys/usb/usb_c/usbc_stack.c`, `subsys/usb/usb_c/usbc_stack.h`, `subsys/usb/usb_c/usbc_pe_common.c` — rename the existing callback symbol family `policy_cb_set_port_partner_snk_cap` → `policy_cb_set_partner_snk_cap` (typedef, setter declaration, setter definition, struct field, internal helper). Five touchpoints. This aligns with the `partner_*` naming used by the new `policy_cb_set_partner_src_cap` and `policy_cb_set_partner_source_info` callbacks.

### PD message-type constants

Add to `include/zephyr/drivers/usb_c/usbc_pd.h`:

```c
PD_CTRL_GET_SOURCE_INFO = 23,   /* 10111b — spec §6.3.23 */

PD_DATA_SOURCE_INFO     = 11,   /* 01011b — spec §6.4.10 */
```

`PD_DATA_REVISION = 12` is **not** added in this PRD (no state uses it; defer to whichever PRD introduces `Get_Revision` handling).

### Source Info Data Objects

In `include/zephyr/drivers/usb_c/usbc_pd.h`, add packed-struct typedefs for SIDO1 (spec Table 6.29) and SIDO2 (spec Table 6.30), plus thin pack/parse helpers. Encoding work is one helper pair.

### DPM API additions

In `include/zephyr/usb_c/usbc.h`:

- Extend `enum usbc_policy_request_t` with `REQUEST_PE_GET_SRC_INFO`.
- Add callback typedef and registration for inbound Source_Info reply:
  - `typedef int (*policy_cb_get_source_info_t)(const struct device *dev, uint32_t *sido1, uint32_t *sido2);`
  - `void usbc_set_policy_cb_get_source_info(const struct device *dev, const policy_cb_get_source_info_t cb);`
- Add callback typedef and registration for the partner-source-cap direction (mirror of the existing sink-cap one):
  - `typedef void (*policy_cb_set_partner_src_cap_t)(const struct device *dev, const uint32_t *pdos, const int num_pdos);`
  - `void usbc_set_policy_cb_set_partner_src_cap(const struct device *dev, const policy_cb_set_partner_src_cap_t cb);`
- Rename the existing sink-cap callback family for consistency (one public-API symbol change; see Migration Note):
  - `policy_cb_set_port_partner_snk_cap_t` → `policy_cb_set_partner_snk_cap_t`
  - `usbc_set_policy_cb_set_port_partner_snk_cap` → `usbc_set_policy_cb_set_partner_snk_cap`
- Add callback typedef and registration for partner source-info delivery:
  - `typedef void (*policy_cb_set_partner_source_info_t)(const struct device *dev, uint32_t sido1, uint32_t sido2);`
  - `void usbc_set_policy_cb_set_partner_source_info(const struct device *dev, const policy_cb_set_partner_source_info_t cb);`
- If `policy_cb_get_source_info` is not registered, the PE responds with `Not_Supported` (control message) when it receives `Get_Source_Info`.

### Entered-from tracking for role-agnostic states

`PE_DR_GET_SINK_CAP`, `PE_DR_GET_SOURCE_CAP`, `PE_DR_GIVE_SINK_CAP`, `PE_DR_GET_SOURCE_INFO`, and `PE_DR_GIVE_SOURCE_INFO` all need to know which `Ready` they came from so they can return to the right one on completion or timeout. Two acceptable implementation patterns; either is fine, the implementing agent picks:

1. Use `pe_get_last_state()` (already exposed in `usbc_pe_common_internal.h`) — returns the previous PE state.
2. Snapshot `pe->power_role` at entry; on exit, transition to `PE_SNK_READY` if sink, `PE_SRC_READY` if source.

Pattern 2 is more robust against intermediate state transitions and matches the existing `pe_set_ready_state()` helper.

### Entry edges

| Enum value | Entered from | On |
|------------|--------------|----|
| `PE_DR_GET_SINK_CAP` | `PE_SRC_READY` | DPM `REQUEST_GET_SNK_CAPS` |
| `PE_DR_GET_SINK_CAP` | `PE_SNK_READY` | DPM `REQUEST_GET_SNK_CAPS` |
| `PE_DR_GET_SOURCE_CAP` | `PE_SRC_READY` | DPM `REQUEST_PE_GET_SRC_CAPS` |
| `PE_DR_GET_SOURCE_CAP` | `PE_SNK_READY` | DPM `REQUEST_PE_GET_SRC_CAPS` |
| `PE_DR_GIVE_SINK_CAP` | `PE_SRC_READY` | inbound `PD_CTRL_GET_SINK_CAP` |
| `PE_DR_GIVE_SINK_CAP` | `PE_SNK_READY` | inbound `PD_CTRL_GET_SINK_CAP` |
| `PE_DR_SNK_GIVE_SOURCE_CAP` | `PE_SNK_READY` | inbound `PD_CTRL_GET_SOURCE_CAP` |
| `PE_DR_GET_SOURCE_INFO` | `PE_SRC_READY` or `PE_SNK_READY` | DPM `REQUEST_PE_GET_SRC_INFO` |
| `PE_DR_GIVE_SOURCE_INFO` | `PE_SRC_READY` or `PE_SNK_READY` | inbound `PD_CTRL_GET_SOURCE_INFO` |

Outgoing states use the standard `SenderResponseTimer` pattern (existing `pd_t_sender_response`). Inbound-reply states use the existing `PE_FLAGS_TX_COMPLETE` / `PE_FLAGS_MSG_DISCARDED` flow.

### Kconfig

- Add `CONFIG_USBC_DR_STATES` (bool, default y when `USBC_CSM_SUPPORTS_SINK` or `USBC_CSM_SUPPORTS_SOURCE` is set). Always-on for now; the flag exists so a future code-size-constrained build can opt out. When disabled, inbound `Get_Sink_Cap` from a source, `Get_Source_Cap` from a sink, and `Get_Source_Info` from either role all respond `Not_Supported`. (Sink response to inbound `Get_Sink_Cap` and source response to inbound `Get_Source_Cap` remain available since those are basic-flow paths.)

### Migration note

Add a short entry to the Zephyr migration guide for the next release:
> `subsys/usb/usb_c`: Three Policy Engine state enum values have been renamed to reflect dual-role-aware behavior. `PE_GET_SINK_CAP` → `PE_DR_GET_SINK_CAP`, `PE_SNK_GET_SOURCE_CAP` → `PE_DR_GET_SOURCE_CAP`, `PE_SNK_GIVE_SINK_CAP` → `PE_DR_GIVE_SINK_CAP`. These are private symbol changes.
>
> One public API symbol has been renamed for consistency with new partner-cap callbacks added in this release: `policy_cb_set_port_partner_snk_cap_t` → `policy_cb_set_partner_snk_cap_t` and the corresponding setter `usbc_set_policy_cb_set_port_partner_snk_cap()` → `usbc_set_policy_cb_set_partner_snk_cap()`. Downstream callers must update the callback typedef name and the setter call.
>
> Existing DPM request enum values (`REQUEST_GET_SNK_CAPS`, `REQUEST_PE_GET_SRC_CAPS`) are unchanged.

## Testing Decisions

Tests exercise the new states through DPM requests in and message bytes out, on `native_sim` with the existing test harness in `tests/subsys/usb_c/`. They do not poke private flags or call internal SMF setters.

Modules to test:

- `PE_DR_GET_SOURCE_CAP` outgoing from source role: given `PE_SRC_READY`, when DPM requests `REQUEST_PE_GET_SRC_CAPS`, then `Get_Source_Cap` is sent and on `Source_Capabilities` the new `policy_cb_set_partner_src_cap` callback fires; state returns to `PE_SRC_READY`.
- `PE_DR_GET_SOURCE_CAP` outgoing from sink role: same setup from `PE_SNK_READY`; returns to `PE_SNK_READY` (regression check of existing behavior under the new name).
- `PE_DR_GET_SINK_CAP` outgoing from source role: given `PE_SRC_READY`, when DPM requests `REQUEST_GET_SNK_CAPS`, then `Get_Sink_Cap` is sent and the existing `policy_cb_set_partner_snk_cap` fires (regression check).
- `PE_DR_GET_SINK_CAP` outgoing from sink role: same setup from `PE_SNK_READY`; returns to sink ready.
- `PE_DR_GIVE_SINK_CAP` inbound from source role: when `PE_SRC_READY` receives `Get_Sink_Cap`, then `policy_get_snk_cap` is invoked and `Sink_Capabilities` is sent; returns to `PE_SRC_READY`.
- `PE_DR_GIVE_SINK_CAP` inbound from sink role: same setup from `PE_SNK_READY` (regression check of existing behavior under the new name).
- `PE_DR_SNK_GIVE_SOURCE_CAP` inbound: when `PE_SNK_READY` receives `Get_Source_Cap`, then `policy_cb_get_src_caps` is invoked and `Source_Capabilities` is sent.
- Source-side inbound `Get_Source_Cap` regression: when `PE_SRC_READY` receives `Get_Source_Cap`, it still transitions to `PE_SRC_SEND_CAPABILITIES` (unchanged).
- `PE_DR_GET_SOURCE_INFO` outgoing: when DPM requests it from either role, then `Get_Source_Info` is sent and on `Source_Info` response the partner-source-info callback fires with both SIDOs.
- `PE_DR_GIVE_SOURCE_INFO` inbound (callback registered): when either role receives `Get_Source_Info`, then `policy_cb_get_source_info` is invoked and a `Source_Info` data message with the returned SIDOs is sent.
- `PE_DR_GIVE_SOURCE_INFO` inbound (callback not registered): same input, then `Not_Supported` is sent.
- SenderResponseTimer timeout on every outgoing state: returns to the originating Ready state and emits `SENDER_RESPONSE_TIMEOUT` notify.
- Build matrix: `west twister -p native_sim -T tests/subsys/usb_c` passes for sink-only, source-only, and DRP configurations.

Prior art: the existing `pe_get_sink_cap_run` tests (if present) and the PRS PR test layout. Keep test files focused per state; one `testcase.yaml` entry per state covered.

## Out of Scope

- The four extended-cap states (`PE_DR_SRC_Get_Source_Cap_Ext`, `PE_DR_SNK_Give_Source_Cap_Ext`, `PE_DR_SNK_Get_Sink_Cap_Ext`, `PE_DR_SRC_Give_Sink_Cap_Ext`) per spec §9.2.20.11–.14. Require PRL extended-message (chunking) support that does not exist in Zephyr today. Tracked separately.
- EPR variants (`EPR_Get_Source_Cap`, `EPR_Source_Capabilities`, etc.). Zephyr has no EPR plumbing.
- `Get_Revision` handling and the related `PD_DATA_REVISION` constant.
- DPM-level policy logic that consumes the new partner-cap or partner-source-info data; this PRD only delivers the data into the application via the callback.
- Any change to `PE_SRC_SEND_CAPABILITIES` — it continues to handle both the unsolicited startup/post-PRS send and the inbound-`Get_Source_Cap` reply path.

## Further Notes

- Once the PRL Extended Messages PRD lands, the four `*_Cap_Ext` states can be added under the same `usbc_pe_dr_states.c` translation unit, parameterized for extended messages instead of data messages.
- The Source_Info SIDO1/SIDO2 bit layouts per spec Tables 6.29/6.30 are stable across R3.0+. Defining them in `usbc_pd.h` once is fine.
- This PRD does not introduce any new timers — `SenderResponseTimer` covers all outgoing states.
- Coordination with the VCONN Swap PRD: the two PRDs are independent. The VCS PRD adds new files (`usbc_pe_vcs_states.c`) and this PRD adds new files (`usbc_pe_dr_states.c`); they do not edit the same lines.
- Asymmetry note: `PE_DR_SNK_GIVE_SOURCE_CAP` is kept sink-role-prefixed (not collapsed to a role-agnostic `PE_DR_GIVE_SOURCE_CAP`) because the source side already has `PE_SRC_SEND_CAPABILITIES` as its inbound-`Get_Source_Cap` handler. Folding the source path into a new role-agnostic state would require either retiring or specializing `PE_SRC_SEND_CAPABILITIES`, which would expand the PRD's blast radius. The naming honestly reflects that asymmetry.
