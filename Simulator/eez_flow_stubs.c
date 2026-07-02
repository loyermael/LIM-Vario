/* Stubs pour les quelques symboles eez-flow reellement references par screens.c
 * (creation des ecrans uniquement -- le moteur Flow n'est jamais tické, ni sur le
 * vrai firmware ni ici : cf main.cpp qui n'appelle jamais ui_tick()/eez_flow_tick()).
 * On evite ainsi de compiler/lier tout eez-flow.cpp (framework C++ complet, MQTT/JSON/...).
 */
#include <stddef.h>
#include <stdint.h>
#include "fonts.h"

void eez_flow_init_fonts(const ext_font_desc_t *fonts, size_t numFonts) {
    (void)fonts; (void)numFonts;
}

void eez_flow_init_screen_names(const char **screenNames, size_t numScreens) {
    (void)screenNames; (void)numScreens;
}

void eez_flow_init_object_names(const char **objectNames, size_t numObjects) {
    (void)objectNames; (void)numObjects;
}

void *getFlowState(void *flowState, unsigned x) {
    (void)flowState; (void)x;
    return NULL;
}

int32_t _evalIntegerProperty(void *flowState, unsigned componentIndex, unsigned propertyIndex,
                              const char *errorMessage, const char *file, int line) {
    (void)flowState; (void)componentIndex; (void)propertyIndex;
    (void)errorMessage; (void)file; (void)line;
    return 0;
}
