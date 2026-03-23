/*
 * spc2channels - Extract individual channels from SPC/game music files as WAV
 *
 * Uses libgme (game-music-emu) for emulation with per-voice muting.
 * Supports SPC (SNES), NSF (NES), GBS (GB), VGM (SMS/GG/MD), and other
 * formats that libgme handles.
 *
 * License: MIT (see LICENSE)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <getopt.h>
#include <sys/stat.h>
#include <errno.h>
#include "gme/gme.h"

#ifndef VERSION
#define VERSION "1.0.0"
#endif
#define DEFAULT_SAMPLE_RATE 44100
#define DEFAULT_FADE_MS     5000
#define DEFAULT_DURATION_MS 120000
#define BUFFER_SIZE         4096  /* stereo sample pairs per read */
#define MAX_VOICES          32
#define MAX_PATH_LEN        1024

/* ---------- Output format ---------- */
typedef enum {
    FMT_STEREO,   /* interleaved stereo WAV (default) */
    FMT_MONO,     /* (L+R)/2 mono WAV */
    FMT_SPLIT_LR  /* separate _L.wav / _R.wav mono files */
} out_fmt_t;

static const char *fmt_names[] = { "stereo", "mono", "split-lr" };

/* ---------- Per-channel format override ---------- */
typedef struct {
    int       channel;   /* 1-based */
    out_fmt_t fmt;
} ch_fmt_t;

/* ---------- Options ---------- */
typedef struct {
    const char *input_path;
    const char *output_dir;
    int         sample_rate;
    int         duration_ms;   /* 0 = auto */
    int         fade_ms;
    int         no_echo;
    int         with_mix;
    int         info_only;
    out_fmt_t   default_fmt;

    /* channel selection: 0 = all */
    int         ch_mask;       /* bitmask, bit0 = voice0 */
    int         ch_all;

    /* per-channel format overrides */
    ch_fmt_t    ch_fmts[MAX_VOICES];
    int         ch_fmt_count;
} options_t;

/* ================================================================
 *  WAV writer (supports mono and stereo)
 * ================================================================ */
static void wav_write_header(FILE *f, uint16_t num_ch, uint32_t sample_rate,
                             uint32_t data_size) {
    uint16_t bps         = 16;
    uint32_t byte_rate   = sample_rate * num_ch * (bps / 8);
    uint16_t block_align = num_ch * (bps / 8);
    uint32_t chunk_size  = 36 + data_size;
    uint32_t fmt_size    = 16;
    uint16_t audio_fmt   = 1; /* PCM */

    fwrite("RIFF", 1, 4, f);
    fwrite(&chunk_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    fwrite(&fmt_size, 4, 1, f);
    fwrite(&audio_fmt, 2, 1, f);
    fwrite(&num_ch, 2, 1, f);
    fwrite(&sample_rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bps, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&data_size, 4, 1, f);
}

static void wav_finalize(FILE *f, uint32_t data_size) {
    uint32_t chunk_size = 36 + data_size;
    fseek(f, 4, SEEK_SET);
    fwrite(&chunk_size, 4, 1, f);
    fseek(f, 40, SEEK_SET);
    fwrite(&data_size, 4, 1, f);
}

/* ================================================================
 *  Filename helpers
 * ================================================================ */

/* Replace characters unsafe for filenames */
static void sanitize_filename(char *s) {
    for (; *s; s++) {
        if (*s == ' ' || *s == '/' || *s == '\\' || *s == ':')
            *s = '_';
    }
}

static int ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }
#ifdef _WIN32
    return mkdir(path);
#else
    return mkdir(path, 0755);
#endif
}

/* ================================================================
 *  Parse helpers
 * ================================================================ */

static out_fmt_t parse_format(const char *s) {
    if (strcmp(s, "stereo") == 0)   return FMT_STEREO;
    if (strcmp(s, "mono") == 0)     return FMT_MONO;
    if (strcmp(s, "split-lr") == 0) return FMT_SPLIT_LR;
    if (strcmp(s, "split") == 0)    return FMT_SPLIT_LR;
    fprintf(stderr, "Unknown format: %s (use stereo/mono/split-lr)\n", s);
    exit(1);
}

