// SPDX-License-Identifier: GPL-2.0-only
#include "stealth_net.h"
#include <linux/icmp.h>
#include <linux/icmpv6.h>
#include <linux/if.h>
#include <linux/workqueue.h>

static bool ghost_is_tracked_reply(const struct sk_buff *skb)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct;

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct)
		return false;
	return ctinfo == IP_CT_ESTABLISHED_REPLY ||
	       ctinfo == IP_CT_RELATED_REPLY ||
	       ctinfo == IP_CT_ESTABLISHED || ctinfo == IP_CT_RELATED;
}

static bool ghost_discovery_port(__be16 dest)
{
	return dest == htons(137) || dest == htons(138) ||
	       dest == htons(1900) || dest == htons(5353) ||
	       dest == htons(5355);
}

static bool ghost_drop_ipv4_out(const struct sk_buff *skb,
				const struct iphdr *iph, int trans_off)
{
	struct udphdr _udph, *udph;

	if (atomic_read(&stealth_ghost_mode) < 2 || iph->protocol != IPPROTO_UDP)
		return false;
	udph = skb_header_pointer(skb, trans_off, sizeof(_udph), &_udph);
	if (!udph)
		return false;
	return ghost_discovery_port(udph->dest);
}

static bool ghost_drop_ipv6_out(const struct sk_buff *skb, u8 nexthdr,
				int trans_off)
{
	struct udphdr _udph, *udph;

	if (atomic_read(&stealth_ghost_mode) < 2 || nexthdr != IPPROTO_UDP)
		return false;
	udph = skb_header_pointer(skb, trans_off, sizeof(_udph), &_udph);
	if (!udph)
		return false;
	return ghost_discovery_port(udph->dest);
}

static bool ghost_drop_ipv4_in(const struct sk_buff *skb,
			       const struct iphdr *iph, int trans_off)
{
	struct icmphdr _icmph, *icmph;
	struct udphdr _udph, *udph;

	if (!atomic_read(&stealth_ghost_mode) || ghost_is_tracked_reply(skb))
		return false;
	if (iph->protocol == IPPROTO_UDP) {
		udph = skb_header_pointer(skb, trans_off, sizeof(_udph), &_udph);
		if (udph && udph->source == htons(67) && udph->dest == htons(68))
			return false;
		return true;
	}
	if (iph->protocol == IPPROTO_TCP)
		return true;
	if (iph->protocol != IPPROTO_ICMP)
		return false;
	icmph = skb_header_pointer(skb, trans_off, sizeof(_icmph), &_icmph);
	return icmph && icmph->type == ICMP_ECHO;
}

static bool ghost_drop_ipv6_in(const struct sk_buff *skb, u8 nexthdr,
			       int trans_off)
{
	struct icmp6hdr _icmp6h, *icmp6h;
	struct udphdr _udph, *udph;

	if (!atomic_read(&stealth_ghost_mode) || ghost_is_tracked_reply(skb))
		return false;
	if (nexthdr == IPPROTO_UDP) {
		udph = skb_header_pointer(skb, trans_off, sizeof(_udph), &_udph);
		if (udph && udph->source == htons(547) && udph->dest == htons(546))
			return false;
		return true;
	}
	if (nexthdr == IPPROTO_TCP)
		return true;
	if (nexthdr != IPPROTO_ICMPV6)
		return false;
	icmp6h = skb_header_pointer(skb, trans_off, sizeof(_icmp6h), &_icmp6h);
	if (!icmp6h)
		return false;
	if (icmp6h->icmp6_type < 128 ||
	    (icmp6h->icmp6_type >= 133 && icmp6h->icmp6_type <= 137))
		return false;
	return icmp6h->icmp6_type == ICMPV6_ECHO_REQUEST;
}

static struct sk_buff *craft_spoof_reply(struct sk_buff *skb, const u8 *qbuf,
	int qlen, uint16_t id, uint16_t flags, uint16_t qtype,
	const struct stealth_rule_match *m, bool is_ipv6)
{
	bool has_answer = (qtype == DNS_TYPE_A && m->ip_version == 4) ||
		(qtype == DNS_TYPE_AAAA && m->ip_version == 6);

	if (is_ipv6)
		return craft_clean_dns_reply_ipv6(skb, qbuf, qlen, id, flags,
			&m->spoofed_ip, m->ip_version, DNS_RCODE_NOERROR, has_answer);
	return craft_clean_dns_reply_ipv4(skb, qbuf, qlen, id, flags,
		&m->spoofed_ip, m->ip_version, DNS_RCODE_NOERROR, has_answer);
}

