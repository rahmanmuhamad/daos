/**
 * (C) Copyright 2017 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * rebuild: rebuild initator
 *
 * This file contains the server API methods and the RPC handlers for rebuild
 * initiator.
 */
#define DDSUBSYS	DDFAC(rebuild)

#include <daos_srv/pool.h>
#include <daos/pool_map.h>
#include <daos/rpc.h>
#include <daos/object.h>
#include <daos/container.h>
#include <daos/pool.h>
#include <daos_srv/container.h>
#include <daos_srv/daos_server.h>
#include <daos_srv/iv.h>
#include <cart/iv.h>
#include "rpc.h"
#include "rebuild_internal.h"
#include "rpc.h"
#include "rebuild_internal.h"

static int
rebuild_iv_alloc_internal(d_sg_list_t *sgl)
{
	int	rc;

	rc = daos_sgl_init(sgl, 1);
	if (rc)
		return rc;

	D_ALLOC(sgl->sg_iovs[0].iov_buf, sizeof(struct rebuild_iv));
	if (sgl->sg_iovs[0].iov_buf == NULL)
		D_GOTO(free, rc = -DER_NOMEM);
	sgl->sg_iovs[0].iov_buf_len = sizeof(struct rebuild_iv);
free:
	if (rc)
		daos_sgl_fini(sgl, true);
	return rc;
}

static int
rebuild_iv_ent_alloc(struct ds_iv_key *iv_key, void *data,
		     d_sg_list_t *sgl)
{
	return rebuild_iv_alloc_internal(sgl);
}

static int
rebuild_iv_ent_get(d_sg_list_t *sgl, struct ds_iv_entry *entry)
{
	if (sgl->sg_iovs != NULL && sgl->sg_iovs[0].iov_buf != NULL)
		return 0;

	return rebuild_iv_alloc_internal(sgl);
}

static int
rebuild_iv_ent_put(d_sg_list_t *sgl, struct ds_iv_entry *entry)
{
	return 0;
}

static int
rebuild_iv_ent_destroy(d_sg_list_t *sgl)
{
	daos_sgl_fini(sgl, true);
	return 0;
}

static int
rebuild_iv_ent_fetch(d_sg_list_t *dst, d_sg_list_t *src)
{
	struct rebuild_iv *src_iv = src->sg_iovs[0].iov_buf;
	struct rebuild_iv *dst_iv = dst->sg_iovs[0].iov_buf;

	if (dst_iv == src_iv)
		return 0;

	D__ASSERT(src_iv != NULL);
	D__ASSERT(dst_iv != NULL);

	dst_iv->riv_master_rank = src_iv->riv_master_rank;
	uuid_copy(dst_iv->riv_pool_uuid, src_iv->riv_pool_uuid);
	D__DEBUG(DB_TRACE, "pool "DF_UUID" rank %d\n",
		 DP_UUID(dst_iv->riv_pool_uuid), src_iv->riv_master_rank);

	return 0;
}

/* Update the rebuild status from leaves to the master */
static int
rebuild_iv_ent_update(d_sg_list_t *dst, d_sg_list_t *src)
{
	struct rebuild_iv *src_iv = src->sg_iovs[0].iov_buf;
	struct rebuild_iv *dst_iv = dst->sg_iovs[0].iov_buf;
	struct rebuild_global_pool_tracker *rgt;
	d_rank_t	  rank;
	int		  rc;

	rc = crt_group_rank(NULL, &rank);
	if (rc)
		return rc;

	D__DEBUG(DB_TRACE, "rank %d master rank %d\n", rank,
		src_iv->riv_master_rank);

	if (rank != src_iv->riv_master_rank)
		return 0;

	dst_iv->riv_master_rank = src_iv->riv_master_rank;
	uuid_copy(dst_iv->riv_pool_uuid, src_iv->riv_pool_uuid);

	/* Gathering the rebuild status here */
	rgt = rebuild_global_pool_tracker_lookup(src_iv->riv_pool_uuid,
						 src_iv->riv_ver);
	if (rgt) {
		/* update the rebuild global status */
		rgt->rgt_status.rs_obj_nr += src_iv->riv_obj_count;
		rgt->rgt_status.rs_rec_nr += src_iv->riv_rec_count;

		rebuild_global_status_update(rgt, src_iv);
		if (rgt->rgt_status.rs_errno == 0)
			rgt->rgt_status.rs_errno = src_iv->riv_status;

		D__DEBUG(DB_TRACE, "update rebuild "DF_UUID" ver %d "
			"obj/rec/global done/status/rank "
			DF_U64"/"DF_U64"/%d/%d/%d\n",
			DP_UUID(rgt->rgt_pool_uuid), rgt->rgt_rebuild_ver,
			rgt->rgt_status.rs_obj_nr, rgt->rgt_status.rs_rec_nr,
			rgt->rgt_status.rs_done, rgt->rgt_status.rs_errno,
			src_iv->riv_rank);
	}

	D__DEBUG(DB_TRACE, "pool "DF_UUID" master_rank %d\n",
		 DP_UUID(dst_iv->riv_pool_uuid), dst_iv->riv_master_rank);

	return 0;
}

