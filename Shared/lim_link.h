/* ============================================================
 *  L!M Vario - Protocole de liaison CALCULATEUR <-> ECRAN
 *  Fichier PARTAGE par les 2 projets (Calculateur + Firmware).
 *
 *  Liaison UART, duplex :
 *    Calculateur -> Ecran : trame binaire fixe ~50 Hz (lim_packet_t)
 *    Ecran -> Calculateur : trame de commande legere sur changement (lim_cmd_t)
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

// Bits du champ "flags" (trame Calculateur -> Ecran)
#define LIM_FLAG_BMP_OK   0x01   // BMP388 lu correctement
#define LIM_FLAG_SPD_OK   0x02   // MS4525 present -> vario compense TE
#define LIM_FLAG_GPS_OK   0x04   // fix GPS valide -> airspeed = vitesse sol GPS

// Bits du champ "cmd" (trame Ecran -> Calculateur)
#define LIM_CMD_SINK_SOUND 0x01  // 1 = son descente actif (Full), 0 = silencieux (Mute)

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

// ============================================================
//  Trame de commande : ECRAN -> CALCULATEUR (3 octets)
//  Envoye uniquement sur changement d'etat (pas en continu).
//  Sync differents pour ne pas confondre avec lim_packet_t.
// ============================================================
#define LIM_CMD_SYNC0  0xC3
#define LIM_CMD_SYNC1  0x3C

#pragma pack(push, 1)
typedef struct {
  uint8_t sync0;  // 0xC3
  uint8_t sync1;  // 0x3C
  uint8_t cmd;    // LIM_CMD_* flags
  uint8_t crc8;   // XOR de sync0 ^ sync1 ^ cmd (verification simple)
} lim_cmd_t;
#pragma pack(pop)

// Cote ECRAN : prepare et envoie la trame de commande
static inline void lim_cmd_send(uint8_t cmd, void* serial_ptr) {
  lim_cmd_t c;
  c.sync0 = LIM_CMD_SYNC0;
  c.sync1 = LIM_CMD_SYNC1;
  c.cmd   = cmd;
  c.crc8  = c.sync0 ^ c.sync1 ^ c.cmd;
  // Cast generique: l'appelant passe son objet HardwareSerial* caste en void*
  // On utilise une macro pour eviter une dependance Arduino dans ce header C pur
  // -> voir lim_cmd_write() ci-dessous
  (void)serial_ptr; // non utilise ici, macro recommandee
}

// Macro pratique pour envoyer depuis un HardwareSerial Arduino :
// LIM_CMD_SEND(Serial1, cmd)
#define LIM_CMD_SEND(serial, cmd_val) do { \
  lim_cmd_t _c; \
  _c.sync0 = LIM_CMD_SYNC0; \
  _c.sync1 = LIM_CMD_SYNC1; \
  _c.cmd   = (cmd_val); \
  _c.crc8  = _c.sync0 ^ _c.sync1 ^ _c.cmd; \
  (serial).write((const uint8_t*)&_c, sizeof(_c)); \
} while(0)

// Cote CALCULATEUR : valide une trame de commande recue
static inline bool lim_cmd_check(const lim_cmd_t* c) {
  if (c->sync0 != LIM_CMD_SYNC0 || c->sync1 != LIM_CMD_SYNC1) return false;
  return c->crc8 == (uint8_t)(c->sync0 ^ c->sync1 ^ c->cmd);
}