static unsigned int hook_dns_core(struct sk_buff *skb, const struct nf_hook_state *state,
	int trans_off, bool is_ipv6)
{
	struct udphdr _udph, *udph;
	struct dns_hdr _dnsh, *dnsh;
	struct sock *sk;
	struct stealth_rule_match match;
	struct sk_buff *reply = NULL;
	uid_t uid = 0;
	char domain[MAX_DOMAIN_WIRE_LEN + 1];
	u8 qbuf[MAX_DOMAIN_WIRE_LEN + 4];
	int udp_len = sizeof(*udph);
	int dns_hdr_len = sizeof(*dnsh);
	int dom_len = sizeof(domain);
	int qbuf_len = sizeof(qbuf);
	int payload_off, avail_len, qlen, ret;
	uint16_t qtype = 0, qclass = 0;
	uint16_t orig_id, orig_flags;

	if (unlikely(skb->len < trans_off + udp_len))
		return NF_ACCEPT;
	udph = skb_header_pointer(skb, trans_off, udp_len, &_udph);
	if (unlikely(!udph || udph->dest != htons(DNS_PORT)))
		return NF_ACCEPT;
	if (unlikely(ntohs(udph->len) < udp_len + dns_hdr_len))
		return NF_ACCEPT;
	if (unlikely(ntohs(udph->len) > skb->len - trans_off))
		return NF_ACCEPT;

	sk = skb_to_full_sk(skb);
	if (sk && sk_fullsock(sk))
		uid = from_kuid_munged(&init_user_ns, sock_net_uid(state->net, sk));
	payload_off = trans_off + udp_len;
	dnsh = skb_header_pointer(skb, payload_off, dns_hdr_len, &_dnsh);
	if (unlikely(!dnsh))
		return NF_ACCEPT;
	orig_id = dnsh->id;
	orig_flags = ntohs(dnsh->flags);
	if (unlikely((orig_flags & (DNS_FLAG_QR | DNS_FLAG_OPCODE)) != 0 ||
	    ntohs(dnsh->qdcount) != 1))
		return NF_ACCEPT;
	avail_len = ntohs(udph->len) - udp_len - dns_hdr_len;
	if (unlikely(avail_len <= 0))
		return NF_ACCEPT;
	qlen = parse_qname_strict(skb, payload_off + dns_hdr_len, avail_len,
		domain, dom_len, &qtype, &qclass);
	if (unlikely(qlen < 0 || qlen > qbuf_len || qlen > MAX_QUESTION_BYTES || qclass != DNS_CLASS_IN))
		return NF_ACCEPT;
	if (unlikely(skb_copy_bits(skb, payload_off + dns_hdr_len, qbuf, qlen) < 0))
		return NF_ACCEPT;
	atomic_inc(&total_dns_detected);

	if (!lookup_rule(domain, uid, &match))
		return NF_ACCEPT;
	push_event(uid, match.action, is_ipv6 ? 6 : 4, domain);
	if (match.action == ACTION_AUDIT)
		return NF_ACCEPT;
	if (match.action == ACTION_DROP)
		goto drop_stolen;
	if (match.action == ACTION_BLOCK && is_ipv6)
		reply = craft_clean_dns_reply_ipv6(skb, qbuf, qlen, orig_id,
			orig_flags, NULL, 0, DNS_RCODE_NXDOMAIN, false);
	if (match.action == ACTION_BLOCK && !is_ipv6)
		reply = craft_clean_dns_reply_ipv4(skb, qbuf, qlen, orig_id,
			orig_flags, NULL, 0, DNS_RCODE_NXDOMAIN, false);
	if (match.action == ACTION_SPOOF)
		reply = craft_spoof_reply(skb, qbuf, qlen, orig_id, orig_flags,
			qtype, &match, is_ipv6);
	if (unlikely(!reply))
		return NF_ACCEPT;
	if (is_ipv6)
		ret = send_ipv6_reply(state->net, reply);
	else
		ret = send_ipv4_reply(state->net, reply);
	(void)ret;
drop_stolen:
	kfree_skb(skb);
	return NF_STOLEN;
}

