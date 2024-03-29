/*
 * Key Value Store
 *
 * Copyright (c) 2022 Laczen
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "kvs/kvs.h"

#define hdr_get_data(hdr) (hdr & ((KVS_HDRVALMASK << KVS_HDRVALSHIFT) |	       \
				  (KVS_HDRKEYMASK << KVS_HDRKEYSHIFT)))
#define entry_set_len(ent, klen, vlen) (ent->he_hdr =			       \
	((vlen & KVS_HDRVALMASK) << KVS_HDRVALSHIFT) |			       \
	((klen & KVS_HDRKEYMASK) << KVS_HDRKEYSHIFT))

static int kvs_dev_init(const struct kvs *kvs)
{
	const struct kvs_cfg *cfg = kvs->cfg;

	if (cfg->init == NULL) {
		return 0;
	}

	return cfg->init(cfg->ctx);
}

static int kvs_dev_release(const struct kvs *kvs)
{
	const struct kvs_cfg *cfg = kvs->cfg;

	if (cfg->release == NULL) {
		return 0;
	}

	return cfg->release(cfg->ctx);
}

static int kvs_dev_lock(const struct kvs *kvs)
{
	const struct kvs_cfg *cfg = kvs->cfg;

	if (cfg->lock == NULL) {
		return 0;
	}

	return cfg->lock(cfg->ctx);
}

static int kvs_dev_unlock(const struct kvs *kvs)
{
	const struct kvs_cfg *cfg = kvs->cfg;

	if (cfg->unlock == NULL) {
		return 0;
	}

	return cfg->unlock(cfg->ctx);
}

static int kvs_dev_sync(const struct kvs *kvs)
{
	const struct kvs_cfg *cfg = kvs->cfg;

	if (cfg->sync == NULL) {
		return 0;
	}

	return cfg->sync(cfg->ctx, kvs->data->pos);
}

static int kvs_dev_read(const struct kvs *kvs, uint32_t off, void *data,
			size_t len)
{
	const struct kvs_cfg *cfg = kvs->cfg;

	return cfg->read(cfg->ctx, off, data, len);
}

static int kvs_dev_prog(const struct kvs *kvs, uint32_t off, const void *data,
			size_t len)
{
	const struct kvs_cfg *cfg = kvs->cfg;

	return cfg->prog(cfg->ctx, off, data, len);
}

static int kvs_dev_comp(const struct kvs *kvs, uint32_t off, const void *data,
			size_t len)
{
	const struct kvs_cfg *cfg = kvs->cfg;

	if (cfg->comp == NULL) {
		return 0;
	}

	return cfg->comp(cfg->ctx, off, data, len);
}

static uint32_t get_le32(const uint8_t *buf)
{
	return (uint32_t)buf[0] + ((uint32_t)buf[1] << 8) +
	       ((uint32_t)buf[2] << 16) + ((uint32_t)buf[3] << 24);
}

static void put_le32(uint8_t *buf, uint32_t value)
{
	buf[0] = (uint8_t)(value & 0x000000ff);
	buf[1] = (uint8_t)((value & 0x0000ff00) >> 8);
	buf[2] = (uint8_t)((value & 0x00ff0000) >> 16);
	buf[3] = (uint8_t)((value & 0xff000000) >> 24);
}

static uint8_t crc8(uint8_t crc, const void *buf, size_t len)
{
	static const uint8_t kvs_crc8_ccitt_table[16] = {
		0x00, 0x07, 0x0e, 0x09, 0x1c, 0x1b, 0x12, 0x15,
		0x38, 0x3f, 0x36, 0x31, 0x24, 0x23, 0x2a, 0x2d
	};
	const uint8_t *p = buf;
	size_t i;
	
	for (i = 0; i < len; i++) {
		crc ^= p[i];
		crc = (crc << 4) ^ kvs_crc8_ccitt_table[crc >> 4];
		crc = (crc << 4) ^ kvs_crc8_ccitt_table[crc >> 4];
	}

	return crc;
}

static uint32_t crc32(uint32_t crc, const void *buf, size_t len)
{
	const uint8_t *data = (const uint8_t *)buf;
	/* crc table generated from polynomial 0xedb88320 */
	static const uint32_t kvs_crc32_table[16] = {
		0x00000000U, 0x1db71064U, 0x3b6e20c8U, 0x26d930acU,
		0x76dc4190U, 0x6b6b51f4U, 0x4db26158U, 0x5005713cU,
		0xedb88320U, 0xf00f9344U, 0xd6d6a3e8U, 0xcb61b38cU,
		0x9b64c2b0U, 0x86d3d2d4U, 0xa00ae278U, 0xbdbdf21cU,
	};

	crc = ~crc;

	for (size_t i = 0; i < len; i++) {
		uint8_t byte = data[i];

		crc = (crc >> 4) ^ kvs_crc32_table[(crc ^ byte) & 0x0f];
		crc = (crc >> 4) ^ kvs_crc32_table[(crc ^ (byte >> 4)) & 0x0f];
	}

	return (~crc);
}

