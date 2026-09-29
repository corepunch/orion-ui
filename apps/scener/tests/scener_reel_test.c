#include "test_framework.h"
#include "scener.h"
#include "reel.h"
#include <unistd.h>

app_state_t *g_app;

#define REEL_TEST_EPSILON 0.0001f
#define REEL_TEST_PATH 1024

static bool reel_test_near(float a, float b) { return fabsf(a - b) < REEL_TEST_EPSILON; }

static float reel_test_eval(reel_t *r, const char *source) {
	reel_expr_t e = {0};
	if (!reel_expr_compile(r, source, &e)) return NAN;
	float v = reel_expr_eval(&e, r);
	reel_expr_free(&e);
	return v;
}

static reel_t *reel_test_write(const char *xml, char *path) {
	const char *dir = getenv("TMPDIR");
	snprintf(path, REEL_TEST_PATH, "%s/scener-reel-XXXXXX", dir && *dir ? dir : ".");
	int fd = mkstemp(path);
	if (fd < 0) return NULL;
	FILE *f = fdopen(fd, "w");
	fputs(xml, f);
	fclose(f);
	return reel_load(path);
}

static void test_reel_expressions(void) {
	TEST("scener reels: expressions compile once and evaluate as functions of t");
	reel_t r = {0};
	r.fps = 24; r.duration = 6; r.width = 1600; r.height = 1000;
	ASSERT_TRUE(reel_test_near(reel_test_eval(&r, "1 + 2 * 3 ^ 2"), 19));
	ASSERT_TRUE(reel_test_near(reel_test_eval(&r, "-2 ^ 2"), -4));
	ASSERT_TRUE(reel_test_near(reel_test_eval(&r, "(1 + 2) * 3 % 4"), 1));
	ASSERT_TRUE(reel_test_near(reel_test_eval(&r, "2 < 3 && 3 <= 3 ? 10 : 20"), 10));
	ASSERT_TRUE(reel_test_near(reel_test_eval(&r, "1 > 2 || !1 ? 10 : 1 == 1 ? 30 : 40"), 30));
	ASSERT_TRUE(reel_test_near(reel_test_eval(&r, "max(1, 7, 3) + min(4, -2)"), 5));
	ASSERT_TRUE(reel_test_near(reel_test_eval(&r, "clamp(5, 0, 2) + mix(10, 20, 0.25)"), 14.5f));
	ASSERT_TRUE(reel_test_near(reel_test_eval(&r, "progress(1.5, 1, 2) + outCubic(1) + smoothstep(0, 1, 0.5)"), 2));
	ASSERT_TRUE(reel_test_near(reel_test_eval(&r, "dist(vec(0, 0, 0), vec(3, 4, 12))"), 13));
	ASSERT_TRUE(reel_test_near(reel_test_eval(&r, "duration * fps / width"), 0.09f));

	reel_expr_t e = {0};
	ASSERT_TRUE(reel_expr_compile(&r, "3 * 4", &e));
	ASSERT_TRUE(e.constant && reel_test_near(e.value, 12));
	ASSERT_TRUE(reel_expr_compile(&r, "sin(t * pi / 2) * 10", &e));
	ASSERT_FALSE(e.constant);
	r.t = 1; ASSERT_TRUE(reel_test_near(reel_expr_eval(&e, &r), 10));
	r.t = 0; ASSERT_TRUE(reel_test_near(reel_expr_eval(&e, &r), 0));
	reel_expr_free(&e);

	ASSERT_FALSE(reel_expr_compile(&r, "tt + 1", &e));
	ASSERT_TRUE(strstr(r.error, "unknown name tt") != NULL);
	ASSERT_FALSE(reel_expr_compile(&r, "clamp(1, 2)", &e));
	ASSERT_TRUE(strstr(r.error, "clamp() takes 3 arguments") != NULL);
	ASSERT_FALSE(reel_expr_compile(&r, "1 +", &e));
	ASSERT_FALSE(reel_expr_compile(&r, "Joe.hand.x", &e));
	ASSERT_TRUE(strstr(r.error, "no scene layer has joint Joe.hand") != NULL);
	PASS();
}