static bool inspect_tls_sni(struct sk_buff *skb, int trans_off, int tot_len,
	char *domain, int dom_len)
{
	struct tcphdr _tcph, *tcph;
	int tcp_len = sizeof(*tcph);
	int payload_off, payload_len, tcp_hdr_len;

	tcph = skb_header_pointer(skb, trans_off, tcp_len, &_tcph);
	if (unlikely(!tcph || tcph->dest != htons(HTTPS_PORT)))
		return false;
	if (unlikely(tcph->doff < 5))
		return false;
	tcp_hdr_len = tcph->doff * 4;
	payload_off = trans_off + tcp_hdr_len;
	if (unlikely(payload_off > skb->len || payload_off > tot_len))
		return false;
	payload_len = tot_len - payload_off;
	if (unlikely(payload_len <= 0))
		return false;
	return parse_tls_sni(skb, payload_off, payload_len, domain, dom_len) == 0;
}

#define MAX_PENDING_QUIC_INSPECTIONS 128

struct stealth_quic_work {
	struct work_struct work;
	struct sk_buff *payload;
	struct net *net;
	uid_t uid;
	int ifindex;
	bool is_ipv6;
	union {
		__be32 v4;
		struct in6_addr v6;
	} remote;
};

static struct workqueue_struct *quic_inspection_wq;
static atomic_t pending_quic_inspections = ATOMIC_INIT(0);

static void report_app_hostname(struct net *net, int ifindex, uid_t uid,
				bool is_ipv6, const void *remote_ip,
				const char *domain)
{
	struct stealth_rule_match match;
	char host_event[MAX_DOMAIN_WIRE_LEN + 16];
	bool is_block;

	if (ifindex)
		stealth_neigh_set_hostname(net, ifindex, is_ipv6 ? 6 : 4,
					   remote_ip, domain);
	if (!lookup_rule(domain, uid, &match))
		return;

	is_block = match.action == ACTION_BLOCK || match.action == ACTION_DROP;
	snprintf(host_event, sizeof(host_event), "%s:%s",
		 is_block ? "BYPASS" : "HOST", domain);
	push_event(uid, match.action, is_ipv6 ? 6 : 4, host_event);
}

static void inspect_quic_work(struct work_struct *work)
{
	struct stealth_quic_work *job =
		container_of(work, struct stealth_quic_work, work);
	char domain[MAX_DOMAIN_WIRE_LEN + 1];
	const void *remote_ip = job->is_ipv6 ?
		(const void *)&job->remote.v6 : (const void *)&job->remote.v4;

	if (!parse_quic_sni(job->payload, 0, job->payload->len,
			    domain, sizeof(domain)))
		report_app_hostname(job->net, job->ifindex, job->uid,
				    job->is_ipv6, remote_ip, domain);

	put_net(job->net);
	kfree_skb(job->payload);
	kfree(job);
	atomic_dec(&pending_quic_inspections);
}