static int entry_read(const struct kvs_ent *ent, uint32_t off, void *data,
		      size_t len)
{
	const struct kvs *kvs = ent->kvs;

	if ((ent->start + off + len) > ent->next) {
		return -KVS_EINVAL;
	}

	return kvs_dev_read(kvs, ent->start + off, data, len);
}

static int entry_data_read(const struct kvs_ent *ent, uint32_t off, void *data,
			   size_t len)
{
	return entry_read(ent, KVS_HDRSIZE + off, data, len);
}

static int entry_write(const struct kvs_ent *ent, uint32_t off,
		       const void *data, size_t len)
{
	const struct kvs *kvs = ent->kvs;
	const struct kvs_cfg *cfg = kvs->cfg;
	const uint32_t psz = cfg->psz;
	const uint32_t rem = KVS_ALIGNUP(off, psz) - off;
	uint8_t *pbuf8 = (uint8_t *)cfg->pbuf;
	uint8_t *data8 = (uint8_t *)data;
	int rc = 0;

	if ((ent->next - ent->start) < (off + len)) {
		return -KVS_EINVAL;
	}

	if ((data == NULL) || (len == 0U)) {
		return 0;
	}

	off = ent->start + KVS_ALIGNDOWN(off, psz);

	/* fill remaining part of program buffer and write if needed */
	if (rem != 0) {
		const uint32_t wrlen = KVS_MIN(len, rem);
		uint8_t *buf = pbuf8 + (psz - rem);

		memcpy(buf, data8, wrlen);
		if (wrlen == rem) {
			rc = kvs_dev_prog(kvs, off, pbuf8, psz);
			if (rc != 0) {
				goto end;
			}

			rc = kvs_dev_comp(kvs, off, pbuf8, psz);
			if (rc != 0) {
				goto end;
			}

			off += psz;
		}

		data8 += wrlen;
		len -= wrlen;
	}

	/* perform direct write if possible */
	if (len >= psz) {
		uint32_t wrlen = KVS_ALIGNDOWN(len, psz);

		rc = kvs_dev_prog(kvs, off, data8, wrlen);
		if (rc != 0) {
			goto end;
		}

		rc = kvs_dev_comp(kvs, off, data8, wrlen);
		if (rc != 0) {
			goto end;
		}

		data8 += wrlen;
		len -= wrlen;
	}

	/* add remaining part to program buffer */
	if (len != 0U) {
		memcpy(pbuf8, data8, len);
	}

	return 0;
end:
	/* write failure has occured - advance kvs->data->pos to block end */
	kvs->data->pos = kvs->data->bend;
	return rc;
}

static int entry_write_fill(const struct kvs_ent *ent, uint32_t off)
{
	const struct kvs *kvs = ent->kvs;
	const struct kvs_cfg *cfg = kvs->cfg;
	const uint32_t psz = cfg->psz;
	const uint32_t rem = KVS_ALIGNUP(off, psz) - off;
	uint8_t *pbuf8 = (uint8_t *)cfg->pbuf;
	int rc = 0;

	if (rem == 0U) {
		return 0;
	}

	if ((ent->next - ent->start) < off) {
		return -KVS_EINVAL;
	}

	off = ent->start + KVS_ALIGNDOWN(off, psz);
	pbuf8 += (psz - rem);
	memset(pbuf8, KVS_FILLCHAR, rem);
	pbuf8 -= (psz - rem);
	rc = kvs_dev_prog(kvs, off, pbuf8, psz);
	if (rc != 0) {
		goto end;
	}

	return 0;
end:
	/* write failure has occured - advance kvs->data->pos to block end */
	kvs->data->pos = kvs->data->bend;
	return rc;
}


static uint32_t entry_hdr_add_crc(uint32_t d)
{
	uint32_t e = (d & 0xffffff);
	uint8_t crc = crc8(0, &e, 4);

	return d | (crc << 24);
}

