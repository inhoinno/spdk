/*-
 *   BSD LICENSE
 *
 *   Copyright (c) Intel Corporation. All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "spdk/stdinc.h"
#include "spdk_cunit.h"
#include "common/lib/test_env.c"
#include "nvmf/vfio_user.c"
#include "nvmf/transport.c"

DEFINE_STUB(spdk_nvmf_ctrlr_get_regs, const struct spdk_nvmf_registers *,
	    (struct spdk_nvmf_ctrlr *ctrlr), NULL);
DEFINE_STUB(spdk_mem_register, int, (void *vaddr, size_t len), 0);
DEFINE_STUB(spdk_mem_unregister, int, (void *vaddr, size_t len), 0);
DEFINE_STUB_V(spdk_nvmf_request_exec, (struct spdk_nvmf_request *req));
DEFINE_STUB_V(spdk_nvmf_request_exec_fabrics, (struct spdk_nvmf_request *req));
DEFINE_STUB(spdk_nvmf_request_complete, int, (struct spdk_nvmf_request *req), 0);
DEFINE_STUB_V(spdk_nvmf_tgt_new_qpair, (struct spdk_nvmf_tgt *tgt, struct spdk_nvmf_qpair *qpair));
DEFINE_STUB(nvmf_ctrlr_abort_request, int, (struct spdk_nvmf_request *req), 0);
DEFINE_STUB(spdk_nvmf_qpair_disconnect, int, (struct spdk_nvmf_qpair *qpair,
		nvmf_qpair_disconnect_cb cb_fn, void *ctx), 0);
DEFINE_STUB(spdk_nvmf_subsystem_get_nqn, const char *,
	    (const struct spdk_nvmf_subsystem *subsystem), NULL);
DEFINE_STUB(spdk_bdev_get_block_size, uint32_t, (const struct spdk_bdev *bdev), 512);
DEFINE_STUB_V(nvmf_ctrlr_abort_aer, (struct spdk_nvmf_ctrlr *ctrlr));

static void *
gpa_to_vva(void *prv, uint64_t addr, uint64_t len, int prot)
{
	return (void *)(uintptr_t)addr;
}

static void
test_nvme_cmd_map_prps(void)
{
	struct spdk_nvme_cmd cmd = {};
	struct iovec iovs[33];
	uint64_t phy_addr, *prp;
	uint32_t len;
	void *buf, *prps;
	int i, ret;
	size_t mps = 4096;

	buf = spdk_zmalloc(132 * 1024, 4096, &phy_addr, 0, 0);
	CU_ASSERT(buf != NULL);
	prps = spdk_zmalloc(4096, 4096, &phy_addr, 0, 0);
	CU_ASSERT(prps != NULL);

	/* test case 1: 4KiB with PRP1 only */
	cmd.dptr.prp.prp1 = (uint64_t)(uintptr_t)buf;
	len = 4096;
	ret = nvme_cmd_map_prps(NULL, &cmd, iovs, 33, len, mps, gpa_to_vva);
	CU_ASSERT(ret == 1);
	CU_ASSERT(iovs[0].iov_base == (void *)(uintptr_t)cmd.dptr.prp.prp1);
	CU_ASSERT(iovs[0].iov_len == len);

	/* test case 2: 4KiB with PRP1 and PRP2, 1KiB in first iov, and 3KiB in second iov */
	cmd.dptr.prp.prp1 = (uint64_t)(uintptr_t)buf + 1024 * 3;
	cmd.dptr.prp.prp2 = (uint64_t)(uintptr_t)buf + 4096;
	len = 4096;
	ret = nvme_cmd_map_prps(NULL, &cmd, iovs, 1, len, mps, gpa_to_vva);
	CU_ASSERT(ret == -ERANGE);
	ret = nvme_cmd_map_prps(NULL, &cmd, iovs, 33, len, mps, gpa_to_vva);
	CU_ASSERT(ret == 2);
	CU_ASSERT(iovs[0].iov_base == (void *)(uintptr_t)cmd.dptr.prp.prp1);
	CU_ASSERT(iovs[0].iov_len == 1024);
	CU_ASSERT(iovs[1].iov_base == (void *)(uintptr_t)cmd.dptr.prp.prp2);
	CU_ASSERT(iovs[1].iov_len == 1024 * 3);

	/* test case 3: 128KiB with PRP list, 1KiB in first iov, 3KiB in last iov */
	cmd.dptr.prp.prp1 = (uint64_t)(uintptr_t)buf + 1024 * 3;
	cmd.dptr.prp.prp2 = (uint64_t)(uintptr_t)prps;
	len = 128 * 1024;
	prp = prps;
	for (i = 1; i < 33; i++) {
		*prp = (uint64_t)(uintptr_t)buf + i * 4096;
		prp++;
	}
	ret = nvme_cmd_map_prps(NULL, &cmd, iovs, 33, len, mps, gpa_to_vva);
	CU_ASSERT(ret == 33);
	CU_ASSERT(iovs[0].iov_base == (void *)(uintptr_t)cmd.dptr.prp.prp1);
	CU_ASSERT(iovs[0].iov_len == 1024);
	for (i = 1; i < 32; i++) {
		CU_ASSERT(iovs[i].iov_base == (void *)((uintptr_t)buf + i * 4096));
		CU_ASSERT(iovs[i].iov_len == 4096);
	}
	CU_ASSERT(iovs[32].iov_base == (void *)((uintptr_t)buf + 32 * 4096));
	CU_ASSERT(iovs[32].iov_len == 1024 * 3);

	/* test case 4: 256KiB with PRP list, not enough iovs */
	cmd.dptr.prp.prp1 = (uint64_t)(uintptr_t)buf + 1024 * 3;
	cmd.dptr.prp.prp2 = (uint64_t)(uintptr_t)prps;
	len = 256 * 1024;
	ret = nvme_cmd_map_prps(NULL, &cmd, iovs, 33, len, mps, gpa_to_vva);
	CU_ASSERT(ret == -ERANGE);

	spdk_free(buf);
	spdk_free(prps);
}