static void queue_quic_inspection(const struct sk_buff *skb,
				  const struct nf_hook_state *state,
				  int trans_off, uid_t uid, bool is_ipv6,
				  const void *remote_ip)
{
	struct stealth_quic_work *job;
	struct udphdr header;
	const struct udphdr *udp;
	u8 initial[5];
	int offset, len;

	udp = skb_header_pointer(skb, trans_off, sizeof(header), &header);
	if (!udp || udp->dest != htons(HTTPS_PORT))
		return;
	offset = trans_off + sizeof(header);
	len = ntohs(udp->len) - (int)sizeof(header);
	if (len < 64 || offset > skb->len || len > skb->len - offset)
		return;
	if (skb_copy_bits(skb, offset, initial, sizeof(initial)) ||
	    (initial[0] & 0xb0) != 0x80 ||
	    initial[1] || initial[2] || initial[3] || initial[4] != 1)
		return;

	if (atomic_inc_return(&pending_quic_inspections) >
	    MAX_PENDING_QUIC_INSPECTIONS)
		goto out_count;

	job = kzalloc(sizeof(*job), GFP_ATOMIC);
	if (!job)
		goto out_count;
	len = min(len, 1500);
	job->payload = alloc_skb(len, GFP_ATOMIC);
	if (!job->payload)
		goto out_free;
	if (skb_copy_bits(skb, offset, skb_put(job->payload, len), len))
		goto out_payload;

	job->net = get_net(state->net);
	job->uid = uid;
	job->ifindex = state->out ? state->out->ifindex : 0;
	job->is_ipv6 = is_ipv6;
	if (is_ipv6)
		job->remote.v6 = *(const struct in6_addr *)remote_ip;
	else
		job->remote.v4 = *(const __be32 *)remote_ip;
	INIT_WORK(&job->work, inspect_quic_work);
	if (queue_work(quic_inspection_wq, &job->work))
		return;

	put_net(job->net);
out_payload:
	kfree_skb(job->payload);
out_free:
	kfree(job);
out_count:
	atomic_dec(&pending_quic_inspections);
}

int stealth_inspection_init(void)
{
	quic_inspection_wq = alloc_ordered_workqueue("stealth_quic", 0);
	return quic_inspection_wq ? 0 : -ENOMEM;
}

void stealth_inspection_cleanup(void)
{
	if (quic_inspection_wq) {
		destroy_workqueue(quic_inspection_wq);
		quic_inspection_wq = NULL;
	}
}

static void inspect_app_traffic(struct sk_buff *skb, const struct nf_hook_state *state,
	int trans_off, u8 protocol, int tot_len, bool is_ipv6,
	const void *remote_ip)
{
	struct sock *sk;
	uid_t uid;
	char domain[MAX_DOMAIN_WIRE_LEN + 1];

	sk = skb_to_full_sk(skb);
	if (!sk || !sk_fullsock(sk))
		return;
	uid = from_kuid_munged(&init_user_ns, sock_net_uid(state->net, sk));
	if (protocol == IPPROTO_UDP) {
		queue_quic_inspection(skb, state, trans_off, uid, is_ipv6, remote_ip);
		return;
	}
	if (protocol == IPPROTO_TCP &&
	    inspect_tls_sni(skb, trans_off, tot_len, domain, sizeof(domain)))
		report_app_hostname(state->net, state->out ? state->out->ifindex : 0,
				    uid, is_ipv6, remote_ip, domain);
}

static unsigned int hook_dns_ipv4(void *priv, struct sk_buff *skb,
	const struct nf_hook_state *state)
{
	struct iphdr _iph, *iph;
	int ip_len = sizeof(*iph);
	int ip_hl, tot_len;

	if (unlikely(!atomic_read(&stealth_enabled)))
		return NF_ACCEPT;
	atomic_inc(&total_packets_hooked);
	iph = skb_header_pointer(skb, 0, ip_len, &_iph);
	if (unlikely(!iph || iph->ihl < 5 || skb->len < iph->ihl * 4))
		return NF_ACCEPT;
	ip_hl = iph->ihl * 4;
	tot_len = ntohs(iph->tot_len);
	if (unlikely(tot_len < ip_hl || tot_len > skb->len))
		return NF_ACCEPT;
	if (state->out) {
		stealth_neigh_account_traffic(state->net, state->out->ifindex, 4,
					       &iph->daddr, tot_len, true);
		if (unlikely(stealth_neigh_is_ip_blocked(state->net,
							 state->out->ifindex, 4,
							 &iph->daddr)))
			return NF_DROP;
	}
	if (unlikely(stealth_tunnel_filter(skb, state))) {
		stealth_capture_packet(skb, state, ip_hl, iph->protocol, true,
				       "DROP", "tunnel_leak");
		return NF_DROP;
	}
	if (state->out && !(state->out->flags & IFF_LOOPBACK) &&
	    unlikely(ghost_drop_ipv4_out(skb, iph, ip_hl))) {
		stealth_capture_packet(skb, state, ip_hl, iph->protocol, true,
				       "DROP", "ghost");
		atomic64_inc(&stealth_ghost_drops);
		return NF_DROP;
	}
	if (unlikely(stealth_tcp_filter(skb, state, ip_hl, iph->protocol, true))) {
		stealth_capture_packet(skb, state, ip_hl, iph->protocol, true,
				       "DROP", "tcp_hardening");
		return NF_DROP;
	}
	stealth_capture_packet(skb, state, ip_hl, iph->protocol, true,
			       "ACCEPT", "-");
	inspect_app_traffic(skb, state, ip_hl, iph->protocol, tot_len, false,
		&iph->daddr);
	if (iph->protocol != IPPROTO_UDP)
		return NF_ACCEPT;

	return hook_dns_core(skb, state, ip_hl, false);
}

