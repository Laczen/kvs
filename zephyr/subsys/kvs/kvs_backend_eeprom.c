/*
 * Copyright (c) 2023 Laczen
 *
 * KVS eeprom backend definition
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/eeprom.h>
#include <zephyr/kernel.h>
#include "kvs/kvs.h"

#define LOG_LEVEL CONFIG_KVS_BACKEND_EEPROM_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(kvs_backend_eeprom);

struct kvs_be_eeprom {
	const struct device *const dev;
	const off_t off;
	const size_t size;
	struct k_sem *sem;
};

static int kvs_be_eeprom_read(const void *ctx, uint32_t off, void *data,
			      size_t len)
{
	const struct kvs_be_eeprom *be = (const struct kvs_be_eeprom *)ctx;
	const uint32_t rdoff = be->off + off;
	int rc;

	if ((off + len) > be->size) {
		LOG_ERR("read out of bounds [%x - %d]", off, len);
		rc = -EINVAL;
		goto end;
	}

	rc = eeprom_read(be->dev, rdoff, data, len);
end:
	LOG_DBG("read %d bytes at %x [%d]", len, rdoff, rc);
	return rc;
}

static int kvs_be_eeprom_prog(const void *ctx, uint32_t off, const void *data,
			      size_t len)
{
	const struct kvs_be_eeprom *be = (const struct kvs_be_eeprom *)ctx;
	const uint32_t wroff = be->off + off;
	int rc;

	if ((off + len) > be->size) {
		LOG_ERR("prog out of bounds [%x - %d]", off, len);
		rc = -EINVAL;
		goto end;
	}

	rc = eeprom_write(be->dev, wroff, data, len);
end:
	LOG_DBG("prog %d bytes at %x [%d]", len, wroff, rc);
	return rc;
}

static int kvs_be_eeprom_comp(const void *ctx, uint32_t off, const void *data,
			      size_t len)
{
	const struct kvs_be_eeprom *be = (const struct kvs_be_eeprom *)ctx;
	const uint32_t rdoff = be->off + off;
	const uint8_t *data8 = (const uint8_t *)data;
	size_t cmplen = len;
	uint8_t buf[32];
	int rc;

	while (cmplen != 0) {
		uint32_t rdlen = MIN(cmplen, sizeof(buf));
		
		rc = kvs_be_eeprom_read(ctx, off, buf, rdlen);
		if (rc != 0) {
			goto end;
		}

		if (memcmp(buf, data8, rdlen) != 0) {
			rc = -EIO;
			goto end;
		}

		cmplen -= rdlen;
		off += rdlen;
		data8 += rdlen;
	}
end:
	LOG_DBG("comp %d bytes at %x [%d]", len, rdoff, rc);
	return rc;
}

static int kvs_be_eeprom_sync(const void *ctx, uint32_t off)
{
	const struct kvs_be_eeprom *be = (const struct kvs_be_eeprom *)ctx;
	const uint32_t wroff = be->off + off; 
	const char end[4] = "\0\0\0\0";
	int rc;

	if (off > be->size) {
		LOG_ERR("sync out of bounds [%x]", off);
		rc = -EINVAL;
		goto end;
	}

	if ((off + sizeof(end)) > be->size) {
		rc = 0;
		goto end;
	}

	rc = eeprom_write(be->dev, wroff, end, 4);
end:
	return rc;
}

static int kvs_be_eeprom_lock(const void *ctx)
{
	const struct kvs_be_eeprom *be = (const struct kvs_be_eeprom *)ctx;

	if (IS_ENABLED(CONFIG_MULTITHREADING)) {
		return k_sem_take(be->sem, K_FOREVER);
	}

	return 0;
}

static int kvs_be_eeprom_unlock(const void *ctx)
{
	const struct kvs_be_eeprom *be = (const struct kvs_be_eeprom *)ctx;
	
	if (IS_ENABLED(CONFIG_MULTITHREADING)) {
		k_sem_give(be->sem);
	}
	
	return 0;
}

static int kvs_be_eeprom_init(const void *ctx)
{
	LOG_DBG("backend init [%d]", 0);
	return 0;
}

static int kvs_be_eeprom_release(const void *ctx)
{
	return 0;
}

#define KVS_DEV(inst) DEVICE_DT_GET(DT_PHANDLE(inst, eeprom))
#define KVS_DEVSIZE(inst) (DT_PROP(DT_PHANDLE(inst, eeprom), size))
#define KVS_DEVOFF(inst) (DT_PROP(inst, eeprom_offset))
#define KVS_MAXSIZE(inst) (KVS_DEVSIZE(inst) - KVS_DEVOFF(inst))
#define KVS_SIZE(inst) COND_CODE_1(DT_NODE_HAS_PROP(inst, size),		\
	(DT_PROP(inst, size)), (KVS_MAXSIZE(inst)))
#define KVS_BLSIZE(inst) (DT_PROP(inst, block_size))
#define KVS_BCNT(inst) KVS_SIZE(inst)/KVS_BLSIZE(inst)

#define KVS_CHECK_DEVSIZE(inst)							\
	BUILD_ASSERT((KVS_DEVOFF(inst) + KVS_SIZE(inst)) <= KVS_DEVSIZE(inst),	\
		     "Bad eeprom-offset and size combination, please review")
#define KVS_CHECK_BLSIZE(inst)							\
	BUILD_ASSERT((KVS_BLSIZE(inst) & (KVS_BLSIZE(inst) - 1)) == 0,		\
		     "Block size not a power of 2")
#define KVS_CHECK_SCNT(inst)							\
	BUILD_ASSERT((KVS_SIZE(inst) % KVS_BLSIZE(inst)) == 0,			\
		     "Partition size not a multiple of block size")

#define KVS_EEPROM_DEFINE(inst)							\
	KVS_CHECK_DEVSIZE(inst);						\
	KVS_CHECK_BLSIZE(inst);							\
	KVS_CHECK_SCNT(inst);							\
	K_SEM_DEFINE(kvs_be_eeprom_sem_##inst, 1, 1);				\
	const struct kvs_be_eeprom kvs_be_eeprom_##inst = {			\
		.dev = KVS_DEV(inst),						\
		.off = KVS_DEVOFF(inst),					\
		.size = KVS_SIZE(inst),						\
		.sem = &kvs_be_eeprom_sem_##inst,				\
	};									\
	const char kvs_be_eeprom_cookie_##inst[] = "Zephyr-KVS";		\
	DEFINE_KVS(								\
		inst, &kvs_be_eeprom_##inst, KVS_BLSIZE(inst), KVS_BCNT(inst),	\
		1, NULL, 1, kvs_be_eeprom_read, kvs_be_eeprom_prog, 		\
		kvs_be_eeprom_comp, kvs_be_eeprom_sync,	kvs_be_eeprom_init,	\
		kvs_be_eeprom_release, kvs_be_eeprom_lock,			\
		kvs_be_eeprom_unlock, (void *)&kvs_be_eeprom_cookie_##inst,	\
		sizeof(kvs_be_eeprom_cookie_##inst) - 1				\
	);
	
DT_FOREACH_STATUS_OKAY(zephyr_kvs_eeprom, KVS_EEPROM_DEFINE)
