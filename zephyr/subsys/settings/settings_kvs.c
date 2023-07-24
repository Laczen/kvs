/*
 * Copyright (c) 2023 Laczen
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/settings/settings.h>
#include <zephyr/subsys/kvs.h>

#define LOG_LEVEL CONFIG_SETTINGS_KVS_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(settings_kvs);

static int settings_kvs_load(struct settings_store *cs,
			     const struct settings_load_arg *arg);
static int settings_kvs_save(struct settings_store *cs, const char *name,
			     const char *value, size_t val_len);
static void *settings_kvs_storage_get(struct settings_store *cs);

static struct settings_store_itf settings_kvs_itf = {
	.csi_load = settings_kvs_load,
	.csi_save = settings_kvs_save,
	.csi_storage_get = settings_kvs_storage_get
};

struct settings_kvs {
	struct settings_store cf_store;
	struct kvs *cf_kvs;
};

static ssize_t settings_kvs_read_fn(void *item, void *data, size_t len)
{
	struct kvs_ent *ent = (struct kvs_ent *)item;
	ssize_t rc;

        len = MIN(len, entry_get_vlen(ent));
	rc = kvs_entry_read(ent, entry_get_klen(ent), data, len);
        if (rc != 0) {
                return rc;
        }

        return len;
}

int settings_kvs_src(struct settings_kvs *cf)
{
	cf->cf_store.cs_itf = &settings_kvs_itf;
	settings_src_register(&cf->cf_store);

	return 0;
}

int settings_kvs_dst(struct settings_kvs *cf)
{
	cf->cf_store.cs_itf = &settings_kvs_itf;
	settings_dst_register(&cf->cf_store);

	return 0;
}

static int kvs_load_cb(struct kvs_ent *ent, void *cb_arg)
{
        const struct settings_load_arg *arg = (struct settings_load_arg *)cb_arg;
        char name[entry_get_klen(ent) + 1];
        int rc;

        rc = kvs_entry_read(ent, 0, name, entry_get_klen(ent));
        if (rc != 0) {
                /* Continue when a read name read fails */
                rc = 0;
                goto end;
        }

        name[entry_get_klen(ent)]= '\0';
        rc = settings_call_set_handler(name, entry_get_vlen(ent),
			               settings_kvs_read_fn, (void *)ent, arg);
end:
        return rc;
}

static int settings_kvs_load(struct settings_store *cs,
			     const struct settings_load_arg *arg)
{
	struct settings_kvs *cf = CONTAINER_OF(cs, struct settings_kvs,
                                               cf_store);
	const char *subtree = (arg->subtree == NULL) ? "" : arg->subtree;

        return kvs_walk_unique(cf->cf_kvs, subtree, kvs_load_cb, (void *)arg);
}

static int settings_kvs_save(struct settings_store *cs, const char *name,
			     const char *value, size_t val_len)
{
	struct settings_kvs *cf = CONTAINER_OF(cs, struct settings_kvs,
                                               cf_store);

	if (!name) {
		return -EINVAL;
	}

        if ((value == NULL) || (val_len == 0)) {
                return kvs_delete(cf->cf_kvs, name);
        }

        return kvs_write(cf->cf_kvs, name, (const void *)value, val_len);
}

/* Initialize the kvs backend. */
int settings_kvs_backend_init(struct settings_kvs *cf)
{
        int rc;

	rc = kvs_mount(cf->cf_kvs);
	if (rc) {
		return rc;
	}

	LOG_DBG("Initialized");
	return rc;
}

int settings_backend_init(void)
{
	static struct settings_kvs default_settings_kvs;
	int rc;

        default_settings_kvs.cf_kvs = GET_KVS(DT_NODELABEL(kvs_storage));
	rc = settings_kvs_backend_init(&default_settings_kvs);
	if (rc) {
		return rc;
	}

	rc = settings_kvs_src(&default_settings_kvs);

	if (rc) {
		return rc;
	}

	rc = settings_kvs_dst(&default_settings_kvs);

	return rc;
}

static void *settings_kvs_storage_get(struct settings_store *cs)
{
	struct settings_kvs *cf = CONTAINER_OF(cs, struct settings_kvs, 
                                               cf_store);

	return cf->cf_kvs;
}