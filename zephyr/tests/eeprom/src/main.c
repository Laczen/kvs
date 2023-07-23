#include <zephyr/kernel.h>
#include "zephyr/ztest.h"
#include <zephyr/logging/log.h>
#include <zephyr/subsys/kvs.h>

LOG_MODULE_REGISTER(kvs_test);

ZTEST_SUITE(kvs_tests, NULL, NULL, NULL, NULL, NULL);

void report_kvs(struct kvs *kvs)
{
	struct kvs_data *data = kvs->data;
	const struct kvs_cfg *cfg = kvs->cfg;

	LOG_INF("KVS data: pos %d bend %d wrapcnt %d", data->pos, data->bend,
		data->wrapcnt);
	LOG_INF("KVS cfg: bsz %d bcnt %d bspr %d psz %d", cfg->bsz, cfg->bcnt,
		cfg->bspr, cfg->psz);
	
}

ZTEST(kvs_tests, a_kvs_mount)
{
	struct kvs *kvs = GET_KVS(DT_NODELABEL(kvs_storage));
	int rc;

	(void)kvs_unmount(kvs);
	rc = kvs_erase(kvs);
	zassert_false(rc != 0, "erase failed [%d]", rc);
	rc = kvs_mount(kvs);
	zassert_false(rc != 0, "mount failed [%d]", rc);
	rc = kvs_unmount(kvs);
	zassert_false(rc != 0, "unmount failed [%d]", rc);
	
	report_kvs(kvs);
	rc = kvs_unmount(kvs);
	zassert_true(rc == 0, "unmount failed [%d]", rc);
}

ZTEST(kvs_tests, b_kvs_rw)
{
	struct kvs *kvs = GET_KVS(DT_NODELABEL(kvs_storage));
	uint32_t cnt, cnt1, cn, rd_cnt;
	int rc;

	(void)kvs_unmount(kvs);
	rc = kvs_mount(kvs);
	zassert_false(rc != 0, "mount failed [%d]", rc);

	cnt = 0U;
	rc = kvs_write(kvs, "/cnt", &cnt, sizeof(cnt));
	zassert_false(rc != 0, "write failed [%d]", rc);

	rd_cnt = 255U;
	rc = kvs_read(kvs, "/cnt", &rd_cnt, sizeof(rd_cnt));
	zassert_false(rc != 0, "read failed [%d]", rc);
	zassert_false(rd_cnt != cnt, "wrong read value");

	cnt1 = 1U;
	rc = kvs_write(kvs, "/cnt1", &cnt1, sizeof(cnt1));
	zassert_false(rc != 0, "write failed [%d]", rc);

	rd_cnt = 255U;
	rc = kvs_read(kvs, "/cnt1", &rd_cnt, sizeof(rd_cnt));
	zassert_false(rc != 0, "read failed [%d]", rc);
	zassert_false(rd_cnt != cnt1, "wrong read value");

	cn = 2U;
	rc = kvs_write(kvs, "/cn", &cn, sizeof(cn));
	zassert_false(rc != 0, "write failed [%d]", rc);

	rd_cnt = 255U;
	rc = kvs_read(kvs, "/cn", &rd_cnt, sizeof(rd_cnt));
	zassert_false(rc != 0, "read failed [%d]", rc);
	zassert_false(rd_cnt != cn, "wrong read value");

	rd_cnt = 255U;
	rc = kvs_read(kvs, "/cnt", &rd_cnt, sizeof(rd_cnt));
	zassert_false(rc != 0, "read failed [%d]", rc);
	zassert_false(rd_cnt != cnt, "wrong read value");

	rd_cnt = 255U;
	rc = kvs_read(kvs, "/cnt1", &rd_cnt, sizeof(rd_cnt));
	zassert_false(rc != 0, "read failed [%d]", rc);
	zassert_false(rd_cnt != cnt1, "wrong read value");

	report_kvs(kvs);
	rc = kvs_unmount(kvs);
	zassert_true(rc == 0, "unmount failed [%d]", rc);
}

