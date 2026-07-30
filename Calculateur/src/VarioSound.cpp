/* ============================================================
 *  VarioSound - Sine/Square/Triangle over I2S (MAX98357A class-D amp)
 *
 *  Output backend: ESP-IDF 5.x standard I2S driver (driver/i2s_std.h).
 *  Replaces the former internal-DAC (GPIO25) path: the MAX98357A is a
 *  DIGITAL amplifier, so the calculator now streams 16-bit PCM over I2S.
 *
 *  Pins (calculator, see schematic v2):
 *    BCLK  = GPIO22   (bit clock)
 *    LRCLK = GPIO23   (word select / WS)
 *    DIN   = GPIO25   (serial data -> MAX98357A DIN)
 *  MAX98357A SD and GAIN left unconnected (amp always on, 9 dB default).
 *
 *  Architecture highlights (unchanged from the DAC version):
 *  - Audio task pinned to Core 1 -> WiFi (Core 0) never preempts audio
 *  - Frequency inc / amplitude sampled ONCE per DMA buffer -> zero mid-buffer click
 *  - Phase reset to 0 on tone start -> clean attack envelope
 *  - 8 DMA descriptors x 256 frames -> ~46 ms pipeline headroom
 *  - Silence = a buffer of digital zeros (class-D amp = true silence, no carrier)
 *  - Square-root perceptual loudness mapping curve
 * ============================================================ */
#include "VarioSound.h"
#include <math.h>
#include <string.h>
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SAMPLE_RATE      44100
#define DMA_FRAME_NUM    256                     // frames per DMA buffer (5.8 ms @ 44.1k)
#define DMA_DESC_NUM     8                       // 8 * 5.8 ms = 46 ms audio buffer depth

// Full 16-bit samples (-32000..+32000, a hair under int16 range for multiply headroom).
// Was uint8_t (256 amplitude levels total, ~48 dB of resolution) -> that quantization
// floor is what made the tone sound gritty/lo-fi ("8-bit"-sounding) rather than clean,
// especially at low volume where the already-coarse steps get scaled down even further.
// Phase resolution (256 table entries/cycle) was never the issue -- it's plenty even at
// 1500 Hz (~29 samples/cycle @ 44.1 kHz) since consecutive samples don't visit adjacent
// entries; only the AMPLITUDE axis needed the extra bits.
static int16_t  s_sinTab[256];
static uint32_t s_phase  = 0;  // Modified exclusively from the dedicated audio task

// Populates wave lookup table (read by audio task). 0 = Sine, 1 = Square, 2 = Triangle.
static void fill_wave_table(uint8_t w)
{
    for (int i = 0; i < 256; i++) {
        int16_t val;
        switch (w) {
            case 1:  val = (i < 128) ? 32000 : -32000; break;                              // Square
            case 2: {                                                                      // Triangle
                float x = (float)i / 256.0f;                                                // 0..1
                float t = (x < 0.5f) ? (4.0f * x - 1.0f) : (3.0f - 4.0f * x);               // -1..+1..-1
                val = (int16_t)(t * 32000.0f);
                break;
            }
            default: val = (int16_t)(32000.0f * sinf(2.0f * M_PI * i / 256.0f)); break;    // Sine
        }
        s_sinTab[i] = val;
    }
}

// Volatile: written from loop() (Core 1), read from audio task (Core 1).
// No true parallelism, but volatile prevents register caching.
static volatile uint32_t s_phaseInc = 0;   // 0 = silence
static volatile uint8_t  s_amp      = 0;   // 0..127

static i2s_chan_handle_t s_txHandle = NULL;