/* get lengths from entry header */
static int entry_get_param(struct kvs_ent *ent, const uint8_t *hdr)
{
	const uint32_t psz = ent->kvs->cfg->psz;
	uint32_t he_hdr;
	uint32_t next;
	int rc = 0;

	he_hdr = get_le32(hdr);
	if (he_hdr != entry_hdr_add_crc(hdr_get_data(he_hdr))) {
		rc = -KVS_EINVAL;
		goto end;
	}
	
	ent->he_hdr = he_hdr;
	next = ent->start + KVS_HDRSIZE + KVS_KVCRCSIZE - 1U;
	next += entry_get_klen(ent) + entry_get_vlen(ent);
	next = KVS_ALIGNDOWN(next, psz) + psz;

	if ((next > ent->next) || (next < ent->start)) {
		rc = -KVS_EINVAL;
		goto end;
	}

	ent->next = next;
end:
	return rc;
}

/* get wrap counter from entry (only works when key_len is zero) */
static void entry_get_wrapcnt(const struct kvs_ent *ent, uint32_t *wrapcnt)
{
	uint8_t buf[KVS_WRAPCNTSIZE];

	if (entry_get_klen(ent) != 0U) {
		goto end;
	}

	if (entry_data_read(ent, 0, buf, KVS_WRAPCNTSIZE) != 0) {
		goto end;
	}

	*wrapcnt = get_le32(buf);
end:
}

static bool entry_kvcrc_ok(const struct kvs_ent *ent)
{
	uint32_t kvcrc32 = KVS_KVCRCINIT;
	uint32_t off = 0;
	size_t len =  entry_get_klen(ent) + entry_get_vlen(ent);

	while (len != 0) {
		uint8_t buf[KVS_BUFSIZE];
		size_t rdlen = KVS_MIN(len, sizeof(buf));
		if (entry_data_read(ent, off, buf, rdlen) != 0) {
			goto end;
		}

		kvcrc32 = crc32(kvcrc32, buf, rdlen);
		off += rdlen;
		len -= rdlen;
	}

	uint8_t kvcrcbuf[KVS_KVCRCSIZE];

	if (entry_data_read(ent, off, kvcrcbuf, KVS_KVCRCSIZE) != 0) {
		goto end;
	}

	if (kvcrc32 != get_le32(kvcrcbuf)) {
		goto end;
	}

	return true;
end:
	return false;
}


static int entry_get_info(struct kvs_ent *ent)
{
	const uint32_t bsz = ent->kvs->cfg->bsz;
	uint8_t hdr[KVS_HDRSIZE];

	ent->next = KVS_ALIGNDOWN(ent->start, bsz) + bsz;
	if (entry_read(ent, 0, hdr, sizeof(hdr)) != 0) {
		goto end;
	}

	if (entry_get_param(ent, hdr) != 0) {
		goto end;
	};

	return 0;
end:
	return -KVS_ENOENT;
}

static int entry_set_info(struct kvs_ent *ent, uint8_t *hdr, uint8_t key_len,
			  uint16_t val_len)
{
	const size_t wbs = ent->kvs->cfg->psz;
	struct kvs_data *data = ent->kvs->data;
	uint32_t req_space;
	
	req_space = KVS_HDRSIZE + key_len + val_len + KVS_KVCRCSIZE - 1;
	req_space = KVS_ALIGNDOWN(req_space, wbs) + wbs;
	if (req_space > (data->bend - data->pos)) {
		return -KVS_ENOSPC;
	}

	ent->start = data->pos;
	ent->next = ent->start + req_space;
	entry_set_len(ent, key_len, val_len);
	ent->he_hdr = entry_hdr_add_crc(ent->he_hdr);
	put_le32(hdr, ent->he_hdr);
	data->pos = ent->next;
	return 0;
}

static uint32_t block_advance_n(const struct kvs *kvs, uint32_t pos, uint32_t n)
{
	const size_t bsz = kvs->cfg->bsz;
	const uint32_t end = bsz * kvs->cfg->bcnt;

	for (uint32_t i = 0; i < n; i++) {
		if (pos >= end) {
			pos -= end;
		}

		pos +=bsz;

	}
	
	return pos;
}

static void wblock_advance(const struct kvs *kvs)
{
	const size_t bsz = kvs->cfg->bsz;
	struct kvs_data *data = kvs->data;

	data->bend = block_advance_n(kvs, data->bend, 1);
	data->pos = data->bend - bsz;
	if (data->pos == 0U) {
		data->wrapcnt++;
	}

}

struct read_cb {
	const void *ctx;
	uint32_t off;
	size_t len;
	int (*read)(const void *ctx, uint32_t off, void *data, uint32_t len);
};

static int read_cb_entry(const void *ctx, uint32_t off, void *data,
			 size_t len)
{
	struct kvs_ent *ent = (struct kvs_ent *)(ctx);

	return entry_data_read(ent, off, data, len);
}