ZTEST(kvs_tests, c_kvs_remount)
{
	struct kvs *kvs = GET_KVS(DT_NODELABEL(kvs_storage));
	uint32_t cnt, pos, bend, wrapcnt;
	int rc;

	(void)kvs_unmount(kvs);
	rc = kvs_mount(kvs);
	zassert_false(rc != 0, "mount failed [%d]", rc);

	cnt = 0U;
	rc = kvs_write(kvs, "/cnt", &cnt, sizeof(cnt));
	zassert_false(rc != 0, "write failed [%d]", rc);

	pos = kvs->data->pos;
	bend = kvs->data->bend;
	wrapcnt = kvs->data->wrapcnt;

	rc = kvs_unmount(kvs);
	zassert_true(rc == 0, "unmount failed [%d]", rc);

	rc = kvs_mount(kvs);
	zassert_false(rc != 0, "mount failed [%d]", rc);
	zassert_false(pos != kvs->data->pos, "wrong kvs->data->pos");
	zassert_false(bend != kvs->data->bend, "wrong kvs->data->bend");
	zassert_false(wrapcnt != kvs->data->wrapcnt, "wrong kvs->data->wrapcnt");

	report_kvs(kvs);
	rc = kvs_unmount(kvs);
	zassert_true(rc == 0, "unmount failed [%d]", rc);
}

int kvs_walk_test_cb(struct kvs_ent *ent, void *cb_arg)
{
	uint32_t *cnt = (uint32_t *)cb_arg;

	(*cnt) += 1;
	return 0;
}

int kvs_walk_unique_test_cb(struct kvs_ent *ent, void *cb_arg)
{
	uint32_t *value = (uint32_t *)cb_arg;

	return kvs_entry_read(ent, entry_get_klen(ent), value, sizeof(uint32_t));
}

ZTEST(kvs_tests, d_kvs_walk)
{
	struct kvs *kvs = GET_KVS(DT_NODELABEL(kvs_storage));
	uint32_t cnt, en_cnt;
	int rc;

	(void)kvs_unmount(kvs);
	rc = kvs_mount(kvs);
	zassert_false(rc != 0, "mount failed [%d]", rc);
	
	/*
	 * write one entry "/wlk_tst", walk searching for "/wlk_tst" and
	 * count appearances, this should be one
	 */
	cnt = 0U;
	rc = kvs_write(kvs, "/wlk_tst", &cnt, sizeof(cnt));
	zassert_false(rc != 0, "write failed [%d]", rc);
	en_cnt = 0U;
	rc = kvs_walk(kvs, "/wlk_tst", kvs_walk_test_cb, (void *)&en_cnt);
	zassert_false(rc != 0, "walk failed [%d]", rc);
	zassert_false(en_cnt != 1U, "wrong walk result value [%d]", en_cnt);

	/* walk again searching for "/wlk" and count appearances, this should be
	 * one again.
	 */
	en_cnt = 0U;
	rc = kvs_walk(kvs, "/wlk", kvs_walk_test_cb, (void *)&en_cnt);
	zassert_false(rc != 0, "walk failed [%d]", rc);
	zassert_false(en_cnt != 1U, "wrong walk result value");
	/*
	 * write another entry "/wlk_tst", walk searching for "/wlk_tst" and
	 * count appearances, this should now be two
	 */
	cnt++;
	rc = kvs_write(kvs, "/wlk_tst", &cnt, sizeof(cnt));
	zassert_false(rc != 0, "write failed [%d]", rc);
	en_cnt = 0U;
	rc = kvs_walk(kvs, "/wlk_tst", kvs_walk_test_cb, (void *)&en_cnt);
	zassert_false(rc != 0, "walk failed [%d]", rc);
	zassert_false(en_cnt != 2U, "wrong walk result value");

	/* walk_unique searching for "/wlk_tst" and get the value */
	rc = kvs_walk_unique(kvs, "/wlk_tst", kvs_walk_unique_test_cb,
			     (void *)&en_cnt);
	zassert_false(rc != 0, "walk failed [%d]", rc);
	zassert_false(en_cnt != cnt, "wrong walk result value");

	report_kvs(kvs);
	rc = kvs_unmount(kvs);
	zassert_true(rc == 0, "unmount failed [%d]", rc);
}

