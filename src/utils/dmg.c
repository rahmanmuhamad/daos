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
 * dmg(8): DAOS Management Utility
 */
#define DDSUBSYS	DDFAC(utils)

#include <getopt.h>
#include <stdio.h>
#include <daos.h>
#include <daos/common.h>
#include <daos/object.h>
#include <daos/mgmt.h>

const unsigned int	 default_mode = 0731;
const char		*default_size = "256M";
const char		*default_group;
const unsigned int	 default_svc_nreplicas = 1;

const int max_svc_nreplicas = 13;

typedef int (*command_hdlr_t)(int, char *[]);

daos_size_t
tobytes(const char *str)
{
	daos_size_t	 size;
	char		*end;

	size = strtoull(str, &end, 0);

	/** no suffix used */
	if (*end == '\0')
		return size;

	/** let's be permissive and allow MB, Mb, mb ...*/
	if (*(end + 1) != '\0' &&
	    ((*(end + 1) != 'b' && *(end + 1) != 'B') || (*(end + 2) != '\0')))
		return 0;

	switch (*end) {
	case 'b':
	case 'B':
		break;
	case 'k':
	case 'K':
		size <<= 10;
		break;
	case 'm':
	case 'M':
		size <<= 20;
		break;
	case 'g':
	case 'G':
		size <<= 30;
		break;
	case 't':
	case 'T':
		size <<= 40;
		break;
	case 'p':
	case 'P':
		size <<= 50;
		break;
	case 'e':
	case 'E':
		size <<= 60;
		break;
	default:
		return 0;
	}

	return size;
}

static int
create_hdlr(int argc, char *argv[])
{
	struct option		options[] = {
		{"gid",		required_argument,	NULL,	'g'},
		{"group",	required_argument,	NULL,	'G'},
		{"mode",	required_argument,	NULL,	'm'},
		{"size",	required_argument,	NULL,	's'},
		{"target",	required_argument,	NULL,	't'},
		{"svcn",	required_argument,	NULL,	'v'},
		{"uid",		required_argument,	NULL,	'u'},
		{NULL,		0,			NULL,	0}
	};
	unsigned int		mode = default_mode;
	unsigned int		uid = geteuid();
	unsigned int		gid = getegid();
	daos_size_t		size = tobytes(default_size);
	const char	       *group = default_group;
	const char	       *targets_str = NULL;
	d_rank_list_t	       *targets = NULL;
	d_rank_t		ranks[max_svc_nreplicas];
	d_rank_list_t		svc = {};
	uuid_t			pool_uuid;
	int			i;
	int			rc;

	memset(ranks, 0, sizeof(ranks));
	svc.rl_ranks = ranks;
	svc.rl_nr.num = default_svc_nreplicas;

	while ((rc = getopt_long(argc, argv, "", options, NULL)) != -1) {
		switch (rc) {
		case 'g':
			gid = atoi(optarg);
			break;
		case 'G':
			group = optarg;
			break;
		case 'm':
			mode = strtoul(optarg, NULL /* endptr */, 0 /* base */);
			break;
		case 's':
			size = tobytes(optarg);
			if (size == 0) {
				fprintf(stderr, "Invalid size: %s\n", optarg);
				return 2;
			}
			break;
		case 't':
			targets_str = optarg;
			break;
		case 'u':
			gid = atoi(optarg);
			break;
		case 'v':
			svc.rl_nr.num = atoi(optarg);
			break;
		default:
			return 2;
		}
	}

	if (targets_str != NULL) {
		targets = daos_rank_list_parse(targets_str, ":");
		if (targets == NULL) {
			fprintf(stderr, "failed to parse target ranks\n");
			return 2;
		}
	}

	if (svc.rl_nr.num < 1 || svc.rl_nr.num > ARRAY_SIZE(ranks)) {
		fprintf(stderr, "--svcn must be in [1, %lu]\n",
			ARRAY_SIZE(ranks));
		if (targets != NULL)
			daos_rank_list_free(targets);
		return 2;
	}

	rc = daos_pool_create(mode, uid, gid, group, targets, "pmem", size,
			      &svc, pool_uuid, NULL /* ev */);
	if (targets != NULL)
		daos_rank_list_free(targets);
	if (rc != 0) {
		fprintf(stderr, "failed to create pool: %d\n", rc);
		return rc;
	}

	/* Print the pool UUID. */
	printf(DF_UUIDF" ", DP_UUID(pool_uuid));
	/* Print the pool service replica ranks. */
	for (i = 0; i < svc.rl_nr.num_out - 1; i++)
		printf("%u:", svc.rl_ranks[i]);
	printf("%u\n", svc.rl_ranks[svc.rl_nr.num_out - 1]);

	return 0;
}