/* Parse "1,3,5" or "1-4" or "1-4,7" into bitmask */
static int parse_channels(const char *s) {
    int mask = 0;
    const char *p = s;
    while (*p) {
        int a = (int)strtol(p, (char **)&p, 10);
        if (a < 1 || a > MAX_VOICES) {
            fprintf(stderr, "Invalid channel number: %d\n", a);
            exit(1);
        }
        if (*p == '-') {
            p++;
            int b = (int)strtol(p, (char **)&p, 10);
            if (b < a || b > MAX_VOICES) {
                fprintf(stderr, "Invalid channel range: %d-%d\n", a, b);
                exit(1);
            }
            for (int i = a; i <= b; i++)
                mask |= (1 << (i - 1));
        } else {
            mask |= (1 << (a - 1));
        }
        if (*p == ',') p++;
    }
    return mask;
}

/* Parse "5:split-lr,6:mono" */
static void parse_ch_formats(const char *s, options_t *opts) {
    const char *p = s;
    while (*p) {
        int ch = (int)strtol(p, (char **)&p, 10);
        if (*p != ':') {
            fprintf(stderr, "Invalid --ch-format syntax (expected CH:FORMAT): %s\n", s);
            exit(1);
        }
        p++; /* skip ':' */
        char fmt_str[32];
        int i = 0;
        while (*p && *p != ',' && i < (int)sizeof(fmt_str) - 1)
            fmt_str[i++] = *p++;
        fmt_str[i] = '\0';
        if (*p == ',') p++;

        if (opts->ch_fmt_count >= MAX_VOICES) {
            fprintf(stderr, "Too many --ch-format entries\n");
            exit(1);
        }
        opts->ch_fmts[opts->ch_fmt_count].channel = ch;
        opts->ch_fmts[opts->ch_fmt_count].fmt     = parse_format(fmt_str);
        opts->ch_fmt_count++;
    }
}

static out_fmt_t get_ch_format(const options_t *opts, int voice_1based) {
    for (int i = 0; i < opts->ch_fmt_count; i++) {
        if (opts->ch_fmts[i].channel == voice_1based)
            return opts->ch_fmts[i].fmt;
    }
    return opts->default_fmt;
}

/* ================================================================
 *  Render one pass (one voice solo, or full mix)
 * ================================================================ */

/* Write stereo buffer as mono WAV: (L+R)/2 */
static void write_mono_from_stereo(FILE *f, const short *buf, int stereo_samples) {
    int frames = stereo_samples / 2;
    for (int i = 0; i < frames; i++) {
        short mono = (short)(((int)buf[i * 2] + (int)buf[i * 2 + 1]) / 2);
        fwrite(&mono, sizeof(short), 1, f);
    }
}

/* Write L or R channel from stereo buffer as mono */
static void write_channel_from_stereo(FILE *f, const short *buf,
                                      int stereo_samples, int channel) {
    int frames = stereo_samples / 2;
    for (int i = 0; i < frames; i++) {
        fwrite(&buf[i * 2 + channel], sizeof(short), 1, f);
    }
}

typedef struct {
    FILE     *f1;     /* main file (stereo/mono/L) */
    FILE     *f2;     /* R file for split-lr, NULL otherwise */
    out_fmt_t fmt;
    uint32_t  bytes1;
    uint32_t  bytes2;
} render_ctx_t;

