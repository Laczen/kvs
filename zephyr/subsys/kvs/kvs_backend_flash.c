/*
 * Copyright (c) 2023 Laczen
 *
 * KVS flash backend definition
 * 
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <errno.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/subsys/kvs.h>

#define LOG_LEVEL CONFIG_KVS_BACKEND_FLASH_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(kvs_backend_flash);

struct kvs_be_flash {
	const struct device *const dev;
	const off_t off;
	const size_t size;
	const size_t free;
	const size_t blsize;
	struct k_sem *sem;
};

static int kvs_be_flash_read(const void *ctx, uint32_t off, void *data,
			     size_t len)
{
	const struct kvs_be_flash *be = (const struct kvs_be_flash *)ctx;
	const uint32_t rdoff = be->off + off;
	int rc;

	if ((off + len) > be->size) {
		LOG_ERR("read out of bounds [%x - %d]", off, len);
		rc = -EINVAL;
		goto end;
	}

	rc = flash_read(be->dev, rdoff, data, len);
end:
	LOG_DBG("read %d bytes at %x [%d]", len, rdoff, rc);
	return rc;
}

static int kvs_be_flash_prog(const void *ctx, uint32_t off, const void *data,
			     size_t len)
{
	struct kvs_be_flash *be = (struct kvs_be_flash *)ctx;
	const uint32_t wroff = be->off + off;
	struct flash_pages_info fp_info;
	int rc;

	if ((off + len) > be->size) {
		LOG_ERR("prog out of bounds [%x - %d]", off, len);
		rc = -EINVAL;
		goto end;
	}

	if ((off % be->blsize) == 0U) {
		rc = flash_get_page_info_by_offs(be->dev, wroff, &fp_info);
		if (rc) {
			LOG_ERR("failed to get page info");
			goto end;
		}

		if (fp_info.start_offset == wroff) {
			size_t esize = MAX(fp_info.size, be->blsize);
			rc = flash_erase(be->dev, wroff, esize);
			if (rc) {
				LOG_ERR("failed to erase %d bytes at %x",
					esize, wroff);
				goto end;
			}

			LOG_DBG("erased %d bytes at %x [%d]", esize, wroff, rc);

		}

	}

	rc = flash_write(be->dev, wroff, data, len);
end:
	LOG_DBG("prog %d bytes at %x [%d]", len, wroff, rc);
	return rc;
}

static int kvs_be_flash_comp(const void *ctx, uint32_t off, const void *data,
			     size_t len)
{
	const struct kvs_be_flash *be = (const struct kvs_be_flash *)ctx;
	const uint32_t rdoff = be->off + off;
	const uint8_t *data8 = (const uint8_t *)data;
	size_t cmplen = len;
	uint8_t buf[32];
	int rc;

	while (cmplen != 0) {
		uint32_t rdlen = MIN(cmplen, sizeof(buf));
		
		rc = kvs_be_flash_read(ctx, off, buf, rdlen);
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

static int kvs_be_flash_sync(const void *ctx, uint32_t off)
{
	return 0;
}

static int kvs_be_flash_lock(const void *ctx)
{
	const struct kvs_be_flash *be = (const struct kvs_be_flash *)ctx;
		
	if (IS_ENABLED(CONFIG_MULTITHREADING)) {
		return k_sem_take(be->sem, K_FOREVER);
	}

	return 0;
}

static int kvs_be_flash_unlock(const void *ctx)
{
	const struct kvs_be_flash *be = (const struct kvs_be_flash *)ctx;

	if (IS_ENABLED(CONFIG_MULTITHREADING)) {
		k_sem_give(be->sem);
	}

	return 0;
}

static int kvs_be_flash_init(const void *ctx)
{
	const struct kvs_be_flash *be = (const struct kvs_be_flash *)ctx;
	int rc = 0;
	off_t eboff = 0;
	size_t ebmin = be->size;
	size_t ebmax = 0U;
	struct flash_pages_info ebinfo;

	rc = kvs_be_flash_lock(ctx);
	if (rc != 0) {
		goto end;
	}

	while (eboff < be->size) {
		rc = flash_get_page_info_by_offs(be->dev, be->off + eboff,
						 &ebinfo);
		if (rc != 0) {
			LOG_ERR("failed to get page info");
			goto end;
		}

		if (ebinfo.start_offset != (be->off + eboff)) {
			LOG_ERR("partition is not aligned to erase-block-size");
			rc = -EINVAL;
			goto end;
		}

		if (ebinfo.size < ebmin) {
			ebmin = ebinfo.size;
		}

		if (ebinfo.size > ebmax) {
			ebmax = ebinfo.size;
		}

		eboff += ebinfo.size;
	}

	if (ebmax > be->free) {
		LOG_ERR("insufficient free space");
		rc = -EINVAL;
	}

end:
	(void)kvs_be_flash_unlock(ctx);
	LOG_DBG("backend init [%d]", rc);
	return rc;
}

static int kvs_be_flash_release(const void *ctx)
{
	return 0;
}

#define KVS_PART(inst) DT_PHANDLE(inst, partition)
#define KVS_FLASHCTRL(inst) DT_GPARENT(KVS_PART(inst))
#define KVS_MTD(inst) DT_MTD_FROM_FIXED_PARTITION(KVS_PART(inst))
#define KVS_DEV(inst) DEVICE_DT_GET(KVS_MTD(inst))
#define KVS_SIZE(inst) DT_REG_SIZE(KVS_PART(inst))
#define KVS_OFF(inst) DT_REG_ADDR(KVS_PART(inst))
#define KVS_BLSIZE(inst) DT_PROP(inst, block_size)
#define KVS_FSIZE(inst)								\
	COND_CODE_1(DT_NODE_HAS_PROP(inst, free_size),				\
		    (DT_PROP(inst, free_size)), (DT_PROP(inst, block_size)))
#define KVS_BCNT(inst) KVS_SIZE(inst)/KVS_BLSIZE(inst)
#define KVS_FBCNT(inst) KVS_FSIZE(inst)/KVS_BLSIZE(inst)
#define KVS_PBUFSIZE(inst)							\
	COND_CODE_1(DT_NODE_HAS_PROP(KVS_FLASHCTRL(inst), write_block_size),	\
		    (DT_PROP(KVS_FLASHCTRL(inst), write_block_size)), (8))

#define KVS_CHECK_BLSIZE(inst)							\
	BUILD_ASSERT((KVS_BLSIZE(inst) & (KVS_BLSIZE(inst) - 1)) == 0,		\
		     "Block size not a power of 2")
#define KVS_CHECK_SCNT(inst)							\
	BUILD_ASSERT((KVS_SIZE(inst) % KVS_BLSIZE(inst)) == 0,			\
		     "Partition size not a multiple of block size")
#define KVS_CHECK_FSCNT(inst)							\
	BUILD_ASSERT((KVS_FSIZE(inst) % KVS_BLSIZE(inst)) == 0,			\
		     "Free size not a multiple of block size")

#define KVS_FLASH_DEFINE(inst)							\
	KVS_CHECK_BLSIZE(inst);							\
	KVS_CHECK_SCNT(inst);							\
	KVS_CHECK_FSCNT(inst);							\
	K_SEM_DEFINE(kvs_be_flash_sem_##inst, 1, 1);				\
	const struct kvs_be_flash kvs_be_flash_##inst = {			\
		.dev = KVS_DEV(inst),						\
		.off = KVS_OFF(inst),						\
		.size = KVS_SIZE(inst),						\
		.free = KVS_FSIZE(inst),					\
		.blsize = KVS_BLSIZE(inst),					\
		.sem = &kvs_be_flash_sem_##inst,				\
	};									\
	uint8_t kvs_be_flash_pbuf_##inst[KVS_PBUFSIZE(inst)];			\
	const char kvs_be_flash_cookie_##inst[] = "Zephyr-KVS";			\
	DEFINE_KVS(								\
		inst, &kvs_be_flash_##inst, KVS_BLSIZE(inst), KVS_BCNT(inst),	\
		KVS_FBCNT(inst), (void *)&kvs_be_flash_pbuf_##inst,		\
		KVS_PBUFSIZE(inst), kvs_be_flash_read, kvs_be_flash_prog,	\
		kvs_be_flash_comp, kvs_be_flash_sync, kvs_be_flash_init,	\
		kvs_be_flash_release, kvs_be_flash_lock, kvs_be_flash_unlock,	\
		(void *)&kvs_be_flash_cookie_##inst,				\
		sizeof(kvs_be_flash_cookie_##inst) - 1				\
	);
	
DT_FOREACH_STATUS_OKAY(zephyr_kvs_flash, KVS_FLASH_DEFINE)