// ---- Audio Task (Core 1, priority 5) -----------------------------------
// Pinned to Core 1 = same core as loop(), but priority 5 > loop priority (1).
// WiFi stack resides on Core 0 -> NEVER preempts this real-time audio generator.
// The I2S channel stays enabled: i2s_channel_write() blocks on DMA space, which
// paces the loop at real time. Silence is streamed as zeros (no analog carrier).
static void audio_task_fn(void*)
{
    static int16_t buf[DMA_FRAME_NUM * 2];   // interleaved L/R, 16-bit
    static float   curAmp  = 0.0f;           // smoothed amplitude envelope (0..127), see below
    static uint32_t lastInc = 0;             // last active phase increment, held during release
    bool prevTone = false;
    size_t written;

    for (;;) {
        // ---- Read control parameters ONCE for the entire DMA buffer ----
        uint32_t inc = s_phaseInc;
        uint8_t  amp = s_amp;
        bool     hasTone = (inc != 0) && (amp != 0);

        // Clean attack: restart phase at zero-crossing, but ONLY when coming from genuine
        // silence (curAmp already settled near 0). If the beep state re-triggers fast (vz
        // dithering right around the beep threshold re-fires SILENCE->BEEP_ON repeatedly
        // before the previous envelope finished decaying), the waveform is still actively
        // playing at an audible level -- forcing the phase to 0 there is itself an abrupt
        // jump in the sample value while curAmp isn't near zero to mask it, i.e. exactly the
        // kind of click this was meant to prevent. Only reset when there's nothing playing
        // to interrupt; otherwise let the (fast, ~3 ms) envelope carry the transition and
        // leave the phase running continuously.
        if (hasTone && !prevTone && curAmp < 0.5f) s_phase = 0;
        prevTone = hasTone;
        if (hasTone) lastInc = inc;

        float target = hasTone ? (float)amp : 0.0f;
        uint32_t useInc = hasTone ? inc : lastInc;   // hold last freq through fade-out (see below)

        // ---- Generate waveform buffer (mono duplicated on L and R) ----
        if (useInc != 0) {
            // Amplitude envelope, updated ONCE PER SAMPLE (not once per DMA buffer -- a
            // block-rate update was itself introducing "zipper noise", audible as its own
            // small roughness). tau = 1/(44100*ENV_ALPHA) ~= 2.3 ms, ~95% risen in ~7 ms:
            // a middle ground between an earlier value that was too slow (~5.7 ms tau,
            // ~17-28 ms to reach full level -- swallowed most of each beep whenever the
            // state machine re-triggered faster than that, close to silent) and one that
            // was too fast to fully mask the click (~1.1 ms tau, ~3 ms). useInc keeps the
            // oscillator running at the last active frequency through the fade-out --
            // inc=0 there would freeze the waveform at a fixed sample (DC), turning the
            // "fade" into its own click instead of a real decay.
            const float ENV_ALPHA = 0.01f;
            for (int i = 0; i < DMA_FRAME_NUM; i++) {
                curAmp += (target - curAmp) * ENV_ALPHA;
                s_phase += useInc;
                // s_phase>>24 alone (256 discrete positions/cycle, no interpolation) is what
                // still made the tone sound "steppy"/digital even with a 16-bit-amplitude
                // table: between two lookups the output snaps instead of gliding, so the
                // waveform itself is still a 256-point staircase. Interpolate linearly between
                // the two neighbouring table entries using the next 8 phase bits as the
                // fractional position -- this is what actually removes the audible "steps".
                uint32_t idx  = s_phase >> 24;
                uint32_t frac = (s_phase >> 16) & 0xFF;
                int32_t  s0 = s_sinTab[idx];
                int32_t  s1 = s_sinTab[(idx + 1) & 0xFF];
                int32_t  s  = s0 + (((s1 - s0) * (int32_t)frac) >> 8);
                int16_t  v  = (int16_t)((float)s * (curAmp * (1.0f / 127.0f)));
                buf[2 * i]     = v;   // Left
                buf[2 * i + 1] = v;   // Right (MAX98357A averages L+R when SD floats)
            }
            // No forced snap-to-zero here anymore: it used to force curAmp to exactly 0.0f
            // once per BUFFER (outside the per-sample loop above), which is itself a small
            // step discontinuity right at that buffer boundary -- the one-pole decay below
            // 0.5/127 (~0.4% of full scale) is already inaudible, so let it settle there
            // asymptotically instead of forcing a boundary jump to fix it.
        } else {
            memset(buf, 0, sizeof(buf));
            curAmp = 0.0f;
        }

        // ---- Write to DMA engine (blocks until buffer space frees) ----
        i2s_channel_write(s_txHandle, buf, sizeof(buf), &written, portMAX_DELAY);
    }
}

// ---- Constructor -----------------------------------------------------------
VarioSound::VarioSound(uint8_t dout, uint8_t bclk, uint8_t ws)
    : _pin(dout), _bclk(bclk), _ws(ws), _vz(0.0f), _sinkAlarmEnabled(false),
      _state(SILENCE), _lastTickMs(0), _nextTransitionMs(0),
      _currentFreq(0), _currentDuration(0) {}

// ---- begin() ---------------------------------------------------------------
void VarioSound::begin()
{
    fill_wave_table(0);   // Default sine wave (the display will send configured waveform)

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num  = DMA_DESC_NUM;
    chan_cfg.dma_frame_num = DMA_FRAME_NUM;
    chan_cfg.auto_clear    = true;   // stream zeros on DMA underflow instead of garbage

    if (i2s_new_channel(&chan_cfg, &s_txHandle, NULL) != ESP_OK) {
        Serial.println("[VarioSound] ERR i2s_new_channel");
        return;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)_bclk,
            .ws   = (gpio_num_t)_ws,
            .dout = (gpio_num_t)_pin,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    if (i2s_channel_init_std_mode(s_txHandle, &std_cfg) != ESP_OK) {
        Serial.println("[VarioSound] ERR i2s_channel_init_std_mode");
        return;
    }
    if (i2s_channel_enable(s_txHandle) != ESP_OK) {
        Serial.println("[VarioSound] ERR i2s_channel_enable");
        return;
    }

    // Core 1 = APP_CPU (same core as loop), priority 5 > loop priority (1)
    xTaskCreatePinnedToCore(audio_task_fn, "i2s", 4096, NULL, 5, NULL, 1);
}