static void
test_nvme_cmd_map_sgls(void)
{
	struct spdk_nvme_cmd cmd = {};
	struct iovec iovs[33];
	uint64_t phy_addr;
	uint32_t len;
	void *buf, *sgls;
	struct spdk_nvme_sgl_descriptor *sgl;
	int i, ret;
	size_t mps = 4096;

	buf = spdk_zmalloc(132 * 1024, 4096, &phy_addr, 0, 0);
	CU_ASSERT(buf != NULL);
	sgls = spdk_zmalloc(4096, 4096, &phy_addr, 0, 0);
	CU_ASSERT(sgls != NULL);

	/* test case 1: 8KiB with 1 data block */
	len = 8192;
	cmd.dptr.sgl1.unkeyed.type = SPDK_NVME_SGL_TYPE_DATA_BLOCK;
	cmd.dptr.sgl1.unkeyed.length = len;
	cmd.dptr.sgl1.address = (uint64_t)(uintptr_t)buf;

	ret = nvme_cmd_map_sgls(NULL, &cmd, iovs, 33, len, mps, gpa_to_vva);
	CU_ASSERT(ret == 1);
	CU_ASSERT(iovs[0].iov_base == buf);
	CU_ASSERT(iovs[0].iov_len == 8192);

	/* test case 2: 8KiB with 2 data blocks and 1 last segment */
	sgl = (struct spdk_nvme_sgl_descriptor *)sgls;
	sgl[0].unkeyed.type = SPDK_NVME_SGL_TYPE_DATA_BLOCK;
	sgl[0].unkeyed.length = 2048;
	sgl[0].address = (uint64_t)(uintptr_t)buf;
	sgl[1].unkeyed.type = SPDK_NVME_SGL_TYPE_DATA_BLOCK;
	sgl[1].unkeyed.length = len - 2048;
	sgl[1].address = (uint64_t)(uintptr_t)buf + 16 * 1024;

	cmd.dptr.sgl1.unkeyed.type = SPDK_NVME_SGL_TYPE_LAST_SEGMENT;
	cmd.dptr.sgl1.unkeyed.length = 2 * sizeof(*sgl);
	cmd.dptr.sgl1.address = (uint64_t)(uintptr_t)sgls;

	ret = nvme_cmd_map_sgls(NULL, &cmd, iovs, 33, len, mps, gpa_to_vva);
	CU_ASSERT(ret == 2);
	CU_ASSERT(iovs[0].iov_base == (void *)(uintptr_t)buf);
	CU_ASSERT(iovs[0].iov_len == 2048);
	CU_ASSERT(iovs[1].iov_base == (void *)((uintptr_t)buf + 16 * 1024));
	CU_ASSERT(iovs[1].iov_len == len - 2048);

	/* test case 3: 8KiB with 1 segment, 1 last segment and 3 data blocks */
	sgl[0].unkeyed.type = SPDK_NVME_SGL_TYPE_DATA_BLOCK;
	sgl[0].unkeyed.length = 2048;
	sgl[0].address = (uint64_t)(uintptr_t)buf;
	sgl[1].unkeyed.type = SPDK_NVME_SGL_TYPE_LAST_SEGMENT;
	sgl[1].unkeyed.length = 2 * sizeof(*sgl);
	sgl[1].address = (uint64_t)(uintptr_t)&sgl[9];

	sgl[9].unkeyed.type = SPDK_NVME_SGL_TYPE_DATA_BLOCK;
	sgl[9].unkeyed.length = 4096;
	sgl[9].address = (uint64_t)(uintptr_t)buf + 4 * 1024;
	sgl[10].unkeyed.type = SPDK_NVME_SGL_TYPE_DATA_BLOCK;
	sgl[10].unkeyed.length = 2048;
	sgl[10].address = (uint64_t)(uintptr_t)buf + 16 * 1024;

	cmd.dptr.sgl1.unkeyed.type = SPDK_NVME_SGL_TYPE_SEGMENT;
	cmd.dptr.sgl1.unkeyed.length = 2 * sizeof(*sgl);
	cmd.dptr.sgl1.address = (uint64_t)(uintptr_t)&sgl[0];

	ret = nvme_cmd_map_sgls(NULL, &cmd, iovs, 33, len, mps, gpa_to_vva);
	CU_ASSERT(ret == 3);
	CU_ASSERT(iovs[0].iov_base == (void *)(uintptr_t)buf);
	CU_ASSERT(iovs[0].iov_len == 2048);
	CU_ASSERT(iovs[1].iov_base == (void *)((uintptr_t)buf + 4 * 1024));
	CU_ASSERT(iovs[1].iov_len == 4096);
	CU_ASSERT(iovs[2].iov_base == (void *)((uintptr_t)buf + 16 * 1024));
	CU_ASSERT(iovs[2].iov_len == 2048);

	/* test case 4: not enough iovs */
	len = 12 * 1024;
	for (i = 0; i < 6; i++) {
		sgl[0].unkeyed.type = SPDK_NVME_SGL_TYPE_DATA_BLOCK;
		sgl[0].unkeyed.length = 2048;
		sgl[0].address = (uint64_t)(uintptr_t)buf + i * 4096;
	}

	cmd.dptr.sgl1.unkeyed.type = SPDK_NVME_SGL_TYPE_LAST_SEGMENT;
	cmd.dptr.sgl1.unkeyed.length = 6 * sizeof(*sgl);
	cmd.dptr.sgl1.address = (uint64_t)(uintptr_t)sgls;

	ret = nvme_cmd_map_sgls(NULL, &cmd, iovs, 4, len, mps, gpa_to_vva);
	CU_ASSERT(ret == -ERANGE);

	spdk_free(buf);
	spdk_free(sgls);
}

int main(int argc, char **argv)
{
	CU_pSuite	suite = NULL;
	unsigned int	num_failures;

	CU_set_error_action(CUEA_ABORT);
	CU_initialize_registry();

	suite = CU_add_suite("vfio_user", NULL, NULL);

	CU_ADD_TEST(suite, test_nvme_cmd_map_prps);
	CU_ADD_TEST(suite, test_nvme_cmd_map_sgls);

	CU_basic_set_mode(CU_BRM_VERBOSE);
	CU_basic_run_tests();
	num_failures = CU_get_number_of_failures();
	CU_cleanup_registry();
	return num_failures;
}
