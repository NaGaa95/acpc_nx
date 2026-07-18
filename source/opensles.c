/* OpenSL ES buffer queues mixed through SDL2.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <math.h>
#include <SDL2/SDL.h>

#include "opensles.h"
#include "util.h"

#define SL_RESULT_SUCCESS              0
#define SL_RESULT_PARAMETER_INVALID    0x0D
#define SL_RESULT_FEATURE_UNSUPPORTED  0x0C

#define SL_BOOLEAN_FALSE 0
#define SL_BOOLEAN_TRUE  1

#define SL_PLAYSTATE_STOPPED 1
#define SL_PLAYSTATE_PAUSED  2
#define SL_PLAYSTATE_PLAYING 3

#define SL_OBJECT_STATE_REALIZED 2

typedef uint32_t SLuint32;
typedef int32_t  SLint32;
typedef uint16_t SLuint16;
typedef int16_t  SLint16;
typedef uint8_t  SLuint8;
typedef uint32_t SLresult;
typedef uint32_t SLboolean;
typedef int32_t  SLmillibel;

/* PCM samplesPerSec is expressed in millihertz. */
typedef struct {
  SLuint32 formatType;
  SLuint32 numChannels;
  SLuint32 samplesPerSec;
  SLuint32 bitsPerSample;
  SLuint32 containerSize;
  SLuint32 channelMask;
  SLuint32 endianness;
} SLDataFormat_PCM;

typedef struct {
  SLuint32 locatorType;
  SLuint32 numBuffers;
} SLDataLocator_BufferQueue;

typedef struct {
  void *pLocator;
  void *pFormat;
} SLDataSource;

typedef struct {
  void *pLocator;
  void *pFormat;
} SLDataSink;

typedef void *SLObjectItf;       // -> &obj->obj_vt
typedef void *SLInterfaceID;

typedef void (*slBufferQueueCallback)(void *caller, void *context);

#define DEF_IID(n) void *SL_IID_##n = &SL_IID_##n
DEF_IID(3DCOMMIT); DEF_IID(3DDOPPLER); DEF_IID(3DGROUPING); DEF_IID(3DLOCATION);
DEF_IID(3DMACROSCOPIC); DEF_IID(3DSOURCE); DEF_IID(ANDROIDCONFIGURATION);
DEF_IID(ANDROIDEFFECT); DEF_IID(ANDROIDEFFECTCAPABILITIES); DEF_IID(ANDROIDEFFECTSEND);
DEF_IID(ANDROIDSIMPLEBUFFERQUEUE); DEF_IID(AUDIODECODERCAPABILITIES); DEF_IID(AUDIOENCODER);
DEF_IID(AUDIOENCODERCAPABILITIES); DEF_IID(AUDIOIODEVICECAPABILITIES); DEF_IID(BASSBOOST);
DEF_IID(BUFFERQUEUE); DEF_IID(DEVICEVOLUME); DEF_IID(DYNAMICINTERFACEMANAGEMENT);
DEF_IID(DYNAMICSOURCE); DEF_IID(EFFECTSEND); DEF_IID(ENGINE); DEF_IID(ENGINECAPABILITIES);
DEF_IID(ENVIRONMENTALREVERB); DEF_IID(EQUALIZER); DEF_IID(LED); DEF_IID(METADATAEXTRACTION);
DEF_IID(METADATATRAVERSAL); DEF_IID(MIDIMESSAGE); DEF_IID(MIDIMUTESOLO); DEF_IID(MIDITEMPO);
DEF_IID(MIDITIME); DEF_IID(MUTESOLO); DEF_IID(NULL); DEF_IID(OBJECT); DEF_IID(OUTPUTMIX);
DEF_IID(PITCH); DEF_IID(PLAY); DEF_IID(PLAYBACKRATE); DEF_IID(PREFETCHSTATUS);
DEF_IID(PRESETREVERB); DEF_IID(RATEPITCH); DEF_IID(RECORD); DEF_IID(SEEK); DEF_IID(THREADSYNC);
DEF_IID(VIBRA); DEF_IID(VIRTUALIZER); DEF_IID(VISUALIZATION); DEF_IID(VOLUME);
#undef DEF_IID

/* Vtable order follows OpenSL ES 1.0.1. */

