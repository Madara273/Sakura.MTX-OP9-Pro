/*
 * Copyright (c) 2016-2020 The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * @file cdp_txrx_mon.h
 * @brief Define the monitor mode API functions
 * called by the host control SW and the OS interface module
 */

#ifndef _CDP_TXRX_MON_H_
#define _CDP_TXRX_MON_H_
#include "cdp_txrx_handle.h"

/*
 * Private, same-module STA/monitor controls. Keep these outside the public
 * CDP operation tables and configuration union to preserve their ABI.
 */
#if defined(FEATURE_FRAME_INJECTION_SUPPORT) && (defined(QCA_MONITOR_PKT_SUPPORT) || defined(FEATURE_MONITOR_MODE_SUPPORT))
bool dp_mon_sta_coex_supported(struct cdp_soc_t *soc);
void dp_mon_set_sta_coex(struct cdp_soc_t *soc, bool enabled);
void dp_mon_sta_coex_pause(struct cdp_soc_t *soc);
void dp_mon_sta_coex_unpause(struct cdp_soc_t *soc);
QDF_STATUS dp_mon_sta_coex_rebind(struct cdp_soc_t *soc,
				 uint8_t monitor_id, uint8_t mac_id,
				 uint32_t frequency);
/* The validated WMI management nbuf is borrowed and remains unchanged. */
void dp_mon_sta_coex_mgmt_rx(struct cdp_soc_t *soc,
			   uint8_t pdev_id, uint8_t mac_id, uint8_t monitor_id,
			   uint64_t generation, qdf_nbuf_t nbuf,
			   uint32_t frequency, int32_t rssi,
			   bool (*token_valid)(void *arg, uint8_t monitor_id,
					       uint64_t generation), void *arg);
#else
static inline bool dp_mon_sta_coex_supported(struct cdp_soc_t *soc)
{
	return false;
}

static inline void dp_mon_set_sta_coex(struct cdp_soc_t *soc, bool enabled)
{
}

static inline void dp_mon_sta_coex_pause(struct cdp_soc_t *soc)
{
}
static inline void dp_mon_sta_coex_unpause(struct cdp_soc_t *soc)
{
}
static inline QDF_STATUS dp_mon_sta_coex_rebind(struct cdp_soc_t *soc,
				 uint8_t monitor_id, uint8_t mac_id,
				 uint32_t frequency)
{
	return QDF_STATUS_E_NOSUPPORT;
}
#endif

static inline QDF_STATUS cdp_reset_monitor_mode(ol_txrx_soc_handle soc,
						uint8_t pdev_id,
						u_int8_t smart_monitor)
{
	if (!soc || !soc->ops) {
		QDF_TRACE(QDF_MODULE_ID_CDP, QDF_TRACE_LEVEL_DEBUG,
				"%s: Invalid Instance", __func__);
		QDF_BUG(0);
		return 0;
	}

	if (!soc->ops->mon_ops ||
	    !soc->ops->mon_ops->txrx_reset_monitor_mode)
		return 0;

	return soc->ops->mon_ops->txrx_reset_monitor_mode(soc, pdev_id,
							  smart_monitor);
}

static inline QDF_STATUS
cdp_refresh_monitor_mode(ol_txrx_soc_handle soc, uint8_t pdev_id,
			 uint8_t vdev_id)
{
	if (!soc || !soc->ops || !soc->ops->mon_ops ||
	    !soc->ops->mon_ops->txrx_refresh_monitor_mode)
		return QDF_STATUS_E_INVAL;

	return soc->ops->mon_ops->txrx_refresh_monitor_mode(soc, pdev_id,
							  vdev_id);
}

/**
 * cdp_deliver_tx_mgmt() - Deliver mgmt frame for tx capture
 * @soc: Datapath SOC handle
 * @pdev_id: id of datapath PDEV handle
 * @nbuf: Management frame buffer
 */
static inline QDF_STATUS
cdp_deliver_tx_mgmt(ol_txrx_soc_handle soc, uint8_t pdev_id,
		    qdf_nbuf_t nbuf)
{
	if (!soc || !soc->ops) {
		QDF_TRACE(QDF_MODULE_ID_CDP, QDF_TRACE_LEVEL_DEBUG,
			  "%s: Invalid Instance", __func__);
		QDF_BUG(0);
		return QDF_STATUS_E_FAILURE;
	}

	if (!soc->ops->mon_ops ||
	    !soc->ops->mon_ops->txrx_deliver_tx_mgmt)
		return QDF_STATUS_E_FAILURE;

	return soc->ops->mon_ops->txrx_deliver_tx_mgmt(soc, pdev_id, nbuf);
}

#endif
