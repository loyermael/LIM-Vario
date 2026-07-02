/* Simulateur LVGL natif (Win32/GDI) du L!M Vario.
 * Compile le vrai screens.c genere par EEZ Studio avec le vrai LVGL 8.4 utilise sur
 * l'ESP32-S3, meme lv_conf.h (RGB565, memes polices/tailles) -> rendu pixel-identique
 * a l'ecran physique et a l'apercu EEZ Studio. Le moteur Flow d'EEZ n'est pas utilise
 * (jamais tické sur le vrai firmware non plus, cf main.cpp) : create_screens() est
 * appelee directement, cf eez_flow_stubs.c pour les quelques symboles necessaires
 * a la seule CREATION des ecrans.
 *
 * La logique de menu (setup + quick menu + editeur info boxes) est portee dans
 * sim_menu.c (copie de main.cpp, hardware remplace par des stubs). Ce fichier ne fait
 * que la fenetre Win32 + le driver LVGL + la traduction clavier -> encodeurs simules :
 *   Haut/Bas    = rotation ENC1 (navigation menu / edition / reglage MacCready)
 *   Entree      = bouton ENC1 (appui court = valider ; maintenu 600ms+ = ouvrir/fermer
 *                 le setup, ou fermer le quick menu -- exactement le seuil reel LONG_PRESS_MS)
 *   Gauche/Droite = rotation ENC2 (volume)
 *   Espace      = bouton ENC2 (maintenu 600ms+ = bascule App connect / WiFi logs)
 *   Molette souris = alias de Haut/Bas (confort)
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdio.h>

#include "lvgl.h"
#include "screens.h"
#include "images.h"

#define SCR_W 480
#define SCR_H 480

static lv_color_t s_buf1[SCR_W * SCR_H];
static uint32_t   s_rgb32[SCR_W * SCR_H];   /* framebuffer BGRA pour GDI (32bpp BI_RGB) */
static HWND       s_hwnd;

/* API exposee par sim_menu.c */
extern void SimMenu_Init(void);
extern void SimMenu_Tick(double t);
extern void SimMenu_OnRotate1(long delta);
extern void SimMenu_OnButton1(void);
extern void SimMenu_OnLongPress1(void);
extern void SimMenu_OnRotate2(long delta);
extern void SimMenu_OnLongPress2(void);

#define LONG_PRESS_MS 600   /* meme seuil que Firmware/src/main.cpp */