ZTEST(kvs_tests, e_kvs_compact)
{
	struct kvs *kvs = GET_KVS(DT_NODELABEL(kvs_storage));
	uint32_t cnt, rd_cnt;
	int rc;

	(void)kvs_unmount(kvs);
	rc = kvs_mount(kvs);
	zassert_false(rc != 0, "mount failed [%d]", rc);

	cnt = 0U;
	rc = kvs_write(kvs, "/cnt0", &cnt, sizeof(cnt));
	zassert_false(rc != 0, "write failed [%d]", rc);
	cnt++;
	rc = kvs_write(kvs, "/cnt", &cnt, sizeof(cnt));
	zassert_false(rc != 0, "write failed [%d]", rc);
	cnt++;
	rc = kvs_write(kvs, "/cnt1", &cnt, sizeof(cnt));
	zassert_false(rc != 0, "write failed [%d]", rc);
	
	for (int i = 0; i < kvs->cfg->bcnt; i++) {
		rc = kvs_read(kvs, "/cnt", &rd_cnt, sizeof(rd_cnt));
		zassert_false(rc != 0, "read failed [%d]", rc);

		rc = kvs_read(kvs, "/cnt0", &rd_cnt, sizeof(rd_cnt));
		zassert_false(rc != 0, "read failed [%d]", rc);

		rc = kvs_read(kvs, "/cnt1", &rd_cnt, sizeof(rd_cnt));
		zassert_false(rc != 0, "read failed [%d]", rc);

		rc = kvs_compact(kvs);
		zassert_false(rc != 0, "compact failed [%d]", rc);
	}

	rc = kvs_delete(kvs, "/cnt0");
	zassert_false(rc != 0, "delete failed [%d]", rc);

	for (int i = 0; i < kvs->cfg->bcnt; i++) {
		rc = kvs_read(kvs, "/cnt", &rd_cnt, sizeof(rd_cnt));
		zassert_false(rc != 0, "read failed [%d]", rc);

		rc = kvs_read(kvs, "/cnt1", &rd_cnt, sizeof(rd_cnt));
		zassert_false(rc != 0, "read failed [%d]", rc);

		rc = kvs_read(kvs, "/cnt0", &rd_cnt, sizeof(rd_cnt));
		zassert_false(rc == 0, "read succeeded on deleted item [%d]", rc);

		rc = kvs_compact(kvs);
		zassert_false(rc != 0, "compact failed [%d]", rc);
	}

	report_kvs(kvs);
	rc = kvs_unmount(kvs);
	zassert_true(rc == 0, "unmount failed [%d]", rc);
}

ZTEST(kvs_tests, f_kvs_gc)
{
	struct kvs *kvs = GET_KVS(DT_NODELABEL(kvs_storage));
	const size_t bsz = kvs->cfg->bsz;
	const uint32_t gc_trigger = bsz * (kvs->cfg->bcnt - kvs->cfg->bspr);
	uint32_t cnt, rdcnt, pos;
	int rc;

	(void)kvs_unmount(kvs);
	rc = kvs_erase(kvs);
	zassert_false(rc != 0, "erase failed [%d]", rc);
	rc = kvs_mount(kvs);
	zassert_false(rc != 0, "mount failed [%d]", rc);

	cnt = 0U;
	rc = kvs_write(kvs, "/bas", &cnt, sizeof(cnt));
	zassert_false(rc != 0, "write failed [%d]", rc);

	while (kvs->data->pos < gc_trigger) {
		cnt++;
		rc = kvs_write(kvs, "/cnt", &cnt, sizeof(cnt));
		zassert_false(rc != 0, "write failed [%d]", rc);
	}

	rc = kvs_read(kvs, "/cnt", &rdcnt, sizeof(rdcnt));
	zassert_false(rc != 0, "/cnt read failed [%d]", rc);
	zassert_false(rdcnt != cnt, "/cnt bad read value [%d] != [%d]", rdcnt,
		      cnt);

	rc = kvs_read(kvs, "/bas", &rdcnt, sizeof(rdcnt));
	zassert_false(rc != 0, "/bas read failed [%d]", rc);
	zassert_false(rdcnt != 0U, "/bas bad read value [%d] != [%d]", rdcnt,
		      cnt);
	
	rc = kvs_delete(kvs, "/bas");
	zassert_false(rc != 0, "/bas delete failed [%d]", rc);
	rc = kvs_read(kvs, "/bas", &rdcnt, sizeof(rdcnt));
	zassert_false(rc == 0, "/bas read succeeded on deleted item");

	pos = 0U;
	while ((kvs->data->pos + pos) < (2 * gc_trigger)) {
		uint32_t wrapcnt = kvs->data->wrapcnt;

		cnt++;
		rc = kvs_write(kvs, "/cnt", &cnt, sizeof(cnt));
		zassert_false(rc != 0, "write failed [%d]", rc);
		if (wrapcnt != kvs->data->wrapcnt) {
			pos += kvs->cfg->bsz * kvs->cfg->bcnt; 
		}

	}

	rc = kvs_read(kvs, "/bas", &rdcnt, sizeof(rdcnt));
	zassert_false(rc == 0, "read succeeded on deleted item");

	rc = kvs_read(kvs, "/cnt", &rdcnt, sizeof(rdcnt));
	zassert_false(rc != 0, "/cnt read failed [%d]", rc);
	zassert_false(rdcnt != cnt, "/cnt bad read value [%d] != [%d]", rdcnt,
		      cnt);

	report_kvs(kvs);
	rc = kvs_unmount(kvs);
	zassert_true(rc == 0, "unmount failed [%d]", rc);
}