typedef struct {
  SLresult (*Realize)(void *self, SLboolean async);
  SLresult (*Resume)(void *self, SLboolean async);
  SLresult (*GetState)(void *self, SLuint32 *pState);
  SLresult (*GetInterface)(void *self, const SLInterfaceID iid, void *pInterface);
  SLresult (*RegisterCallback)(void *self, void *cb, void *ctx);
  SLresult (*AbortAsyncOperation)(void *self);
  void     (*Destroy)(void *self);
  SLresult (*SetPriority)(void *self, SLint32 priority, SLboolean preemptable);
  SLresult (*GetPriority)(void *self, SLint32 *pPriority);
  SLresult (*SetLossOfControlInterfaces)(void *self, SLint32 n, SLInterfaceID *ids, SLboolean enabled);
} SLObjectItf_;

typedef struct {
  void *CreateLEDDevice;
  void *CreateVibraDevice;
  SLresult (*CreateAudioPlayer)(void *self, SLObjectItf *pPlayer, SLDataSource *src, SLDataSink *snk,
                                SLuint32 numIfaces, const SLInterfaceID *ids, const SLboolean *req);
  void *CreateAudioRecorder;
  void *CreateMidiPlayer;
  void *CreateListener;
  void *Create3DGroup;
  SLresult (*CreateOutputMix)(void *self, SLObjectItf *pMix, SLuint32 numIfaces, const SLInterfaceID *ids, const SLboolean *req);
  void *CreateMetadataExtractor;
  void *CreateExtensionObject;
  void *QueryNumSupportedInterfaces;
  void *QuerySupportedInterfaces;
  void *QueryNumSupportedExtensions;
  void *QuerySupportedExtension;
  void *IsExtensionSupported;
} SLEngineItf_;

typedef struct {
  SLresult (*SetPlayState)(void *self, SLuint32 state);
  SLresult (*GetPlayState)(void *self, SLuint32 *pState);
  SLresult (*GetDuration)(void *self, SLuint32 *pMsec);
  SLresult (*GetPosition)(void *self, SLuint32 *pMsec);
  SLresult (*RegisterCallback)(void *self, void *cb, void *ctx);
  SLresult (*SetCallbackEventsMask)(void *self, SLuint32 mask);
  SLresult (*GetCallbackEventsMask)(void *self, SLuint32 *pMask);
  SLresult (*SetMarkerPosition)(void *self, SLuint32 m);
  SLresult (*ClearMarkerPosition)(void *self);
  SLresult (*GetMarkerPosition)(void *self, SLuint32 *p);
  SLresult (*SetPositionUpdatePeriod)(void *self, SLuint32 m);
  SLresult (*GetPositionUpdatePeriod)(void *self, SLuint32 *p);
} SLPlayItf_;

typedef struct {
  SLresult (*Enqueue)(void *self, const void *pBuffer, SLuint32 size);
  SLresult (*Clear)(void *self);
  SLresult (*GetState)(void *self, void *pState);
  SLresult (*RegisterCallback)(void *self, slBufferQueueCallback cb, void *ctx);
} SLBufferQueueItf_;

typedef struct {
  SLresult (*SetVolumeLevel)(void *self, SLmillibel level);
  SLresult (*GetVolumeLevel)(void *self, SLmillibel *p);
  SLresult (*GetMaxVolumeLevel)(void *self, SLmillibel *p);
  SLresult (*SetMute)(void *self, SLboolean mute);
  SLresult (*GetMute)(void *self, SLboolean *p);
  SLresult (*EnableStereoPosition)(void *self, SLboolean enable);
  SLresult (*IsEnabledStereoPosition)(void *self, SLboolean *p);
  SLresult (*SetStereoPosition)(void *self, SLint32 perMille);
  SLresult (*GetStereoPosition)(void *self, SLint32 *p);
} SLVolumeItf_;

/* Playback-rate calls are accepted without pitch adjustment. */
typedef struct {
  SLresult (*SetRate)(void *self, SLint16 rate);
  SLresult (*GetRate)(void *self, SLint16 *p);
  SLresult (*SetPropertyConstraints)(void *self, SLuint32 c);
  SLresult (*GetProperties)(void *self, SLuint32 *p);
  SLresult (*GetCapabilitiesOfRate)(void *self, SLuint32 *p);
  SLresult (*GetRateRange)(void *self, SLuint8 i, SLint16 *min, SLint16 *max, SLint16 *step, SLuint32 *prop);
} SLPlaybackRateItf_;