static int
destroy_hdlr(int argc, char *argv[])
{
	struct option		options[] = {
		{"force",	no_argument,		NULL,	'f'},
		{"group",	required_argument,	NULL,	'G'},
		{"pool",	required_argument,	NULL,	'p'},
		{NULL,		0,			NULL,	0}
	};
	const char	       *group = default_group;
	uuid_t			pool_uuid;
	int			force = 0;
	int			rc;

	uuid_clear(pool_uuid);

	while ((rc = getopt_long(argc, argv, "", options, NULL)) != -1) {
		switch (rc) {
		case 'f':
			force = 1;
			break;
		case 'G':
			group = optarg;
			break;
		case 'p':
			if (uuid_parse(optarg, pool_uuid) != 0) {
				fprintf(stderr,
					"failed to parse pool UUID: %s\n",
					optarg);
				return 2;
			}
			break;
		default:
			return 2;
		}
	}

	if (uuid_is_null(pool_uuid)) {
		fprintf(stderr, "pool UUID required\n");
		return 2;
	}

	rc = daos_pool_destroy(pool_uuid, group, force, NULL /* ev */);
	if (rc != 0) {
		fprintf(stderr, "failed to destroy pool: %d\n", rc);
		return rc;
	}

	return 0;
}

enum pool_op {
	POOL_EVICT,
	POOL_EXCLUDE,
	POOL_QUERY
};

static enum pool_op
pool_op_parse(const char *str)
{
	if (strcmp(str, "evict") == 0)
		return POOL_EVICT;
	else if (strcmp(str, "exclude") == 0)
		return POOL_EXCLUDE;
	else if (strcmp(str, "query") == 0)
		return POOL_QUERY;
	assert(0);
	return -1;
}

