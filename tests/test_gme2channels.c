/*
 * Unit tests for gme2channels utility functions.
 *
 * Includes gme2channels.c directly (with mock libgme) to test static
 * functions: parsing, WAV writing, audio conversion, etc.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Rename main so it doesn't conflict with our test main */
#define main gme2channels_main
#include "../gme2channels.c"
#undef main

/* Helper to suppress warn_unused_result for fread in tests */
static size_t test_fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t n = fread(ptr, size, nmemb, stream);
    return n;
}

/* ================================================================
 *  Simple test framework
 * ================================================================ */
static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN_TEST(name) do { \
    printf("  %-50s", #name); \
    tests_run++; \
    name(); \
    tests_passed++; \
    printf("PASS\n"); \
} while (0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("FAIL\n    %s:%d: %s == %d, expected %d\n", \
               __FILE__, __LINE__, #a, (int)(a), (int)(b)); \
        tests_failed++; tests_passed--; \
        return; \
    } \
} while (0)

#define ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        printf("FAIL\n    %s:%d: %s == \"%s\", expected \"%s\"\n", \
               __FILE__, __LINE__, #a, (a), (b)); \
        tests_failed++; tests_passed--; \
        return; \
    } \
} while (0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    %s:%d: %s is false\n", \
               __FILE__, __LINE__, #cond); \
        tests_failed++; tests_passed--; \
        return; \
    } \
} while (0)

/* ================================================================
 *  Tests: sanitize_filename
 * ================================================================ */
TEST(test_sanitize_spaces) {
    char s[] = "hello world";
    sanitize_filename(s);
    ASSERT_STR_EQ(s, "hello_world");
}

TEST(test_sanitize_slashes) {
    char s[] = "path/to\\file:name";
    sanitize_filename(s);
    ASSERT_STR_EQ(s, "path_to_file_name");
}

TEST(test_sanitize_no_change) {
    char s[] = "DSP_1";
    sanitize_filename(s);
    ASSERT_STR_EQ(s, "DSP_1");
}

TEST(test_sanitize_empty) {
    char s[] = "";
    sanitize_filename(s);
    ASSERT_STR_EQ(s, "");
}

/* ================================================================
 *  Tests: parse_format
 * ================================================================ */
TEST(test_parse_format_stereo) {
    ASSERT_EQ(parse_format("stereo"), FMT_STEREO);
}

TEST(test_parse_format_mono) {
    ASSERT_EQ(parse_format("mono"), FMT_MONO);
}

TEST(test_parse_format_split_lr) {
    ASSERT_EQ(parse_format("split-lr"), FMT_SPLIT_LR);
}

TEST(test_parse_format_split_alias) {
    ASSERT_EQ(parse_format("split"), FMT_SPLIT_LR);
}

/* ================================================================
 *  Tests: parse_channels
 * ================================================================ */
TEST(test_parse_channels_single) {
    int mask = parse_channels("1");
    ASSERT_EQ(mask, 1 << 0);
}

TEST(test_parse_channels_list) {
    int mask = parse_channels("1,3,5");
    ASSERT_EQ(mask, (1 << 0) | (1 << 2) | (1 << 4));
}

TEST(test_parse_channels_range) {
    int mask = parse_channels("1-4");
    ASSERT_EQ(mask, (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3));
}

TEST(test_parse_channels_mixed) {
    int mask = parse_channels("1-3,7");
    ASSERT_EQ(mask, (1 << 0) | (1 << 1) | (1 << 2) | (1 << 6));
}

TEST(test_parse_channels_all_8) {
    int mask = parse_channels("1-8");
    ASSERT_EQ(mask, 0xFF);
}

/* ================================================================
 *  Tests: parse_ch_formats / get_ch_format
 * ================================================================ */
TEST(test_parse_ch_formats_single) {
    options_t opts = { .default_fmt = FMT_STEREO, .ch_fmt_count = 0 };
    parse_ch_formats("5:mono", &opts);
    ASSERT_EQ(opts.ch_fmt_count, 1);
    ASSERT_EQ(opts.ch_fmts[0].channel, 5);
    ASSERT_EQ(opts.ch_fmts[0].fmt, FMT_MONO);
}

TEST(test_parse_ch_formats_multiple) {
    options_t opts = { .default_fmt = FMT_STEREO, .ch_fmt_count = 0 };
    parse_ch_formats("5:split-lr,6:mono", &opts);
    ASSERT_EQ(opts.ch_fmt_count, 2);
    ASSERT_EQ(opts.ch_fmts[0].channel, 5);
    ASSERT_EQ(opts.ch_fmts[0].fmt, FMT_SPLIT_LR);
    ASSERT_EQ(opts.ch_fmts[1].channel, 6);
    ASSERT_EQ(opts.ch_fmts[1].fmt, FMT_MONO);
}

TEST(test_get_ch_format_default) {
    options_t opts = { .default_fmt = FMT_STEREO, .ch_fmt_count = 0 };
    ASSERT_EQ(get_ch_format(&opts, 1), FMT_STEREO);
}

TEST(test_get_ch_format_override) {
    options_t opts = { .default_fmt = FMT_STEREO, .ch_fmt_count = 0 };
    parse_ch_formats("3:mono", &opts);
    ASSERT_EQ(get_ch_format(&opts, 3), FMT_MONO);
    ASSERT_EQ(get_ch_format(&opts, 1), FMT_STEREO); /* non-overridden */
}

/* ================================================================
 *  Tests: WAV header
 * ================================================================ */
TEST(test_wav_header_stereo) {
    FILE *f = tmpfile();
    ASSERT_TRUE(f != NULL);

    wav_write_header(f, 2, 44100, 1000);

    /* Verify header */
    fseek(f, 0, SEEK_SET);
    char buf[44];
    size_t n = fread(buf, 1, 44, f);
    ASSERT_EQ(n, 44);

    /* RIFF magic */
    ASSERT_TRUE(memcmp(buf, "RIFF", 4) == 0);
    /* WAVE magic */
    ASSERT_TRUE(memcmp(buf + 8, "WAVE", 4) == 0);
    /* fmt  */
    ASSERT_TRUE(memcmp(buf + 12, "fmt ", 4) == 0);
    /* data */
    ASSERT_TRUE(memcmp(buf + 36, "data", 4) == 0);

    /* num channels = 2 */
    uint16_t num_ch;
    memcpy(&num_ch, buf + 22, 2);
    ASSERT_EQ(num_ch, 2);

    /* sample rate = 44100 */
    uint32_t sr;
    memcpy(&sr, buf + 24, 4);
    ASSERT_EQ(sr, 44100);

    /* audio format = 1 (PCM) */
    uint16_t audio_fmt;
    memcpy(&audio_fmt, buf + 20, 2);
    ASSERT_EQ(audio_fmt, 1);

    fclose(f);
}

TEST(test_wav_header_mono) {
    FILE *f = tmpfile();
    ASSERT_TRUE(f != NULL);

    wav_write_header(f, 1, 22050, 500);

    fseek(f, 0, SEEK_SET);
    char buf[44];
    test_fread(buf, 1, 44, f);

    uint16_t num_ch;
    memcpy(&num_ch, buf + 22, 2);
    ASSERT_EQ(num_ch, 1);

    uint32_t sr;
    memcpy(&sr, buf + 24, 4);
    ASSERT_EQ(sr, 22050);

    fclose(f);
}

TEST(test_wav_finalize) {
    FILE *f = tmpfile();
    ASSERT_TRUE(f != NULL);

    wav_write_header(f, 2, 44100, 0);
    /* Write some fake data */
    short data[100];
    memset(data, 0, sizeof(data));
    fwrite(data, sizeof(short), 100, f);

    uint32_t data_size = 100 * sizeof(short);
    wav_finalize(f, data_size);

    /* Verify chunk_size at offset 4 */
    fseek(f, 4, SEEK_SET);
    uint32_t chunk_size;
    test_fread(&chunk_size, 4, 1, f);
    ASSERT_EQ(chunk_size, 36 + data_size);

    /* Verify data_size at offset 40 */
    fseek(f, 40, SEEK_SET);
    uint32_t ds;
    test_fread(&ds, 4, 1, f);
    ASSERT_EQ(ds, data_size);

    fclose(f);
}

/* ================================================================
 *  Tests: write_mono_from_stereo
 * ================================================================ */
TEST(test_write_mono_from_stereo) {
    FILE *f = tmpfile();
    ASSERT_TRUE(f != NULL);

    /* stereo input: L=100, R=200, L=300, R=400 */
    short stereo[] = {100, 200, 300, 400};
    write_mono_from_stereo(f, stereo, 4); /* 4 stereo samples = 2 frames */

    fseek(f, 0, SEEK_SET);
    short mono[2];
    test_fread(mono, sizeof(short), 2, f);

    ASSERT_EQ(mono[0], 150);  /* (100+200)/2 */
    ASSERT_EQ(mono[1], 350);  /* (300+400)/2 */

    fclose(f);
}

TEST(test_write_mono_from_stereo_negative) {
    FILE *f = tmpfile();
    ASSERT_TRUE(f != NULL);

    short stereo[] = {-100, 100, 0, 0};
    write_mono_from_stereo(f, stereo, 4);

    fseek(f, 0, SEEK_SET);
    short mono[2];
    test_fread(mono, sizeof(short), 2, f);

    ASSERT_EQ(mono[0], 0);   /* (-100+100)/2 */
    ASSERT_EQ(mono[1], 0);   /* (0+0)/2 */

    fclose(f);
}

/* ================================================================
 *  Tests: write_channel_from_stereo
 * ================================================================ */
TEST(test_write_left_channel) {
    FILE *f = tmpfile();
    ASSERT_TRUE(f != NULL);

    short stereo[] = {100, 200, 300, 400};
    write_channel_from_stereo(f, stereo, 4, 0); /* channel 0 = L */

    fseek(f, 0, SEEK_SET);
    short out[2];
    test_fread(out, sizeof(short), 2, f);

    ASSERT_EQ(out[0], 100);
    ASSERT_EQ(out[1], 300);

    fclose(f);
}

TEST(test_write_right_channel) {
    FILE *f = tmpfile();
    ASSERT_TRUE(f != NULL);

    short stereo[] = {100, 200, 300, 400};
    write_channel_from_stereo(f, stereo, 4, 1); /* channel 1 = R */

    fseek(f, 0, SEEK_SET);
    short out[2];
    test_fread(out, sizeof(short), 2, f);

    ASSERT_EQ(out[0], 200);
    ASSERT_EQ(out[1], 400);

    fclose(f);
}

/* ================================================================
 *  Main
 * ================================================================ */
int main(void) {
    printf("gme2channels unit tests\n");
    printf("========================\n\n");

    printf("[sanitize_filename]\n");
    RUN_TEST(test_sanitize_spaces);
    RUN_TEST(test_sanitize_slashes);
    RUN_TEST(test_sanitize_no_change);
    RUN_TEST(test_sanitize_empty);

    printf("\n[parse_format]\n");
    RUN_TEST(test_parse_format_stereo);
    RUN_TEST(test_parse_format_mono);
    RUN_TEST(test_parse_format_split_lr);
    RUN_TEST(test_parse_format_split_alias);

    printf("\n[parse_channels]\n");
    RUN_TEST(test_parse_channels_single);
    RUN_TEST(test_parse_channels_list);
    RUN_TEST(test_parse_channels_range);
    RUN_TEST(test_parse_channels_mixed);
    RUN_TEST(test_parse_channels_all_8);

    printf("\n[parse_ch_formats / get_ch_format]\n");
    RUN_TEST(test_parse_ch_formats_single);
    RUN_TEST(test_parse_ch_formats_multiple);
    RUN_TEST(test_get_ch_format_default);
    RUN_TEST(test_get_ch_format_override);

    printf("\n[wav_write_header / wav_finalize]\n");
    RUN_TEST(test_wav_header_stereo);
    RUN_TEST(test_wav_header_mono);
    RUN_TEST(test_wav_finalize);

    printf("\n[write_mono_from_stereo]\n");
    RUN_TEST(test_write_mono_from_stereo);
    RUN_TEST(test_write_mono_from_stereo_negative);

    printf("\n[write_channel_from_stereo]\n");
    RUN_TEST(test_write_left_channel);
    RUN_TEST(test_write_right_channel);

    printf("\n========================\n");
    printf("Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf("\n");

    return tests_failed > 0 ? 1 : 0;
}