/* Android audio configuration is accepted and ignored. */
typedef struct {
  SLresult (*SetConfiguration)(void *self, const void *key, const void *value, SLuint32 valueSize);
  SLresult (*GetConfiguration)(void *self, const void *key, SLuint32 *pValueSize, void *value);
  SLresult (*AcquireJavaProxy)(void *self, SLuint32 proxyType, void *pProxyObj);
  SLresult (*ReleaseJavaProxy)(void *self, SLuint32 proxyType);
} SLAndroidConfigurationItf_;

#define MAX_PLAYERS 64
/* Capacity for FMOD's initial buffer batch. */
#define BQ_SLOTS 256

typedef struct {
  const void *data;
  SLuint32 size;
} BQBuffer;

typedef struct Player {
  const SLObjectItf_ *obj_vt;
  const SLPlayItf_   *play_vt;
  const SLBufferQueueItf_ *bq_vt;
  const SLVolumeItf_ *vol_vt;
  const SLPlaybackRateItf_ *rate_vt;
  const SLAndroidConfigurationItf_ *config_vt;

  int in_use;
  int channels;
  int rate;
  int sbytes;    // bytes per sample in the enqueued buffers (2=16-bit, 4=32-bit)
  int is_float;  // 1 if samples are 32-bit float, 0 if signed integer
  int playing;
  int drained;   // consecutive callbacks this playing player produced no audio
  float gain; // linear, from SetVolumeLevel (millibels)

  slBufferQueueCallback cb;
  void *cb_ctx;

  BQBuffer q[BQ_SLOTS];
  int q_head, q_tail; // count = (tail - head + N) % N
  const uint8_t *cur;
  SLuint32 cur_size, cur_pos;
  double cur_fpos; // fractional sample index into cur (for rate conversion)

  SDL_mutex *lock;
} Player;

typedef struct {
  const SLObjectItf_ *obj_vt;
} OutputMix;

typedef struct {
  const SLObjectItf_ *obj_vt;
  const SLEngineItf_ *eng_vt;
} Engine;

#define CONTAINER(ptr, type, member) \
  ((type *)((char *)(ptr) - offsetof(type, member)))

static SDL_AudioDeviceID g_dev = 0;
static int g_dev_rate = 44100;
static Player *g_players[MAX_PLAYERS];
static int g_player_count = 0;
static SDL_mutex *g_reg_lock = NULL;

static float mb_to_linear(SLmillibel mb) {
  if (mb <= -9600) return 0.0f;
  return powf(10.0f, (float)mb / 2000.0f); // 100 mB = 1 dB
}

/* Convert one input sample to signed 16-bit. */
static inline int32_t read_sample_s16(const void *buf, long k, int sbytes, int is_float) {
  if (is_float) {
    float f = ((const float *)buf)[k];
    if (f > 1.0f) f = 1.0f; else if (f < -1.0f) f = -1.0f;
    return (int32_t)(f * 32767.0f);
  }
  if (sbytes == 4)
    return ((const int32_t *)buf)[k] >> 16;   // S32 -> S16 range
  return (int32_t)((const int16_t *)buf)[k];  // S16
}

static int prepare_player_buffer(Player *p, int bytes_per_frame) {
  for (;;) {
    if (!p->cur) {
      SDL_LockMutex(p->lock);
      const int have = (p->q_head != p->q_tail);
      BQBuffer b = { NULL, 0 };
      if (have) {
        b = p->q[p->q_head];
        p->q_head = (p->q_head + 1) % BQ_SLOTS;
      }
      SDL_UnlockMutex(p->lock);
      if (!have) return 0;
      p->cur = b.data;
      p->cur_size = b.size;
    }

    const long n = (long)(p->cur_size / (SLuint32)bytes_per_frame);
    if (n > 0 && (long)p->cur_fpos < n) return 1;
    p->cur_fpos -= (double)n;
    if (p->cur_fpos < 0.0) p->cur_fpos = 0.0;
    p->cur = NULL;
    if (p->cb) p->cb(&p->bq_vt, p->cb_ctx);
  }
}

static void mix_s16_stereo_exact(Player *p, int32_t *acc, int frames, float gain) {
  int out_frame = 0;
  while (out_frame < frames) {
    if (!prepare_player_buffer(p, 4)) return;
    const long n = (long)(p->cur_size / 4u);
    const long pos = (long)p->cur_fpos;
    int chunk = (int)(n - pos);
    if (chunk > frames - out_frame) chunk = frames - out_frame;
    const int16_t *src = (const int16_t *)p->cur + pos * 2;
    int32_t *dst = acc + out_frame * 2;
    const int samples = chunk * 2;
    if (gain == 1.0f) {
      for (int i = 0; i < samples; i++) dst[i] += src[i];
    } else if (gain != 0.0f) {
      for (int i = 0; i < samples; i++) dst[i] += (int32_t)(src[i] * gain);
    }
    p->cur_fpos += (double)chunk;
    out_frame += chunk;
  }
}