/* ---- Driver d'affichage : recopie le buffer LVGL (RGB565) dans s_rgb32 (32bpp) ---- */
static void my_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    for (lv_coord_t y = area->y1; y <= area->y2; y++) {
        for (lv_coord_t x = area->x1; x <= area->x2; x++) {
            lv_color_t c = *color_p++;
            uint8_t r = (uint8_t)((c.ch.red   * 255) / 31);
            uint8_t g = (uint8_t)((c.ch.green * 255) / 63);
            uint8_t b = (uint8_t)((c.ch.blue  * 255) / 31);
            s_rgb32[y * SCR_W + x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }
    lv_disp_flush_ready(drv);
}

/* ---- Correctif runtime applique dans main.cpp (2 juillet 2026, cf setup()) : le fond
 * genere par EEZ est en 0x000000, mais l'image de fond du cadran a des zones semi-
 * transparentes qui doivent se composer sur la couleur du center_hub (0x1f333e) pour
 * eviter une difference de teinte visible en RGB565. screens.c seul ne l'applique pas
 * -> on le reproduit ici pour refleter l'etat reel du vario. ---- */
static void apply_hardware_fixes(void)
{
    lv_obj_set_style_bg_color(objects.main, lv_color_hex(0x1f333e), LV_PART_MAIN | LV_STATE_DEFAULT);
}

/* ---- Suivi appui long clavier (Enter=ENC1, Espace=ENC2), meme seuil que le materiel ---- */
static bool     s_enc1Down = false; static uint32_t s_enc1DownAt = 0; static bool s_enc1LongFired = false;
static bool     s_enc2Down = false; static uint32_t s_enc2DownAt = 0; static bool s_enc2LongFired = false;

static void check_long_press(uint32_t now)
{
    if (s_enc1Down && !s_enc1LongFired && (now - s_enc1DownAt) >= LONG_PRESS_MS) {
        s_enc1LongFired = true;
        SimMenu_OnLongPress1();
    }
    if (s_enc2Down && !s_enc2LongFired && (now - s_enc2DownAt) >= LONG_PRESS_MS) {
        s_enc2LongFired = true;
        SimMenu_OnLongPress2();
    }
}

/* ---- Fenetre Win32 : peint s_rgb32 tel quel (1:1, pas de mise a l'echelle) ---- */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            BITMAPINFO bmi;
            ZeroMemory(&bmi, sizeof(bmi));
            bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth       = SCR_W;
            bmi.bmiHeader.biHeight      = -SCR_H;  /* top-down */
            bmi.bmiHeader.biPlanes      = 1;
            bmi.bmiHeader.biBitCount    = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            StretchDIBits(hdc, 0, 0, SCR_W, SCR_H, 0, 0, SCR_W, SCR_H,
                          s_rgb32, &bmi, DIB_RGB_COLORS, SRCCOPY);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_KEYDOWN: {
            bool isRepeat = (lp & 0x40000000) != 0;
            uint32_t now = (uint32_t)GetTickCount64();
            switch (wp) {
                case VK_UP:    SimMenu_OnRotate1(-1); break;
                case VK_DOWN:  SimMenu_OnRotate1(+1); break;
                case VK_LEFT:  SimMenu_OnRotate2(-1); break;
                case VK_RIGHT: SimMenu_OnRotate2(+1); break;
                case VK_RETURN:
                    if (!isRepeat) { s_enc1Down = true; s_enc1DownAt = now; s_enc1LongFired = false; }
                    break;
                case VK_SPACE:
                    if (!isRepeat) { s_enc2Down = true; s_enc2DownAt = now; s_enc2LongFired = false; }
                    break;
            }
            return 0;
        }
        case WM_KEYUP: {
            switch (wp) {
                case VK_RETURN:
                    if (s_enc1Down && !s_enc1LongFired) SimMenu_OnButton1();
                    s_enc1Down = false;
                    break;
                case VK_SPACE:
                    s_enc2Down = false;   /* appui court ENC2 = pas d'action câblée sur le vrai materiel */
                    break;
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            SimMenu_OnRotate1(delta > 0 ? -1 : +1);
            return 0;
        }
        case WM_TIMER: {
            static ULONGLONG lastTick = 0;
            ULONGLONG now = GetTickCount64();
            if (lastTick == 0) lastTick = now;
            uint32_t dt = (uint32_t)(now - lastTick);
            lastTick = now;
            check_long_press((uint32_t)now);
            lv_tick_inc(dt);
            SimMenu_Tick(now / 1000.0);
            lv_timer_handler();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdline, int show)
{
    (void)hPrev; (void)cmdline;

    WNDCLASS wc = {0};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = "LimVarioSim";
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClass(&wc);

    DWORD style = WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX;
    RECT r = {0, 0, SCR_W, SCR_H};
    AdjustWindowRect(&r, style, FALSE);

    s_hwnd = CreateWindow("LimVarioSim",
                           "L!M Vario - Simulateur (Haut/Bas=ENC1 rotate, Entree=ENC1 clic/long, "
                           "Gauche/Droite=ENC2 volume, Espace=ENC2 long)",
                           style, CW_USEDEFAULT, CW_USEDEFAULT,
                           r.right - r.left, r.bottom - r.top,
                           NULL, NULL, hInst, NULL);
    ShowWindow(s_hwnd, show);

    lv_init();

    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, s_buf1, NULL, SCR_W * SCR_H);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &draw_buf;
    disp_drv.flush_cb = my_flush_cb;
    disp_drv.hor_res  = SCR_W;
    disp_drv.ver_res  = SCR_H;
    lv_disp_drv_register(&disp_drv);

    create_screens();
    lv_disp_load_scr(objects.main);   /* on saute le splash : vue vol directe */
    apply_hardware_fixes();
    SimMenu_Init();                   /* hide des panneaux setup/quick menu/editeur inclus */

    SetTimer(s_hwnd, 1, 16, NULL);    /* ~60 Hz : tick LVGL + demo + repaint */

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
