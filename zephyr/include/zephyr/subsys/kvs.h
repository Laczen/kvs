#include <zephyr/kernel.h>
#include "kvs/kvs.h"

#define KVS_EXT_DEFINE(inst)                                              \
        extern struct kvs inst;

DT_FOREACH_STATUS_OKAY(zephyr_kvs_flash, KVS_EXT_DEFINE)

DT_FOREACH_STATUS_OKAY(zephyr_kvs_eeprom, KVS_EXT_DEFINE)