/* Mix one player; completion callbacks run without the queue lock. */
static void mix_player(Player *p, int32_t *acc, int frames) {
  if (!p->playing)
    return;

  /* Track drained players for slot reuse. */
  SDL_LockMutex(p->lock);
  const int dry = (!p->cur) && (p->q_head == p->q_tail);
  SDL_UnlockMutex(p->lock);
  if (dry) { if (p->drained < (1 << 20)) p->drained++; return; }
  p->drained = 0;

  const float g = p->gain;
  const int stereo = (p->channels >= 2);
  const int sbytes = p->sbytes > 0 ? p->sbytes : 2;    // bytes per sample
  const int is_float = p->is_float;
  const int bps = stereo ? sbytes * 2 : sbytes;        // bytes per input frame
  if (p->channels == 2 && sbytes == 2 && !is_float &&
      p->rate == g_dev_rate && p->cur_fpos == (double)(long)p->cur_fpos) {
    mix_s16_stereo_exact(p, acc, frames, g);
    return;
  }
  /* Resample each player to the output device rate. */
  const double ratio = g_dev_rate > 0 ? (double)p->rate / (double)g_dev_rate : 1.0;

  for (int i = 0; i < frames; i++) {
    if (!prepare_player_buffer(p, bps)) return;

    const long n = (long)(p->cur_size / (SLuint32)bps);
    const long idx = (long)p->cur_fpos;
    const double frac = p->cur_fpos - (double)idx;
    const void *s = p->cur;
    int32_t l, r;
    if (stereo) {
      const long j0 = idx * 2, j1 = (idx + 1 < n ? idx + 1 : idx) * 2;
      const int32_t l0 = read_sample_s16(s, j0,     sbytes, is_float);
      const int32_t l1 = read_sample_s16(s, j1,     sbytes, is_float);
      const int32_t r0 = read_sample_s16(s, j0 + 1, sbytes, is_float);
      const int32_t r1 = read_sample_s16(s, j1 + 1, sbytes, is_float);
      l = (int32_t)(l0 * (1.0 - frac) + l1 * frac);
      r = (int32_t)(r0 * (1.0 - frac) + r1 * frac);
    } else {
      const int32_t a  = read_sample_s16(s, idx, sbytes, is_float);
      const int32_t b2 = (idx + 1 < n) ? read_sample_s16(s, idx + 1, sbytes, is_float) : a;
      l = r = (int32_t)(a * (1.0 - frac) + b2 * frac);
    }
    acc[i * 2 + 0] += (int32_t)(l * g);
    acc[i * 2 + 1] += (int32_t)(r * g);
    p->cur_fpos += ratio;
  }
}

static void SDLCALL audio_callback(void *ud, Uint8 *stream, int len) {
  (void)ud;

  /* Completion callbacks require bionic TLS on SDL's audio thread. */
  static uint8_t audio_tls[BIONIC_TLS_SIZE] __attribute__((aligned(16)));
  static int tls_ready = 0;
  if (!tls_ready) { install_bionic_tls(audio_tls); tls_ready = 1; }

  const int frames = len / 4; // S16 stereo
  static int32_t acc[8192 * 2];
  if (frames > 8192) { memset(stream, 0, len); return; }
  memset(acc, 0, frames * 2 * sizeof(int32_t));

  SDL_LockMutex(g_reg_lock);
  for (int i = 0; i < g_player_count; i++)
    if (g_players[i] && g_players[i]->in_use)
      mix_player(g_players[i], acc, frames);
  SDL_UnlockMutex(g_reg_lock);

  int16_t *out = (int16_t *)stream;
  for (int i = 0; i < frames * 2; i++) {
    int32_t v = acc[i];
    if (v > 32767) v = 32767;
    else if (v < -32768) v = -32768;
    out[i] = (int16_t)v;
  }
}

static void ensure_device(int rate) {
  (void)rate;
  if (!g_reg_lock)
    g_reg_lock = SDL_CreateMutex();
  if (g_dev)
    return;
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) return;
  SDL_AudioSpec want, have;
  SDL_zero(want);
  want.freq = 48000;
  want.format = AUDIO_S16SYS;
  want.channels = 2;
  want.samples = 1024;
  want.callback = audio_callback;
  g_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  if (!g_dev) return;
  g_dev_rate = have.freq;
  SDL_PauseAudioDevice(g_dev, 0);
}

