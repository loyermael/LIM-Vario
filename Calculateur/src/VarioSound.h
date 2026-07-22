#pragma once

#include <Arduino.h>

class VarioSound {
public:
    // Constructor. I2S output to a MAX98357A class-D amplifier:
    //   dout = serial data (DIN), bclk = bit clock, ws = word select (LRCLK).
    // Defaults match schematic v2 (DIN=25, BCLK=22, LRCLK=23).
    VarioSound(uint8_t dout, uint8_t bclk = 22, uint8_t ws = 23);

    // Hardware initialization (to be called inside setup)
    void begin();

    // Updates vertical speed (Vz) in m/s
    void setVz(float vz);

    // Enables or disables sink alarm audio (sink sound mute/full)
    void setSinkAlarm(bool enable);

    // Software volume control 0..20 (digital amplitude scale; eliminates hardware potentiometer)
    void setVolume(uint8_t vol);

    // --- Sound menu settings (received from display unit via lim_scfg_t) ---
    void setCenterFreq(float hz);   // Tone pitch : Base frequency (Hz)
    void setSpread(uint8_t s);      // Tone spread : 0..10 (frequency variation intensity vs vario)
    void setWaveform(uint8_t w);    // Waveform : 0=Sine 1=Square 2=Triangle

    // Main update tick routine to be invoked periodically inside loop()
    void tick();

private:
    uint8_t _pin;    // I2S data out (DIN)
    uint8_t _bclk;   // I2S bit clock
    uint8_t _ws;     // I2S word select (LRCLK)
    float _vz;
    bool _sinkAlarmEnabled;
    uint8_t _vol = 12;  // volume level 0..20

    // Default acoustic thresholds
    float _thresholdClimb = 0.2f;
    float _thresholdSink = -0.5f;

    // Tone generation algorithm parameters
    float _sndCenterFreq = 500.0f;
    float _sndExpMul = 0.138629f;

    // Acoustic state machine state
    enum State {
        SILENCE,
        BEEP_ON,
        BEEP_OFF,
        CONTINUOUS
    };
    State _state;
    
    uint32_t _lastTickMs;
    uint32_t _nextTransitionMs;
    
    // Calculated current output tone frequency
    uint32_t _currentFreq;
    // Calculated current beep duration
    uint32_t _currentDuration;
    
    void stopTone();
    void playTone(uint32_t freq);
};
