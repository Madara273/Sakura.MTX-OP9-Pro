/* SPDX-License-Identifier: ISC */
#ifndef _WLAN_MLME_STA_MON_H_
#define _WLAN_MLME_STA_MON_H_

#include <wlan_objmgr_vdev_obj.h>

/* Private host state; this is not an nl80211 or firmware interface.
 * Entry points are for process/softirq context, never hard IRQ context.
 */
enum wlan_mlme_sta_mon_state {
	WLAN_STA_MON_OFF,
	WLAN_STA_MON_QUIESCED,
	WLAN_STA_MON_ARMING,
	WLAN_STA_MON_READY,
	WLAN_STA_MON_QUIESCING,
	WLAN_STA_MON_HANDOFF,
};

enum wlan_mlme_sta_mon_handoff_phase {
	WLAN_STA_MON_HANDOFF_NONE,
	WLAN_STA_MON_HANDOFF_RESERVED,
	WLAN_STA_MON_HANDOFF_DRAIN,
	WLAN_STA_MON_HANDOFF_DISCONNECT,
	WLAN_STA_MON_HANDOFF_DISCONNECTED,
	WLAN_STA_MON_HANDOFF_RESTART,
};

struct wlan_mlme_sta_mon_snapshot {
	uint64_t generation;
	/* Historical name: the real STA or AP which owns the shared channel. */
	uint8_t sta_vdev_id;
	uint8_t mon_vdev_id;
	uint8_t pdev_id;
	uint8_t mac_id;
	bool exclusive;
	bool ap_owner;
	/* Triple mode retains the AP as channel/power owner. */
	bool sta_ap_coexist;
	uint8_t companion_sta_vdev_id;
	struct wlan_channel channel;
};

/* Called under the control lock: only pause delivery and queue work. */
typedef void (*wlan_mlme_sta_mon_stop_cb)(void *arg);

void wlan_mlme_sta_mon_register(struct wlan_objmgr_psoc *psoc,
			       wlan_mlme_sta_mon_stop_cb stop, void *arg);
bool wlan_mlme_sta_mon_attach(struct wlan_objmgr_psoc *psoc);
void wlan_mlme_sta_mon_detach(struct wlan_objmgr_psoc *psoc);
uint64_t wlan_mlme_sta_mon_epoch(struct wlan_objmgr_psoc *psoc);
bool wlan_mlme_sta_mon_arm(struct wlan_objmgr_psoc *psoc,
			  struct wlan_mlme_sta_mon_snapshot *snapshot);
/* Record native ownership before CREATE can start an asynchronous cleanup. */
void wlan_mlme_sta_mon_vdev_created(struct wlan_objmgr_psoc *psoc,
				   uint8_t mon_vdev_id);
bool wlan_mlme_sta_mon_bind(struct wlan_objmgr_psoc *psoc, uint8_t mon_vdev_id,
			   uint64_t generation);
bool wlan_mlme_sta_mon_start_valid(struct wlan_objmgr_psoc *psoc,
				 uint64_t generation);
bool wlan_mlme_sta_mon_set_ready(struct wlan_objmgr_psoc *psoc,
				uint64_t generation);
bool wlan_mlme_sta_mon_quiesced(struct wlan_objmgr_psoc *psoc);
/* WLAN_INVALID_VDEV_ID selects the stored owner after partial CREATE failure. */
void wlan_mlme_sta_mon_fw_timeout(struct wlan_objmgr_psoc *psoc,
				 uint8_t mon_vdev_id);
bool wlan_mlme_sta_mon_fw_uncertain(struct wlan_objmgr_psoc *psoc);
bool wlan_mlme_sta_mon_can_connect(struct wlan_objmgr_psoc *psoc);
bool wlan_mlme_sta_mon_needs_stop(struct wlan_objmgr_psoc *psoc);
bool wlan_mlme_sta_mon_is_owner(struct wlan_objmgr_psoc *psoc,
			       uint8_t mon_vdev_id);
bool wlan_mlme_sta_mon_get_token(struct wlan_objmgr_psoc *psoc,
				uint8_t mon_vdev_id, uint64_t *generation);
bool wlan_mlme_sta_mon_token_valid(struct wlan_objmgr_psoc *psoc,
				  uint8_t mon_vdev_id, uint64_t generation);
/* True holds the control lock across the final descriptor publication only. */
bool wlan_mlme_sta_mon_tx_begin(struct wlan_objmgr_psoc *psoc,
			       uint8_t mon_vdev_id, uint64_t generation,
			       unsigned long *flags);
void wlan_mlme_sta_mon_tx_end(unsigned long flags);
bool wlan_mlme_sta_mon_get_snapshot(struct wlan_objmgr_psoc *psoc,
				   uint8_t mon_vdev_id, uint64_t generation,
				   struct wlan_mlme_sta_mon_snapshot *out);
/* Discover the READY owner for RX events without a monitor vdev ID. */
bool wlan_mlme_sta_mon_get_active_snapshot(
				struct wlan_objmgr_psoc *psoc,
				struct wlan_mlme_sta_mon_snapshot *out);