// ---- Setters ---------------------------------------------------------------
void VarioSound::setVz(float vz)        { _vz = vz; }
void VarioSound::setSinkAlarm(bool en)  { _sinkAlarmEnabled = en; }
void VarioSound::setVolume(uint8_t vol) { _vol = (vol > 20) ? 20 : vol; }

// --- Sound menu settings (received from display unit) ---
void VarioSound::setCenterFreq(float hz) {
    if (hz < 200.0f)  hz = 200.0f;
    if (hz > 1500.0f) hz = 1500.0f;
    _sndCenterFreq = hz;
}
// spread 0..10 -> _sndExpMul. 5 = reference value (0.138629), 0 = fixed frequency.
void VarioSound::setSpread(uint8_t s) {
    if (s > 10) s = 10;
    _sndExpMul = (float)s * (0.138629f / 5.0f);
}
void VarioSound::setWaveform(uint8_t w) {
    if (w > 2) w = 0;
    fill_wave_table(w);
}

// ---- Acoustic primitives ---------------------------------------------------
void VarioSound::stopTone()
{
    s_phaseInc = 0;
    s_amp      = 0;
}

void VarioSound::playTone(uint32_t freq)
{
    // Volume 0 = total silence
    if (_vol == 0) { stopTone(); return; }

    // Square root loudness mapping: human auditory perception scales logarithmically.
    // sqrt provides significantly more "punch" at mid-volume and a usable perception span.
    s_amp = (uint8_t)(127.0f * sqrtf((float)_vol / 20.0f) + 0.5f);
    s_phaseInc = (uint32_t)((float)freq * (4294967296.0f / SAMPLE_RATE));
}

// ---- tick() : Beep cadence state machine update ----------------------------
void VarioSound::tick()
{
    uint32_t now = millis();

    float freqF = _sndCenterFreq * expf(_sndExpMul * _vz);
    if (freqF < 200.0f)  freqF = 200.0f;
    if (freqF > 1500.0f) freqF = 1500.0f;
    uint32_t newFreq = (uint32_t)freqF;

    float vc = _vz;
    if (vc < 0.2f) vc = 0.2f;
    if (vc > 4.0f) vc = 4.0f;
    int32_t dur = (int32_t)(300.0f - 60.0f * vc);
    if (dur < 50) dur = 50;
    uint32_t newDuration = (uint32_t)dur;

    State targetState;
    if      (_vz > _thresholdClimb) targetState = BEEP_ON;
    else if (_vz < _thresholdSink)  targetState = _sinkAlarmEnabled ? CONTINUOUS : SILENCE;
    else                            targetState = SILENCE;

    if (_state == SILENCE) {
        if (targetState == BEEP_ON) {
            _state = BEEP_ON;
            _currentFreq = newFreq; _currentDuration = newDuration;
            playTone(_currentFreq); _nextTransitionMs = now + _currentDuration;
        } else if (targetState == CONTINUOUS) {
            _state = CONTINUOUS;
            _currentFreq = newFreq; playTone(_currentFreq);
        }

    } else if (_state == CONTINUOUS) {
        if      (targetState == SILENCE) { _state = SILENCE; stopTone(); }
        else if (targetState == BEEP_ON) {
            _state = BEEP_ON;
            _currentFreq = newFreq; _currentDuration = newDuration;
            playTone(_currentFreq); _nextTransitionMs = now + _currentDuration;
        } else if (_currentFreq != newFreq) {
            _currentFreq = newFreq; playTone(_currentFreq);
        }

    } else {  // BEEP_ON or BEEP_OFF
        if      (targetState == SILENCE)    { _state = SILENCE;    stopTone(); }
        else if (targetState == CONTINUOUS) { _state = CONTINUOUS; _currentFreq = newFreq; playTone(_currentFreq); }
        else if (now >= _nextTransitionMs) {
            if (_state == BEEP_ON) {
                _state = BEEP_OFF; stopTone();
                _nextTransitionMs = now + _currentDuration;
            } else {
                _state = BEEP_ON;
                _currentFreq = newFreq; _currentDuration = newDuration;
                playTone(_currentFreq); _nextTransitionMs = now + _currentDuration;
            }
        }
    }
}
