/* ============================================================
 *  L!M Vario - Protocole de liaison CALCULATEUR -> ECRAN
 *  Fichier PARTAGE par les 2 projets (Calculateur + Firmware).
 *
 *  Liaison UART, trame binaire a taille fixe, ~50 Hz, simplex
 *  (le calculateur envoie, l'ecran ecoute).
 *
 *  Le calculateur envoie : donnees vario + etat des 2 encodeurs.
 *  L'ecran : affiche le vario et alimente sa logique menu avec
 *            les encodeurs recus (deltas + boutons).
 * ============================================================ */
#pragma once
#include <stdint.h>

#define LIM_SYNC0    0xA5
#define LIM_SYNC1    0x5A
#define LIM_VERSION  2           // v2 : envoie la pression (QNH applique cote ecran)
#define LIM_BAUD     115200      // debit UART de la liaison (fiable sur les 2 ESP)

// Bits du champ "flags"
#define LIM_FLAG_BMP_OK   0x01   // BMP388 lu correctement
#define LIM_FLAG_SPD_OK   0x02   // MS4525 present -> vario compense TE

#pragma pack(push, 1)
typedef struct {
  uint8_t  sync0;       // 0xA5
  uint8_t  sync1;       // 0x5A
  uint8_t  ver;         // LIM_VERSION
  uint8_t  flags;       // LIM_FLAG_*
  float    pressure;    // Pa (pression absolue brute -> l'ecran calcule l'altitude avec le QNH)
  float    vario;       // m/s (compense TE si dispo)
  float    vario_int;   // m/s (integre ~20 s)
  float    airspeed;    // m/s
  int32_t  enc1_count;  // position cumulee encodeur 1 (crans)
  int32_t  enc2_count;  // position cumulee encodeur 2 (crans)
  uint8_t  enc1_btn;    // niveau bouton enc1 (1 = appuye)
  uint8_t  enc2_btn;    // niveau bouton enc2 (1 = appuye)
  uint16_t crc;         // CRC16-CCITT sur tous les octets precedents
} lim_packet_t;
#pragma pack(pop)

// CRC16-CCITT (poly 0x1021, init 0xFFFF)
static inline uint16_t lim_crc16(const uint8_t* d, uint32_t n) {
  uint16_t crc = 0xFFFF;
  for (uint32_t i = 0; i < n; i++) {
    crc ^= (uint16_t)d[i] << 8;
    for (int b = 0; b < 8; b++)
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

// Cote CALCULATEUR : termine un paquet (sync/ver/flags/crc) deja rempli de donnees
static inline void lim_finalize(lim_packet_t* p, uint8_t flags) {
  p->sync0 = LIM_SYNC0;
  p->sync1 = LIM_SYNC1;
  p->ver   = LIM_VERSION;
  p->flags = flags;
  p->crc   = lim_crc16((const uint8_t*)p, sizeof(lim_packet_t) - sizeof(uint16_t));
}

// Cote ECRAN : valide un paquet recu
static inline bool lim_check(const lim_packet_t* p) {
  if (p->sync0 != LIM_SYNC0 || p->sync1 != LIM_SYNC1) return false;
  if (p->ver != LIM_VERSION) return false;
  return p->crc == lim_crc16((const uint8_t*)p, sizeof(lim_packet_t) - sizeof(uint16_t));
}
