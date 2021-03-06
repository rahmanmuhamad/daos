/**
 * (C) Copyright 2016 Intel Corporation.
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
 * This file is part of dsm
 *
 * vos/tests/vts_pool.c
 *
 * Author: Vishwanath Venkatesan <vishwanath.venaktesan@intel.com>
 */
#define DDSUBSYS	DDFAC(tests)

#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <vts_common.h>

#include <vos_layout.h>
#include <daos_srv/vos.h>

struct vp_test_args {
	char			**fname;
	int			nfiles;
	int			*seq_cnt;
	enum vts_ops_type	**ops_seq;
	bool			*fcreate;
	daos_handle_t		*poh;
	uuid_t			*uuid;
};

static inline void
pool_set_param(enum vts_ops_type seq[], int cnt, bool flag, bool *cflag,
	       int *seq_cnt, enum vts_ops_type **ops)
{
	*seq_cnt = cnt;
	memcpy(*ops, seq, cnt * sizeof(enum vts_ops_type));
	*cflag = flag;
}

static int
pool_ref_count_setup(void **state)
{
	struct vp_test_args	*arg = *state;
	int			ret = 0;

	arg->fname = (char **) malloc(sizeof(char *));
	assert_ptr_not_equal(arg->fname, NULL);

	arg->poh = (daos_handle_t *)malloc(10 * sizeof(daos_handle_t));
	assert_ptr_not_equal(arg->poh, NULL);

	ret = vts_alloc_gen_fname(&arg->fname[0]);
	assert_int_equal(ret, 0);
	return 0;
}

static void
pool_ref_count_test(void **state)
{
	int			ret = 0, i;
	uuid_t			uuid;
	struct vp_test_args	*arg = *state;
	int			num = 10;

	uuid_generate(uuid);
	ret = vos_pool_create(arg->fname[0], uuid, VPOOL_16M);
	for (i = 0; i < num; i++) {
		ret = vos_pool_open(arg->fname[0], uuid,
				    &arg->poh[i]);
		assert_int_equal(ret, 0);
	}
	for (i = 0; i < num - 1; i++) {
		ret = vos_pool_close(arg->poh[i]);
		assert_int_equal(ret, 0);
	}
	ret = vos_pool_destroy(arg->fname[0], uuid);
	assert_int_equal(ret, -DER_BUSY);
	ret = vos_pool_close(arg->poh[num - 1]);
	assert_int_equal(ret, 0);
	ret = vos_pool_destroy(arg->fname[0], uuid);
	assert_int_equal(ret, 0);
}

static int
pool_ref_count_destroy(void **state)
{
	struct vp_test_args	*arg = *state;

	if (arg->fname[0]) {
		remove(arg->fname[0]);
		free(arg->fname[0]);
	}
	free(arg->fname);
	return 0;
}


static void
pool_ops_run(void **state)
{
	int			ret = 0, i, j;
	struct vp_test_args	*arg = *state;
	vos_pool_info_t		pinfo;


	for (j = 0; j < arg->nfiles; j++) {
		for (i = 0; i < arg->seq_cnt[j]; i++) {
			switch (arg->ops_seq[j][i]) {
			case CREAT:
				uuid_generate(arg->uuid[j]);
				if (arg->fcreate[j]) {
					ret =
					vts_pool_fallocate(&arg->fname[j]);
					assert_int_equal(ret, 0);
					ret = vos_pool_create(arg->fname[j],
							      arg->uuid[j],
							      0);
				} else {
					ret =
					vts_alloc_gen_fname(&arg->fname[j]);
					assert_int_equal(ret, 0);
					ret = vos_pool_create(arg->fname[j],
							      arg->uuid[j],
							      VPOOL_16M);
				}
				break;
			case OPEN:
				ret = vos_pool_open(arg->fname[j],
						    arg->uuid[j],
						    &arg->poh[j]);
				break;
			case CLOSE:
				ret = vos_pool_close(arg->poh[j]);
			break;
			case DESTROY:
				ret = vos_pool_destroy(arg->fname[j],
						       arg->uuid[j]);
				break;
			case QUERY:
				ret = vos_pool_query(arg->poh[j], &pinfo);
				assert_int_equal(ret, 0);
				assert_int_equal(pinfo.pif_cont_nr, 0);
				assert_false(pinfo.pif_size != VPOOL_16M);
				assert_false(pinfo.pif_avail !=
				     (VPOOL_16M - sizeof(struct vos_pool_df)));
				break;
			default:
				fail_msg("Shoudln't be here Unkown ops?\n");
				break;
			}
			if (arg->ops_seq[j][i] != QUERY)
				assert_int_equal(ret, 0);
		}
	}
}

