/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: Copyright (c) 2022 Laczen
 */

/**
 * @defgroup    kvs
 * @{
 * @brief       Key Value Store
 *
 * Generic key value store interface to store and retrieve key-value entries on
 * different kind of memory devices e.g. RAM, FLASH (nor or nand), EEPROM, ...
 *
 * KVS stores Key-value entries as:
 *
 * Entry header: the length of the header is 4 byte.
 *	byte 0: CRC8 over the header
 *	byte 1: key length (this allows keys of up to 256 chars)
 *	byte 2-3: value length bytes
 * Entry data:
 *	key bytes (key length)
 *	value bytes (value length)
 *	CRC32 over key and value
 *fill bytes (for alignment)
 *
 * Entries are written sequentially to blocks that have a configurable size. At
 * the beginning of each block a special entry is written that has the key size
 * set to 0 and its data consists of a wrap counter (4 byte) and a cookie. The
 * wrap counter is increased each time the memory wraps around. The use of the
 * cookie is left up to the user, it could e.g. by used as means to identify the
 * key value store or its version.
 *
 * When a new block is strated the key value store verifies whether it needs to
 * move old entries to keep a copy and does so if required.
 *
 * The configurable block size needs to be a power of 2. The block size limits
 * the maximum size of an entry as it needs to fit within one block. The block
 * size is not limited to an erase block size of the memory device, this allows
 * using memory devices with non constant erase block sizes. However in this
 * last case carefull parameter selection is required to guarantee that there
 * will be no loss of data.
 */

#ifndef KVS_H_
#define KVS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KVS_MIN(a, b)		  (a < b ? a : b)
#define KVS_MAX(a, b)		  (a < b ? b : a)
#define KVS_ALIGNUP(num, align)	  (((num) + ((align) - 1)) & ~((align) - 1))
#define KVS_ALIGNDOWN(num, align) ((num) & ~((align) - 1))

/**
 * @brief KVS interface definition
 *
 */

/**
 * @brief KVS constant values
 *
 */
enum kvs_constants
{
	KVS_FILLCHAR = 0xFF,
	KVS_HDRSIZE = 4,
	KVS_HDRKEYMASK = 0xFF,
	KVS_HDRKEYSHIFT = 0,
	KVS_HDRVALMASK = 0xFFFF,
	KVS_HDRVALSHIFT = 8,
	KVS_HDRCRCMASK = 0xFF,
	KVS_HDRCRCSHIFT = 24,
	KVS_KVCRCSIZE = 4,
	KVS_KVCRCINIT = 0x0,
	KVS_BUFSIZE = 16,
	KVS_WRAPCNTSIZE = 4,
};

/**
 * @brief KVS error codes
 *
 */
enum kvs_error_codes
{
	KVS_ENOENT = 2,		/**< No such entry */
	KVS_EIO = 5,		/**< I/O Error */
	KVS_EAGAIN = 11,	/**< No more contexts */
	KVS_EFAULT = 14,	/**< Bad address */
	KVS_EINVAL = 22,	/**< Invalid argument */
	KVS_ENOSPC = 28,	/**< No space left on device */
	KVS_EDEADLK = 45,	/**< Resource deadlock avoided */
};

/**
 * @brief KVS stop codes
 *
 */
enum kvs_stop_codes
{
	KVS_DONE = 1,	/**< Finished processing */
};

/**
 * @brief KVS entry structure
 *
 */
struct kvs_ent {
	struct kvs *kvs;	/**< pointer to the kvs */
	uint32_t start;		/**< start position of the entry */
	uint32_t next;		/**< position of the next entry */
	uint32_t he_hdr;	/**< hamming encoded header */
};

#define entry_get_klen(ent) ((ent->he_hdr >> KVS_HDRKEYSHIFT) & KVS_HDRKEYMASK)
#define entry_get_vlen(ent) ((ent->he_hdr >> KVS_HDRVALSHIFT) & KVS_HDRVALMASK)

/**
 * @brief KVS memory configuration definition
 *
 * This defines the functions provided to access the memory and os interface
 *
 */

