// SPDX-License-Identifier: GPL-2.0
#include <uapi/linux/bpf.h>
#include <linux/if_link.h>
#include <test_progs.h>

#define IFINDEX_LO 1

void test_xdp_with_devmap_helpers(void)
{
	struct bpf_object *obj;
	struct bpf_program *prog, *redir_prog, *dm_prog, *dummy_prog;
	struct bpf_prog_info info = {};
	struct bpf_devmap_val val = {
		.ifindex = IFINDEX_LO,
	};
	__u32 len = sizeof(info);
	__u32 duration = 0, idx = 0;
	int err, dm_fd, map_fd;

	obj = bpf_object__open("./test_xdp_with_devmap_helpers.o");
	if (CHECK_FAIL(IS_ERR_OR_NULL(obj)))
		return;

	/* This libbpf does not infer the attach type from xdp_devmap. */
	bpf_object__for_each_program(prog, obj)
		bpf_program__set_type(prog, BPF_PROG_TYPE_XDP);
	redir_prog = bpf_object__find_program_by_title(obj, "xdp_redir");
	dm_prog = bpf_object__find_program_by_title(obj, "xdp_devmap");
	dummy_prog = bpf_object__find_program_by_title(obj, "xdp_dummy");
	if (CHECK_FAIL(!redir_prog || !dm_prog || !dummy_prog))
		goto out_close;
	bpf_program__set_expected_attach_type(dm_prog, BPF_XDP_DEVMAP);
	err = bpf_object__load(obj);
	if (CHECK_FAIL(err))
		goto out_close;

	/* can not attach program with DEVMAPs that allow programs
	 * as xdp generic
	 */
	dm_fd = bpf_program__fd(redir_prog);
	err = bpf_set_link_xdp_fd(IFINDEX_LO, dm_fd, XDP_FLAGS_SKB_MODE);
	CHECK(err == 0, "Generic attach of program with 8-byte devmap",
	      "should have failed\n");

	dm_fd = bpf_program__fd(dm_prog);
	map_fd = bpf_find_map(__func__, obj, "dm_ports");
	if (CHECK_FAIL(map_fd < 0))
		goto out_close;
	err = bpf_obj_get_info_by_fd(dm_fd, &info, &len);
	if (CHECK_FAIL(err))
		goto out_close;

	val.bpf_prog.fd = dm_fd;
	err = bpf_map_update_elem(map_fd, &idx, &val, 0);
	CHECK(err, "Add program to devmap entry",
	      "err %d errno %d\n", err, errno);

	err = bpf_map_lookup_elem(map_fd, &idx, &val);
	CHECK(err, "Read devmap entry", "err %d errno %d\n", err, errno);
	CHECK(info.id != val.bpf_prog.id, "Expected program id in devmap entry",
	      "expected %u read %u\n", info.id, val.bpf_prog.id);

	/* can not attach BPF_XDP_DEVMAP program to a device */
	err = bpf_set_link_xdp_fd(IFINDEX_LO, dm_fd, XDP_FLAGS_SKB_MODE);
	CHECK(err == 0, "Attach of BPF_XDP_DEVMAP program",
	      "should have failed\n");

	val.ifindex = 1;
	val.bpf_prog.fd = bpf_program__fd(dummy_prog);
	err = bpf_map_update_elem(map_fd, &idx, &val, 0);
	CHECK(err == 0, "Add non-BPF_XDP_DEVMAP program to devmap entry",
	      "should have failed\n");

out_close:
	bpf_object__close(obj);
}

void test_neg_xdp_devmap_helpers(void)
{
	struct bpf_object *obj;
	__u32 duration = 0;
	int err, prog_fd;

	err = bpf_prog_load("./test_xdp_devmap_helpers.o", BPF_PROG_TYPE_XDP,
			    &obj, &prog_fd);
	if (CHECK(!err,
		  "Load of XDP program accessing egress ifindex without attach type",
		  "should have failed\n")) {
		bpf_object__close(obj);
	}
}


void test_xdp_devmap_attach(void)
{
	if (test__start_subtest("DEVMAP with programs in entries"))
		test_xdp_with_devmap_helpers();

	if (test__start_subtest("Verifier check of DEVMAP programs"))
		test_neg_xdp_devmap_helpers();
}
