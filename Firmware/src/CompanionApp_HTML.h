#pragma once
#ifndef LIM_SIM_BUILD
#include <Arduino.h>
#else
#define PROGMEM
#endif

static const char COMPANION_APP_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover">
<title>L!M Vario — Companion App</title>
<!-- PWA: home-screen icon + standalone mode (the app looks like a real app).
     NB: no service worker possible (HTTP on 192.168.4.1 is not a secure context)
     -> no offline launch, but "Add to Home Screen" works. -->
<link rel="manifest" href="/manifest.webmanifest">
<meta name="theme-color" content="#2563eb">
<meta name="mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<meta name="apple-mobile-web-app-title" content="L!M Vario">
<link rel="icon" href="/icon.svg" type="image/svg+xml">
<link rel="apple-touch-icon" href="/icon.svg">
<!-- No external fonts (Google Fonts): the app is served ONLY on the vario's WiFi AP,
     WITHOUT internet -> requests to fonts.googleapis.com would hang and the app would
     seem to "not open". We rely on system fonts (fallback already defined in
     --font-sans / --font-mono). (6 July 2026) -->
<style>
  :root {
    --bg-app: #f4f6f9;
    --bg-card: #ffffff;
    --border-color: #e2e8f0;
    --border-focus: #2563eb;
    --text-main: #0f172a;
    --text-muted: #64748b;
    --accent-blue: #2563eb;
    --accent-hover: #1d4ed8;
    --accent-amber: #d97706;
    --danger: #ef4444;
    --danger-hover: #dc2626;
    --shadow-sm: 0 1px 2px 0 rgba(0, 0, 0, 0.05);
    --shadow-md: 0 4px 6px -1px rgba(0, 0, 0, 0.07), 0 2px 4px -2px rgba(0, 0, 0, 0.05);
    --shadow-lg: 0 10px 15px -3px rgba(0, 0, 0, 0.08), 0 4px 6px -4px rgba(0, 0, 0, 0.04);
    --font-sans: 'Inter', -apple-system, BlinkMacSystemFont, sans-serif;
    --font-mono: 'JetBrains Mono', monospace;
    --tabbar-h: 64px;
    --safe-b: env(safe-area-inset-bottom, 0px);
  }

  * { box-sizing: border-box; margin: 0; padding: 0; -webkit-tap-highlight-color: transparent; }

  html { -webkit-text-size-adjust: 100%; }

  body {
    background-color: var(--bg-app);
    color: var(--text-main);
    font-family: var(--font-sans);
    min-height: 100vh;
    display: flex;
    flex-direction: column;
    -webkit-font-smoothing: antialiased;
  }

  /* TOP HEADER (slim on mobile) */
  header {
    background: #ffffff;
    border-bottom: 1px solid var(--border-color);
    padding: 0.65rem 1rem;
    padding-top: max(0.65rem, env(safe-area-inset-top));
    display: flex;
    justify-content: space-between;
    align-items: center;
    position: sticky;
    top: 0;
    z-index: 50;
    box-shadow: var(--shadow-sm);
  }

  .brand {
    font-size: 1.05rem;
    font-weight: 700;
    display: flex;
    align-items: center;
    gap: 0.5rem;
    color: var(--text-main);
    letter-spacing: -0.02em;
  }

  .brand-badge {
    background: var(--text-main);
    color: #fff;
    padding: 0.2rem 0.5rem;
    border-radius: 6px;
    font-size: 0.72rem;
    font-weight: 700;
    letter-spacing: 0.05em;
  }

  .status-badge {
    display: inline-flex;
    align-items: center;
    gap: 0.35rem;
    font-size: 0.72rem;
    font-weight: 600;
    padding: 0.3rem 0.65rem;
    border-radius: 9999px;
    background: #ecfdf5;
    color: #065f46;
    border: 1px solid #a7f3d0;
    white-space: nowrap;
  }

  .status-dot {
    width: 6px;
    height: 6px;
    border-radius: 50%;
    background: #10b981;
    flex-shrink: 0;
  }

  /* MAIN CONTENT AREA -- room reserved at bottom for fixed tab bar */
  main {
    flex: 1;
    max-width: 640px;
    width: 100%;
    margin: 0 auto;
    padding: 1rem 0.85rem calc(var(--tabbar-h) + var(--safe-b) + 1.5rem) 0.85rem;
  }

  .view-pane { display: none; animation: fadeIn 0.18s ease-in-out; }
  .view-pane.active { display: block; }

  @keyframes fadeIn {
    from { opacity: 0; transform: translateY(3px); }
    to { opacity: 1; transform: translateY(0); }
  }

  /* CARDS */
  .card {
    background: var(--bg-card);
    border: 1px solid var(--border-color);
    border-radius: 12px;
    padding: 1.1rem;
    box-shadow: var(--shadow-sm);
    margin-bottom: 1rem;
  }

  .card-header {
    display: flex;
    flex-wrap: wrap;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 1rem;
    padding-bottom: 0.75rem;
    border-bottom: 1px solid var(--border-color);
    gap: 0.75rem;
  }

  .card-title {
    font-size: 1.02rem;
    font-weight: 700;
    color: var(--text-main);
  }

  .card-subtitle {
    font-size: 0.82rem;
    color: var(--text-muted);
    margin-top: 0.2rem;
  }

  /* ACCORDIONS -- 48px+ tap target on the header */
  .accordion-item {
    background: #ffffff;
    border: 1px solid var(--border-color);
    border-radius: 10px;
    margin-bottom: 0.65rem;
    overflow: hidden;
    transition: border-color 0.15s ease;
  }

  .accordion-header {
    width: 100%;
    min-height: 52px;
    background: #ffffff;
    border: none;
    padding: 0.9rem 1.1rem;
    display: flex;
    justify-content: space-between;
    align-items: center;
    font-family: var(--font-sans);
    font-size: 1rem;
    font-weight: 600;
    color: var(--text-main);
    cursor: pointer;
    text-align: left;
  }

  @media (hover: hover) { .accordion-header:hover { background: #f8fafc; } }
  .accordion-header:active { background: #f1f5f9; }

  .accordion-arrow {
    font-size: 0.8rem;
    color: var(--text-muted);
    transition: transform 0.2s ease;
    flex-shrink: 0;
    margin-left: 0.75rem;
  }

  .accordion-item.open { border-color: #cbd5e1; box-shadow: var(--shadow-sm); }
  .accordion-item.open .accordion-arrow { transform: rotate(180deg); }

  .accordion-body {
    display: none;
    padding: 1.1rem;
    border-top: 1px solid var(--border-color);
    background: #fdfdfd;
  }

  .accordion-item.open .accordion-body { display: block; }

  /* FORM ELEMENTS -- 16px min font-size prevents iOS auto-zoom-on-focus */
  .form-group { margin-bottom: 1.1rem; }
  .form-group:last-child { margin-bottom: 0; }

  .form-label {
    display: block;
    font-size: 0.74rem;
    color: var(--text-muted);
    margin-bottom: 0.4rem;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.03em;
  }

  .form-control {
    width: 100%;
    min-height: 46px;
    background: #ffffff;
    border: 1px solid var(--border-color);
    border-radius: 8px;
    padding: 0.6rem 0.8rem;
    color: var(--text-main);
    font-family: var(--font-sans);
    font-size: 16px;
    font-weight: 500;
    transition: all 0.15s ease;
  }

  .form-control:focus {
    outline: none;
    border-color: var(--border-focus);
    box-shadow: 0 0 0 3px rgba(37, 99, 235, 0.12);
  }

  .form-grid-2 { display: grid; grid-template-columns: 1fr 1fr; gap: 0.75rem; }
  @media (max-width: 360px) { .form-grid-2 { grid-template-columns: 1fr; } }

  .range-row { display: flex; align-items: center; gap: 0.75rem; }

  .range-val {
    font-family: var(--font-mono);
    font-weight: 600;
    color: var(--accent-blue);
    min-width: 62px;
    text-align: right;
    font-size: 0.85rem;
    background: #f1f5f9;
    padding: 0.3rem 0.5rem;
    border-radius: 6px;
    flex-shrink: 0;
  }

  input[type=range] {
    flex: 1;
    accent-color: var(--accent-blue);
    height: 28px;
    background: transparent;
    outline: none;
  }

  /* BUTTONS -- 44px+ tap target everywhere */
  .btn {
    background: #ffffff;
    border: 1px solid var(--border-color);
    color: var(--text-main);
    min-height: 44px;
    padding: 0.6rem 1.05rem;
    border-radius: 8px;
    font-weight: 600;
    font-size: 0.9rem;
    cursor: pointer;
    transition: background 0.1s ease;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    text-decoration: none;
  }

  @media (hover: hover) { .btn:hover { background: #f8fafc; border-color: #cbd5e1; } }
  .btn:active { background: #eef1f5; transform: scale(0.98); }

  .btn-primary { background: var(--accent-blue); color: #ffffff; border: 1px solid transparent; }
  @media (hover: hover) { .btn-primary:hover { background: var(--accent-hover); } }
  .btn-primary:active { background: var(--accent-hover); }

  .btn-danger { background: #fef2f2; color: var(--danger); border-color: #fecaca; }
  @media (hover: hover) { .btn-danger:hover { background: #fee2e2; border-color: #f87171; } }
  .btn-danger:active { background: #fee2e2; }

  .btn-block { width: 100%; }
  .btn-row { display: flex; gap: 0.65rem; }
  .btn-row .btn { flex: 1; }

  /* FLIGHT LOG LIST (cards, not a table -- reflows cleanly on narrow phones) */
  .file-list { display: flex; flex-direction: column; gap: 0.6rem; }

  .file-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 0.75rem;
    padding: 0.85rem 0.95rem;
    background: #fdfdfd;
    border: 1px solid var(--border-color);
    border-radius: 10px;
  }

  .file-info { display: flex; flex-direction: column; gap: 0.15rem; min-width: 0; }
  .file-name { font-family: var(--font-mono); font-size: 0.88rem; font-weight: 600; overflow-wrap: anywhere; }
  .file-size { font-size: 0.75rem; color: var(--text-muted); }
  .file-actions { display: flex; gap: 0.4rem; flex-shrink: 0; }
  .file-actions .btn { min-height: 38px; padding: 0.4rem 0.7rem; font-size: 0.8rem; }

  .empty-hint { color: var(--text-muted); font-size: 0.88rem; text-align: center; padding: 1.5rem 0; }

  /* Climb / Cruise mode toggle above the dial */
  .mode-toggle {
    display: flex;
    gap: 4px;
    background: #f1f5f9;
    border: 1px solid var(--border-color);
    border-radius: 10px;
    padding: 4px;
    margin-top: 0.6rem;
  }
  .mode-btn {
    flex: 1;
    border: none;
    background: transparent;
    color: var(--text-muted);
    font-family: var(--font-sans);
    font-size: 0.85rem;
    font-weight: 600;
    padding: 0.5rem;
    border-radius: 7px;
    cursor: pointer;
  }
  .mode-btn.active { background: var(--bg-card); color: var(--accent-blue); box-shadow: var(--shadow-sm); }

  /* ROUND DIAL -- fully fluid sizing, no fixed breakpoints */
  .vario-screen-scaler {
    display: flex;
    justify-content: center;
    align-items: center;
    padding: 0.5rem 0;
  }

  .round-dial-container {
    --dial: min(78vw, 340px);
    width: var(--dial);
    height: var(--dial);
    position: relative;
    border-radius: 50%;
    overflow: hidden;
    background: #080c14;
    border: 6px solid #1e293b;
    box-shadow: 0 15px 35px rgba(0, 0, 0, 0.25), inset 0 0 15px rgba(0,0,0,0.8);
    font-family: var(--font-mono);
    user-select: none;
    flex-shrink: 0;
  }

  /* Center Clickable Hub -- scaled from the dial's own size, no media queries needed */
  .thermal-circle {
    position: absolute;
    width: 33%;
    height: 33%;
    top: 33.5%;
    left: 33.5%;
    border-radius: 50%;
    border: 2px solid #2563eb;
    background: #0b1329;
    display: flex;
    align-items: center;
    justify-content: center;
    color: #f8fafc;
    font-size: clamp(0.7rem, 3.6vw, 0.86rem);
    font-family: var(--font-sans);
    font-weight: 600;
    text-align: center;
    padding: 0.4rem;
    cursor: pointer;
    transition: background 0.15s ease;
    z-index: 10;
  }

  @media (hover: hover) { .thermal-circle:hover { background: #152244; border-color: #60a5fa; } }
  .thermal-circle:active { background: #152244; }

  /* InfoBox Slots -- percentage-based so they scale with the dial */
  .ib-box {
    position: absolute;
    border: 2px solid #2563eb;
    border-radius: 8px;
    background: #0f172a;
    color: #ffffff;
    cursor: pointer;
    transition: background 0.15s ease;
    display: flex;
    align-items: center;
    justify-content: center;
    padding: 5px 10px;
    z-index: 20;
    font-size: clamp(0.68rem, 3.4vw, 0.86rem);
    font-weight: 700;
    text-align: center;
    line-height: 1.15;
    white-space: nowrap;
  }

  @media (hover: hover) { .ib-box:hover { background: #1e293b; border-color: #60a5fa; } }
  .ib-box:active { background: #1e293b; }

  /* slot-0..3 hug their text: shrink-to-fit width/height, only centered horizontally via left+transform */
  .slot-0, .slot-1, .slot-2, .slot-3 { left: 50%; max-width: 62%; transform: translateX(-50%); }
  .slot-0 { top: 6%; }
  .slot-1 { top: 20%; }
  .slot-2 { bottom: 20%; }
  .slot-3 { bottom: 6%; }

  .slot-4 {
    top: 43%;
    right: 10%;
    width: 11%;
    height: 14%;
    cursor: default;
    background: #0a0f1d;
    border-color: #1e293b;
  }
  .slot-4:active, .slot-4:hover { background: #0a0f1d; border-color: #1e293b; }

  /* MODALS -- bottom sheet on narrow screens, centered dialog on wide ones */
  .modal-overlay {
    display: none;
    position: fixed;
    top: 0; left: 0; width: 100%; height: 100%;
    background: rgba(15, 23, 42, 0.6);
    z-index: 100;
    align-items: flex-end;
    justify-content: center;
  }

  .modal-overlay.active { display: flex; }

  .modal-content {
    background: #ffffff;
    border-radius: 16px 16px 0 0;
    padding: 1rem 1rem calc(1rem + var(--safe-b)) 1rem;
    width: 100%;
    max-width: 480px;
    max-height: 80vh;
    box-shadow: var(--shadow-lg);
    border: 1px solid var(--border-color);
    border-bottom: none;
    overflow-y: auto;
  }

  @media (min-width: 480px) {
    .modal-overlay { align-items: center; padding: 1rem; }
    .modal-content { border-radius: 14px; border-bottom: 1px solid var(--border-color); }
  }

  .modal-grabber {
    width: 36px; height: 4px; border-radius: 2px; background: var(--border-color);
    margin: 0 auto 0.85rem auto;
  }
  @media (min-width: 480px) { .modal-grabber { display: none; } }

  .modal-header {
    font-size: 1.02rem;
    font-weight: 700;
    color: var(--text-main);
    margin-bottom: 0.85rem;
    display: flex;
    justify-content: space-between;
    align-items: center;
  }

  .modal-close {
    background: #f1f5f9;
    border: none;
    width: 34px;
    height: 34px;
    border-radius: 50%;
    font-size: 1rem;
    cursor: pointer;
    color: var(--text-muted);
    display: flex;
    align-items: center;
    justify-content: center;
    flex-shrink: 0;
  }

  .picker-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 0.5rem;
  }

  .picker-grid.single-col { grid-template-columns: 1fr; }
  @media (max-width: 380px) { .picker-grid { grid-template-columns: 1fr; } }

  .picker-btn {
    text-align: left;
    justify-content: flex-start;
    padding: 0.8rem 0.9rem;
    font-size: 0.88rem;
    min-height: 48px;
  }

  /* CHART AREA */
  #polar-canvas {
    width: 100%;
    height: 220px;
    background: #ffffff;
    border: 1px solid var(--border-color);
    border-radius: 8px;
  }

  .chart-legend {
    display: flex;
    flex-wrap: wrap;
    gap: 0.9rem 1.1rem;
    font-size: 0.75rem;
    font-family: var(--font-mono);
    margin-top: 0.7rem;
    align-items: center;
  }

  .legend-item { display: flex; align-items: center; gap: 0.4rem; color: var(--text-muted); }
  .line-sample { width: 16px; height: 3px; border-radius: 2px; flex-shrink: 0; }

  /* BOTTOM TAB BAR -- fixed, thumb-reachable, safe-area aware */
  .tabbar {
    position: fixed;
    left: 0; right: 0; bottom: 0;
    height: calc(var(--tabbar-h) + var(--safe-b));
    padding-bottom: var(--safe-b);
    background: #ffffff;
    border-top: 1px solid var(--border-color);
    display: flex;
    z-index: 60;
    box-shadow: 0 -2px 8px rgba(0,0,0,0.04);
  }

  .tab-btn {
    flex: 1;
    height: var(--tabbar-h);
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    gap: 0.2rem;
    background: transparent;
    border: none;
    color: var(--text-muted);
    font-family: var(--font-sans);
    font-size: 0.68rem;
    font-weight: 600;
    cursor: pointer;
  }

  .tab-btn .tab-icon { width: 20px; height: 20px; }
  .tab-btn.active { color: var(--accent-blue); }
  .tab-btn:active { background: #f8fafc; }

  /* ---- "Not connected to the vario" screen (full-screen overlay) ---- */
  #disconnected {
    position: fixed; inset: 0; z-index: 9999;
    display: none; flex-direction: column; align-items: center; justify-content: center;
    text-align: center; padding: 32px 24px;
    padding-bottom: calc(32px + var(--safe-b));
    background: var(--bg-app); color: var(--text-main);
  }
  #disconnected.show { display: flex; }
  #disconnected .dc-badge {
    width: 84px; height: 84px; border-radius: 22px; margin-bottom: 22px;
    display: flex; align-items: center; justify-content: center;
    background: linear-gradient(135deg, #2563eb, #1d4ed8);
    color: #fff; font-weight: 800; font-size: 26px; letter-spacing: -1px;
    box-shadow: var(--shadow-lg);
  }
  #disconnected h1 { font-size: 22px; font-weight: 800; margin-bottom: 8px; }
  #disconnected p  { color: var(--text-muted); font-size: 15px; max-width: 340px; line-height: 1.5; }
  #disconnected .dc-net {
    margin: 20px 0 4px; width: 100%; max-width: 340px;
    background: var(--bg-card); border: 1px solid var(--border-color);
    border-radius: 14px; box-shadow: var(--shadow-sm); overflow: hidden;
  }
  #disconnected .dc-net .row {
    display: flex; justify-content: space-between; align-items: center;
    padding: 13px 16px; font-size: 15px;
  }
  #disconnected .dc-net .row + .row { border-top: 1px solid var(--border-color); }
  #disconnected .dc-net .k { color: var(--text-muted); }
  #disconnected .dc-net .v { font-family: var(--font-mono); font-weight: 700; font-size: 16px; }
  #disconnected .dc-actions { margin-top: 22px; width: 100%; max-width: 340px; display: flex; flex-direction: column; gap: 10px; }
  #disconnected .dc-btn {
    width: 100%; padding: 15px 16px; border-radius: 12px; border: none;
    font-size: 16px; font-weight: 700; cursor: pointer; font-family: var(--font-sans);
  }
  #disconnected .dc-btn.primary { background: var(--accent-blue); color: #fff; }
  #disconnected .dc-btn.primary:active { background: var(--accent-hover); }
  #disconnected .dc-btn.ghost { background: var(--bg-card); color: var(--text-main); border: 1px solid var(--border-color); }
  #disconnected .dc-hint { margin-top: 18px; font-size: 13px; color: var(--text-muted); max-width: 340px; }
  #disconnected .dc-spin { margin-top: 16px; font-size: 13px; color: var(--accent-blue); font-weight: 600; display: none; }
  #disconnected.checking .dc-spin { display: block; }

  /* header status badge: red when disconnected */
  .status-badge.offline { color: var(--danger); }
  .status-badge.offline .status-dot { background: var(--danger); }
</style>
</head>
<body>

<!-- Screen shown when the app can no longer reach the vario (WiFi connection lost). -->
<div id="disconnected">
  <div class="dc-badge">L!M</div>
  <h1>Not connected to the vario</h1>
  <p>Your phone is not (or no longer) connected to the L!M Vario WiFi network.</p>
  <div class="dc-net">
    <div class="row"><span class="k">WiFi network</span><span class="v" id="dc-ssid">LIM-Vario</span></div>
    <div class="row"><span class="k">Password</span><span class="v" id="dc-pass">limvario</span></div>
  </div>
  <div class="dc-actions">
    <button class="dc-btn primary" onclick="openWifiSettings()">Open WiFi settings</button>
    <button class="dc-btn ghost" onclick="retryLink()">Retry</button>
  </div>
  <p class="dc-hint">Tip: on the vario screen, enable "App connect" then scan the QR code with your camera to join the WiFi directly.</p>
  <div class="dc-spin">Checking connection…</div>
</div>

<header>
  <div class="brand"><span class="brand-badge">L!M</span> Vario</div>
  <div class="status-badge" id="statusBadge"><div class="status-dot"></div> <span id="statusText">Connected</span></div>
</header>

<main>
  <!-- 1. SCREEN INFOBOXES -->
  <div id="tab-infobox" class="view-pane active">
    <div class="card">
      <div class="card-header">
        <div>
          <span class="card-title">Screen layout</span>
          <p class="card-subtitle">Tap any box or the center display to assign a metric.</p>
        </div>
      </div>

      <div class="mode-toggle" role="tablist">
        <button class="mode-btn active" id="mode-btn-cruise" onclick="switchIbMode('cruise')">Cruise</button>
        <button class="mode-btn" id="mode-btn-climb" onclick="switchIbMode('climb')">Climb</button>
      </div>

      <div class="vario-screen-scaler">
        <div class="round-dial-container">
          <div class="thermal-circle" id="center-val" onclick="openCenterModal()">Wind Direction</div>
          <div class="ib-box slot-0" id="slot-lbl-0" onclick="openMetricModal(0)">Inst. Vario</div>
          <div class="ib-box slot-1" id="slot-lbl-1" onclick="openMetricModal(1)">MacCready</div>
          <div class="ib-box slot-2" id="slot-lbl-2" onclick="openMetricModal(2)">Baro Alt.</div>
          <div class="ib-box slot-3" id="slot-lbl-3" onclick="openMetricModal(3)">Glide Ratio</div>
          <div class="ib-box slot-4"></div>
        </div>
      </div>

      <button class="btn btn-primary btn-block" style="margin-top: 0.75rem;" onclick="applyScreenConfig()">Apply to vario</button>
    </div>
  </div>

  <!-- 2. SYSTEM SETTINGS -->
  <div id="tab-settings" class="view-pane">
    <div class="card">
      <div class="card-header">
        <span class="card-title">Instrument settings</span>
      </div>
      <p class="card-subtitle" style="margin-bottom: 0.9rem;">Tap a section to configure. Changes sync directly to the instrument.</p>

      <div class="accordion-item open">
        <button class="accordion-header" onclick="toggleAccordion(this)">
          <span>Display</span>
          <span class="accordion-arrow">▼</span>
        </button>
        <div class="accordion-body">
          <div class="form-group">
            <label class="form-label">Screen brightness</label>
            <div class="range-row">
              <input type="range" id="cfg-bright" min="0" max="20" value="16" oninput="document.getElementById('lbl-bright').innerText=this.value; autoSync()">
              <span class="range-val" id="lbl-bright">16</span>
            </div>
          </div>
          <div class="form-group">
            <label class="form-label">Vertical speed unit</label>
            <select id="cfg-uvert" class="form-control" onchange="autoSync()">
              <option value="0">Meters per second (m/s)</option>
              <option value="1">Knots (kt)</option>
            </select>
          </div>
          <div class="form-group">
            <label class="form-label">Altitude unit</label>
            <select id="cfg-ualt" class="form-control" onchange="autoSync()">
              <option value="0">Meters (m)</option>
              <option value="1">Feet (ft)</option>
            </select>
          </div>
          <div class="form-group">
            <label class="form-label">Airspeed unit</label>
            <select id="cfg-uspeed" class="form-control" onchange="autoSync()">
              <option value="0">Kilometers per hour (km/h)</option>
              <option value="1">Knots (kt)</option>
            </select>
          </div>
          <div class="form-group">
            <label class="form-label">Screen rotation angle</label>
            <select id="cfg-rot" class="form-control" onchange="autoSync()">
              <option value="0">0 degrees</option>
              <option value="90">90 degrees</option>
              <option value="180">180 degrees</option>
              <option value="270">270 degrees</option>
            </select>
          </div>
        </div>
      </div>

      <div class="accordion-item">
        <button class="accordion-header" onclick="toggleAccordion(this)">
          <span>Sound</span>
          <span class="accordion-arrow">▼</span>
        </button>
        <div class="accordion-body">
          <div class="form-group">
            <label class="form-label">Audio tone pitch frequency</label>
            <div class="range-row">
              <input type="range" id="cfg-pitch" min="200" max="1500" step="50" value="700" oninput="document.getElementById('lbl-pitch').innerText=this.value+' Hz'; autoSync()">
              <span class="range-val" id="lbl-pitch">700 Hz</span>
            </div>
          </div>
          <div class="form-group">
            <label class="form-label">Synthesizer waveform</label>
            <select id="cfg-wave" class="form-control" onchange="autoSync()">
              <option value="0">Sine wave</option>
              <option value="1">Square wave</option>
              <option value="2">Triangle wave</option>
            </select>
          </div>
          <div class="form-group">
            <label class="form-label">Audio tone spread factor</label>
            <div class="range-row">
              <input type="range" id="cfg-spread" min="0" max="10" value="5" oninput="document.getElementById('lbl-spread').innerText=this.value; autoSync()">
              <span class="range-val" id="lbl-spread">5</span>
            </div>
          </div>
        </div>
      </div>

      <div class="accordion-item">
        <button class="accordion-header" onclick="toggleAccordion(this)">
          <span>Vario</span>
          <span class="accordion-arrow">▼</span>
        </button>
        <div class="accordion-body">
          <div class="form-group">
            <label class="form-label">Variometer scale range</label>
            <select id="cfg-range" class="form-control" onchange="autoSync()">
              <option value="5">+/- 5 m/s</option>
              <option value="10">+/- 10 m/s</option>
            </select>
          </div>
          <div class="form-group">
            <label class="form-label">Variometer damping filter</label>
            <select id="cfg-filter" class="form-control" onchange="autoSync()">
              <option value="0">Fast response</option>
              <option value="1" selected>Medium response</option>
              <option value="2">Slow response</option>
            </select>
          </div>
          <div class="form-group">
            <label class="form-label">Average climb window period</label>
            <select id="cfg-avg" class="form-control" onchange="autoSync()">
              <option value="0">15 seconds</option>
              <option value="1" selected>20 seconds</option>
              <option value="2">30 seconds</option>
            </select>
          </div>
        </div>
      </div>

      <div class="accordion-item">
        <button class="accordion-header" onclick="toggleAccordion(this)">
          <span>System</span>
          <span class="accordion-arrow">▼</span>
        </button>
        <div class="accordion-body">
          <div class="form-group">
            <label class="form-label">Companion app connection</label>
            <select id="cfg-appconn" class="form-control" onchange="autoSync()">
              <option value="1">Enabled (ON)</option>
              <option value="0">Disabled (OFF)</option>
            </select>
          </div>
          <div class="form-group">
            <label class="form-label">Condor simulator telemetry link</label>
            <select id="cfg-condor" class="form-control" onchange="autoSync()">
              <option value="0">Disabled (OFF)</option>
              <option value="1">Enabled (ON)</option>
            </select>
          </div>
          <div style="margin-top: 1.1rem; padding-top: 1.1rem; border-top: 1px solid var(--border-color); display: flex; flex-wrap: wrap; justify-content: space-between; align-items: center; gap: 0.75rem;">
            <span style="font-size: 0.82rem; color: var(--text-muted); font-weight: 600;">Firmware v0.8.0</span>
            <button class="btn btn-danger" onclick="confirm('Perform full Factory Reset on Vario?')">Factory reset</button>
          </div>
        </div>
      </div>
    </div>
  </div>

  <!-- 3. PROFILES & GLIDER -->
  <div id="tab-profiles" class="view-pane">
    <div class="card">
      <div class="card-header">
        <span class="card-title">Pilot & glider profile</span>
      </div>
      <p class="card-subtitle" style="margin-bottom: 0.9rem;">Saving a profile stores the glider model, weights and polar curve into that slot on the L!M Vario.</p>
      <div class="form-group">
        <label class="form-label">Active profile</label>
        <select class="form-control" id="prof-select" onchange="selectProfile(this.selectedIndex)"></select>
      </div>
      <div class="form-group">
        <label class="form-label">Profile name (max 5 characters)</label>
        <input type="text" id="prof-name" class="form-control" maxlength="5" placeholder="e.g. LS4" style="font-family: var(--font-mono); font-weight: 700; text-transform: uppercase;">
      </div>
      <div class="btn-row" style="margin-top: 0.6rem;">
        <button class="btn" onclick="createNewProfile()">New profile</button>
        <button class="btn btn-danger" onclick="deleteProfile()">Delete profile</button>
      </div>
      <button class="btn btn-primary btn-block" style="margin-top: 0.75rem;" onclick="saveProfile()">Save profile to vario</button>
    </div>

    <div class="card">
      <div class="card-header"><span class="card-title">Glider specifications</span></div>
      <div class="form-group">
        <label class="form-label">Glider model preset</label>
        <select class="form-control" id="glider-select" onchange="applyGliderPreset(this.value)"></select>
      </div>
      <div class="form-grid-2">
        <div class="form-group">
          <label class="form-label">Empty weight (kg)</label>
          <input type="number" id="polar-wt" class="form-control" value="310" oninput="updatePolarChart(); autoSync()">
        </div>
        <div class="form-group">
          <label class="form-label">Max ballast (kg)</label>
          <input type="number" id="polar-bal" class="form-control" value="450" oninput="updatePolarChart(); autoSync()">
        </div>
      </div>
    </div>

    <div class="card">
      <div class="card-header"><span class="card-title">Aerodynamic polar curve</span></div>
      <canvas id="polar-canvas"></canvas>
      <div class="chart-legend">
        <div class="legend-item"><div class="line-sample" style="background:#2563eb"></div><span>Empty (<span id="wt-dry-lbl">310 kg</span>)</span></div>
        <div class="legend-item"><div class="line-sample" style="border-top: 3px dashed #d97706; background:transparent"></div><span>Ballasted (<span id="wt-bal-lbl">450 kg</span>)</span></div>
      </div>
      <div style="margin-top:0.5rem; font-weight:700; color:var(--accent-blue); font-size: 0.85rem;" id="best-ld-lbl">L/D: 40.5 @ 100 km/h</div>
    </div>

    <div class="card">
      <div class="card-header"><span class="card-title">3-point polar coefficients</span></div>
      <div style="display: flex; flex-direction: column; gap: 0.7rem;">
        <div class="form-grid-2">
          <div><label class="form-label">Airspeed 1 (km/h)</label><input type="number" id="v1" class="form-control" value="80" oninput="updatePolarChart(); autoSync()"></div>
          <div><label class="form-label">Sink rate 1 (m/s)</label><input type="number" step="0.01" id="si1" class="form-control" value="-0.65" oninput="updatePolarChart(); autoSync()"></div>
        </div>
        <div class="form-grid-2">
          <div><label class="form-label">Airspeed 2 (km/h)</label><input type="number" id="v2" class="form-control" value="105" oninput="updatePolarChart(); autoSync()"></div>
          <div><label class="form-label">Sink rate 2 (m/s)</label><input type="number" step="0.01" id="si2" class="form-control" value="-0.72" oninput="updatePolarChart(); autoSync()"></div>
        </div>
        <div class="form-grid-2">
          <div><label class="form-label">Airspeed 3 (km/h)</label><input type="number" id="v3" class="form-control" value="160" oninput="updatePolarChart(); autoSync()"></div>
          <div><label class="form-label">Sink rate 3 (m/s)</label><input type="number" step="0.01" id="si3" class="form-control" value="-1.85" oninput="updatePolarChart(); autoSync()"></div>
        </div>
      </div>
      <button class="btn btn-primary btn-block" style="margin-top: 1.1rem;" onclick="saveProfile()">Save profile to vario</button>
    </div>
  </div>

  <!-- 4. FLIGHT LOGS -->
  <div id="tab-files" class="view-pane">
    <div class="card">
      <div class="card-header">
        <span class="card-title">Flight logs</span>
        <button class="btn" onclick="loadFiles()">Refresh</button>
      </div>
      <div class="file-list" id="files-list">
        <p class="empty-hint">Loading flight logs from SD card…</p>
      </div>
    </div>
  </div>

  <!-- 5. FIRMWARE UPDATE (OTA) -->
  <div id="tab-update" class="view-pane">
    <div class="card">
      <div class="card-header">
        <div>
          <span class="card-title">Firmware update (OTA)</span>
          <p class="card-subtitle">Upload a compiled binary (.bin) over Wi-Fi to update the instrument.</p>
        </div>
      </div>
      <div class="form-group" style="margin-top: 0.75rem;">
        <label class="form-label">Firmware binary file (.bin)</label>
        <input type="file" id="ota-file" accept=".bin" class="form-control" style="padding: 0.5rem; min-height: 46px;">
      </div>
      <div id="ota-progress-container" style="display:none; margin-top:1.1rem;">
        <div style="background:#e2e8f0; border-radius:6px; height:12px; overflow:hidden;">
          <div id="ota-progress-bar" style="background:#2563eb; width:0%; height:100%; transition:width 0.2s ease;"></div>
        </div>
        <p id="ota-status" style="font-size:0.85rem; color:var(--text-main); font-weight:600; margin-top:0.5rem; text-align:center;">Uploading: 0%</p>
      </div>
      <button class="btn btn-primary btn-block" style="margin-top: 1.25rem; padding: 0.85rem;" onclick="uploadFirmware()">Install firmware update</button>
    </div>
  </div>
</main>

<!-- BOTTOM TAB BAR -->
<nav class="tabbar">
  <button class="tab-btn active" data-tab="tab-infobox" onclick="switchTab('tab-infobox', this)">
    <svg class="tab-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><circle cx="12" cy="12" r="9"/><path d="M12 3v5M12 16v5M3 12h5M16 12h5"/></svg>
    Boxes
  </button>
  <button class="tab-btn" data-tab="tab-settings" onclick="switchTab('tab-settings', this)">
    <svg class="tab-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06A1.65 1.65 0 0 0 4.6 15a1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06A1.65 1.65 0 0 0 9 4.6a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06A1.65 1.65 0 0 0 19.4 9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/></svg>
    Settings
  </button>
  <button class="tab-btn" data-tab="tab-profiles" onclick="switchTab('tab-profiles', this)">
    <svg class="tab-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><path d="M12 2l9 4.5v11L12 22l-9-4.5v-11L12 2z"/><path d="M12 8v8M8 12h8"/></svg>
    Profile
  </button>
  <button class="tab-btn" data-tab="tab-files" onclick="switchTab('tab-files', this)">
    <svg class="tab-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><path d="M4 4h6l2 2h8v12H4z"/></svg>
    Logs
  </button>
  <button class="tab-btn" data-tab="tab-update" onclick="switchTab('tab-update', this)">
    <svg class="tab-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><path d="M12 3v12M7 10l5 5 5-5M5 21h14"/></svg>
    Update
  </button>
</nav>

<!-- MODAL FOR INFOBOX METRIC SELECTION -->
<div id="metric-modal" class="modal-overlay">
  <div class="modal-content">
    <div class="modal-grabber"></div>
    <div class="modal-header">
      <span>Select InfoBox metric</span>
      <button class="modal-close" onclick="closeMetricModal()">✕</button>
    </div>
    <div class="picker-grid">
      <button class="btn picker-btn" onclick="selectMetricValue('Inst. Vario')">Inst. Vario</button>
      <button class="btn picker-btn" onclick="selectMetricValue('Avg. Vario')">Avg. Vario</button>
      <button class="btn picker-btn" onclick="selectMetricValue('MacCready')">MacCready</button>
      <button class="btn picker-btn" onclick="selectMetricValue('Baro Alt.')">Baro Alt.</button>
      <button class="btn picker-btn" onclick="selectMetricValue('GPS Alt.')">GPS Alt.</button>
      <button class="btn picker-btn" onclick="selectMetricValue('Time')">Time</button>
      <button class="btn picker-btn" onclick="selectMetricValue('Flight Time')">Flight Time</button>
      <button class="btn picker-btn" onclick="selectMetricValue('Wind')">Wind</button>
      <button class="btn picker-btn" onclick="selectMetricValue('Climb Gain')">Climb Gain</button>
      <button class="btn picker-btn" onclick="selectMetricValue('Flight Level')">Flight Level</button>
      <button class="btn picker-btn" onclick="selectMetricValue('Glide Ratio')">Glide Ratio</button>
      <button class="btn picker-btn" onclick="selectMetricValue('Airspeed')">Airspeed</button>
      <button class="btn picker-btn" onclick="selectMetricValue('Ground Speed')">Ground Speed</button>
      <button class="btn picker-btn" onclick="selectMetricValue('Netto')">Netto</button>
      <button class="btn picker-btn" onclick="selectMetricValue('Speed to Fly')">Speed to Fly</button>
      <button class="btn picker-btn btn-danger" onclick="selectMetricValue('Disabled')">Disabled</button>
    </div>
  </div>
</div>

<!-- MODAL FOR CENTER DISPLAY SELECTION -->
<div id="center-modal" class="modal-overlay">
  <div class="modal-content">
    <div class="modal-grabber"></div>
    <div class="modal-header">
      <span>Select center display mode</span>
      <button class="modal-close" onclick="closeCenterModal()">✕</button>
    </div>
    <div class="picker-grid single-col">
      <button class="btn picker-btn" onclick="selectCenterValue('Thermal Helper')">Thermal Helper</button>
      <button class="btn picker-btn" onclick="selectCenterValue('Wind Direction')">Wind Direction</button>
      <button class="btn picker-btn btn-danger" onclick="selectCenterValue('Disabled')">Disabled</button>
    </div>
  </div>
</div>

<script>
  let activeSlot = 0;
  let activeProfileIndex = 0;
  let syncTimeout = null;

  let profiles = [];  // [{idx,name,used}] x5, from /api/profiles
  let gliderDb = [];  // [{name,empty_wt,max_bal,v1,si1,v2,si2,v3,si3}], from /api/gliders

  function autoSync() {
    clearTimeout(syncTimeout);
    syncTimeout = setTimeout(() => { saveConfigSilent(); }, 600);
  }

  function loadGliderDb() {
    return fetch('/api/gliders').then(r => r.json()).then(list => {
      gliderDb = list;
      let sel = document.getElementById('glider-select');
      sel.innerHTML = '';
      gliderDb.forEach((g, idx) => {
        let opt = document.createElement('option');
        opt.value = idx;
        opt.text = g.name;
        sel.appendChild(opt);
      });
    }).catch(() => {});
  }

  function loadProfiles() {
    return fetch('/api/profiles').then(r => r.json()).then(list => {
      profiles = list;
      renderProfileDropdown(activeProfileIndex);
    }).catch(() => {});
  }

  function renderProfileDropdown(selectedIdx) {
    let sel = document.getElementById('prof-select');
    if (!sel) return;
    sel.innerHTML = '';
    profiles.forEach((p) => {
      let opt = document.createElement('option');
      opt.value = p.idx;
      opt.text = p.used ? p.name : ('Empty ' + (p.idx + 1));
      if (p.idx === selectedIdx) opt.selected = true;
      sel.appendChild(opt);
    });
    selectProfile(selectedIdx);
  }

  function selectProfile(idx) {
    activeProfileIndex = idx;
    let p = profiles[idx];
    fetch('/api/profiles/select', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ idx })
    }).then(() => fetch('/api/config')).then(r => r.json()).then(c => {
      document.getElementById('prof-name').value = (p && p.used) ? p.name : '';
      if (c.glidx !== undefined) document.getElementById('glider-select').value = c.glidx;
      if (c.glewt !== undefined) document.getElementById('polar-wt').value = c.glewt;
      if (c.glmbal !== undefined) document.getElementById('polar-bal').value = c.glmbal;
      if (c.v1 !== undefined) document.getElementById('v1').value = c.v1;
      if (c.si1 !== undefined) document.getElementById('si1').value = c.si1;
      if (c.v2 !== undefined) document.getElementById('v2').value = c.v2;
      if (c.si2 !== undefined) document.getElementById('si2').value = c.si2;
      if (c.v3 !== undefined) document.getElementById('v3').value = c.v3;
      if (c.si3 !== undefined) document.getElementById('si3').value = c.si3;
      updatePolarChart();
    }).catch(() => {});
  }

  function createNewProfile() {
    let empty = profiles.find(p => !p.used);
    if (!empty) { alert('All 5 profile slots are used. Delete one first.'); return; }
    document.getElementById('prof-select').value = empty.idx;
    selectProfile(empty.idx);
    document.getElementById('prof-name').value = '';
    document.getElementById('prof-name').focus();
  }

  function saveProfile() {
    let nameInput = document.getElementById('prof-name');
    let newName = nameInput.value.trim().toUpperCase();
    if (!newName) { alert('Error: Profile name cannot be empty.'); return; }
    if (newName.length > 5) { alert('Error: Profile name must be 5 characters maximum.'); return; }
    let isDup = profiles.some(p => p.idx !== activeProfileIndex && p.used && p.name.toUpperCase() === newName);
    if (isDup) { alert(`Error: A profile named "${newName}" already exists. Each profile must have a unique name.`); return; }

    let cfgBody = {
      glidx: parseInt(document.getElementById('glider-select').value) || 0,
      glewt: parseFloat(document.getElementById('polar-wt').value) || 300,
      glmbal: parseFloat(document.getElementById('polar-bal').value) || 450,
      v1: parseFloat(document.getElementById('v1').value) || 80,
      si1: parseFloat(document.getElementById('si1').value) || -0.65,
      v2: parseFloat(document.getElementById('v2').value) || 105,
      si2: parseFloat(document.getElementById('si2').value) || -0.72,
      v3: parseFloat(document.getElementById('v3').value) || 160,
      si3: parseFloat(document.getElementById('si3').value) || -1.85
    };
    fetch('/api/config', {
      method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(cfgBody)
    }).then(() => fetch('/api/profiles/save', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ idx: activeProfileIndex, name: newName })
    })).then(() => loadProfiles()).then(() => {
      alert(`Profile "${newName}" saved directly to L!M Vario.`);
    }).catch(() => alert('Could not reach the vario.'));
  }

  function deleteProfile() {
    let p = profiles[activeProfileIndex];
    if (!p || !p.used) { alert('This slot is already empty.'); return; }
    if (!confirm(`Delete profile "${p.name}"?`)) return;
    fetch('/api/profiles/delete', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ idx: activeProfileIndex })
    }).then(() => loadProfiles()).catch(() => {});
  }

  function uploadFirmware() {
    let fileInput = document.getElementById('ota-file');
    if (!fileInput.files || fileInput.files.length === 0) {
      alert("Please select a valid firmware binary file (.bin).");
      return;
    }
    let file = fileInput.files[0];
    if (!confirm(`Install firmware update "${file.name}" onto the L!M Vario? The instrument will reboot automatically after completion.`)) {
      return;
    }
    let formData = new FormData();
    formData.append("update", file, file.name);

    document.getElementById('ota-progress-container').style.display = 'block';
    let xhr = new XMLHttpRequest();
    xhr.open("POST", "/update", true);
    xhr.upload.addEventListener("progress", function(e) {
      if (e.lengthComputable) {
        let percent = Math.round((e.loaded / e.total) * 100);
        document.getElementById('ota-progress-bar').style.width = percent + "%";
        document.getElementById('ota-status').innerText = "Uploading: " + percent + "%";
      }
    });
    xhr.onreadystatechange = function() {
      if (xhr.readyState === 4) {
        if (xhr.status === 200 && xhr.responseText.trim() === "OK") {
          document.getElementById('ota-status').innerText = "Update successful! Instrument restarting...";
          alert("Firmware update completed successfully! The Vario is rebooting now.");
        } else {
          document.getElementById('ota-status').innerText = "Update failed.";
          alert("Firmware update failed. Please verify the binary file and try again.");
        }
      }
    };
    xhr.send(formData);
  }

  function switchTab(id, btn) {
    document.querySelectorAll('.view-pane').forEach(el => el.classList.remove('active'));
    document.querySelectorAll('.tab-btn').forEach(el => el.classList.remove('active'));
    document.getElementById(id).classList.add('active');
    btn.classList.add('active');
    window.scrollTo(0, 0);
    if (id === 'tab-profiles') updatePolarChart();
  }

  function toggleAccordion(btn) {
    const item = btn.parentElement;
    item.classList.toggle('open');
  }

  // Ordre exact de l'enum InfoBoxMetric cote firmware (main.cpp) -- Netto/Speed to Fly
  // sont deja geres par la structure de donnees meme si pas encore choisissables dans
  // le menu physique (labels EEZ ibname16/17 pas encore construits).
  const IB_METRIC_NAMES = ['Inst. Vario','Avg. Vario','MacCready','Baro Alt.','GPS Alt.',
    'Airspeed','Ground Speed','Time','Flight Time','Wind','Climb Gain','Flight Level',
    'Glide Ratio','Disabled','Netto','Speed to Fly'];
  const IB_DISABLED = 13;
  // Ordre de l'enum CenterZoneMetric
  const CENTER_METRIC_NAMES = ['Thermal Helper', 'Wind Direction', 'Disabled'];
  const CENTER_DISABLED = 2;
  // Slot visuel (0-3) -> index de zone dans g_ibConfigClimb/Cruise[6] (zone2=centre reserve,
  // zone5=toujours vide cote ecran physique, jamais affichee)
  const IB_SLOT_ZONE = [0, 1, 3, 4];

  let ibMode = 'cruise'; // 'cruise' | 'climb' -- matches g_ibEditCruiseMode on the vario
  let screenConfig = { climb: [0,1,13,3,10,9], cruise: [0,2,13,3,12,6] };
  let centerConfig = { climb: 0, cruise: 1 };

  function renderIbConfig() {
    IB_SLOT_ZONE.forEach((zone, slot) => {
      let metricIdx = screenConfig[ibMode][zone];
      let el = document.getElementById('slot-lbl-' + slot);
      if (!el) return;
      el.innerHTML = IB_METRIC_NAMES[metricIdx] || 'Disabled';
      el.style.color = (metricIdx === IB_DISABLED) ? '#64748b' : '#f8fafc';
    });
    let centerIdx = centerConfig[ibMode];
    let center = document.getElementById('center-val');
    if (center) {
      center.innerHTML = CENTER_METRIC_NAMES[centerIdx] || 'Disabled';
      center.style.color = (centerIdx === CENTER_DISABLED) ? '#64748b' : '#f8fafc';
    }
  }

  function loadScreenConfig() {
    fetch('/api/screen').then(r => r.json()).then(c => {
      screenConfig.climb = c.climb;
      screenConfig.cruise = c.cruise;
      centerConfig.climb = c.center_climb;
      centerConfig.cruise = c.center_cruise;
      renderIbConfig();
    }).catch(() => {});
  }

  function saveScreenConfig() {
    fetch('/api/screen', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        climb: screenConfig.climb, cruise: screenConfig.cruise,
        center_climb: centerConfig.climb, center_cruise: centerConfig.cruise
      })
    }).catch(() => {});
  }

  function applyScreenConfig() {
    saveScreenConfig();
    alert('Screen layout sent and applied to L!M Vario.');
  }

  function switchIbMode(mode) {
    ibMode = mode;
    document.getElementById('mode-btn-cruise').classList.toggle('active', mode === 'cruise');
    document.getElementById('mode-btn-climb').classList.toggle('active', mode === 'climb');
    renderIbConfig();
  }

  function openMetricModal(idx) {
    activeSlot = idx;
    document.getElementById('metric-modal').classList.add('active');
  }

  function closeMetricModal() {
    document.getElementById('metric-modal').classList.remove('active');
  }

  function selectMetricValue(metricName) {
    let idx = IB_METRIC_NAMES.indexOf(metricName);
    if (idx < 0) idx = IB_DISABLED;
    screenConfig[ibMode][IB_SLOT_ZONE[activeSlot]] = idx;
    renderIbConfig();
    closeMetricModal();
    saveScreenConfig();
  }

  function openCenterModal() {
    document.getElementById('center-modal').classList.add('active');
  }

  function closeCenterModal() {
    document.getElementById('center-modal').classList.remove('active');
  }

  function selectCenterValue(modeText) {
    let idx = CENTER_METRIC_NAMES.indexOf(modeText);
    if (idx < 0) idx = CENTER_DISABLED;
    centerConfig[ibMode] = idx;
    renderIbConfig();
    closeCenterModal();
    saveScreenConfig();
  }

  function applyGliderPreset(idxStr) {
    let g = gliderDb[parseInt(idxStr)];
    if (!g) return;
    document.getElementById('v1').value = g.v1;
    document.getElementById('si1').value = g.si1;
    document.getElementById('v2').value = g.v2;
    document.getElementById('si2').value = g.si2;
    document.getElementById('v3').value = g.v3;
    document.getElementById('si3').value = g.si3;
    document.getElementById('polar-wt').value = g.empty_wt;
    updatePolarChart();
    autoSync();
  }

  function updatePolarChart() {
    const canvas = document.getElementById('polar-canvas');
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const dpr = window.devicePixelRatio || 1;
    const cssW = canvas.parentElement.clientWidth - (canvas.parentElement === canvas.parentNode ? 0 : 0);
    const w = cssW, h = 220;
    canvas.style.width = w + 'px';
    canvas.style.height = h + 'px';
    canvas.width = w * dpr;
    canvas.height = h * dpr;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

    const padL = 42, padR = 12, padT = 12, padB = 26;
    const chartW = w - padL - padR, chartH = h - padT - padB;

    ctx.clearRect(0, 0, w, h);
    ctx.strokeStyle = '#f1f5f9'; ctx.lineWidth = 1;
    ctx.fillStyle = '#64748b'; ctx.font = '10px JetBrains Mono';
    ctx.textAlign = 'right'; ctx.textBaseline = 'middle';

    const maxSink = 3.5;
    for (let s = 0; s <= 3.5; s += 0.5) {
      let y = padT + (s / maxSink) * chartH;
      ctx.beginPath(); ctx.moveTo(padL, y); ctx.lineTo(w - padR, y); ctx.stroke();
      ctx.fillText(-s.toFixed(1), padL - 6, y);
    }

    ctx.textAlign = 'center'; ctx.textBaseline = 'top';
    const minV = 60, maxV = 220;
    for (let v = 60; v <= 220; v += 40) {
      let x = padL + ((v - minV) / (maxV - minV)) * chartW;
      ctx.beginPath(); ctx.moveTo(x, padT); ctx.lineTo(x, padT + chartH); ctx.stroke();
      ctx.fillText(v, x, padT + chartH + 5);
    }
    ctx.fillText('km/h', padL + chartW / 2, padT + chartH + 16);

    const v1 = parseFloat(document.getElementById('v1').value) || 80, si1 = Math.abs(parseFloat(document.getElementById('si1').value)) || 0.65;
    const v2 = parseFloat(document.getElementById('v2').value) || 105, si2 = Math.abs(parseFloat(document.getElementById('si2').value)) || 0.72;
    const v3 = parseFloat(document.getElementById('v3').value) || 160, si3 = Math.abs(parseFloat(document.getElementById('si3').value)) || 1.85;
    const wtDry = parseFloat(document.getElementById('polar-wt').value) || 310, wtBal = parseFloat(document.getElementById('polar-bal').value) || 450;

    if (document.getElementById('wt-dry-lbl')) document.getElementById('wt-dry-lbl').innerText = wtDry + ' kg';
    if (document.getElementById('wt-bal-lbl')) document.getElementById('wt-bal-lbl').innerText = wtBal + ' kg';

    const balRatio = Math.sqrt(Math.max(wtBal, wtDry) / Math.max(wtDry, 100));

    function getSink(v, pV1, pSi1, pV2, pSi2, pV3, pSi3) {
      let l1 = ((v - pV2) * (v - pV3)) / ((pV1 - pV2) * (pV1 - pV3));
      let l2 = ((v - pV1) * (v - pV3)) / ((pV2 - pV1) * (pV2 - pV3));
      let l3 = ((v - pV1) * (v - pV2)) / ((pV3 - pV1) * (pV3 - pV2));
      return pSi1 * l1 + pSi2 * l2 + pSi3 * l3;
    }

    if (wtBal > wtDry) {
      ctx.beginPath(); ctx.setLineDash([4, 4]); ctx.strokeStyle = '#d97706'; ctx.lineWidth = 2;
      let first = true;
      for (let v = minV; v <= maxV; v += 2) {
        let vUnbal = v / balRatio;
        let sink = getSink(vUnbal, v1, si1, v2, si2, v3, si3) * balRatio;
        if (sink < 0.1) sink = 0.1;
        let x = padL + ((v - minV) / (maxV - minV)) * chartW;
        let y = padT + (sink / maxSink) * chartH;
        if (y > padT + chartH) continue;
        if (first) { ctx.moveTo(x, y); first = false; } else ctx.lineTo(x, y);
      }
      ctx.stroke(); ctx.setLineDash([]);
    }

    ctx.beginPath(); ctx.strokeStyle = '#2563eb'; ctx.lineWidth = 2.5;
    let bestLD = 0, bestV = 0, first = true;
    for (let v = minV; v <= maxV; v += 2) {
      let sink = getSink(v, v1, si1, v2, si2, v3, si3);
      if (sink < 0.1) sink = 0.1;
      let ld = (v / 3.6) / sink;
      if (ld > bestLD && v >= v1) { bestLD = ld; bestV = v; }
      let x = padL + ((v - minV) / (maxV - minV)) * chartW;
      let y = padT + (sink / maxSink) * chartH;
      if (y > padT + chartH) continue;
      if (first) { ctx.moveTo(x, y); first = false; } else ctx.lineTo(x, y);
    }
    ctx.stroke();
    if (bestLD > 0 && document.getElementById('best-ld-lbl')) {
      document.getElementById('best-ld-lbl').innerText = `L/D: ${bestLD.toFixed(1)} @ ${Math.round(bestV)} km/h`;
    }
  }

  function loadFiles() {
    fetch('/api/files').then(r => r.json()).then(files => {
      let h = '';
      if (files.length === 0) {
        h = '<p class="empty-hint">No flight logs found on SD card</p>';
      } else {
        files.forEach(f => {
          h += `<div class="file-row">
            <div class="file-info">
              <span class="file-name">${f.name}</span>
              <span class="file-size">${Math.round(f.size/1024)} KB</span>
            </div>
            <div class="file-actions">
              <a href="/dl?f=${f.name}" class="btn">Get</a>
              <a href="/del?f=${f.name}" class="btn btn-danger" onclick="return confirm('Delete file?')">Del</a>
            </div>
          </div>`;
        });
      }
      if (document.getElementById('files-list')) document.getElementById('files-list').innerHTML = h;
    }).catch(e => {});
  }

  function loadConfig() {
    return fetch('/api/config').then(r => r.json()).then(c => {
      if (c.bright !== undefined) { document.getElementById('cfg-bright').value = c.bright; document.getElementById('lbl-bright').innerText = c.bright; }
      if (c.pitch !== undefined) { document.getElementById('cfg-pitch').value = c.pitch; document.getElementById('lbl-pitch').innerText = c.pitch + ' Hz'; }
      if (c.wave !== undefined) document.getElementById('cfg-wave').value = c.wave;
      if (c.spread !== undefined) { document.getElementById('cfg-spread').value = c.spread; document.getElementById('lbl-spread').innerText = c.spread; }
      if (c.range !== undefined) document.getElementById('cfg-range').value = c.range;
      if (c.filter !== undefined) document.getElementById('cfg-filter').value = c.filter;
      if (c.avg !== undefined) document.getElementById('cfg-avg').value = c.avg;
      if (c.ualt !== undefined) document.getElementById('cfg-ualt').value = c.ualt;
      if (c.uspeed !== undefined) document.getElementById('cfg-uspeed').value = c.uspeed;
      if (c.uvert !== undefined) document.getElementById('cfg-uvert').value = c.uvert;
      if (c.rot !== undefined) document.getElementById('cfg-rot').value = c.rot;
      if (c.appconn !== undefined) document.getElementById('cfg-appconn').value = c.appconn;
      if (c.condor !== undefined) document.getElementById('cfg-condor').value = c.condor;
      if (c.glidx !== undefined) document.getElementById('glider-select').value = c.glidx;
      if (c.glewt !== undefined) document.getElementById('polar-wt').value = c.glewt;
      if (c.glmbal !== undefined) document.getElementById('polar-bal').value = c.glmbal;
      if (c.v1 !== undefined) document.getElementById('v1').value = c.v1;
      if (c.si1 !== undefined) document.getElementById('si1').value = c.si1;
      if (c.v2 !== undefined) document.getElementById('v2').value = c.v2;
      if (c.si2 !== undefined) document.getElementById('si2').value = c.si2;
      if (c.v3 !== undefined) document.getElementById('v3').value = c.v3;
      if (c.si3 !== undefined) document.getElementById('si3').value = c.si3;
      updatePolarChart();
    }).catch(e => {});
  }

  function saveConfigSilent() {
    fetch('/api/config', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        bright: parseInt(document.getElementById('cfg-bright').value),
        pitch: parseInt(document.getElementById('cfg-pitch').value),
        wave: parseInt(document.getElementById('cfg-wave').value),
        spread: parseInt(document.getElementById('cfg-spread').value),
        range: parseInt(document.getElementById('cfg-range').value),
        filter: parseInt(document.getElementById('cfg-filter').value),
        avg: parseInt(document.getElementById('cfg-avg').value),
        ualt: parseInt(document.getElementById('cfg-ualt').value),
        uspeed: parseInt(document.getElementById('cfg-uspeed').value),
        uvert: parseInt(document.getElementById('cfg-uvert').value),
        rot: parseInt(document.getElementById('cfg-rot').value),
        appconn: parseInt(document.getElementById('cfg-appconn').value),
        condor: parseInt(document.getElementById('cfg-condor').value),
        glidx: parseInt(document.getElementById('glider-select').value) || 0,
        glewt: parseFloat(document.getElementById('polar-wt').value) || 0,
        glmbal: parseFloat(document.getElementById('polar-bal').value) || 0,
        v1: parseFloat(document.getElementById('v1').value) || 0,
        si1: parseFloat(document.getElementById('si1').value) || 0,
        v2: parseFloat(document.getElementById('v2').value) || 0,
        si2: parseFloat(document.getElementById('si2').value) || 0,
        v3: parseFloat(document.getElementById('v3').value) || 0,
        si3: parseFloat(document.getElementById('si3').value) || 0
      })
    }).catch(() => {});
  }

  function saveConfig() {
    saveConfigSilent();
    alert('Settings successfully sent and applied to L!M Vario.');
  }

  // ---- Connected / not-connected detection ----
  // Periodic ping of a vario API. Two consecutive failures -> "not connected"
  // screen. A success -> hide the screen and (re)load the data.
  let linkFails = 0;
  let wasOffline = false;
  function setOnline(online) {
    const dc = document.getElementById('disconnected');
    const badge = document.getElementById('statusBadge');
    const txt = document.getElementById('statusText');
    if (online) {
      dc.classList.remove('show', 'checking');
      if (badge) badge.classList.remove('offline');
      if (txt) txt.textContent = 'Connected';
      if (wasOffline) {   // vario just came back -> reload everything
        wasOffline = false;
        loadGliderDb().then(loadConfig).then(loadProfiles);
        loadFiles(); loadScreenConfig();
      }
    } else {
      wasOffline = true;
      dc.classList.add('show');
      dc.classList.remove('checking');
      if (badge) badge.classList.add('offline');
      if (txt) txt.textContent = 'Disconnected';
    }
  }
  function checkLink() {
    const ctrl = new AbortController();
    const to = setTimeout(() => ctrl.abort(), 2500);
    return fetch('/api/config', { signal: ctrl.signal, cache: 'no-store' })
      .then(r => { clearTimeout(to); if (!r.ok) throw 0; linkFails = 0; setOnline(true); return true; })
      .catch(() => { clearTimeout(to); linkFails++; if (linkFails >= 2) setOnline(false); return false; });
  }
  function retryLink() {
    document.getElementById('disconnected').classList.add('checking');
    linkFails = 2;  // a single failure is enough to flip back if still down
    checkLink();
  }
  // Tries to open the WiFi settings (Android: intent; otherwise on-screen instructions).
  function openWifiSettings() {
    const ua = navigator.userAgent || '';
    if (/Android/i.test(ua)) {
      // Some Android browsers honor this intent; otherwise nothing happens
      // and the user follows the network info shown just above.
      window.location.href = 'intent://#Intent;action=android.settings.WIFI_SETTINGS;end';
    } else {
      alert('Open Settings > Wi-Fi and select the "LIM-Vario" network (password: limvario).');
    }
  }

  window.onload = () => {
    loadGliderDb().then(loadConfig).then(loadProfiles); loadFiles(); loadScreenConfig();
    checkLink();
    setInterval(checkLink, 4000);   // continuously monitor the connection
  };
</script>
</body>
</html>)rawliteral";