struct kvs_cfg {
	const void *ctx;	/**< opaque context pointer */
	const size_t bsz;	/**< block or sector size (byte), power of 2! */
	const uint32_t bcnt;	/**< block count (including spare blocks) */
	const uint32_t bspr;	/**< spare block count */
	const void *pbuf;	/**< pointer to prog buffer */
	const uint32_t psz;  	/**< prog buffer size (byte), power of 2! */

	/**
	 * @brief read from memory device
	 *
	 * @param[in] ctx pointer to memory context
	 * @param[in] off starting address
	 * @param[in] data pointer to buffer to place read data
	 * @param[in] len number of bytes
	 *
	 * @return 0 on success, -KVS_EIO on error.
	 */
	int (*read)(const void *ctx, uint32_t off, void *data, size_t len);

	/**
	 * @brief program memory device
	 *
	 * REMARK: When writing to a memory device that needs to be erased
	 * before write, the first write to a erase block should wipe (erase)
	 * the block.
	 *
	 * @param[in] ctx pointer to memory context
	 * @param[in] off starting address
	 * @param[in] data pointer to data to be written
	 * @param[in] len number of bytes
	 *
	 * @return 0 on success, -KVS_EIO on error
	 */
	int (*prog)(const void *ctx, uint32_t off, const void *data,
		    size_t len);

	/**
	 * @brief compare data to memory device content (optional)
	 *
	 * @param[in] ctx pointer to memory context
	 * @param[in] off starting address
	 * @param[in] data pointer to data to be compared
	 * @param[in] len number of bytes
	 *
	 * @return 0 on success, -KVS_EIO on error
	 */
	int (*comp)(const void *ctx, uint32_t off, const void *data,
		    size_t len);

	/**
	 * @brief memory device sync
	 *
	 * @param[in] ctx pointer to memory context
	 * @param[in] off next writing address, passed to allow writing a end
	 *                marker to the backend (e.g. for eeprom).
	 *
	 * @return 0 on success, error is propagated to user
	 */
	int (*sync)(const void *ctx, uint32_t off);

	/**
	 * @brief memory device init function
	 *
	 * @param[in] ctx pointer to memory context
	 *
	 * @return 0 on success, error is propagated to user
	 */
	int (*init)(const void *ctx);

	/**
	 * @brief memory device release function
	 *
	 * @param[in] ctx pointer to memory context
	 *
	 * @return 0 on success, error is propagated to user
	 */
	int (*release)(const void *ctx);

	/**
	 * @brief os provided lock function
	 *
	 * @param[in] ctx pointer to memory context
	 *
	 * @return 0 on success, error is propagated to user
	 */
	int (*lock)(const void *ctx);

	/**
	 * @brief os provided unlock function
	 *
	 * @param[in] ctx pointer to memory context
	 *
	 * @return 0 on success, error is ignored
	 */
	int (*unlock)(const void *ctx);
};

/**
 * @brief KVS data structure
 *
 */
struct kvs_data {
	bool ready;		/**< kvs state */
	uint32_t pos;		/**< current memory (write) position */
	uint32_t bend;		/**< current memory (write) block end */
	uint32_t wrapcnt;	/**< current wrap/erase counter */
	void *cookie;		/**< pointer to cookie */
	size_t csz;		/**< cookie size */
};

/**
 * @brief KVS structure
 *
 */
struct kvs {
	const struct kvs_cfg *cfg;
	struct kvs_data *data;
};

/**
 * @brief Helper macro to define a kvs
 *
 */
#define DEFINE_KVS(_name, _ctx, _bsz, _bcnt, _bspr, _pbuf, _psz, _read, _prog, \
		   _comp, _sync, _init, _release, _lock, _unlock, _cookie,     \
		   _csz)						       \
	struct kvs_cfg _name##_cfg = {                                         \
		.ctx = _ctx,                                                   \
		.bsz = _bsz,		                                       \
		.bcnt = _bcnt,                                                 \
		.bspr = _bspr,                                                 \
		.pbuf = _pbuf,                                                 \
		.psz = _psz,                                         	       \
		.read = _read,                                                 \
		.prog = _prog,                                                 \
		.comp = _comp,                                                 \
		.sync = _sync,                                                 \
		.init = _init,                                                 \
		.release = _release,                                           \
		.lock = _lock,                                                 \
		.unlock = _unlock,                                             \
	};                                                                     \
	struct kvs_data _name##_data = {				       \
		.wrapcnt = 0U,						       \
		.cookie = _cookie,					       \
		.csz = _csz,						       \
	};								       \
	struct kvs _name = {                                                   \
		.cfg = &_name##_cfg,                                           \
		.data = &_name##_data,                                         \
	}