static SLresult bq_Enqueue(void *self, const void *pBuffer, SLuint32 size) {
  Player *p = CONTAINER(self, Player, bq_vt);
  SDL_LockMutex(p->lock);
  const int next = (p->q_tail + 1) % BQ_SLOTS;
  if (next == p->q_head) { // full
    SDL_UnlockMutex(p->lock);
    return SL_RESULT_PARAMETER_INVALID;
  }
  p->q[p->q_tail].data = pBuffer;
  p->q[p->q_tail].size = size;
  p->q_tail = next;
  SDL_UnlockMutex(p->lock);
  return SL_RESULT_SUCCESS;
}

static SLresult bq_Clear(void *self) {
  Player *p = CONTAINER(self, Player, bq_vt);
  SDL_LockMutex(p->lock);
  p->q_head = p->q_tail = 0;
  p->cur = NULL;
  p->cur_pos = p->cur_size = 0;
  p->cur_fpos = 0.0;
  SDL_UnlockMutex(p->lock);
  return SL_RESULT_SUCCESS;
}

typedef struct { SLuint32 count; SLuint32 index; } SLBufferQueueState;

static SLresult bq_GetState(void *self, void *pState) {
  Player *p = CONTAINER(self, Player, bq_vt);
  if (pState) {
    SLBufferQueueState *st = pState;
    SDL_LockMutex(p->lock);
    st->count = (p->q_tail - p->q_head + BQ_SLOTS) % BQ_SLOTS + (p->cur ? 1 : 0);
    st->index = 0;
    SDL_UnlockMutex(p->lock);
  }
  return SL_RESULT_SUCCESS;
}

static SLresult bq_RegisterCallback(void *self, slBufferQueueCallback cb, void *ctx) {
  Player *p = CONTAINER(self, Player, bq_vt);
  p->cb = cb;
  p->cb_ctx = ctx;
  return SL_RESULT_SUCCESS;
}

static const SLBufferQueueItf_ bq_vtable = {
  bq_Enqueue, bq_Clear, bq_GetState, bq_RegisterCallback,
};

static SLresult play_SetPlayState(void *self, SLuint32 state) {
  Player *p = CONTAINER(self, Player, play_vt);
  SDL_LockMutex(p->lock);
  p->playing = (state == SL_PLAYSTATE_PLAYING);
  SDL_UnlockMutex(p->lock);
  return SL_RESULT_SUCCESS;
}
static SLresult play_GetPlayState(void *self, SLuint32 *pState) {
  Player *p = CONTAINER(self, Player, play_vt);
  if (pState) *pState = p->playing ? SL_PLAYSTATE_PLAYING : SL_PLAYSTATE_STOPPED;
  return SL_RESULT_SUCCESS;
}
static SLresult play_ret0_u32(void *self, SLuint32 *p) { (void)self; if (p) *p = 0; return SL_RESULT_SUCCESS; }
static SLresult play_ok_u32(void *self, SLuint32 v) { (void)self; (void)v; return SL_RESULT_SUCCESS; }
static SLresult play_ok(void *self) { (void)self; return SL_RESULT_SUCCESS; }
static SLresult play_RegisterCallback(void *self, void *cb, void *ctx) { (void)self; (void)cb; (void)ctx; return SL_RESULT_SUCCESS; }

static const SLPlayItf_ play_vtable = {
  play_SetPlayState, play_GetPlayState, play_ret0_u32, play_ret0_u32,
  play_RegisterCallback, play_ok_u32, play_ret0_u32, play_ok_u32,
  play_ok, play_ret0_u32, play_ok_u32, play_ret0_u32,
};