static int read_cb_ptr(const void *ctx, uint32_t off, void *data, size_t len)
{
	uint8_t *src = (uint8_t *)ctx;

	memcpy(data, src + off, len);
	return 0;
}

static int entry_write_data(struct kvs_ent *ent, uint32_t dstart,
			    const struct read_cb *drd_cb, uint32_t *crc)
{
	uint32_t len, off;
	uint8_t buf[KVS_BUFSIZE];
	int rc = 0;

	len = drd_cb->len;
	off = 0U;
	while (len != 0) {
		uint32_t rwlen = KVS_MIN(len, sizeof(buf));
		rc = drd_cb->read(drd_cb->ctx, drd_cb->off + off, buf, rwlen);
		if (rc != 0) {
			goto end;
		}

		*crc = crc32(*crc, buf, rwlen);
		rc = entry_write(ent, dstart + off, buf, rwlen);
		if (rc != 0) {
			goto end;
		}

		off += rwlen;
		len -= rwlen;
	}

end:
	return rc;
}

static int entry_write_hdr(struct kvs_ent *ent, uint32_t key_len,
			   uint32_t val_len)
{
	uint8_t hdr[KVS_HDRSIZE];
	int rc;

	rc = entry_set_info(ent, hdr, key_len, val_len);
	if (rc != 0) {
		goto end;
	}

	rc = entry_write(ent, 0, hdr, KVS_HDRSIZE);
end:
	return rc;
}

static int entry_write_crc(struct kvs_ent *ent, uint32_t off, uint32_t crc)
{
	uint8_t buf[KVS_KVCRCSIZE];
	int rc;

	put_le32(buf, crc);
	rc = entry_write(ent, off, buf, KVS_KVCRCSIZE);
	if (rc != 0) {
		goto end;
	}

	off += KVS_KVCRCSIZE;
	rc = entry_write_fill(ent, off);
end:
	return rc;
}

static int kvs_meta_write(const struct kvs *kvs)
{
	if ((kvs->data->pos & (kvs->cfg->bsz - 1)) != 0) {
		return 0;
	}

	struct kvs_ent meta = {
		.kvs = (struct kvs *)kvs,
	};
	uint8_t buf[KVS_BUFSIZE];
	uint32_t off = 0U;
	uint32_t metacrc = KVS_KVCRCINIT;
	int rc;
	
	rc = entry_write_hdr(&meta, 0U, KVS_WRAPCNTSIZE + kvs->data->csz);
	if (rc != 0) {
		goto end;
	}

	off += KVS_HDRSIZE;
	put_le32(buf, kvs->data->wrapcnt);
	metacrc = crc32(metacrc, buf, KVS_WRAPCNTSIZE);
	rc = entry_write(&meta, off, buf, KVS_WRAPCNTSIZE);
	if (rc != 0) {
		goto end;
	}

	off += KVS_WRAPCNTSIZE;
	if ((kvs->data->cookie != NULL) && (kvs->data->csz != 0U)) {
		rc = entry_write(&meta, off, kvs->data->cookie, kvs->data->csz);
		if (rc != 0) {
			goto end;
		}
		metacrc = crc32(metacrc, kvs->data->cookie, kvs->data->csz);
		off += kvs->data->csz;
	}

	rc = entry_write_crc(&meta, off, metacrc);
end:
	return rc;
}

static int entry_append(struct kvs_ent *ent, const struct read_cb *krd_cb,
			const struct read_cb *vrd_cb)
{
	uint32_t off = KVS_HDRSIZE;
	uint32_t crc = KVS_KVCRCINIT;
	int rc;

	rc = kvs_meta_write(ent->kvs);
	if (rc != 0) {
		goto end;
	}

	rc = entry_write_hdr(ent, krd_cb->len, vrd_cb->len);
	if (rc != 0) {
		goto end;
	}

	rc = entry_write_data(ent, off, krd_cb, &crc);
	if (rc != 0) {
		goto end;
	}

	off += entry_get_klen(ent);
	rc = entry_write_data(ent, off, vrd_cb, &crc);
	if (rc != 0) {
		goto end;
	}

	off += entry_get_vlen(ent);
	rc = entry_write_crc(ent, off, crc);
	if (rc != 0) {
		goto end;
	}

	rc = kvs_dev_sync(ent->kvs);
end:
	return rc;
}

static int entry_add(struct kvs_ent *ent, const char *key, const void *value,
		     uint32_t val_len)
{
	const struct read_cb krd_cb = {
		.ctx = (void *)key,
		.off = 0U,
		.len = strlen(key),
		.read = read_cb_ptr,
	};
	const struct read_cb vrd_cb = {
		.ctx = (void *)value,
		.off = 0U,
		.len = val_len,
		.read = read_cb_ptr,
	};

	return entry_append(ent, &krd_cb, &vrd_cb);
}