/**
 * @brief Helper macro to get a pointer to a KVS structure
 *
 */
#define GET_KVS(_name) &_name

/**
 * @brief mount the key value store
 *
 * @param[in] kvs pointer to key value store
 *
 * @return 0 on success, negative errorcode on error
 */
int kvs_mount(struct kvs *kvs);

/**
 * @brief unmount the key value store
 *
 * @param[in] kvs pointer to key value store
 *
 * @return 0 on success, negative errorcode on error
 */
int kvs_unmount(struct kvs *kvs);

/**
 * @brief erase the key value store, should be called on a unmounted fs.
 *        Overwrites the memory backend with a preset value (KVS_FILLCHAR) so
 *        that it is sure that no data is left in the memory backend.
 *
 * @param[in] kvs pointer to key value store
 *
 * @return 0 on success, negative errorcode on error
 */
int kvs_erase(struct kvs *kvs);

/**
 * @brief compact the key value store (refreshes key value store and minimizes
 *        occupied flash).
 *
 * @param[in] kvs pointer to key value store
 *
 * @return 0 on success, negative errorcode on error
 */
int kvs_compact(const struct kvs *kvs);

/**
 * @brief get a entry from the key value store
 *
 * @param[out] ent pointer to the entry
 * @param[in] kvs pointer to key value store
 * @param[in] key key of the entry
 *
 * @return 0 on success, negative errorcode on error
 */
int kvs_entry_get(struct kvs_ent *ent, const struct kvs *kvs, const char *key);

/**
 * @brief read data from a entry in the kvs at offset
 *
 * @param[in] ent pointer to the entry
 * @param[in] off offset from entry start
 * @param[out] data
 * @param[in] len bytes to read
 *
 * @return 0 on success, negative errorcode on error
 */
int kvs_entry_read(const struct kvs_ent *ent, uint32_t off, void *data,
		   size_t len);

/**
 * @brief read value for a key in the kvs
 *
 * @param[in] kvs pointer to the kvs
 * @param[in] key
 * @param[out] value
 * @param[in] len value length (bytes)
 *
 * @return 0 on success, negative errorcode on error
 */
int kvs_read(const struct kvs *kvs, const char *key, void *value, size_t len);

/**
 * @brief write value for a key in the kvs
 *
 * @param[in] kvs pointer to the kvs
 * @param[in] key
 * @param[in] value
 * @param[in] len value length (bytes)
 *
 * @return 0 on success, negative errorcode on error
 */
int kvs_write(const struct kvs *kvs, const char *key, const void *value,
	      size_t len);

/**
 * @brief delete a key in the kvs
 *
 * @param[in] kvs pointer to the kvs
 * @param[in] key
 *
 * @return 0 on success, negative errorcode on error
 */
int kvs_delete(const struct kvs *kvs, const char *key);

/**
 * @brief walk over entries in kvs and issue a cb for each entry that starts
 *        with the specified key. Walking can be stopped by returning KVS_DONE
 *	  from the callback.
 *
 * @param[in] kvs pointer to the kvs
 * @param[in] key
 * @param[in] cb callback function
 * @param[in] arg callback function argument
 *
 * @return 0 on success, negative errorcode on error
 */
int kvs_walk(const struct kvs *kvs, const char *key,
	     int (*cb)(struct kvs_ent *ent, void *arg), void *arg);

/**
 * @brief walk over entries in kvs and issue a cb for each entry that starts
 *        with the specified key, the cb is only called for the last added
 *	  entry. Walking can be stopped by returning KVS_DONE from the callback.
 *
 * @param[in] kvs pointer to the kvs
 * @param[in] key
 * @param[in] cb callback function
 * @param[in] arg callback function argument
 *
 * @return 0 on success, negative errorcode on error
 */
int kvs_walk_unique(const struct kvs *kvs, const char *key,
		    int (*cb)(struct kvs_ent *ent, void *arg), void *arg);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* KVS_H_ */