/* For operations that take <pool_uuid, pool_group, pool_svc_ranks>. */
static int
pool_op_hdlr(int argc, char *argv[])
{
	struct option		options[] = {
		{"group",	required_argument,	NULL,	'G'},
		{"pool",	required_argument,	NULL,	'p'},
		{"svc",		required_argument,	NULL,	'v'},
		{"target",	required_argument,	NULL,	't'},
		{NULL,		0,			NULL,	0}
	};
	const char	       *group = default_group;
	uuid_t			pool_uuid;
	d_rank_t		target = -1;
	daos_handle_t		pool;
	const char	       *svc_str = NULL;
	d_rank_list_t       *svc;
	enum pool_op		op = pool_op_parse(argv[1]);
	int			rc;

	uuid_clear(pool_uuid);

	while ((rc = getopt_long(argc, argv, "", options, NULL)) != -1) {
		switch (rc) {
		case 'G':
			group = optarg;
			break;
		case 't':
			target = atoi(optarg);
			break;
		case 'p':
			if (uuid_parse(optarg, pool_uuid) != 0) {
				fprintf(stderr,
					"failed to parse pool UUID: %s\n",
					optarg);
				return 2;
			}
			break;
		case 'v':
			svc_str = optarg;
			break;
		default:
			return 2;
		}
	}

	/* Check the pool UUID. */
	if (uuid_is_null(pool_uuid)) {
		fprintf(stderr, "pool UUID required\n");
		return 2;
	}
	/* Check the pool service ranks. */
	if (svc_str == NULL) {
		fprintf(stderr, "--svc must be specified\n");
		return 2;
	}
	svc = daos_rank_list_parse(svc_str, ":");
	if (svc == NULL) {
		fprintf(stderr, "failed to parse service ranks\n");
		return 2;
	}
	if (svc->rl_nr.num == 0) {
		fprintf(stderr, "--svc mustn't be empty\n");
		daos_rank_list_free(svc);
		return 2;
	}
	/* Check the target rank for POOL_EXCLUDE. */
	if (target == -1 && op == POOL_EXCLUDE) {
		fprintf(stderr, "valid target rank required\n");
		daos_rank_list_free(svc);
		return 2;
	}

	/* Make a pool connection for operations that need one. */
	if (op == POOL_QUERY) {
		rc = daos_pool_connect(pool_uuid, group, svc, DAOS_PC_RO, &pool,
				       NULL /* info */, NULL /* ev */);
		daos_rank_list_free(svc);
		if (rc != 0) {
			fprintf(stderr, "failed to connect to pool: %d\n", rc);
			return rc;
		}
	}

	/* Do the operation. */
	if (op == POOL_EVICT) {
		rc = daos_pool_evict(pool_uuid, group, svc, NULL /* ev */);
		daos_rank_list_free(svc);
		if (rc != 0) {
			fprintf(stderr, "failed to evict pool connections: "
				"%d\n", rc);
			return rc;
		}
	} else if (op == POOL_EXCLUDE) {
		d_rank_list_t targets;

		memset(&targets, 0, sizeof(targets));
		targets.rl_nr.num = 1;
		targets.rl_nr.num_out = 0;
		targets.rl_ranks = &target;

		rc = daos_pool_exclude(pool_uuid, group, svc, &targets,
				       NULL /* ev */);
		daos_rank_list_free(svc);
		if (rc != 0) {
			fprintf(stderr, "failed to exclude target: %d\n", rc);
			return rc;
		}
	} else if (op == POOL_QUERY) {
		daos_pool_info_t		pinfo;
		struct daos_rebuild_status     *rstat = &pinfo.pi_rebuild_st;

		rc = daos_pool_query(pool, NULL, &pinfo, NULL);
		if (rc != 0) {
			fprintf(stderr, "pool query failed: %d\n", rc);
			return rc;
		}
		D__PRINT("Pool "DF_UUIDF", ntarget=%u, disabled=%u\n",
			DP_UUID(pinfo.pi_uuid), pinfo.pi_ntargets,
			pinfo.pi_ndisabled);

		if (rstat->rs_errno == 0) {
			char	*sstr;

			if (rstat->rs_version == 0)
				sstr = "idle";
			else if (rstat->rs_done)
				sstr = "done";
			else
				sstr = "busy";

			D__PRINT("Rebuild %s, "DF_U64" objs, "DF_U64" recs\n",
				sstr, rstat->rs_obj_nr, rstat->rs_rec_nr);
		} else {
			D__PRINT("Rebuild failed, rc=%d, status=%d\n",
				rc, rstat->rs_errno);
		}
	}

	/* Disconnect from the pool for operations that need a connection. */
	if (op == POOL_QUERY) {
		rc = daos_pool_disconnect(pool, NULL /* ev */);
		if (rc != 0) {
			fprintf(stderr, "failed to disconnect from pool: %d\n",
				rc);
			return rc;
		}
	}

	return 0;
}

static int
kill_hdlr(int argc, char *argv[])
{
	struct option		options[] = {
		{"group",	required_argument,	NULL,	'G'},
		{"force",	0,			NULL,	'f'},
		{"rank",	required_argument,	NULL,	'r'},
		{NULL,		0,			NULL,	0}
	};
	const char	       *group = default_group;
	bool			force = false;
	d_rank_t		rank = -1;
	int			rc;

	while ((rc = getopt_long(argc, argv, "", options, NULL)) != -1) {
		switch (rc) {
		case 'G':
			group = optarg;
			break;
		case 'f':
			force = true;
			break;
		case 'r':
			rank = atoi(optarg);
			break;
		default:
			return 2;
		}
	}

	if (rank == (d_rank_t)-1) {
		fprintf(stderr, "valid target rank required\n");
		return 2;
	}

	rc = daos_mgmt_svc_rip(group, rank, force, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to kill rank: %d\n", rank);
		return rc;
	}

	return 0;
}