static SLresult vol_SetVolumeLevel(void *self, SLmillibel level) {
  Player *p = CONTAINER(self, Player, vol_vt);
  /* Clamp to the OpenSL millibel range. */
  int mb = (int)level;
  if (mb > 0) mb = 0;
  if (mb < -9600) mb = -9600;
  p->gain = mb_to_linear(mb);
  return SL_RESULT_SUCCESS;
}
static SLresult vol_GetVolumeLevel(void *self, SLmillibel *p) { (void)self; if (p) *p = 0; return SL_RESULT_SUCCESS; }
static SLresult vol_GetMaxVolumeLevel(void *self, SLmillibel *p) { (void)self; if (p) *p = 0; return SL_RESULT_SUCCESS; }
static SLresult vol_SetMute(void *self, SLboolean m) {
  Player *p = CONTAINER(self, Player, vol_vt);
  if (m) p->gain = 0.0f;
  return SL_RESULT_SUCCESS;
}
static SLresult vol_GetMute(void *self, SLboolean *p) { (void)self; if (p) *p = 0; return SL_RESULT_SUCCESS; }
static SLresult vol_enable(void *self, SLboolean e) { (void)self; (void)e; return SL_RESULT_SUCCESS; }
static SLresult vol_isenabled(void *self, SLboolean *p) { (void)self; if (p) *p = 0; return SL_RESULT_SUCCESS; }
static SLresult vol_setpos(void *self, SLint32 v) { (void)self; (void)v; return SL_RESULT_SUCCESS; }
static SLresult vol_getpos(void *self, SLint32 *p) { (void)self; if (p) *p = 0; return SL_RESULT_SUCCESS; }

static const SLVolumeItf_ vol_vtable = {
  vol_SetVolumeLevel, vol_GetVolumeLevel, vol_GetMaxVolumeLevel, vol_SetMute,
  vol_GetMute, vol_enable, vol_isenabled, vol_setpos, vol_getpos,
};

static SLresult rate_SetRate(void *self, SLint16 r) { (void)self; (void)r; return SL_RESULT_SUCCESS; }
static SLresult rate_GetRate(void *self, SLint16 *p) { (void)self; if (p) *p = 1000; return SL_RESULT_SUCCESS; }
static SLresult rate_SetProps(void *self, SLuint32 c) { (void)self; (void)c; return SL_RESULT_SUCCESS; }
static SLresult rate_GetProps(void *self, SLuint32 *p) { (void)self; if (p) *p = 0; return SL_RESULT_SUCCESS; }
static SLresult rate_GetCaps(void *self, SLuint32 *p) { (void)self; if (p) *p = 0; return SL_RESULT_SUCCESS; }
static SLresult rate_GetRange(void *self, SLuint8 i, SLint16 *min, SLint16 *max, SLint16 *step, SLuint32 *prop) {
  (void)self; (void)i;
  if (min)  *min  = 500;
  if (max)  *max  = 2000;
  if (step) *step = 1;
  if (prop) *prop = 0;
  return SL_RESULT_SUCCESS;
}
static const SLPlaybackRateItf_ rate_vtable = {
  rate_SetRate, rate_GetRate, rate_SetProps, rate_GetProps, rate_GetCaps, rate_GetRange,
};

static SLresult cfg_SetConfiguration(void *self, const void *key, const void *value, SLuint32 sz) {
  (void)self; (void)key; (void)value; (void)sz; return SL_RESULT_SUCCESS;
}
static SLresult cfg_GetConfiguration(void *self, const void *key, SLuint32 *psz, void *value) {
  (void)self; (void)key; (void)value; if (psz) *psz = 0; return SL_RESULT_SUCCESS;
}
static SLresult cfg_AcquireJavaProxy(void *self, SLuint32 t, void *p) {
  (void)self; (void)t; if (p) *(void **)p = NULL; return SL_RESULT_FEATURE_UNSUPPORTED;
}
static SLresult cfg_ReleaseJavaProxy(void *self, SLuint32 t) { (void)self; (void)t; return SL_RESULT_SUCCESS; }

static const SLAndroidConfigurationItf_ cfg_vtable = {
  cfg_SetConfiguration, cfg_GetConfiguration, cfg_AcquireJavaProxy, cfg_ReleaseJavaProxy,
};

static SLresult player_GetInterface(void *self, const SLInterfaceID iid, void *pInterface);
static void player_Destroy(void *self);

static SLresult obj_Realize(void *self, SLboolean async) { (void)self; (void)async; return SL_RESULT_SUCCESS; }
static SLresult obj_Resume(void *self, SLboolean async) { (void)self; (void)async; return SL_RESULT_SUCCESS; }
static SLresult obj_GetState(void *self, SLuint32 *pState) { (void)self; if (pState) *pState = SL_OBJECT_STATE_REALIZED; return SL_RESULT_SUCCESS; }
static SLresult obj_RegisterCallback(void *self, void *cb, void *ctx) { (void)self; (void)cb; (void)ctx; return SL_RESULT_SUCCESS; }
static SLresult obj_Abort(void *self) { (void)self; return SL_RESULT_SUCCESS; }
static SLresult obj_SetPriority(void *self, SLint32 a, SLboolean b) { (void)self; (void)a; (void)b; return SL_RESULT_SUCCESS; }
static SLresult obj_GetPriority(void *self, SLint32 *p) { (void)self; if (p) *p = 0; return SL_RESULT_SUCCESS; }
static SLresult obj_SetLOC(void *self, SLint32 a, SLInterfaceID *b, SLboolean c) { (void)self; (void)a; (void)b; (void)c; return SL_RESULT_SUCCESS; }

