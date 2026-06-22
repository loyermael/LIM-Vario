#include "VarioSound.h"
#include <math.h>

VarioSound::VarioSound(uint8_t pin) 
    : _pin(pin), _vz(0.0f), _sinkAlarmEnabled(false),
      _state(SILENCE), _lastTickMs(0), _nextTransitionMs(0),
      _currentFreq(0), _currentDuration(0) 
{
}

void VarioSound::begin() {
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, LOW);
}

void VarioSound::setVz(float vz) {
    _vz = vz;
}

void VarioSound::setSinkAlarm(bool enable) {
    _sinkAlarmEnabled = enable;
}

void VarioSound::stopTone() {
    noTone(_pin);
    digitalWrite(_pin, LOW);
}

void VarioSound::playTone(uint32_t freq) {
    tone(_pin, freq);
}

void VarioSound::tick() {
    uint32_t now = millis();

    // 1. Détermination de la fréquence et de la durée (selon l'algorithme Larus)
    // On calcule la fréquence cible quoi qu'il arrive (pour la montée ou la descente)
    float freqF = _sndCenterFreq * expf(_sndExpMul * _vz);
    
    // Clamp entre 200 Hz et 3000 Hz
    if (freqF < 200.0f) freqF = 200.0f;
    if (freqF > 3000.0f) freqF = 3000.0f;
    
    uint32_t newFreq = (uint32_t)freqF;
    uint32_t newDuration = _sndDutyCycleMult / newFreq;

    // 2. Détermination de l'état cible en fonction de la Vz
    State targetState;
    if (_vz > _thresholdClimb) {
        targetState = BEEP_ON; // C'est un état dynamique (bascule ON/OFF)
    } else if (_vz < _thresholdSink) {
        if (_sinkAlarmEnabled) {
            targetState = CONTINUOUS;
        } else {
            targetState = SILENCE;
        }
    } else {
        targetState = SILENCE;
    }

    // 3. Gestion de l'automate d'états
    if (_state == SILENCE) {
        if (targetState == BEEP_ON) {
            _state = BEEP_ON;
            _currentFreq = newFreq;
            _currentDuration = newDuration;
            playTone(_currentFreq);
            _nextTransitionMs = now + _currentDuration;
        } else if (targetState == CONTINUOUS) {
            _state = CONTINUOUS;
            _currentFreq = newFreq;
            playTone(_currentFreq);
        }
        // Sinon reste SILENCE
        
    } else if (_state == CONTINUOUS) {
        if (targetState == SILENCE) {
            _state = SILENCE;
            stopTone();
        } else if (targetState == BEEP_ON) {
            _state = BEEP_ON;
            _currentFreq = newFreq;
            _currentDuration = newDuration;
            playTone(_currentFreq);
            _nextTransitionMs = now + _currentDuration;
        } else {
            // Reste CONTINUOUS, on met à jour la fréquence si elle change
            if (_currentFreq != newFreq) {
                _currentFreq = newFreq;
                playTone(_currentFreq);
            }
        }
        
    } else if (_state == BEEP_ON || _state == BEEP_OFF) {
        if (targetState == SILENCE) {
            _state = SILENCE;
            stopTone();
        } else if (targetState == CONTINUOUS) {
            _state = CONTINUOUS;
            _currentFreq = newFreq;
            playTone(_currentFreq);
        } else {
            // Toujours en mode BEEP
            // Si on est dans un bip ou une pause, on attend la fin
            if (now >= _nextTransitionMs) {
                if (_state == BEEP_ON) {
                    // Fin du bip -> on passe en pause
                    _state = BEEP_OFF;
                    stopTone();
                    _nextTransitionMs = now + _currentDuration; 
                } else {
                    // Fin de la pause -> on lance le prochain bip avec les nouvelles valeurs
                    _state = BEEP_ON;
                    _currentFreq = newFreq;
                    _currentDuration = newDuration;
                    playTone(_currentFreq);
                    _nextTransitionMs = now + _currentDuration;
                }
            } else if (_state == BEEP_ON) {
                // Pendant qu'un bip joue, on peut glisser la fréquence (pitch bend) 
                // pour plus de réactivité, comme le fait Larus (bien que ça marche très bien sans)
                if (_currentFreq != newFreq) {
                    _currentFreq = newFreq;
                    playTone(_currentFreq);
                }
            }
        }
    }
}
