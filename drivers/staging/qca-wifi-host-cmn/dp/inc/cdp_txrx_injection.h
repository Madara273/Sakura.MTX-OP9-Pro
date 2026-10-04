/* SPDX-License-Identifier: ISC */
/* Per-packet monitor TX settings shared by HDD, WMA and the raw datapath. */
#ifndef _CDP_TXRX_INJECTION_H_
#define _CDP_TXRX_INJECTION_H_

#include <qdf_nbuf.h>

#define CDP_TX_INJECTION_DESC_PREFIX 0xf000

enum cdp_tx_injection_preamble {
	CDP_INJECTION_OFDM,
	CDP_INJECTION_CCK,
	CDP_INJECTION_HT,
};

struct cdp_tx_injection_params {
	uint16_t mcs_mask;
	uint16_t tx_flags;
	uint16_t chanfreq;
	/* Owning netdev address, independent of the injected frame's transmitter. */
	uint8_t interface_mac[QDF_MAC_ADDR_SIZE];
	uint8_t nss_mask;
	uint8_t preamble;
	/* HTT bandwidth bitmap: bit 2 = 20 MHz, bit 3 = 40 MHz. */
	uint8_t bw_mask;
	/* HTT guard interval: 0 = 0.4 us, 1 = 0.8 us. */
	uint8_t guard_interval;
	/* Total attempts, including the initial transmission (1..15). */
	uint8_t retries;
	uint8_t tid;
	bool rate_valid;
	bool bandwidth_valid;
	bool gi_valid;
	bool retries_valid;
	bool no_ack;
	bool is_data;
	bool is_eapol;
};

static inline bool cdp_nbuf_is_injection(qdf_nbuf_t nbuf)
{
#ifdef FEATURE_FRAME_INJECTION_SUPPORT
	return (QDF_NBUF_CB_MGMT_TXRX_DESC_ID(nbuf) &
		CDP_TX_INJECTION_DESC_PREFIX) == CDP_TX_INJECTION_DESC_PREFIX;
#else
	return false;
#endif
}

struct wlan_objmgr_psoc;
struct wlan_objmgr_pdev;
struct wlan_objmgr_vdev;
struct reg_tpc_power_info;

#ifdef FEATURE_FRAME_INJECTION_SUPPORT
/* Track the exact effective table sent to firmware, outside exported MLME ABI. */
uint64_t wma_injection_tpc_begin(struct wlan_objmgr_vdev *vdev,
				const struct reg_tpc_power_info *power);
void wma_injection_tpc_end(struct wlan_objmgr_vdev *vdev, uint64_t ticket,
			   QDF_STATUS status);
uint64_t wma_injection_reg_begin(struct wlan_objmgr_pdev *pdev);
void wma_injection_reg_end(struct wlan_objmgr_pdev *pdev, uint64_t ticket);
void wma_injection_power_set_owner(struct wlan_objmgr_vdev *vdev, bool attached);
void wma_injection_power_reset(struct wlan_objmgr_vdev *vdev);
void wma_injection_power_override(uint8_t vdev_id);
#else
static inline uint64_t
wma_injection_tpc_begin(struct wlan_objmgr_vdev *vdev,
			const struct reg_tpc_power_info *power)
{
	return 0;
}

static inline void
wma_injection_tpc_end(struct wlan_objmgr_vdev *vdev, uint64_t ticket,
		      QDF_STATUS status)
{
}

static inline uint64_t wma_injection_reg_begin(struct wlan_objmgr_pdev *pdev)
{
	return 0;
}

static inline void
wma_injection_reg_end(struct wlan_objmgr_pdev *pdev, uint64_t ticket)
{
}

static inline void
wma_injection_power_set_owner(struct wlan_objmgr_vdev *vdev, bool attached)
{
}

static inline void wma_injection_power_reset(struct wlan_objmgr_vdev *vdev)
{
}

static inline void wma_injection_power_override(uint8_t vdev_id)
{
}
#endif

#ifdef FEATURE_FRAME_INJECTION_SUPPORT
bool wma_injection_get_tx_params(qdf_nbuf_t nbuf,
				struct cdp_tx_injection_params *params);

struct cdp_injection_tx_guard {
	unsigned long flags;
	bool held;
};

bool wma_injection_tx_begin(qdf_nbuf_t nbuf,
			    struct cdp_injection_tx_guard *guard);
void wma_injection_tx_end(struct cdp_injection_tx_guard *guard);
void wma_injection_peer_map_notify(uint8_t vdev_id);

struct cdp_soc_t;
#if defined(QCA_MONITOR_PKT_SUPPORT) || defined(FEATURE_MONITOR_MODE_SUPPORT)
QDF_STATUS dp_mon_sta_coex_bind_helper(struct cdp_soc_t *soc,
				     uint8_t helper_id, uint8_t monitor_id,
				     uint64_t generation,
				     bool (*token_valid)(void *arg,
							 uint8_t monitor_id,
							 uint64_t generation),
				     void *arg);
void dp_mon_sta_coex_unbind_helper(struct cdp_soc_t *soc, uint8_t helper_id);
#else
static inline QDF_STATUS
dp_mon_sta_coex_bind_helper(struct cdp_soc_t *soc,
			   uint8_t helper_id, uint8_t monitor_id,
			   uint64_t generation,
			   bool (*token_valid)(void *arg, uint8_t monitor_id,
					       uint64_t generation),
			   void *arg)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline void
dp_mon_sta_coex_unbind_helper(struct cdp_soc_t *soc, uint8_t helper_id)
{
}
#endif
#endif

#endif /* _CDP_TXRX_INJECTION_H_ */