static bool differ(const struct read_cb *rda, const struct read_cb *rdb)
{
	if (rda->len != rdb->len) {
		goto end;
	}

	uint32_t len = rda->len;
	uint32_t off = 0U;

	while (len != 0U) {
		uint8_t bufa[KVS_BUFSIZE], bufb[KVS_BUFSIZE];
		uint32_t rdlen = KVS_MIN(len, KVS_BUFSIZE);

		if (rda->read(rda->ctx, rda->off + off, bufa, rdlen) != 0) {
			goto end;
		};

		if (rdb->read(rdb->ctx, rdb->off + off, bufb, rdlen) != 0) {
			goto end;
		};

		if (memcmp(bufa, bufb, rdlen) != 0) {
			goto end;
		}

		len -= rdlen;
		off += rdlen;
	}

	return false;
end:
	return true;
}

static int entry_copy(const struct kvs_ent *ent)
{
	const struct read_cb krd_cb = {
		.ctx = (void *)ent,
		.off = 0U,
		.len = entry_get_klen(ent),
		.read = read_cb_entry,
	};
	const struct read_cb vrd_cb = {
		.ctx = (void *)ent,
		.off = entry_get_klen(ent),
		.len = entry_get_vlen(ent),
		.read = read_cb_entry,
	};
	struct kvs_ent cp_ent = {
		.kvs = ent->kvs,
	};

	return entry_append(&cp_ent, &krd_cb, &vrd_cb);
}

struct entry_cb {
	int (*cb)(struct kvs_ent *entry, void *cb_arg);
	void *cb_arg;
};

static int walk(struct kvs_ent *ent, const struct read_cb *rdkey,
		const struct entry_cb *cb, uint32_t stop)
{
	const size_t bsz = ent->kvs->cfg->bsz;
	const uint32_t end = ent->kvs->cfg->bcnt * bsz;
	uint32_t wrapcnt;
	int rc = 0;

	do {
		ent->start = (ent->next < end) ? ent->next : 0U;
		if (ent->start == stop) {
			break;
		}

		if (entry_get_info(ent) != 0) {
		 	continue;
		}

		if ((ent->start & (bsz - 1)) == 0U) {
		 	entry_get_wrapcnt(ent, &wrapcnt);
		 	if ((wrapcnt + 1U) < ent->kvs->data->wrapcnt) {
		 		ent->next = ent->start + bsz;
		 		continue;
		 	}

		}

		const struct read_cb readkey = {
		 	.ctx = (void *)ent,
		 	.off = 0U,
		 	.len = rdkey->len,
		 	.read = read_cb_entry,
		};

		if (differ(&readkey, rdkey)) {
		 	continue;
		}

		if (!entry_kvcrc_ok(ent)) {
		 	continue;
		}

		rc = cb->cb(ent, cb->cb_arg);
		if (rc != 0) {
			break;
		}

	} while (ent->next != stop);

	return rc;
}

struct entry_get_cb_arg {
	struct kvs_ent *ent;
	uint32_t klen;
	bool found;
};

static int entry_get_cb(struct kvs_ent *ent, void *cb_arg)
{
	struct entry_get_cb_arg *rv = (struct entry_get_cb_arg *)cb_arg;

	if (entry_get_klen(ent) == rv->klen) {
		memcpy(rv->ent, ent, sizeof(struct kvs_ent));
		rv->found = true;
	}

	return 0;
}

static int entry_get(struct kvs_ent *ent, const struct read_cb *rdkey)
{
	const struct kvs_cfg *cfg = ent->kvs->cfg;
	const size_t bsz = cfg->bsz;
	const uint32_t bcnt = cfg->bcnt - cfg->bspr;
	struct entry_get_cb_arg cb_arg = {
		.ent = ent,
		.klen = rdkey->len,
		.found = false,
	};
	struct entry_cb cb = {
		.cb = entry_get_cb,
		.cb_arg = (void *)&cb_arg,
	};
	struct kvs_ent wlk = {
		.kvs = ent->kvs,
	};
	uint32_t stop = ent->kvs->data->pos;
	uint32_t start = ent->kvs->data->bend - bsz; 

	for (uint32_t i = 0; i < bcnt; i++) {
		wlk.next = start;
		(void)walk(&wlk, rdkey, &cb, stop);
		if (cb_arg.found) {
			break;
		}
		stop = (start == 0U) ? (cfg->bcnt * cfg->bsz) : start;
		start = stop - bsz;
	}

	if ((!cb_arg.found) || (entry_get_vlen(cb_arg.ent) == 0)) {
		return -KVS_ENOENT;
	}

	return 0;
}

