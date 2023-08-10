<!--
  Copyright (c) 2022 Laczen

  SPDX-License-Identifier: Apache-2.0
-->
# KVS - key value store for embedded devices

Generic key value store interface to store and retrieve key-value entries on
different kind of memory devices e.g. RAM, FLASH (nor or nand), EEPROM, ...

## Introduction

KVS stores Key-value entries as:

```
  Entry header: the length of the header is 4 byte.
      byte 0: CRC8 over the header
      byte 1: key length (this allows keys of up to 256 chars)
      byte 2-3: value length bytes
  Entry data:
      key bytes (key length)
      value bytes (value length)
      CRC32 over key and value
      fill bytes (for alignment)
```

Entries are written sequentially to blocks that have a configurable size. At
the beginning of each block a special entry is written that has the key size
set to 0 and its data consists of a wrap counter (4 byte) and a cookie. The
wrap counter is increased each time the memory wraps around. The use of the
cookie is left up to the user, it could e.g. by used as means to identify the
key value store or its version.

When a new block is started the key value store verifies whether it needs to
move old entries to keep a copy and does so if required.

 The configurable block size needs to be a power of 2. The block size limits
 the maximum size of an entry as it needs to fit within one block. The block
 size is not limited to an erase block size of the memory device, this allows
 using memory devices with non constant erase block sizes. However in this
 last case careful parameter selection is required to guarantee that there
 will be no loss of data.

 ## documentation

 The API for KVS is documented in the header file [kvs.h](./lib/include/kvs/kvs.h).