/* Distribute the rebuild uuid/master rank from master to leaves */
static int
rebuild_iv_ent_refresh(d_sg_list_t *dst, d_sg_list_t *src)
{
	struct rebuild_iv *dst_iv = dst->sg_iovs[0].iov_buf;
	struct rebuild_iv *src_iv = src->sg_iovs[0].iov_buf;

	uuid_copy(dst_iv->riv_pool_uuid, src_iv->riv_pool_uuid);
	dst_iv->riv_master_rank = src_iv->riv_master_rank;
	dst_iv->riv_global_done = src_iv->riv_global_done;
	dst_iv->riv_global_scan_done = src_iv->riv_global_scan_done;

	if (dst_iv->riv_global_done || dst_iv->riv_global_scan_done) {
		struct rebuild_tgt_pool_tracker *rpt;

		rpt = rebuild_tgt_pool_tracker_lookup(src_iv->riv_pool_uuid,
						      src_iv->riv_ver);
		if (rpt) {
			D__DEBUG(DB_TRACE, DF_UUID" rebuild finished"
				" sgl/gl %d/%d\n",
				 DP_UUID(src_iv->riv_pool_uuid),
				 dst_iv->riv_global_scan_done,
				 dst_iv->riv_global_done);
			rpt->rt_global_done = dst_iv->riv_global_done;
			rpt->rt_global_scan_done = dst_iv->riv_global_scan_done;
		}
	}

	return 0;
}

struct ds_iv_entry_ops rebuild_iv_ops = {
	.iv_ent_alloc	= rebuild_iv_ent_alloc,
	.iv_ent_get	= rebuild_iv_ent_get,
	.iv_ent_put	= rebuild_iv_ent_put,
	.iv_ent_destroy	= rebuild_iv_ent_destroy,
	.iv_ent_fetch	= rebuild_iv_ent_fetch,
	.iv_ent_update	= rebuild_iv_ent_update,
	.iv_ent_refresh = rebuild_iv_ent_refresh,
};

int
rebuild_iv_fetch(void *ns, struct rebuild_iv *rebuild_iv)
{
	d_sg_list_t		sgl;
	daos_iov_t		iov;
	int			rc;

	memset(&sgl, 0, sizeof(sgl));
	memset(&iov, 0, sizeof(iov));
	iov.iov_buf = rebuild_iv;
	iov.iov_len = sizeof(*rebuild_iv);
	iov.iov_buf_len = sizeof(*rebuild_iv);
	sgl.sg_nr.num = 1;
	sgl.sg_iovs = &iov;

	rc = ds_iv_fetch(ns, IV_REBUILD, &sgl);
	if (rc)
		D__ERROR("iv fetch failed %d\n", rc);

	return rc;
}

int
rebuild_iv_update(void *ns, struct rebuild_iv *iv,
		  unsigned int shortcut, unsigned int sync_mode)
{
	d_sg_list_t	sgl;
	daos_iov_t	iov;
	int		rc;

	iov.iov_buf = iv;
	iov.iov_len = sizeof(*iv);
	iov.iov_buf_len = sizeof(*iv);
	sgl.sg_nr.num = 1;
	sgl.sg_nr.num_out = 0;
	sgl.sg_iovs = &iov;
	rc = ds_iv_update(ns, IV_REBUILD, &sgl, shortcut, sync_mode);
	if (rc)
		D__ERROR("iv update failed %d\n", rc);

	return rc;
}