struct entry_dup_cb_arg {
	struct kvs_ent *ent;
	bool duplicate;
};

static int entry_dup_cb(struct kvs_ent *ent, void *cb_arg)
{
	struct entry_dup_cb_arg *dup = (struct entry_dup_cb_arg *)cb_arg;

	if (entry_get_klen(ent) != entry_get_klen(dup->ent)) {
		return 0;
	}

	dup->duplicate = true;
	return KVS_DONE;

}

static int unique_cb(struct kvs_ent *ent, void *cb_arg)
{
	if (entry_get_klen(ent) == 0U) {
		return 0;
	}

	const struct kvs *kvs = ent->kvs;
	const struct entry_cb *cb = (const struct entry_cb *)cb_arg;
	const struct read_cb readkey = {
		.ctx = (void *)ent,
		.off = 0,
		.len = entry_get_klen(ent),
		.read = read_cb_entry,
	};
	struct entry_dup_cb_arg dup_cb_arg = {
		.ent = ent,
		.duplicate = false,
	};
	const struct entry_cb dup_entry_cb = {
		.cb = entry_dup_cb,
		.cb_arg = (void *)&dup_cb_arg,
	};
	struct kvs_ent wlk = {
		.kvs = (struct kvs *)kvs,
		.next = ent->next,
	};

	(void)walk(&wlk, &readkey, &dup_entry_cb, kvs->data->pos);
	if (!dup_cb_arg.duplicate) {
		return cb->cb(ent, cb->cb_arg);
	}

	return 0;
}

static int walk_unique(struct kvs_ent *ent, const struct read_cb *rdkey,
		       const struct entry_cb *cb, uint32_t stop)
{
	const struct entry_cb walk_cb = {
		.cb = unique_cb,
		.cb_arg = (void *)cb,
	};

	return walk(ent, rdkey, &walk_cb, stop);
}

int copy_cb(struct kvs_ent *ent, void *cb_arg)
{
	int rc = 0;

	if ((entry_get_klen(ent) == 0U) || (entry_get_vlen(ent) == 0U)) {
		return 0;
	}

	for (int i = 0; i < ent->kvs->cfg->bspr; i++) {
	 	rc = entry_copy(ent);
	 	if (rc == 0) {
	 		break;
	 	}
	 	wblock_advance(ent->kvs);
	}

	return rc;
}

static int compact(const struct kvs *kvs, uint32_t stop)
{
	const struct read_cb rdkey = {
		.ctx = (void *)NULL,
		.off = 0U,
		.len = 0U,
		.read = read_cb_ptr,
	};
	const struct entry_cb compact_cb = {
		.cb = copy_cb,
	};
		struct kvs_ent wlk = {
		.kvs = (struct kvs *)kvs,
		.next = block_advance_n(kvs, kvs->data->bend, kvs->cfg->bspr),
	};
	int rc;

	wblock_advance(kvs);
	rc = walk_unique(&wlk, &rdkey, &compact_cb, stop);

	return rc == KVS_DONE ? 0 : rc;
}

int kvs_entry_read(const struct kvs_ent *ent, uint32_t off, void *data,
		   size_t len)
{
	if ((ent == NULL) || (ent->kvs == NULL) || (!ent->kvs->data->ready)) {
		return -KVS_EINVAL;
	}

	return entry_data_read(ent, off, data, len);
}

int kvs_entry_get(struct kvs_ent *ent, const struct kvs *kvs, const char *key)
{
	if ((kvs == NULL) || (!kvs->data->ready) || (key == NULL)) {
		return -KVS_EINVAL;
	}

	const struct read_cb krd_cb = {
		.ctx = (void *)key,
		.off = 0U,
		.len = strlen(key),
		.read = read_cb_ptr,
	};

	ent->kvs = (struct kvs *)kvs;

	return entry_get(ent, &krd_cb);
}

int kvs_read(const struct kvs *kvs, const char *key, void *value, size_t len)
{
	struct kvs_ent wlk;
	struct kvs_ent *ent = &wlk;
	int rc;

	rc = kvs_entry_get(ent, kvs, key);
	if (rc != 0) {
		return rc;
	}

	return entry_data_read(ent, entry_get_klen(ent), value, len);
}

