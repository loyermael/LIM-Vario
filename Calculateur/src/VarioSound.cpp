/* ============================================================
 *  VarioSound - Sine/Square/Triangle DAC via dac_continuous (DMA 44100 Hz)
 *
 *  Architecture highlights:
 *  - Audio task pinned to Core 1 (not Core 0) -> WiFi interrupts no longer preempt acoustic output
 *  - Frequency inc / amplitude sampled ONCE per DMA buffer -> zero clicking mid-buffer
 *  - Phase reset to 0 on tone start -> clean attack envelope
 *  - 8 DMA buffer descriptors -> 46 ms pipeline absorption headroom against system bursts
 *  - DMA disabled during silence periods -> eliminates residual I2S0 carrier noise
 *  - Square root perceptual loudness mapping curve
 * ============================================================ */
#include "VarioSound.h"
#include <math.h>
#include "driver/dac_continuous.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SAMPLE_RATE      44100
#define DMA_BUF_SAMPLES  256
#define DMA_BUF_BYTES    (DMA_BUF_SAMPLES * 2)   // 16-bit internal ESP32 DMA layout
#define DMA_DESC_NUM     8                       // 8 * 5.8 ms = 46 ms audio buffer depth
#define SILENCE_BUFS     3                       // Silent padding buffers prior to halting DMA pipeline

static uint8_t  s_sinTab[256];
static uint32_t s_phase  = 0;  // Modified exclusively from the dedicated audio task

// Populates wave lookup table (read by audio task). Centered at 128 mid-scale.
//  0 = Sine, 1 = Square, 2 = Triangle.
static void fill_wave_table(uint8_t w)
{
    for (int i = 0; i < 256; i++) {
        uint8_t val;
        switch (w) {
            case 1:  val = (i < 128) ? 255 : 0; break;                                  // Square
            case 2:  val = (i < 128) ? (uint8_t)(i * 2) : (uint8_t)(254 - (i - 128) * 2); break;  // Triangle
            default: val = (uint8_t)(128.0f + 127.0f * sinf(2.0f * M_PI * i / 256.0f)); break;    // Sine
        }
        s_sinTab[i] = val;
    }
}

// Volatile: accessed from Core 1 (loop) AND Core 1 (audio task) -> no thread parallelism
// but volatile ensures compiler does not cache variables in CPU registers.
static volatile uint32_t s_phaseInc = 0;   // 0 = silence
static volatile uint8_t  s_amp      = 0;   // 0..127

static dac_continuous_handle_t s_dacHandle  = NULL;
static TaskHandle_t             s_audioTask  = NULL;
static volatile bool            s_dacRunning = false;

// ---- Audio Task (Core 1, priority 5) -----------------------------------
// Pinned to Core 1 = same core as loop(), but priority 5 > priority 1 of loop().
// WiFi stack resides on Core 0 -> NEVER preempts this real-time audio generator.
static void audio_task_fn(void*)
{
    static uint8_t buf[DMA_BUF_SAMPLES];
    uint32_t silentBufs = 0;

    for (;;) {
        // ---- Suspend task while DMA pipeline is dormant ----
        if (!s_dacRunning) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            s_phase = 0;                        // Clean start at zero-crossing
            dac_continuous_enable(s_dacHandle);
            s_dacRunning = true;
            silentBufs   = 0;
        }

        // ---- Read control parameters ONCE for the entire DMA buffer ----
        uint32_t inc = s_phaseInc;
        uint8_t  amp = s_amp;
        bool     hasTone = (inc != 0) && (amp != 0);

        // ---- Generate waveform buffer ----
        if (hasTone) {
            for (int i = 0; i < DMA_BUF_SAMPLES; i++) {
                s_phase += inc;
                int32_t s = (int32_t)s_sinTab[s_phase >> 24] - 128;
                buf[i] = (uint8_t)(128 + (s * (int32_t)amp) / 127);
            }
        } else {
            for (int i = 0; i < DMA_BUF_SAMPLES; i++) buf[i] = 128;
        }

        // ---- Write output to DMA engine ----
        dac_continuous_write(s_dacHandle, buf, DMA_BUF_SAMPLES, NULL, 1000);

        // ---- Disable DMA pipeline after N consecutive silent buffers ----
        if (!hasTone) {
            if (++silentBufs >= SILENCE_BUFS) {
                dac_continuous_disable(s_dacHandle);
                s_dacRunning = false;
            }
        } else {
            silentBufs = 0;
        }
    }
}

// ---- Constructor -----------------------------------------------------------
VarioSound::VarioSound(uint8_t pin)
    : _pin(pin), _vz(0.0f), _sinkAlarmEnabled(false),
      _state(SILENCE), _lastTickMs(0), _nextTransitionMs(0),
      _currentFreq(0), _currentDuration(0) {}

// ---- begin() ---------------------------------------------------------------
void VarioSound::begin()
{
    fill_wave_table(0);   // Default sine wave (the display will send configured waveform)

    dac_continuous_config_t cfg = {
        .chan_mask = DAC_CHANNEL_MASK_CH0,   // GPIO25
        .desc_num  = DMA_DESC_NUM,
        .buf_size  = DMA_BUF_BYTES,
        .freq_hz   = SAMPLE_RATE,
        .offset    = 0,
        .clk_src   = DAC_DIGI_CLK_SRC_APLL,
        .chan_mode  = DAC_CHANNEL_MODE_SIMUL,
    };
    if (dac_continuous_new_channels(&cfg, &s_dacHandle) != ESP_OK) {
        Serial.println("[VarioSound] ERR dac_continuous_new_channels");
        return;
    }
    // DMA not activated here: the audio task activates it upon the first beep.

    // Core 1 = APP_CPU (same core as loop), priority 5 > loop priority (1)
    xTaskCreatePinnedToCore(audio_task_fn, "dac", 2048, NULL, 5, &s_audioTask, 1);
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
// spread 0..10 -> _sndExpMul. 5 = standard Larus value (0.138629), 0 = fixed frequency.
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
    // Volume 0 = total silence: do not wake up DMA engine (prevents residual pops/clicks)
    if (_vol == 0) { stopTone(); return; }

    // Square root loudness mapping: human auditory perception scales logarithmically.
    // sqrt provides significantly more "punch" at mid-volume and a usable perception span.
    s_amp = (uint8_t)(127.0f * sqrtf((float)_vol / 20.0f) + 0.5f);
    s_phaseInc = (uint32_t)((float)freq * (4294967296.0f / SAMPLE_RATE));

    // Wake up audio task only if currently dormant (DMA pipeline halted)
    if (!s_dacRunning && s_audioTask)
        xTaskNotifyGive(s_audioTask);
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