static void test_reel_document(void) {
	TEST("scener reels: lets, curves, checks and text segments load from XML");
	char path[REEL_TEST_PATH];
	reel_t *r = reel_test_write(
		"<reel width=\"320\" height=\"200\" fps=\"10\" duration=\"2\">\n"
		"  <curve name=\"swing\" ease=\"linear\">0:0 1:10 2:0</curve>\n"
		"  <style name=\"body\" size=\"20\" color=\"#FF8000\"/>\n"
		"  <let name=\"a\" value=\"swing(t) * 2\"/>\n"
		"  <let name=\"b\" value=\"a + 1\"/>\n"
		"  <check name=\"bounded\" value=\"b\" max=\"21\"/>\n"
		"  <check name=\"peak\" value=\"a\" over=\"max\" min=\"20\"/>\n"
		"  <group motion=\"pop(0.5) fadeOut(1.5, 0.25)\">\n"
		"    <text style=\"body\" x=\"10\" y=\"20\">Value   {b:%.1f} &amp; {{done}}</text>\n"
		"  </group>\n"
		"</reel>\n", path);
	ASSERT_NOT_NULL(r);
	reel_seek(r, 0.5f);
	ASSERT_TRUE(reel_test_near(r->lets[0], 10) && reel_test_near(r->lets[1], 11));
	reel_seek(r, 1.5f);
	ASSERT_TRUE(reel_test_near(r->lets[0], 10));
	ASSERT_TRUE(reel_sample(r, NULL));
	ASSERT_EQUAL(r->nchecks, 2);
	ASSERT_TRUE(reel_test_near(r->checks[1]->worst, 20) && reel_test_near(r->checks[1]->worst_time, 1));
	reel_node_t *group = r->root.kids[4], *text = group->kids[0];
	ASSERT_EQUAL(group->nmotions, 2);
	ASSERT_EQUAL(group->motions[1].kind, REEL_MOTION_FADE_OUT);
	ASSERT_TRUE(reel_test_near(group->motions[1].a[1], 0.25f));
	ASSERT_EQUAL(text->nsegments, 3);
	ASSERT_STR_EQUAL(text->segments[0].text, "Value   ");
	ASSERT_STR_EQUAL(text->segments[1].format, "%.1f");
	ASSERT_STR_EQUAL(text->segments[2].text, " & {done}");
	ASSERT_TRUE(reel_test_near(text->color[1], 128 / 255.0f));
	reel_free(r);
	unlink(path);
	PASS();
}

static void test_reel_rejects_mistakes(void) {
	TEST("scener reels: typos and failing checks are errors with line numbers");
	static const struct { const char *xml, *error; } cases[] = {
		{"<reel>\n<rect widht=\"3\"/>\n</reel>", ":2: <rect> does not support attribute widht"},
		{"<reel>\n<box/>\n</reel>", "unknown element <box>"},
		{"<reel>\n<let name=\"a\" value=\"b\"/>\n</reel>", "unknown name b"},
		{"<reel>\n<group motion=\"wobble(1)\"/>\n</reel>", "unknown motion wobble()"},
		{"<reel>\n<rect color=\"red\"/>\n</reel>", "must be #RRGGBB"},
		{"<reel>\n<group>\n</reel>", "closes <group>"},
		{"<reel width=\"301\"/>", "must be even"},
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		char path[REEL_TEST_PATH];
		reel_t *r = reel_test_write(cases[i].xml, path);
		unlink(path);
		if (r) { reel_free(r); FAIL(cases[i].xml); return; }
		ASSERT(strstr(reel_error(), cases[i].error) != NULL, cases[i].error);
	}
	char path[REEL_TEST_PATH];
	reel_t *r = reel_test_write("<reel fps=\"10\" duration=\"1\">\n<check name=\"late\" value=\"t\" max=\"0.5\"/>\n</reel>", path);
	unlink(path);
	ASSERT_NOT_NULL(r);
	ASSERT_FALSE(reel_sample(r, NULL));
	ASSERT_EQUAL(r->checks[0]->failures, 5);
	ASSERT_TRUE(reel_test_near(r->checks[0]->first_failure, 0.6f));
	reel_free(r);
	PASS();
}