static int render_pass(const options_t *opts, int voice_idx, int voice_count,
                       int play_length_ms) {
    Music_Emu *emu = NULL;
    gme_err_t err = gme_open_file(opts->input_path, &emu, opts->sample_rate);
    if (err) {
        fprintf(stderr, "Error opening file: %s\n", err);
        return -1;
    }

    int is_mix = (voice_idx < 0);
    int voice_1based = voice_idx + 1;
    out_fmt_t fmt;
    char path1[MAX_PATH_LEN], path2[MAX_PATH_LEN];

    if (is_mix) {
        /* Full mix */
        gme_mute_voices(emu, 0);
        fmt = FMT_STEREO; /* mix is always stereo */
        snprintf(path1, sizeof(path1), "%s/full_mix.wav", opts->output_dir);
        printf("  Rendering full mix (stereo)\n");
    } else {
        /* Solo one voice */
        int mute_mask = (1 << voice_count) - 1;
        mute_mask &= ~(1 << voice_idx);
        gme_mute_voices(emu, mute_mask);
        fmt = get_ch_format(opts, voice_1based);

        const char *vname = gme_voice_name(emu, voice_idx);
        char safe_name[256];
        snprintf(safe_name, sizeof(safe_name), "%s", vname);
        sanitize_filename(safe_name);

        switch (fmt) {
        case FMT_STEREO:
            snprintf(path1, sizeof(path1), "%s/ch%d_%s.wav",
                     opts->output_dir, voice_1based, safe_name);
            break;
        case FMT_MONO:
            snprintf(path1, sizeof(path1), "%s/ch%d_%s_mono.wav",
                     opts->output_dir, voice_1based, safe_name);
            break;
        case FMT_SPLIT_LR:
            snprintf(path1, sizeof(path1), "%s/ch%d_%s_L.wav",
                     opts->output_dir, voice_1based, safe_name);
            snprintf(path2, sizeof(path2), "%s/ch%d_%s_R.wav",
                     opts->output_dir, voice_1based, safe_name);
            break;
        }
        printf("  Rendering ch%d [%s] -> %s\n", voice_1based, vname,
               fmt_names[fmt]);
    }

    if (opts->no_echo)
        gme_disable_echo(emu, 1);

    err = gme_start_track(emu, 0);
    if (err) {
        fprintf(stderr, "Error starting track: %s\n", err);
        gme_delete(emu);
        return -1;
    }

    gme_set_fade(emu, play_length_ms);
    gme_ignore_silence(emu, 1);

    /* Open output files */
    render_ctx_t ctx = { NULL, NULL, fmt, 0, 0 };
    uint16_t out_ch = (fmt == FMT_STEREO) ? 2 : 1;

    ctx.f1 = fopen(path1, "wb");
    if (!ctx.f1) {
        fprintf(stderr, "Error opening: %s (%s)\n", path1, strerror(errno));
        gme_delete(emu);
        return -1;
    }
    wav_write_header(ctx.f1, out_ch, (uint32_t)opts->sample_rate, 0);

    if (fmt == FMT_SPLIT_LR) {
        ctx.f2 = fopen(path2, "wb");
        if (!ctx.f2) {
            fprintf(stderr, "Error opening: %s (%s)\n", path2, strerror(errno));
            fclose(ctx.f1);
            gme_delete(emu);
            return -1;
        }
        wav_write_header(ctx.f2, 1, (uint32_t)opts->sample_rate, 0);
    }

    /* Render loop */
    short buf[BUFFER_SIZE * 2];
    while (!gme_track_ended(emu)) {
        err = gme_play(emu, BUFFER_SIZE * 2, buf);
        if (err) {
            fprintf(stderr, "Playback error: %s\n", err);
            break;
        }

        switch (fmt) {
        case FMT_STEREO:
            fwrite(buf, sizeof(short), BUFFER_SIZE * 2, ctx.f1);
            ctx.bytes1 += BUFFER_SIZE * 2 * sizeof(short);
            break;
        case FMT_MONO:
            write_mono_from_stereo(ctx.f1, buf, BUFFER_SIZE * 2);
            ctx.bytes1 += BUFFER_SIZE * sizeof(short);
            break;
        case FMT_SPLIT_LR:
            write_channel_from_stereo(ctx.f1, buf, BUFFER_SIZE * 2, 0);
            write_channel_from_stereo(ctx.f2, buf, BUFFER_SIZE * 2, 1);
            ctx.bytes1 += BUFFER_SIZE * sizeof(short);
            ctx.bytes2 += BUFFER_SIZE * sizeof(short);
            break;
        }
    }

    /* Finalize */
    wav_finalize(ctx.f1, ctx.bytes1);
    fclose(ctx.f1);
    printf("    -> %s (%u bytes)\n", path1, ctx.bytes1 + 44);

    if (ctx.f2) {
        wav_finalize(ctx.f2, ctx.bytes2);
        fclose(ctx.f2);
        printf("    -> %s (%u bytes)\n", path2, ctx.bytes2 + 44);
    }

    gme_delete(emu);
    return 0;
}