static unsigned int hook_dns_ipv6(void *priv, struct sk_buff *skb,
	const struct nf_hook_state *state)
{
	struct ipv6hdr _iph, *iph;
	int ip_len = sizeof(*iph);
	int trans_off, tot_len;
	u8 nexthdr;
	__be16 frag_off;

	if (unlikely(!atomic_read(&stealth_enabled)))
		return NF_ACCEPT;
	atomic_inc(&total_packets_hooked);
	iph = skb_header_pointer(skb, 0, ip_len, &_iph);
	if (unlikely(!iph))
		return NF_ACCEPT;
	tot_len = ip_len + ntohs(iph->payload_len);
	if (unlikely(tot_len > skb->len))
		return NF_ACCEPT;
	if (state->out) {
		stealth_neigh_account_traffic(state->net, state->out->ifindex, 6,
					       &iph->daddr, tot_len, true);
		if (unlikely(stealth_neigh_is_ip_blocked(state->net,
							 state->out->ifindex, 6,
							 &iph->daddr)))
			return NF_DROP;
	}
	if (unlikely(stealth_tunnel_filter(skb, state))) {
		stealth_capture_packet(skb, state, sizeof(*iph), iph->nexthdr,
				       true, "DROP", "tunnel_leak");
		return NF_DROP;
	}
	nexthdr = iph->nexthdr;
	trans_off = ipv6_skip_exthdr(skb, ip_len, &nexthdr, &frag_off);
	if (unlikely(trans_off < 0 || frag_off != 0))
		return NF_ACCEPT;
	if (state->out && !(state->out->flags & IFF_LOOPBACK) &&
	    unlikely(ghost_drop_ipv6_out(skb, nexthdr, trans_off))) {
		stealth_capture_packet(skb, state, trans_off, nexthdr, true,
				       "DROP", "ghost");
		atomic64_inc(&stealth_ghost_drops);
		return NF_DROP;
	}
	if (unlikely(stealth_tcp_filter(skb, state, trans_off, nexthdr, true))) {
		stealth_capture_packet(skb, state, trans_off, nexthdr, true,
				       "DROP", "tcp_hardening");
		return NF_DROP;
	}
	stealth_capture_packet(skb, state, trans_off, nexthdr, true,
			       "ACCEPT", "-");
	inspect_app_traffic(skb, state, trans_off, nexthdr, tot_len, true,
		&iph->daddr);
	if (nexthdr != IPPROTO_UDP)
		return NF_ACCEPT;

	return hook_dns_core(skb, state, trans_off, true);
}

static unsigned int hook_neigh_ipv4_in(void *priv, struct sk_buff *skb,
	const struct nf_hook_state *state)
{
	struct iphdr _iph, *iph;
	int tot_len, ip_hl;

	if (unlikely(!atomic_read(&stealth_enabled)))
		return NF_ACCEPT;
	iph = skb_header_pointer(skb, 0, sizeof(_iph), &_iph);
	if (unlikely(!iph || iph->ihl < 5))
		return NF_ACCEPT;
	tot_len = ntohs(iph->tot_len);
	if (unlikely(tot_len < iph->ihl * 4 || tot_len > skb->len))
		return NF_ACCEPT;
	ip_hl = iph->ihl * 4;
	if (!state->in)
		return NF_ACCEPT;
	stealth_neigh_account_traffic(state->net, state->in->ifindex, 4,
				       &iph->saddr, tot_len, false);
	if (unlikely(stealth_ids_filter(skb, state, ip_hl, iph->protocol))) {
		stealth_capture_packet(skb, state, ip_hl, iph->protocol, false,
				       "DROP", "ids");
		return NF_DROP;
	}
	if (stealth_neigh_is_ip_blocked(state->net, state->in->ifindex, 4,
					&iph->saddr) ||
	    (!(state->in->flags & IFF_LOOPBACK) &&
	     ghost_drop_ipv4_in(skb, iph, ip_hl))) {
		atomic64_inc(&stealth_ghost_drops);
		return NF_DROP;
	}
	if (unlikely(stealth_tcp_filter(skb, state, ip_hl, iph->protocol, false))) {
		stealth_capture_packet(skb, state, ip_hl, iph->protocol, false,
				       "DROP", "tcp_hardening");
		return NF_DROP;
	}
	stealth_capture_packet(skb, state, ip_hl, iph->protocol, false,
			       "ACCEPT", "-");
	return NF_ACCEPT;
}