static void test_reel_joint_anchors(void) {
	TEST("scener reels: IK/FK example anchors joints, samples tracks and passes checks");
	reel_t *r = reel_load("apps/scener/scenes/infographics/ik_fk.reel");
	ASSERT_NOT_NULL(r);
	ASSERT_EQUAL(r->nlayers, 1);
	ASSERT_TRUE(reel_sample(r, NULL));
	ASSERT_TRUE(r->ntracks == 1 && r->track_frames == 145);
	reel_seek(r, 1.5f);
	float drift = reel_test_eval(r, "drift"), error = reel_test_eval(r, "error");
	ASSERT_TRUE(drift > 15 && drift < 18);
	ASSERT_TRUE(error < 0.01f);
	float sx = reel_test_eval(r, "IK.left_palm.sx"), sy = reel_test_eval(r, "IK.left_palm.sy");
	ASSERT_TRUE(sx > 1200 && sx < 1400 && sy > 205 + 100 && sy < 205 + 400);
	ASSERT_TRUE(reel_test_near(reel_test_eval(r, "IK.left_palm@0.x"), reel_test_eval(r, "IK.left_palm.x")) || error < 0.01f);
	reel_free(r);
	PASS();
}

static void test_reel_mp4_container(void) {
	TEST("scener reels: frames encode to a complete H.264 MP4");
#ifndef __APPLE__
	SKIP("VideoToolbox is macOS only");
#else
	char path[REEL_TEST_PATH];
	const char *dir = getenv("TMPDIR");
	snprintf(path, sizeof(path), "%s/scener-reel-test.mp4", dir && *dir ? dir : ".");
	reel_video_t *v = reel_video_open(path, 64, 48, 12);
	ASSERT_NOT_NULL(v);
	uint8_t pixels[64 * 48 * 4];
	for (int f = 0; f < 5; f++) {
		for (int i = 0; i < 64 * 48; i++) { pixels[i * 4] = (uint8_t)(f * 40); pixels[i * 4 + 1] = (uint8_t)i; pixels[i * 4 + 2] = 128; pixels[i * 4 + 3] = 255; }
		ASSERT_TRUE(reel_video_write(v, pixels));
	}
	ASSERT_TRUE(reel_video_close(v));
	FILE *f = fopen(path, "rb");
	ASSERT_NOT_NULL(f);
	uint8_t data[1 << 16]; size_t n = fread(data, 1, sizeof(data), f); fclose(f); unlink(path);
	ASSERT_TRUE(n > 64 && !memcmp(data + 4, "ftyp", 4));
	const uint8_t *stsz = NULL;
	for (size_t i = 0; i + 4 < n && !stsz; i++) if (!memcmp(data + i, "stsz", 4)) stsz = data + i;
	ASSERT_NOT_NULL(stsz);
	ASSERT_EQUAL(((uint32_t)stsz[12] << 24) | ((uint32_t)stsz[13] << 16) | ((uint32_t)stsz[14] << 8) | stsz[15], 5u);
	bool avcc = false;
	for (size_t i = 0; i + 4 < n; i++) avcc |= !memcmp(data + i, "avcC", 4);
	ASSERT_TRUE(avcc);
	PASS();
#endif
}

int main(void) {
	TEST_START("scener reels");
	test_reel_expressions();
	test_reel_document();
	test_reel_rejects_mistakes();
	test_reel_joint_anchors();
	test_reel_mp4_container();
	TEST_END();
}