/* AP ownership holds the channel until native/DP teardown has completed.
 * WLAN_INVALID_VDEV_ID selects any AP owner, including quiescing resources.
 */
bool wlan_mlme_sta_mon_ap_is_owner(struct wlan_objmgr_psoc *psoc,
				 uint8_t vdev_id);
/* Includes quiescing resources, so STA stop can drain its optional helper. */
bool wlan_mlme_sta_mon_sta_is_companion(struct wlan_objmgr_psoc *psoc,
				       uint8_t vdev_id);

/* Submission admission does not change ordinary STA shared-mode invalidation.
 * AP shared mode rejects competing radio work until its monitor is quiesced.
 * An asynchronous request must retain its existing update reference or publish
 * an observable active state before ending this submission admission.
 */
bool wlan_mlme_sta_mon_admission_begin(struct wlan_objmgr_psoc *psoc);
void wlan_mlme_sta_mon_admission_end(struct wlan_objmgr_psoc *psoc);
/* The AP and its connected STA companion may submit on-channel management. */
bool wlan_mlme_sta_mon_mgmt_admission_begin(struct wlan_objmgr_psoc *psoc,
					  uint8_t vdev_id, uint32_t frequency,
					  bool offchannel);
/* Mandatory AP CSA only: caller already holds an update reference. */
bool wlan_mlme_sta_mon_ap_csa_admission_begin(struct wlan_objmgr_psoc *psoc,
					    uint8_t vdev_id);
/* Mandatory upstream CSA may continue after optional triple mode is revoked. */
bool wlan_mlme_sta_mon_sta_csa_admission_begin(struct wlan_objmgr_psoc *psoc,
					     uint8_t vdev_id);
bool wlan_mlme_sta_mon_mgmt_tx_begin(struct wlan_objmgr_psoc *psoc,
				    uint8_t vdev_id, bool teardown_frame);
/* Reserve without invalidating the shared generation, so a failed preflight
 * can leave the existing connection and monitor untouched. No caller may wait
 * for admission/update counts while holding the control lock.
 */
bool wlan_mlme_sta_mon_exclusive_reserve(struct wlan_objmgr_psoc *psoc,
					 uint8_t monitor_id, uint64_t generation);
void wlan_mlme_sta_mon_handoff_cancel(struct wlan_objmgr_psoc *psoc,
				      uint64_t generation);
bool wlan_mlme_sta_mon_handoff_quiesce(struct wlan_objmgr_psoc *psoc,
				       uint64_t *generation);
bool wlan_mlme_sta_mon_handoff_valid(struct wlan_objmgr_psoc *psoc,
				     uint64_t generation);
bool wlan_mlme_sta_mon_handoff_disconnect(struct wlan_objmgr_psoc *psoc,
					  uint64_t generation);
bool wlan_mlme_sta_mon_handoff_disconnected(struct wlan_objmgr_psoc *psoc,
					    uint64_t generation);
bool wlan_mlme_sta_mon_handoff_start(struct wlan_objmgr_psoc *psoc,
				     struct wlan_mlme_sta_mon_snapshot *next);
bool wlan_mlme_sta_mon_handoff_restarting(struct wlan_objmgr_psoc *psoc,
					  uint8_t monitor_id);
bool wlan_mlme_sta_mon_handoff_ready(struct wlan_objmgr_psoc *psoc,
				     struct wlan_mlme_sta_mon_snapshot *next);
bool wlan_mlme_sta_mon_handoff_complete(struct wlan_objmgr_psoc *psoc,
					uint64_t generation);
/* Typed notifications scope the expected native disconnect/restart only.
 * Global regulatory, SSR, timeout and explicit-owner invalidation never use
 * these exceptions and continue to cancel a handoff.
 */
void wlan_mlme_sta_mon_disconnect_ind(struct wlan_objmgr_psoc *psoc,
				      uint8_t vdev_id, bool requested_handoff);
void wlan_mlme_sta_mon_power_changed(struct wlan_objmgr_psoc *psoc,
				     uint8_t vdev_id);
void wlan_mlme_sta_mon_channel_changed(struct wlan_objmgr_psoc *psoc,
				       uint8_t vdev_id,
				       const struct wlan_channel *channel);
/* Only the successful native monitor START response may use this setter. */
void wlan_mlme_sta_mon_set_start_mac_id(struct wlan_objmgr_pdev *pdev,
				       uint8_t vdev_id, uint8_t mac_id);

/* WLAN_INVALID_VDEV_ID invalidates for any radio/owner transition. */
void wlan_mlme_sta_mon_invalidate(struct wlan_objmgr_psoc *psoc,
				  uint8_t sta_vdev_id);
void wlan_mlme_sta_mon_invalidate_owner(struct wlan_objmgr_psoc *psoc,
					uint8_t mon_vdev_id);
/* Bracket cached MLO link updates so admission cannot sample a partial update. */
void wlan_mlme_sta_mon_update_begin(struct wlan_objmgr_psoc *psoc);
void wlan_mlme_sta_mon_update_end(struct wlan_objmgr_psoc *psoc);

#endif