/* oid str: oid_hi.oid_lo */
static int
daos_obj_id_parse(const char *oid_str, daos_obj_id_t *oid)
{
	const char *ptr = oid_str;

	/* parse hi */
	oid->hi = atoll(ptr);

	/* find 2nd . to parse lo */
	ptr = strchr(ptr, '.');
	if (ptr == NULL)
		return -1;
	ptr++;

	oid->lo = atoll(ptr);

	return 0;
}

static int
obj_op_hdlr(int argc, char *argv[])
{
	struct option		options[] = {
		{"pool",	required_argument,	NULL,	'p'},
		{"cont",	required_argument,	NULL,	'c'},
		{"oid",		required_argument,	NULL,	'o'},
		{"svc",		required_argument,	NULL,	's'},
		{NULL,		0,			NULL,	0}
	};
	const char	       *group = default_group;
	uuid_t			pool_uuid;
	uuid_t			cont_uuid;
	daos_handle_t		poh;
	daos_handle_t		coh;
	const char	       *svc_str = NULL;
	const char	       *oid_str = NULL;
	d_rank_list_t       *svc;
	daos_obj_id_t		oid;
	struct daos_obj_layout *layout;
	int			i;
	int			j;
	int			rc;
	int			ret;

	while ((rc = getopt_long(argc, argv, "", options, NULL)) != -1) {
		switch (rc) {
		case 'p':
			if (uuid_parse(optarg, pool_uuid) != 0) {
				fprintf(stderr,
					"failed to parse pool UUID: %s\n",
					optarg);
				return 2;
			}
			break;
		case 'c':
			if (uuid_parse(optarg, cont_uuid) != 0) {
				fprintf(stderr,
					"failed to parse cont UUID: %s\n",
					optarg);
			}
			break;
		case 's':
			svc_str = optarg;
			break;
		case 'o':
			oid_str = optarg;
			break;
		default:
			return 2;
		}
	}

	if (uuid_is_null(pool_uuid) || uuid_is_null(cont_uuid)) {
		fprintf(stderr, "pool and cont UUID required\n");
		return 2;
	}

	if (oid_str == NULL) {
		fprintf(stderr, "--oid must be specified\n");
		return 2;
	}

	rc = daos_obj_id_parse(oid_str, &oid);
	if (rc) {
		fprintf(stderr, "oid should be oid.hi.oid.mid.oid_lo\n");
		return rc;
	}

	if (svc_str == NULL) {
		fprintf(stderr, "--svc must be specified\n");
		return 2;
	}
	svc = daos_rank_list_parse(svc_str, ":");
	if (svc == NULL) {
		fprintf(stderr, "failed to parse service ranks\n");
		return 2;
	}
	if (svc->rl_nr.num == 0) {
		fprintf(stderr, "--svc mustn't be empty\n");
		daos_rank_list_free(svc);
		return 2;
	}

	rc = daos_pool_connect(pool_uuid, group, svc, DAOS_PC_RO,
			       &poh, NULL /* info */, NULL /* ev */);
	daos_rank_list_free(svc);
	if (rc) {
		fprintf(stderr, "failed to connect to pool: %d\n", rc);
		return rc;
	}

	rc = daos_cont_open(poh, cont_uuid, DAOS_COO_RO, &coh, NULL, NULL);
	if (rc) {
		fprintf(stderr, "daos_cont_open failed, rc: %d\n", rc);
		D__GOTO(disconnect, rc);
	}

	rc = daos_obj_layout_get(coh, oid, &layout);
	if (rc) {
		fprintf(stderr, "daos_cont_open failed, rc: %d\n", rc);
		D__GOTO(close, rc);
	}

	/* Print the object layout */
	fprintf(stdout, "oid: "DF_OID" ver %d grp_nr: %d\n", DP_OID(oid),
		layout->ol_ver, layout->ol_nr);

	for (i = 0; i < layout->ol_nr; i++) {
		struct daos_obj_shard *shard;

		shard = layout->ol_shards[i];
		fprintf(stdout, "grp: %d\n", i);
		for (j = 0; j < shard->os_replica_nr; j++)
			fprintf(stdout, "replica %d %d\n", j,
				shard->os_ranks[j]);
	}

	daos_obj_layout_free(layout);
close:
	ret = daos_cont_close(coh, NULL);
	if (ret != 0) {
		fprintf(stderr, "failed to disconnect from pool: %d\n", ret);
		if (rc == 0)
			rc = ret;
	}
disconnect:
	ret = daos_pool_disconnect(poh, NULL /* ev */);
	if (ret != 0) {
		fprintf(stderr, "failed to disconnect from pool: %d\n", ret);
		if (rc == 0)
			rc = ret;
	}

	return rc;
}