static SLresult mix_GetInterface(void *self, const SLInterfaceID iid, void *pInterface) {
  (void)self; (void)iid;
  if (pInterface) *(void **)pInterface = NULL;
  return SL_RESULT_FEATURE_UNSUPPORTED;
}
static void simple_Destroy(void *self) { free(self); }

static const SLObjectItf_ player_obj_vtable = {
  obj_Realize, obj_Resume, obj_GetState, player_GetInterface, obj_RegisterCallback,
  obj_Abort, player_Destroy, obj_SetPriority, obj_GetPriority, obj_SetLOC,
};
static const SLObjectItf_ mix_obj_vtable = {
  obj_Realize, obj_Resume, obj_GetState, mix_GetInterface, obj_RegisterCallback,
  obj_Abort, simple_Destroy, obj_SetPriority, obj_GetPriority, obj_SetLOC,
};

static SLresult player_GetInterface(void *self, const SLInterfaceID iid, void *pInterface) {
  Player *p = CONTAINER(self, Player, obj_vt);
  if (!pInterface)
    return SL_RESULT_PARAMETER_INVALID;
  if (iid == SL_IID_PLAY) {
    *(void **)pInterface = &p->play_vt;
  } else if (iid == SL_IID_BUFFERQUEUE || iid == SL_IID_ANDROIDSIMPLEBUFFERQUEUE) {
    *(void **)pInterface = &p->bq_vt;
  } else if (iid == SL_IID_VOLUME) {
    *(void **)pInterface = &p->vol_vt;
  } else if (iid == SL_IID_PLAYBACKRATE) {
    *(void **)pInterface = &p->rate_vt;
  } else if (iid == SL_IID_ANDROIDCONFIGURATION) {
    *(void **)pInterface = &p->config_vt;
  } else {
    *(void **)pInterface = NULL;
    return SL_RESULT_FEATURE_UNSUPPORTED;
  }
  return SL_RESULT_SUCCESS;
}

static void player_Destroy(void *self) {
  Player *p = CONTAINER(self, Player, obj_vt);
  SDL_LockMutex(g_reg_lock);
  for (int i = 0; i < g_player_count; i++)
    if (g_players[i] == p) g_players[i] = NULL;
  SDL_UnlockMutex(g_reg_lock);
  if (p->lock) SDL_DestroyMutex(p->lock);
  free(p);
}

static SLresult eng_CreateAudioPlayer(void *self, SLObjectItf *pPlayer, SLDataSource *src, SLDataSink *snk,
                                      SLuint32 numIfaces, const SLInterfaceID *ids, const SLboolean *req) {
  (void)self; (void)snk; (void)numIfaces; (void)ids; (void)req;
  if (!pPlayer)
    return SL_RESULT_PARAMETER_INVALID;

  Player *p = calloc(1, sizeof(*p));
  if (!p)
    return SL_RESULT_PARAMETER_INVALID;
  p->obj_vt = &player_obj_vtable;
  p->play_vt = &play_vtable;
  p->bq_vt = &bq_vtable;
  p->vol_vt = &vol_vtable;
  p->rate_vt = &rate_vtable;
  p->config_vt = &cfg_vtable;
  p->in_use = 1;
  p->gain = 1.0f;
  p->channels = 2;
  p->rate = 44100;
  p->sbytes = 2;     // assume 16-bit signed PCM unless the format says otherwise
  p->is_float = 0;
  p->lock = SDL_CreateMutex();

  if (src && src->pFormat) {
    const SLDataFormat_PCM *fmt = src->pFormat;
    /* Extended PCM adds a representation field. */
    if (fmt->formatType == 2 || fmt->formatType == 4) {
      p->channels = fmt->numChannels ? (int)fmt->numChannels : 2;
      p->rate = fmt->samplesPerSec ? (int)(fmt->samplesPerSec / 1000) : 44100;
      uint32_t stride_bits = fmt->containerSize ? fmt->containerSize : fmt->bitsPerSample;
      p->sbytes = stride_bits >= 32 ? 4 : 2;
      if (fmt->formatType == 4) {
        uint32_t representation = ((const uint32_t *)fmt)[7]; // field after endianness
        p->is_float = (representation == 2);
      }
    }
  }

  ensure_device(p->rate);

  SDL_LockMutex(g_reg_lock);
  int slot = -1;
  for (int i = 0; i < g_player_count; i++)
    if (g_players[i] == NULL) { slot = i; break; }
  if (slot < 0 && g_player_count < MAX_PLAYERS)
    slot = g_player_count++;
  if (slot < 0) {
    /* Reclaim a drained one-shot player. */
    for (int i = 0; i < g_player_count; i++) {
      Player *q = g_players[i];
      if (q && q->playing && q->drained > 40) {
        g_players[i] = NULL;
        if (q->lock) SDL_DestroyMutex(q->lock);
        free(q);
        slot = i;
        break;
      }
    }
  }
  if (slot >= 0)
    g_players[slot] = p;
  SDL_UnlockMutex(g_reg_lock);

  *pPlayer = &p->obj_vt;
  return SL_RESULT_SUCCESS;
}