ZTEST(kvs_tests, g_recovery)
{
	struct kvs *kvs = GET_KVS(DT_NODELABEL(kvs_storage));
	const size_t bsz = kvs->cfg->bsz;
	const uint32_t gc_trigger = bsz * (kvs->cfg->bcnt - kvs->cfg->bspr);
 	uint32_t cnt, rdcnt;
	size_t bufsize, cntwrtsize;
 	int rc;

 	(void)kvs_unmount(kvs);
 	rc = kvs_erase(kvs);
 	zassert_false(rc != 0, "erase failed [%d]", rc);
 	rc = kvs_mount(kvs);
	zassert_false(rc != 0, "mount failed [%d]", rc);

	cnt = 0U;
	rc = kvs_write(kvs, "/bas", &cnt, sizeof(cnt));
	zassert_false(rc != 0, "write failed [%d]", rc);
	bufsize = kvs->data->pos;
	rc = kvs_write(kvs, "/cnt", &cnt, sizeof(cnt));
	cntwrtsize = kvs->data->pos - bufsize;

	while ((kvs->data->pos + cntwrtsize) < gc_trigger) {
		cnt++;
		rc = kvs_write(kvs, "/cnt", &cnt, sizeof(cnt));
		zassert_false(rc != 0, "write failed [%d]", rc);
	}

	rc = kvs_read(kvs, "/cnt", &rdcnt, sizeof(rdcnt));
	zassert_false(rc != 0, "read failed [%d]", rc);
	zassert_false(rdcnt != cnt, "bad read value [%d] != [%d]", rdcnt, cnt);

	/* Simulate a bad gc write by writing part of the first entry */
	uint8_t bad[bufsize - kvs->cfg->psz];

	rc = kvs->cfg->read(kvs->cfg->ctx, 0, bad, sizeof(bad));
	zassert_false(rc != 0, "read failed [%d]", rc);
	rc = kvs->cfg->prog(kvs->cfg->ctx, gc_trigger, bad, sizeof(bad));
	zassert_false(rc != 0, "write failed [%d]", rc);
	
	(void)kvs_unmount(kvs);
	rc = kvs_mount(kvs);
	zassert_false(rc != 0, "mount failed [%d]", rc);

	rc = kvs_read(kvs, "/cnt", &rdcnt, sizeof(rdcnt));
	zassert_false(rc != 0, "read failed [%d]", rc);
	zassert_false(rdcnt != cnt, "bad /cnt read value [%d] != [%d]", rdcnt,
		      cnt);
	rc = kvs_read(kvs, "/bas", &rdcnt, sizeof(rdcnt));
	zassert_false(rc != 0, "read failed [%d]", rc);
	zassert_false(rdcnt != 0U, "bad /bas read value [%d] != [%d]", rdcnt,
	              0U);
}