int kvs_write(const struct kvs *kvs, const char *key, const void *value,
	      size_t len)
{
	if ((kvs == NULL) || (!kvs->data->ready) || (key == NULL)) {
		return -KVS_EINVAL;
	}

	struct kvs_ent wlk;
	struct kvs_ent *ent = &wlk;

	if (kvs_entry_get(ent, kvs, key) == 0) {
		if (entry_get_vlen(ent) == len) {
			if (len == 0U) {
				return 0;
			}
			const struct read_cb val_rd = {
				.ctx = (void *)value,
				.len = len,
				.off = 0U,
				.read = read_cb_ptr,
			};
			const struct read_cb entval_rd = {
				.ctx = (void *)ent,
				.off = entry_get_klen(ent),
				.len = entry_get_vlen(ent),
				.read = read_cb_entry,
			};

			if (!differ(&val_rd, &entval_rd)) {
		 		return 0;
			}

		}

	}

	uint32_t cnt = kvs->cfg->bcnt;
	int rc;

	rc = kvs_dev_lock(kvs);
	if (rc) {
		return rc;
	}

	while (cnt != 0U) {
		rc = entry_add(ent, key, value, len);
		if (rc == 0) {
			goto end;
		}

		uint32_t stop = block_advance_n(kvs, kvs->data->bend, 
						kvs->cfg->bspr + 1);
		rc = compact(kvs, stop);
		cnt--;
	}

	rc = -KVS_ENOSPC;
end:
	(void)kvs_dev_unlock(kvs);
	return rc;
}

int kvs_delete(const struct kvs *kvs, const char *key)
{
	return kvs_write(kvs, key, NULL, 0);
}

int kvs_walk_unique(const struct kvs *kvs, const char *key,
		    int (*cb)(struct kvs_ent *ent, void *cb_arg), void *cb_arg)
{
	if ((kvs == NULL) || (!kvs->data->ready)) {
		return -KVS_EINVAL;
	}

	const struct read_cb rdkey = {
		.ctx = (void *)key,
		.len = strlen(key),
		.off = 0U,
		.read = read_cb_ptr,
	};
	const struct entry_cb unique_cb = {
		.cb = cb,
		.cb_arg = cb_arg,
	};
	struct kvs_ent wlk = {
		.kvs = (struct kvs *)kvs,
		.next = block_advance_n(kvs, kvs->data->bend, kvs->cfg->bspr),
	};

	return walk_unique(&wlk, &rdkey, &unique_cb, kvs->data->pos);
}

int kvs_walk(const struct kvs *kvs, const char *key,
	     int (*cb)(struct kvs_ent *ent, void *cb_arg), void *cb_arg)
{
	if ((kvs == NULL) || (!kvs->data->ready)) {
		return -KVS_EINVAL;
	}

	const struct read_cb rdkey = {
		.ctx = (void *)key,
		.len = strlen(key),
		.off = 0U,
		.read = read_cb_ptr,
	};
	const struct entry_cb entry_cb = {
		.cb = cb,
		.cb_arg = cb_arg,
	};
	struct kvs_ent wlk = {
		.kvs = (struct kvs *)kvs,
		.next = block_advance_n(kvs, kvs->data->bend, kvs->cfg->bspr),
	};

	return walk(&wlk, &rdkey, &entry_cb, kvs->data->pos);
}

int kvs_compact(const struct kvs *kvs)
{
	if ((kvs == NULL) || (!kvs->data->ready))  {
		return -KVS_EINVAL;
	}

	int rc;

	rc = kvs_dev_lock(kvs);
	if (rc != 0) {
		return rc;
	}

	rc = compact(kvs, kvs->data->bend);
	(void)kvs_dev_unlock(kvs);
	return rc;
}

int recovery_check_cb(struct kvs_ent *ent, void *cb_arg)
{
	const uint32_t pos = ent->kvs->data->pos;
	const size_t bsz = ent->kvs->cfg->bsz;
	bool *recovery_needed = (bool *)cb_arg;

	/* if an item was found that has no duplicate except in the current
	 * sector garbage collection was interrupted and recovery is needed.
	 */
	if (KVS_ALIGNDOWN(ent->start, bsz) != KVS_ALIGNDOWN(pos, bsz)) {
		*recovery_needed = true;
	}

	return 0;
}

int recover(const struct kvs *kvs)
{
	const struct kvs_cfg *cfg = kvs->cfg;
	const struct read_cb rdkey = {
		.ctx = (void *)NULL,
		.off = 0U,
		.len = 0U,
		.read = read_cb_ptr,
	};
	bool recover = false;
	const struct entry_cb recover_cb = {
		.cb = recovery_check_cb,
		.cb_arg = (void *)&recover,
	};
	struct kvs_ent wlk = {
		.kvs = (struct kvs *)kvs,
		.next = block_advance_n(kvs, kvs->data->bend, 
					kvs->cfg->bspr - 1),
	};
	uint32_t stop = block_advance_n(kvs, kvs->data->bend, kvs->cfg->bspr);

	(void)walk_unique(&wlk, &rdkey, &recover_cb, stop);
	if (!recover) {
		goto end;
	}

	/* set back data->bend to the start of the sector */
	kvs->data->bend = KVS_ALIGNDOWN(kvs->data->pos, cfg->bsz);

	return compact(kvs, kvs->data->bend);
end:
	return 0;
}