static SLresult eng_CreateOutputMix(void *self, SLObjectItf *pMix, SLuint32 numIfaces,
                                    const SLInterfaceID *ids, const SLboolean *req) {
  (void)self; (void)numIfaces; (void)ids; (void)req;
  OutputMix *m = calloc(1, sizeof(*m));
  if (!m)
    return SL_RESULT_PARAMETER_INVALID;
  m->obj_vt = &mix_obj_vtable;
  if (pMix) *pMix = &m->obj_vt;
  return SL_RESULT_SUCCESS;
}

static SLresult eng_unsupported(void) { return SL_RESULT_FEATURE_UNSUPPORTED; }

static const SLEngineItf_ engine_vtable = {
  .CreateLEDDevice = (void *)eng_unsupported,
  .CreateVibraDevice = (void *)eng_unsupported,
  .CreateAudioPlayer = eng_CreateAudioPlayer,
  .CreateAudioRecorder = (void *)eng_unsupported,
  .CreateMidiPlayer = (void *)eng_unsupported,
  .CreateListener = (void *)eng_unsupported,
  .Create3DGroup = (void *)eng_unsupported,
  .CreateOutputMix = eng_CreateOutputMix,
  .CreateMetadataExtractor = (void *)eng_unsupported,
  .CreateExtensionObject = (void *)eng_unsupported,
  .QueryNumSupportedInterfaces = (void *)eng_unsupported,
  .QuerySupportedInterfaces = (void *)eng_unsupported,
  .QueryNumSupportedExtensions = (void *)eng_unsupported,
  .QuerySupportedExtension = (void *)eng_unsupported,
  .IsExtensionSupported = (void *)eng_unsupported,
};

static SLresult engine_GetInterface(void *self, const SLInterfaceID iid, void *pInterface) {
  Engine *e = CONTAINER(self, Engine, obj_vt);
  if (!pInterface)
    return SL_RESULT_PARAMETER_INVALID;
  if (iid == SL_IID_ENGINE) {
    *(void **)pInterface = &e->eng_vt;
    return SL_RESULT_SUCCESS;
  }
  *(void **)pInterface = NULL;
  return SL_RESULT_FEATURE_UNSUPPORTED;
}

static const SLObjectItf_ engine_obj_vtable = {
  obj_Realize, obj_Resume, obj_GetState, engine_GetInterface, obj_RegisterCallback,
  obj_Abort, simple_Destroy, obj_SetPriority, obj_GetPriority, obj_SetLOC,
};

uint32_t slCreateEngine(void **pEngine, uint32_t numOptions, const void *pEngineOptions,
                        uint32_t numInterfaces, const void *pInterfaceIds,
                        const void *pInterfaceRequired) {
  (void)numOptions; (void)pEngineOptions; (void)numInterfaces;
  (void)pInterfaceIds; (void)pInterfaceRequired;
  if (!g_reg_lock)
    g_reg_lock = SDL_CreateMutex();
  if (!pEngine)
    return SL_RESULT_PARAMETER_INVALID;
  Engine *e = calloc(1, sizeof(*e));
  if (!e)
    return SL_RESULT_PARAMETER_INVALID;
  e->obj_vt = &engine_obj_vtable;
  e->eng_vt = &engine_vtable;
  *pEngine = &e->obj_vt;
  return SL_RESULT_SUCCESS;
}

void opensles_shutdown(void) {
  if (g_dev) {
    SDL_CloseAudioDevice(g_dev);
    g_dev = 0;
  }
}