/* ================================================================
 *  Info display
 * ================================================================ */
static int show_info(const options_t *opts) {
    Music_Emu *emu = NULL;
    gme_err_t err = gme_open_file(opts->input_path, &emu, opts->sample_rate);
    if (err) {
        fprintf(stderr, "Error: %s\n", err);
        return 1;
    }

    gme_info_t *info = NULL;
    err = gme_track_info(emu, &info, 0);

    int vc = gme_voice_count(emu);
    printf("File       : %s\n", opts->input_path);
    if (info) {
        if (info->game[0])   printf("Game       : %s\n", info->game);
        if (info->song[0])   printf("Song       : %s\n", info->song);
        if (info->author[0]) printf("Author     : %s\n", info->author);
        if (info->system[0]) printf("System     : %s\n", info->system);
        printf("Play length: %d ms\n", info->play_length);
        if (info->loop_length > 0)
            printf("Loop length: %d ms\n", info->loop_length);
        gme_free_info(info);
    }
    printf("Voices     : %d\n", vc);
    for (int i = 0; i < vc; i++)
        printf("  [%d] %s\n", i + 1, gme_voice_name(emu, i));

    gme_delete(emu);
    return 0;
}

/* ================================================================
 *  Usage / Help
 * ================================================================ */
static void print_usage(const char *prog) {
    printf(
        "spc2channels v%s - Extract channels from SPC/game music as WAV\n"
        "\n"
        "Usage:\n"
        "  %s [options] <input_file> <output_dir>\n"
        "  %s --info <input_file>\n"
        "\n"
        "Options:\n"
        "  -f, --format <fmt>      Output format: stereo|mono|split-lr (default: stereo)\n"
        "  -c, --channels <spec>   Channels to extract: 1,3,5 or 1-4 or 1-4,7 (default: all)\n"
        "      --ch-format <spec>  Per-channel format: \"5:split-lr,6:mono\"\n"
        "  -d, --duration <sec>    Playback duration in seconds (default: auto-detect)\n"
        "      --fade <sec>        Fade-out duration in seconds (default: 5)\n"
        "  -r, --rate <hz>         Sample rate (default: 44100)\n"
        "      --no-echo           Disable SPC echo/reverb DSP effect\n"
        "      --mix               Also output full stereo mix\n"
        "      --info              Show file/voice info only (no output)\n"
        "  -h, --help              Show this help\n"
        "  -v, --version           Show version\n"
        "\n"
        "Examples:\n"
        "  %s battle.spc ./out\n"
        "  %s -f mono --ch-format \"5:split-lr\" --mix battle.spc ./out\n"
        "  %s --info battle.spc\n",
        VERSION, prog, prog, prog, prog, prog
    );
}

/* ================================================================
 *  Main
 * ================================================================ */
