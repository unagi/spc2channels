/*
 * Mock gme/gme.h for unit testing spc2channels
 *
 * Provides type definitions and stub implementations so that
 * spc2channels.c compiles without the real libgme.
 */
#ifndef MOCK_GME_H
#define MOCK_GME_H

#include <string.h>

typedef const char *gme_err_t;
typedef struct Music_Emu {
    int dummy; /* opaque in real libgme; needs size for mock storage */
} Music_Emu;

typedef struct gme_info_t {
    char game[256];
    char song[256];
    char author[256];
    char system[256];
    int  play_length;
    int  loop_length;
} gme_info_t;

/* ---------- Mock state (tests can modify these) ---------- */
static Music_Emu  mock_emu_storage_;
static gme_info_t mock_info_storage_;
static int         mock_voice_count_  = 8;
static int         mock_track_ended_  = 1; /* end immediately */
static int         mock_play_calls_   = 0;

static const char *mock_voice_names_[32] = {
    "DSP_1", "DSP_2", "DSP_3", "DSP_4",
    "DSP_5", "DSP_6", "DSP_7", "DSP_8",
    "V9","V10","V11","V12","V13","V14","V15","V16",
    "V17","V18","V19","V20","V21","V22","V23","V24",
    "V25","V26","V27","V28","V29","V30","V31","V32"
};

/* ---------- Stub implementations ---------- */
static inline gme_err_t gme_open_file(const char *path, Music_Emu **out,
                                       int rate) {
    (void)path; (void)rate;
    *out = &mock_emu_storage_;
    return NULL;
}

static inline int gme_voice_count(const Music_Emu *emu) {
    (void)emu;
    return mock_voice_count_;
}

static inline const char *gme_voice_name(const Music_Emu *emu, int index) {
    (void)emu;
    if (index >= 0 && index < 32) return mock_voice_names_[index];
    return "Unknown";
}

static inline void gme_mute_voices(Music_Emu *emu, int mask) {
    (void)emu; (void)mask;
}

static inline void gme_disable_echo(Music_Emu *emu, int disable) {
    (void)emu; (void)disable;
}

static inline gme_err_t gme_start_track(Music_Emu *emu, int track) {
    (void)emu; (void)track;
    mock_play_calls_ = 0;
    return NULL;
}

static inline void gme_set_fade(Music_Emu *emu, int start_ms) {
    (void)emu; (void)start_ms;
}

static inline void gme_ignore_silence(Music_Emu *emu, int ignore) {
    (void)emu; (void)ignore;
}

static inline gme_err_t gme_play(Music_Emu *emu, int count, short *out) {
    (void)emu;
    /* Fill with a simple pattern so tests can verify output */
    for (int i = 0; i < count; i++)
        out[i] = (short)(i * 100);
    mock_play_calls_++;
    return NULL;
}

static inline int gme_track_ended(const Music_Emu *emu) {
    (void)emu;
    return mock_track_ended_ || (mock_play_calls_ > 0);
}

static inline gme_err_t gme_track_info(const Music_Emu *emu, gme_info_t **out,
                                        int track) {
    (void)emu; (void)track;
    memset(&mock_info_storage_, 0, sizeof(mock_info_storage_));
    strcpy(mock_info_storage_.game, "Test Game");
    strcpy(mock_info_storage_.song, "Test Song");
    strcpy(mock_info_storage_.system, "SNES");
    mock_info_storage_.play_length = 60000;
    mock_info_storage_.loop_length = 0;
    *out = &mock_info_storage_;
    return NULL;
}

static inline void gme_free_info(gme_info_t *info) {
    (void)info;
}

static inline void gme_delete(Music_Emu *emu) {
    (void)emu;
}

#endif /* MOCK_GME_H */