static int pool_allocate_params(int nfiles, int ops,
			   struct vp_test_args *test_args)
{
	int i;

	test_args->nfiles = nfiles;
	test_args->fname = (char **)malloc(nfiles * sizeof(char *));
	assert_ptr_not_equal(test_args->fname, NULL);

	test_args->seq_cnt = (int *)malloc(nfiles * sizeof(int));
	assert_ptr_not_equal(test_args->seq_cnt, NULL);

	test_args->ops_seq = (enum vts_ops_type **)malloc
		(nfiles * sizeof(enum vts_ops_type *));
	assert_ptr_not_equal(test_args->ops_seq, NULL);

	for (i = 0; i < nfiles; i++) {
		test_args->ops_seq[i] = (enum vts_ops_type *)malloc
			(ops * sizeof(enum vts_ops_type));
		assert_ptr_not_equal(test_args->ops_seq[i], NULL);
	}

	test_args->fcreate = (bool *)malloc(nfiles * sizeof(bool));
	assert_ptr_not_equal(test_args->fcreate, NULL);

	test_args->poh = (daos_handle_t *)malloc(nfiles *
						 sizeof(daos_handle_t));
	assert_ptr_not_equal(test_args->poh, NULL);


	test_args->uuid = (uuid_t *)malloc(nfiles * sizeof(uuid_t));
	assert_ptr_not_equal(test_args->uuid, NULL);

	return 0;
}

static int
setup(void **state)
{
	struct vp_test_args *test_arg = NULL;

	test_arg = malloc(sizeof(struct vp_test_args));
	assert_ptr_not_equal(test_arg, NULL);
	*state = test_arg;

	return 0;
}

static int
teardown(void **state)
{
	struct vp_test_args	*arg = *state;

	free(arg);
	return 0;
}

/**
 * Common teardown for all unit tests
 */
static int
pool_unit_teardown(void **state)
{
	struct vp_test_args	*arg = *state;
	int			i;

	for (i = 0; i < arg->nfiles; i++) {
		if (vts_file_exists(arg->fname[i]))
			remove(arg->fname[i]);
		if (arg->fname[i])
			free(arg->fname[i]);
		if (arg->ops_seq[i])
			free(arg->ops_seq[i]);
	}

	if (arg->fname)
		free(arg->fname);
	if (arg->seq_cnt)
		free(arg->seq_cnt);
	if (arg->ops_seq)
		free(arg->ops_seq);
	if (arg->fcreate)
		free(arg->fcreate);
	if (arg->poh)
		free(arg->poh);
	if (arg->uuid)
		free(arg->uuid);

	return 0;
}

/**
 * Setups for different unit Tests
 */
static inline void
create_pools_test_construct(struct vp_test_args **arr,
			    bool create_type)
{
	struct vp_test_args	*arg   = *arr;
	enum vts_ops_type	tmp[]  = {CREAT};
	int			i, nfiles;

	/** Create number of files as CPUs */
	nfiles = sysconf(_SC_NPROCESSORS_ONLN);
	pool_allocate_params(nfiles, 1, arg);
	arg->nfiles = nfiles;
	print_message("Pool construct test with %d files\n",
		      nfiles);
	for (i = 0; i < nfiles; i++)
		pool_set_param(tmp, 1, create_type, &arg->fcreate[i],
			  &arg->seq_cnt[i], &arg->ops_seq[i]);

}