int main(int argc, char *argv[]) {
    options_t opts = {
        .input_path  = NULL,
        .output_dir  = NULL,
        .sample_rate = DEFAULT_SAMPLE_RATE,
        .duration_ms = 0,
        .fade_ms     = DEFAULT_FADE_MS,
        .no_echo     = 0,
        .with_mix    = 0,
        .info_only   = 0,
        .default_fmt = FMT_STEREO,
        .ch_mask     = 0,
        .ch_all      = 1,
        .ch_fmt_count = 0,
    };

    static struct option long_opts[] = {
        { "format",    required_argument, NULL, 'f' },
        { "channels",  required_argument, NULL, 'c' },
        { "ch-format", required_argument, NULL, 'F' },
        { "duration",  required_argument, NULL, 'd' },
        { "fade",      required_argument, NULL, 'D' },
        { "rate",      required_argument, NULL, 'r' },
        { "no-echo",   no_argument,       NULL, 'E' },
        { "mix",       no_argument,       NULL, 'M' },
        { "info",      no_argument,       NULL, 'I' },
        { "help",      no_argument,       NULL, 'h' },
        { "version",   no_argument,       NULL, 'v' },
        { NULL, 0, NULL, 0 }
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "f:c:d:r:hv", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'f': opts.default_fmt = parse_format(optarg); break;
        case 'c': opts.ch_mask = parse_channels(optarg); opts.ch_all = 0; break;
        case 'F': parse_ch_formats(optarg, &opts); break;
        case 'd': opts.duration_ms = atoi(optarg) * 1000; break;
        case 'D': opts.fade_ms = atoi(optarg) * 1000; break;
        case 'r': opts.sample_rate = atoi(optarg); break;
        case 'E': opts.no_echo = 1; break;
        case 'M': opts.with_mix = 1; break;
        case 'I': opts.info_only = 1; break;
        case 'v': printf("spc2channels v%s\n", VERSION); return 0;
        case 'h': print_usage(argv[0]); return 0;
        default:  print_usage(argv[0]); return 1;
        }
    }

    /* Positional arguments */
    if (optind >= argc) {
        fprintf(stderr, "Error: input file required\n\n");
        print_usage(argv[0]);
        return 1;
    }
    opts.input_path = argv[optind++];

    if (opts.info_only)
        return show_info(&opts);

    if (optind >= argc) {
        fprintf(stderr, "Error: output directory required\n\n");
        print_usage(argv[0]);
        return 1;
    }
    opts.output_dir = argv[optind];

    /* Create output directory */
    if (ensure_dir(opts.output_dir) != 0) {
        fprintf(stderr, "Error: cannot create directory: %s (%s)\n",
                opts.output_dir, strerror(errno));
        return 1;
    }

    /* Get voice count and duration */
    Music_Emu *emu = NULL;
    gme_err_t err = gme_open_file(opts.input_path, &emu, opts.sample_rate);
    if (err) {
        fprintf(stderr, "Error: %s\n", err);
        return 1;
    }

    int voice_count = gme_voice_count(emu);

    gme_info_t *info = NULL;
    err = gme_track_info(emu, &info, 0);

    int play_length_ms;
    if (opts.duration_ms > 0) {
        play_length_ms = opts.duration_ms;
    } else if (info && info->play_length > 0) {
        play_length_ms = info->play_length;
    } else {
        play_length_ms = DEFAULT_DURATION_MS;
    }

    printf("spc2channels v%s\n", VERSION);
    printf("Input      : %s\n", opts.input_path);
    printf("Output dir : %s\n", opts.output_dir);
    printf("Voices     : %d\n", voice_count);
    printf("Duration   : %d ms (fade: %d ms)\n", play_length_ms, opts.fade_ms);
    printf("Sample rate: %d Hz\n", opts.sample_rate);
    printf("Format     : %s", fmt_names[opts.default_fmt]);
    if (opts.ch_fmt_count > 0) {
        printf(" (overrides:");
        for (int i = 0; i < opts.ch_fmt_count; i++)
            printf(" ch%d=%s", opts.ch_fmts[i].channel,
                   fmt_names[opts.ch_fmts[i].fmt]);
        printf(")");
    }
    printf("\n");
    if (opts.no_echo) printf("Echo       : disabled\n");
    printf("\n");

    if (info) gme_free_info(info);
    gme_delete(emu);

    /* Determine effective fade: add fade_ms to play_length for gme_set_fade */
    /* gme_set_fade takes the START of fade, so pass play_length_ms directly */

    /* Render each selected voice */
    int errors = 0;
    for (int i = 0; i < voice_count; i++) {
        if (!opts.ch_all && !(opts.ch_mask & (1 << i)))
            continue;
        if (render_pass(&opts, i, voice_count, play_length_ms) != 0)
            errors++;
    }

    /* Full mix */
    if (opts.with_mix) {
        if (render_pass(&opts, -1, voice_count, play_length_ms) != 0)
            errors++;
    }

    printf("\nDone! %d voice(s) extracted%s.\n",
           voice_count, errors > 0 ? " (with errors)" : "");
    return errors > 0 ? 1 : 0;
}