static int
help_hdlr(int argc, char *argv[])
{
	printf("\
usage: dmg COMMAND [OPTIONS]\n\
commands:\n\
  create	create a pool\n\
  destroy	destroy a pool\n\
  evict		evict all pool connections to a pool\n\
  exclude	exclude a target from a pool\n\
  kill		kill remote daos server\n\
  layout	get object layout\n\
  help		print this message and exit\n");
	printf("\
create options:\n\
  --gid=GID	pool GID (getegid()) \n\
  --group=STR	pool server process group (\"%s\")\n\
  --mode=MODE	pool mode (%#o)\n\
  --size=BYTES	target size in bytes (%s)\n\
		supports K (KB), M (MB), G (GB), T (TB) and P (PB) suffixes\n\
  --svcn=N	number of pool service replicas (\"%u\")\n\
  --target=RANKS\n\
		pool targets like 0:1:2:3:4 (whole group)\n\
  --uid=UID	pool UID (geteuid())\n", default_group, default_mode,
	       default_size, default_svc_nreplicas);
	printf("\
destroy options:\n\
  --force	destroy the pool even if there are connections\n\
  --group=STR	pool server process group (\"%s\")\n\
  --pool=UUID	pool UUID\n", default_group);
	printf("\
evict options:\n\
  --group=STR	pool server process group (\"%s\")\n\
  --pool=UUID	pool UUID\n\
  --svc=RANKS	pool service replicas like 1:2:3\n", default_group);
	printf("\
exclude options:\n\
  --group=STR	pool server process group (\"%s\")\n\
  --pool=UUID	pool UUID\n\
  --svc=RANKS	pool service replicas like 1:2:3\n\
  --target=RANK	target rank\n", default_group);
	printf("\
kill options:\n\
  --group=STR	pool server process group (\"%s\")\n\
  --force	unclean shutdown\n\
  --rank=INT	rank of the DAOS server to kill\n", default_group);
	printf("\
query options:\n\
  --group=STR	pool server process group (\"%s\")\n\
  --pool=UUID	pool UUID\n\
  --svc=RANKS	pool service replicas like 1:2:3\n", default_group);
	printf("\
query obj layout options: \n\
  --pool=UUID	pool uuid\n\
  --cont=UUID	container uuid\n\
  --oid=oid	object oid.\n");
	return 0;
}

int
main(int argc, char *argv[])
{
	command_hdlr_t		hdlr = NULL;
	int			rc = 0;

	if (argc == 1 || strcmp(argv[1], "help") == 0)
		hdlr = help_hdlr;
	else if (strcmp(argv[1], "create") == 0)
		hdlr = create_hdlr;
	else if (strcmp(argv[1], "destroy") == 0)
		hdlr = destroy_hdlr;
	else if (strcmp(argv[1], "evict") == 0)
		hdlr = pool_op_hdlr;
	else if (strcmp(argv[1], "exclude") == 0)
		hdlr = pool_op_hdlr;
	else if (strcmp(argv[1], "kill") == 0)
		hdlr = kill_hdlr;
	else if (strcmp(argv[1], "query") == 0)
		hdlr = pool_op_hdlr;
	else if (strcmp(argv[1], "layout") == 0)
		hdlr = obj_op_hdlr;

	if (hdlr == NULL || hdlr == help_hdlr) {
		help_hdlr(argc, argv);
		return hdlr == NULL ? 2 : 0;
	}

	rc = daos_init();
	if (rc != 0) {
		fprintf(stderr, "failed to initialize daos: %d\n", rc);
		return 1;
	}

	rc = hdlr(argc, argv);

	daos_fini();

	if (rc < 0) {
		return 1;
	} else if (rc > 0) {
		help_hdlr(argc, argv);
		return 2;
	}

	return 0;
}
