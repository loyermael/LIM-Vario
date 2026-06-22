#pragma once

#include <Arduino.h>

class VarioSound {
public:
    // Constructeur avec la broche choisie pour le signal audio (PWM/tone)
    VarioSound(uint8_t pin);

    // Initialisation (à appeler dans setup si besoin)
    void begin();

    // Met à jour la vitesse verticale (Vz) en m/s
    void setVz(float vz);

    // Active ou désactive le son de descente (sink sound mute)
    void setSinkAlarm(bool enable);

    // Fonction à appeler le plus souvent possible dans loop()
    void tick();

private:
    uint8_t _pin;
    float _vz;
    bool _sinkAlarmEnabled;

    // Seuils par défaut
    float _thresholdClimb = 0.2f;
    float _thresholdSink = -0.5f;

    // Variables pour le calcul basé sur l'algorithme Larus
    float _sndCenterFreq = 659.0f;     // Fréquence de base à 0 m/s (Mi 5)
    float _sndExpMul = 0.138629f;      // Multiplicateur exponentiel (double tous les 5 m/s)
    uint32_t _sndDutyCycleMult = 200000; // Constante pour la durée des bips/pauses

    // Etat de l'automate
    enum State {
        SILENCE,
        BEEP_ON,
        BEEP_OFF,
        CONTINUOUS
    };
    State _state;
    
    uint32_t _lastTickMs;
    uint32_t _nextTransitionMs;
    
    // Fréquence courante calculée
    uint32_t _currentFreq;
    // Durée courante du bip calculée
    uint32_t _currentDuration;
    
    void stopTone();
    void playTone(uint32_t freq);
};