static void kvs_set_data_bend(const struct kvs *kvs)
{
	const struct kvs_cfg *cfg = kvs->cfg;
	struct kvs_data *data = kvs->data;
	struct kvs_ent ent = {
		.kvs = (struct kvs *)kvs,
	};
	uint32_t wrapcnt;

	data->wrapcnt = 0U;
	data->pos = 0U;
	data->bend = cfg->bsz;
	for (uint32_t i = 0; i < cfg->bcnt; i++) {
		ent.start = i * cfg->bsz;
		if (entry_get_info(&ent) != 0) {
			continue;
		}

		entry_get_wrapcnt(&ent, &wrapcnt);
		if (wrapcnt >= data->wrapcnt) {
			data->wrapcnt = wrapcnt;
			data->pos = ent.start;
			data->bend = ent.start + cfg->bsz;
		}
	}
}

static void kvs_set_data_pos(const struct kvs *kvs)
{
	struct kvs_data *data = kvs->data;
	struct kvs_ent ent = {
		.kvs = (struct kvs *)kvs,
	};

	while (true) {
		ent.start = data->pos;
		if (entry_get_info(&ent) != 0) {
			break;
		}
		data->pos = ent.next;
	}
}

int kvs_mount(struct kvs *kvs)
{
	int rc;

	/* basic config checks */
	if ((kvs == NULL) || (kvs->cfg == NULL)) {
		return -KVS_EINVAL;
	}

	/* read/prog routines check */
	if ((kvs->cfg->read == NULL) || (kvs->cfg->prog == NULL)) {
		return -KVS_EINVAL;
	}

	/* program size nonzero and power of 2 */
	if ((kvs->cfg->psz == 0U) ||
	    ((kvs->cfg->psz & (kvs->cfg->psz - 1)) != 0U)) {
		return -KVS_EINVAL;
	}

	/* block size nonzero and power of 2 */
	if ((kvs->cfg->bsz == 0U) ||
	    ((kvs->cfg->bsz & (kvs->cfg->bsz - 1)) != 0U)) {
		return -KVS_EINVAL;
	}

	/* block size larger than program size */
	if (kvs->cfg->bsz < kvs->cfg->psz) {
		return -KVS_EINVAL;
	}

	if (kvs->data->ready) {
		return -KVS_EAGAIN;
	}

	rc = kvs_dev_init(kvs);
	if (rc != 0) {
		return rc;
	}

	rc = kvs_dev_lock(kvs);
	if (rc != 0) {
		return rc;
	}

	kvs_set_data_bend(kvs);
	kvs_set_data_pos(kvs);

	rc = recover(kvs);
	if (rc!= 0) {
		goto end;
	}

	kvs->data->ready = true;
end:
	return kvs_dev_unlock(kvs);
}

int kvs_unmount(struct kvs *kvs)
{
	if (kvs == NULL) {
		return -KVS_EINVAL;
	}

	int rc;
	
	rc = kvs_dev_init(kvs);
	if (rc != 0) {
		return rc;
	}

	rc = kvs_dev_lock(kvs);
	if (rc != 0) {
		return rc;
	}

	kvs->data->ready = false;
	(void)kvs_dev_unlock(kvs);
	return kvs_dev_release(kvs);
}

int kvs_erase(struct kvs *kvs)
{
	uint8_t fillchar = KVS_FILLCHAR;
	uint32_t off = 0U;
	int rc;

	if (kvs == NULL) {
		return -KVS_EINVAL;
	}

	if (kvs->data->ready) {
		return -KVS_EAGAIN;
	}

	rc = kvs_dev_init(kvs);
	if (rc != 0) {
		return rc;
	}

	rc = kvs_dev_lock(kvs);
	if (rc != 0) {
		return rc;
	}

	uint8_t buf[kvs->cfg->psz];

	memset(buf, fillchar, sizeof(buf));
	while (off < (kvs->cfg->bsz * kvs->cfg->bcnt)) {
		rc = kvs_dev_prog(kvs, off, buf, sizeof(buf));
		if (rc != 0) {
			break;
		}
		off += sizeof(buf);
	}

	(void)kvs_dev_unlock(kvs);
	(void)kvs_dev_release(kvs);
	return rc;
}