static int
pool_create_empty(void **state)
{
	struct vp_test_args	*arg   = *state;

	create_pools_test_construct(&arg, false);
	return 0;
}

static int
pool_create_exists(void **state)
{
	struct vp_test_args *arg = *state;

	/** This test fallocates */
	create_pools_test_construct(&arg, true);
	return 0;
}


static int
pool_open_close(void **state)
{
	struct vp_test_args	*arg = *state;
	enum vts_ops_type	tmp[] = {CREAT, OPEN, CLOSE};

	pool_allocate_params(1, 3, arg);
	pool_set_param(tmp, 3, true, &arg->fcreate[0],
		  &arg->seq_cnt[0], &arg->ops_seq[0]);

	return 0;
}

static int
pool_destroy(void **state)
{
	struct vp_test_args	*arg = *state;
	enum vts_ops_type	tmp[] = {CREAT, DESTROY};

	pool_allocate_params(1, 2, arg);
	pool_set_param(tmp, 2, true, &arg->fcreate[0],
		  &arg->seq_cnt[0], &arg->ops_seq[0]);
	return 0;
}

static int
pool_query_after_open(void **state)
{
	struct vp_test_args	*arg = *state;
	enum vts_ops_type	tmp[] = {CREAT, OPEN, QUERY, CLOSE};

	pool_allocate_params(1, 4, arg);
	pool_set_param(tmp, 4, true, &arg->fcreate[0],
		  &arg->seq_cnt[0], &arg->ops_seq[0]);
	return 0;
}

static int
pool_all_empty_file(void **state)
{
	struct vp_test_args	*arg = *state;
	enum vts_ops_type	tmp[] = {CREAT, OPEN, QUERY,
					 CLOSE, DESTROY};

	pool_allocate_params(1, 5, arg);
	pool_set_param(tmp, 5, false, &arg->fcreate[0],
		  &arg->seq_cnt[0], &arg->ops_seq[0]);
	return 0;

}

static int
pool_all(void **state)
{
	struct vp_test_args	*arg = *state;
	enum vts_ops_type	tmp[] = {CREAT, OPEN, QUERY,
					 CLOSE, DESTROY};

	pool_allocate_params(1, 5, arg);
	pool_set_param(tmp, 5, true, &arg->fcreate[0],
		  &arg->seq_cnt[0], &arg->ops_seq[0]);
	return 0;
}

static const struct CMUnitTest pool_tests[] = {
	{ "VOS1: Create Pool with existing files (File Count no:of cpus)",
		pool_ops_run, pool_create_exists, pool_unit_teardown},
	{ "VOS2: Create Pool with empty files (File Count no:of cpus)",
		pool_ops_run, pool_create_empty, pool_unit_teardown},
	{ "VOS3: Pool Destroy", pool_ops_run,
		pool_destroy, pool_unit_teardown},
	{ "VOS5: Pool Close after open", pool_ops_run,
		pool_open_close, pool_unit_teardown},
	{ "VOS6: Pool handle refcount", pool_ref_count_test,
		 pool_ref_count_setup, pool_ref_count_destroy},
	{ "VOS7: Pool Query after open", pool_ops_run,
		pool_query_after_open, pool_unit_teardown},
	{ "VOS8: Pool all APIs empty file handle", pool_ops_run,
		pool_all_empty_file, pool_unit_teardown},
	{ "VOS9: Pool all APIs with existing file", pool_ops_run,
		pool_all, pool_unit_teardown},
};


int
run_pool_test(void)
{
	return cmocka_run_group_tests_name("VOS Pool tests", pool_tests,
					   setup, teardown);
}
