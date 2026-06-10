/* Implémentation des variables natives EEZ Flow */
#include "vars.h"

static float g_vario = 0.0f;

float get_var_vario() {
    return g_vario;
}

void set_var_vario(float value) {
    g_vario = value;
}