static unsigned int hook_neigh_ipv6_in(void *priv, struct sk_buff *skb,
	const struct nf_hook_state *state)
{
	struct ipv6hdr _iph, *iph;
	int tot_len, trans_off;
	u8 nexthdr;
	__be16 frag_off;

	if (unlikely(!atomic_read(&stealth_enabled)))
		return NF_ACCEPT;
	iph = skb_header_pointer(skb, 0, sizeof(_iph), &_iph);
	if (unlikely(!iph))
		return NF_ACCEPT;
	tot_len = sizeof(*iph) + ntohs(iph->payload_len);
	if (unlikely(tot_len > skb->len))
		return NF_ACCEPT;
	if (!state->in)
		return NF_ACCEPT;
	stealth_neigh_account_traffic(state->net, state->in->ifindex, 6,
				       &iph->saddr, tot_len, false);
	if (stealth_neigh_is_ip_blocked(state->net, state->in->ifindex, 6,
					&iph->saddr)) {
		atomic64_inc(&stealth_ghost_drops);
		return NF_DROP;
	}
	nexthdr = iph->nexthdr;
	trans_off = ipv6_skip_exthdr(skb, sizeof(*iph), &nexthdr, &frag_off);
	if (trans_off >= 0 && frag_off == 0 &&
	    unlikely(stealth_ids_filter(skb, state, trans_off, nexthdr))) {
		stealth_capture_packet(skb, state, trans_off, nexthdr, false,
				       "DROP", "ids");
		return NF_DROP;
	}
	if (!(state->in->flags & IFF_LOOPBACK) &&
	    trans_off >= 0 && frag_off == 0 &&
	    ghost_drop_ipv6_in(skb, nexthdr, trans_off)) {
		atomic64_inc(&stealth_ghost_drops);
		return NF_DROP;
	}
	if (trans_off >= 0 && frag_off == 0 &&
	    unlikely(stealth_tcp_filter(skb, state, trans_off, nexthdr, false))) {
		stealth_capture_packet(skb, state, trans_off, nexthdr, false,
				       "DROP", "tcp_hardening");
		return NF_DROP;
	}
	if (trans_off >= 0 && frag_off == 0)
		stealth_capture_packet(skb, state, trans_off, nexthdr, false,
				       "ACCEPT", "-");
	return NF_ACCEPT;
}

struct nf_hook_ops stealth_net_hooks[] = {
	{
		.hook = hook_dns_ipv4,
		.pf = NFPROTO_IPV4,
		.hooknum = NF_INET_LOCAL_OUT,
		.priority = NF_IP_PRI_FIRST,
	},
	{
		.hook = hook_dns_ipv6,
		.pf = NFPROTO_IPV6,
		.hooknum = NF_INET_LOCAL_OUT,
		.priority = NF_IP_PRI_FIRST,
	},
	{
		.hook = hook_neigh_ipv4_in,
		.pf = NFPROTO_IPV4,
		.hooknum = NF_INET_LOCAL_IN,
		.priority = NF_IP_PRI_FILTER,
	},
	{
		.hook = hook_neigh_ipv6_in,
		.pf = NFPROTO_IPV6,
		.hooknum = NF_INET_LOCAL_IN,
		.priority = NF_IP_PRI_FILTER,
	}
};

size_t stealth_net_hooks_count = ARRAY_SIZE(stealth_net_hooks);
