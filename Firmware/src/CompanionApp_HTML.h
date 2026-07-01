#pragma once
#include <Arduino.h>

static const char COMPANION_APP_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>L!M Vario — Companion App</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&family=JetBrains+Mono:wght@400;500;600;700&display=swap" rel="stylesheet">
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
  }

  * { box-sizing: border-box; margin: 0; padding: 0; -webkit-tap-highlight-color: transparent; }
  
  body {
    background-color: var(--bg-app);
    color: var(--text-main);
    font-family: var(--font-sans);
    min-height: 100vh;
    display: flex;
    flex-direction: column;
    -webkit-font-smoothing: antialiased;
  }

  /* TOP HEADER */
  header {
    background: #ffffff;
    border-bottom: 1px solid var(--border-color);
    padding: 0.85rem 1.5rem;
    display: flex;
    justify-content: space-between;
    align-items: center;
    position: sticky;
    top: 0;
    z-index: 50;
    box-shadow: var(--shadow-sm);
  }
  
  .brand {
    font-size: 1.15rem;
    font-weight: 700;
    display: flex;
    align-items: center;
    gap: 0.6rem;
    color: var(--text-main);
    letter-spacing: -0.02em;
  }
  
  .brand-badge {
    background: var(--text-main);
    color: #fff;
    padding: 0.2rem 0.5rem;
    border-radius: 6px;
    font-size: 0.75rem;
    font-weight: 700;
    letter-spacing: 0.05em;
  }

  .status-badge {
    display: inline-flex;
    align-items: center;
    gap: 0.4rem;
    font-size: 0.78rem;
    font-weight: 600;
    padding: 0.35rem 0.75rem;
    border-radius: 9999px;
    background: #ecfdf5;
    color: #065f46;
    border: 1px solid #a7f3d0;
  }
  
  .status-dot {
    width: 7px;
    height: 7px;
    border-radius: 50%;
    background: #10b981;
  }

  /* NAVIGATION TABS */
  .nav-wrapper {
    background: #ffffff;
    border-bottom: 1px solid var(--border-color);
    padding: 0.5rem 1rem;
    overflow-x: auto;
  }

  nav {
    display: flex;
    gap: 0.4rem;
    max-width: 850px;
    margin: 0 auto;
  }

  .tab-btn {
    flex: 1;
    white-space: nowrap;
    background: transparent;
    border: none;
    color: var(--text-muted);
    font-family: var(--font-sans);
    font-size: 0.85rem;
    font-weight: 600;
    padding: 0.6rem 0.8rem;
    border-radius: 8px;
    cursor: pointer;
    transition: all 0.15s ease;
    text-align: center;
  }

  .tab-btn:hover { color: var(--text-main); background: #f8fafc; }
  
  .tab-btn.active {
    background: #0f172a;
    color: #ffffff;
    box-shadow: var(--shadow-sm);
  }

  /* MAIN CONTENT AREA */
  main {
    flex: 1;
    max-width: 850px;
    width: 100%;
    margin: 0 auto;
    padding: 1.5rem 1rem 3rem 1rem;
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
    padding: 1.5rem;
    box-shadow: var(--shadow-sm);
    margin-bottom: 1.25rem;
  }

  .card-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 1.25rem;
    padding-bottom: 0.75rem;
    border-bottom: 1px solid var(--border-color);
    gap: 1rem;
  }

  .card-title {
    font-size: 1rem;
    font-weight: 700;
    color: var(--text-main);
  }

  /* ACCORDIONS */
  .accordion-item {
    background: #ffffff;
    border: 1px solid var(--border-color);
    border-radius: 10px;
    margin-bottom: 0.75rem;
    overflow: hidden;
    transition: border-color 0.15s ease;
  }

  .accordion-header {
    width: 100%;
    background: #ffffff;
    border: none;
    padding: 1rem 1.25rem;
    display: flex;
    justify-content: space-between;
    align-items: center;
    font-family: var(--font-sans);
    font-size: 0.95rem;
    font-weight: 600;
    color: var(--text-main);
    cursor: pointer;
    text-align: left;
  }

  .accordion-header:hover { background: #f8fafc; }

  .accordion-arrow {
    font-size: 0.8rem;
    color: var(--text-muted);
    transition: transform 0.2s ease;
  }

  .accordion-item.open { border-color: #cbd5e1; box-shadow: var(--shadow-sm); }
  .accordion-item.open .accordion-arrow { transform: rotate(180deg); }

  .accordion-body {
    display: none;
    padding: 1.25rem;
    border-top: 1px solid var(--border-color);
    background: #fdfdfd;
  }

  .accordion-item.open .accordion-body { display: block; }

  /* FORM ELEMENTS */
  .form-group { margin-bottom: 1.15rem; }
  .form-group:last-child { margin-bottom: 0; }
  
  .form-label {
    display: block;
    font-size: 0.75rem;
    color: var(--text-muted);
    margin-bottom: 0.4rem;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.03em;
  }

  .form-control {
    width: 100%;
    background: #ffffff;
    border: 1px solid var(--border-color);
    border-radius: 8px;
    padding: 0.65rem 0.85rem;
    color: var(--text-main);
    font-family: var(--font-sans);
    font-size: 0.9rem;
    font-weight: 500;
    transition: all 0.15s ease;
  }

  .form-control:focus {
    outline: none;
    border-color: var(--border-focus);
    box-shadow: 0 0 0 3px rgba(37, 99, 235, 0.12);
  }

  .range-row { display: flex; align-items: center; gap: 0.85rem; }
  
  .range-val {
    font-family: var(--font-mono);
    font-weight: 600;
    color: var(--accent-blue);
    min-width: 65px;
    text-align: right;
    font-size: 0.85rem;
    background: #f1f5f9;
    padding: 0.3rem 0.5rem;
    border-radius: 6px;
  }

  input[type=range] {
    flex: 1;
    accent-color: var(--accent-blue);
    height: 6px;
    background: #e2e8f0;
    border-radius: 3px;
    outline: none;
  }

  /* BUTTONS */
  .btn {
    background: #ffffff;
    border: 1px solid var(--border-color);
    color: var(--text-main);
    padding: 0.65rem 1.15rem;
    border-radius: 8px;
    font-weight: 600;
    font-size: 0.88rem;
    cursor: pointer;
    transition: all 0.15s ease;
    display: inline-flex;
    align-items: center;
    justify-content: center;
  }

  .btn:hover { background: #f8fafc; border-color: #cbd5e1; }

  .btn-primary {
    background: var(--accent-blue);
    color: #ffffff;
    border: 1px solid transparent;
  }

  .btn-primary:hover { background: var(--accent-hover); }

  .btn-danger {
    background: #fef2f2;
    color: var(--danger);
    border-color: #fecaca;
  }

  .btn-danger:hover { background: #fee2e2; border-color: #f87171; }

  /* FILE TABLE */
  .file-table {
    width: 100%;
    border-collapse: separate;
    border-spacing: 0;
    text-align: left;
  }

  .file-table th {
    padding: 0.75rem 0.85rem;
    font-size: 0.72rem;
    text-transform: uppercase;
    color: var(--text-muted);
    border-bottom: 2px solid var(--border-color);
    font-weight: 700;
  }

  .file-table td {
    padding: 0.85rem;
    font-size: 0.88rem;
    border-bottom: 1px solid var(--border-color);
    font-family: var(--font-mono);
  }

  /* ROUND DIAL WITH PERFECT SPACING (NO OVERLAP) */
  .vario-screen-scaler {
    display: flex;
    justify-content: center;
    align-items: center;
    overflow: hidden;
    padding: 0.5rem 0;
  }

  .round-dial-container {
    width: 360px;
    height: 360px;
    position: relative;
    border-radius: 50%;
    background: #080c14;
    border: 6px solid #1e293b;
    box-shadow: 0 15px 35px rgba(0, 0, 0, 0.25), inset 0 0 15px rgba(0,0,0,0.8);
    font-family: var(--font-mono);
    user-select: none;
    flex-shrink: 0;
  }

  @media (max-width: 400px) {
    .round-dial-container {
      width: 320px;
      height: 320px;
    }
    .thermal-circle { width: 104px !important; height: 104px !important; top: calc(50% - 52px) !important; left: calc(50% - 52px) !important; font-size: 0.8rem !important; }
    .ib-box { width: 136px !important; height: 34px !important; left: calc(50% - 68px) !important; font-size: 0.84rem !important; }
    .slot-0 { top: 14px !important; }
    .slot-1 { top: 58px !important; }
    .slot-2 { bottom: 58px !important; }
    .slot-3 { bottom: 14px !important; }
    .slot-4 { right: 12px !important; width: 56px !important; height: 42px !important; }
  }

  /* Center Clickable Hub */
  .thermal-circle {
    position: absolute;
    width: 120px;
    height: 120px;
    top: calc(50% - 60px);
    left: calc(50% - 60px);
    border-radius: 50%;
    border: 2px solid #2563eb;
    background: #0b1329;
    display: flex;
    align-items: center;
    justify-content: center;
    color: #f8fafc;
    font-size: 0.86rem;
    font-family: var(--font-sans);
    font-weight: 600;
    text-align: center;
    padding: 0.5rem;
    cursor: pointer;
    transition: all 0.15s ease;
    z-index: 10;
  }

  .thermal-circle:hover { background: #152244; border-color: #60a5fa; }

  /* InfoBox Slots (STRICT GEOMETRY WITH BREATHING ROOM) */
  .ib-box {
    position: absolute;
    border: 2px solid #2563eb;
    border-radius: 8px;
    background: #0f172a;
    color: #ffffff;
    cursor: pointer;
    transition: all 0.15s ease;
    display: flex;
    align-items: center;
    justify-content: center;
    padding: 4px 8px;
    z-index: 20;
    font-size: 0.88rem;
    font-weight: 700;
  }

  .ib-box:hover { background: #1e293b; border-color: #60a5fa; }

  /* Exact geometry ensuring 16px clean gap around center circle */
  .slot-0 { top: 18px; left: calc(50% - 75px); width: 150px; height: 38px; }
  .slot-1 { top: 66px; left: calc(50% - 75px); width: 150px; height: 38px; }
  .slot-2 { bottom: 66px; left: calc(50% - 75px); width: 150px; height: 38px; }
  .slot-3 { bottom: 18px; left: calc(50% - 75px); width: 150px; height: 38px; }
  
  /* Slot 4 passive right frame */
  .slot-4 {
    top: calc(50% - 22px);
    right: 16px;
    width: 60px;
    height: 44px;
    cursor: default;
    background: #0a0f1d;
    border-color: #1e293b;
  }
  .slot-4:hover { background: #0a0f1d; border-color: #1e293b; }

  /* MODALS */
  .modal-overlay {
    display: none;
    position: fixed;
    top: 0; left: 0; width: 100%; height: 100%;
    background: rgba(15, 23, 42, 0.65);
    backdrop-filter: blur(4px);
    z-index: 100;
    align-items: center;
    justify-content: center;
    padding: 1rem;
  }
  
  .modal-overlay.active { display: flex; }

  .modal-content {
    background: #ffffff;
    border-radius: 14px;
    padding: 1.5rem;
    width: 100%;
    max-width: 420px;
    box-shadow: var(--shadow-lg);
    border: 1px solid var(--border-color);
  }

  .modal-header {
    font-size: 1.05rem;
    font-weight: 700;
    color: var(--text-main);
    margin-bottom: 1rem;
    display: flex;
    justify-content: space-between;
    align-items: center;
  }

  .modal-close {
    background: #f1f5f9;
    border: none;
    width: 30px;
    height: 30px;
    border-radius: 50%;
    font-size: 1rem;
    cursor: pointer;
    color: var(--text-muted);
    display: flex;
    align-items: center;
    justify-content: center;
  }

  .picker-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 0.5rem;
    max-height: 65vh;
    overflow-y: auto;
  }
  
  .picker-grid.single-col { grid-template-columns: 1fr; }

  .picker-btn {
    text-align: left;
    justify-content: flex-start;
    padding: 0.75rem 0.9rem;
    font-size: 0.85rem;
  }

  /* CHART AREA */
  #polar-canvas {
    width: 100%;
    height: 250px;
    background: #ffffff;
    border: 1px solid var(--border-color);
    border-radius: 8px;
  }

  .chart-legend {
    display: flex;
    flex-wrap: wrap;
    gap: 1.25rem;
    font-size: 0.78rem;
    font-family: var(--font-mono);
    margin-top: 0.75rem;
    align-items: center;
  }

  .legend-item { display: flex; align-items: center; gap: 0.4rem; color: var(--text-muted); }
  .line-sample { width: 18px; height: 3px; border-radius: 2px; }
</style>
</head>
<body>

<header>
  <div class="brand"><span class="brand-badge">L!M</span> Vario Companion</div>
  <div class="status-badge"><div class="status-dot"></div> Connected</div>
</header>

<div class="nav-wrapper">
  <nav>
    <button class="tab-btn active" onclick="switchTab('tab-infobox')">Screen InfoBoxes</button>
    <button class="tab-btn" onclick="switchTab('tab-settings')">System Settings</button>
    <button class="tab-btn" onclick="switchTab('tab-profiles')">Profiles</button>
    <button class="tab-btn" onclick="switchTab('tab-files')">Flight Logs</button>
    <button class="tab-btn" onclick="switchTab('tab-update')">Firmware Update</button>
  </nav>
</div>

<main>
  <!-- 1. SCREEN INFOBOXES -->
  <div id="tab-infobox" class="view-pane active">
    <div class="card">
      <div class="card-header">
        <div>
          <span class="card-title">Screen Layout</span>
          <p style="font-size: 0.8rem; color: var(--text-muted); margin-top: 0.2rem;">Tap any box or the center display to assign a metric.</p>
        </div>
        <button class="btn btn-primary" onclick="saveConfig()">Apply to Vario</button>
      </div>
      
      <div class="vario-screen-scaler">
        <div class="round-dial-container">
          <!-- Center Clickable Hub -->
          <div class="thermal-circle" id="center-val" onclick="openCenterModal()">Thermal Helper</div>

          <!-- 4 Active Clickable Slots with clean separation -->
          <div class="ib-box slot-0" id="slot-lbl-0" onclick="openMetricModal(0)">Barometric Altitude</div>
          <div class="ib-box slot-1" id="slot-lbl-1" onclick="openMetricModal(1)">Average Vario</div>
          <div class="ib-box slot-2" id="slot-lbl-2" onclick="openMetricModal(2)">Airspeed</div>
          <div class="ib-box slot-3" id="slot-lbl-3" onclick="openMetricModal(3)">Glide Ratio</div>
          
          <!-- Passive empty right slot -->
          <div class="ib-box slot-4"></div>
        </div>
      </div>
    </div>
  </div>

  <!-- 2. SYSTEM SETTINGS (FULL UNABBREVIATED LABELS) -->
  <div id="tab-settings" class="view-pane">
    <div class="card" style="margin-bottom: 1.25rem;">
      <div class="card-header" style="margin-bottom: 0.5rem; border-bottom: none; padding-bottom: 0;">
        <span class="card-title">Instrument Settings</span>
        <button class="btn btn-primary" onclick="saveConfig()">Save to Vario</button>
      </div>
      <p style="font-size: 0.82rem; color: var(--text-muted); margin-bottom: 1rem;">Tap any section below to configure. Changes sync directly to the instrument.</p>

      <!-- Submenu 1: Display -->
      <div class="accordion-item open">
        <button class="accordion-header" onclick="toggleAccordion(this)">
          <span>Display</span>
          <span class="accordion-arrow">▼</span>
        </button>
        <div class="accordion-body">
          <div class="form-group">
            <label class="form-label">Screen Brightness</label>
            <div class="range-row">
              <input type="range" id="cfg-bright" min="0" max="20" value="16" oninput="document.getElementById('lbl-bright').innerText=this.value; autoSync()">
              <span class="range-val" id="lbl-bright">16</span>
            </div>
          </div>
          <div class="form-group">
            <label class="form-label">Vertical Speed Unit</label>
            <select id="cfg-uvert" class="form-control" onchange="autoSync()">
              <option value="0">Meters per second (m/s)</option>
              <option value="1">Knots (kt)</option>
            </select>
          </div>
          <div class="form-group">
            <label class="form-label">Altitude Unit</label>
            <select id="cfg-ualt" class="form-control" onchange="autoSync()">
              <option value="0">Meters (m)</option>
              <option value="1">Feet (ft)</option>
            </select>
          </div>
          <div class="form-group">
            <label class="form-label">Airspeed Unit</label>
            <select id="cfg-uspeed" class="form-control" onchange="autoSync()">
              <option value="0">Kilometers per hour (km/h)</option>
              <option value="1">Knots (kt)</option>
            </select>
          </div>
          <div class="form-group">
            <label class="form-label">Screen Rotation Angle</label>
            <select id="cfg-rot" class="form-control" onchange="autoSync()">
              <option value="0">0 Degrees</option>
              <option value="90">90 Degrees</option>
              <option value="180">180 Degrees</option>
              <option value="270">270 Degrees</option>
            </select>
          </div>
        </div>
      </div>

      <!-- Submenu 2: Sound -->
      <div class="accordion-item">
        <button class="accordion-header" onclick="toggleAccordion(this)">
          <span>Sound</span>
          <span class="accordion-arrow">▼</span>
        </button>
        <div class="accordion-body">
          <div class="form-group">
            <label class="form-label">Audio Tone Pitch Frequency</label>
            <div class="range-row">
              <input type="range" id="cfg-pitch" min="200" max="1500" step="50" value="700" oninput="document.getElementById('lbl-pitch').innerText=this.value+' Hz'; autoSync()">
              <span class="range-val" id="lbl-pitch">700 Hz</span>
            </div>
          </div>
          <div class="form-group">
            <label class="form-label">Synthesizer Waveform</label>
            <select id="cfg-wave" class="form-control" onchange="autoSync()">
              <option value="0">Sine Wave</option>
              <option value="1">Square Wave</option>
              <option value="2">Triangle Wave</option>
            </select>
          </div>
          <div class="form-group">
            <label class="form-label">Audio Tone Spread Factor</label>
            <div class="range-row">
              <input type="range" id="cfg-spread" min="0" max="10" value="5" oninput="document.getElementById('lbl-spread').innerText=this.value; autoSync()">
              <span class="range-val" id="lbl-spread">5</span>
            </div>
          </div>
        </div>
      </div>

      <!-- Submenu 3: Vario -->
      <div class="accordion-item">
        <button class="accordion-header" onclick="toggleAccordion(this)">
          <span>Vario</span>
          <span class="accordion-arrow">▼</span>
        </button>
        <div class="accordion-body">
          <div class="form-group">
            <label class="form-label">Variometer Scale Range</label>
            <select id="cfg-range" class="form-control" onchange="autoSync()">
              <option value="5">+/- 5 m/s</option>
              <option value="10">+/- 10 m/s</option>
            </select>
          </div>
          <div class="form-group">
            <label class="form-label">Variometer Damping Filter</label>
            <select id="cfg-filter" class="form-control" onchange="autoSync()">
              <option value="0">Fast Response</option>
              <option value="1" selected>Medium Response</option>
              <option value="2">Slow Response</option>
            </select>
          </div>
          <div class="form-group">
            <label class="form-label">Average Climb Window Period</label>
            <select id="cfg-avg" class="form-control" onchange="autoSync()">
              <option value="0">15 Seconds</option>
              <option value="1" selected>20 Seconds</option>
              <option value="2">30 Seconds</option>
            </select>
          </div>
        </div>
      </div>

      <!-- Submenu 4: System -->
      <div class="accordion-item">
        <button class="accordion-header" onclick="toggleAccordion(this)">
          <span>System</span>
          <span class="accordion-arrow">▼</span>
        </button>
        <div class="accordion-body">
          <div class="form-group">
            <label class="form-label">Companion App Connection</label>
            <select id="cfg-appconn" class="form-control" onchange="autoSync()">
              <option value="1">Enabled (ON)</option>
              <option value="0">Disabled (OFF)</option>
            </select>
          </div>
          <div class="form-group">
            <label class="form-label">Condor Simulator Telemetry Link</label>
            <select id="cfg-condor" class="form-control" onchange="autoSync()">
              <option value="0">Disabled (OFF)</option>
              <option value="1">Enabled (ON)</option>
            </select>
          </div>
          <div style="margin-top: 1.25rem; padding-top: 1.25rem; border-top: 1px solid var(--border-color); display: flex; justify-content: space-between; align-items: center;">
            <span style="font-size: 0.85rem; color: var(--text-muted); font-weight: 600;">Firmware Version: v0.8.0</span>
            <button class="btn btn-danger" onclick="confirm('Perform full Factory Reset on Vario?')">Factory Reset</button>
          </div>
        </div>
      </div>
    </div>
  </div>

  <!-- 3. PROFILES & GLIDER -->
  <div id="tab-profiles" class="view-pane">
    <div class="card">
      <div class="card-header">
        <span class="card-title">Pilot & Glider Profile</span>
        <button class="btn btn-primary" onclick="saveProfile()">Save Full Profile</button>
      </div>
      <p style="font-size: 0.8rem; color: var(--text-muted); margin-bottom: 1rem;">Saving a profile stores ALL current instrument settings, InfoBoxes layout, and glider aerodynamic curves directly onto the L!M Vario.</p>
      <div class="form-group">
        <label class="form-label">Active Profile</label>
        <select class="form-control" id="prof-select" onchange="selectProfile(this.selectedIndex)"></select>
      </div>
      <div class="form-group">
        <label class="form-label">Profile Name (Maximum 5 Characters)</label>
        <input type="text" id="prof-name" class="form-control" maxlength="5" placeholder="e.g. LS4" style="font-family: var(--font-mono); font-weight: 700; text-transform: uppercase;">
      </div>
      <div style="display: flex; gap: 0.75rem; margin-top: 0.75rem;">
        <button class="btn" style="flex:1" onclick="createNewProfile()">New Profile</button>
        <button class="btn btn-danger" style="flex:1" onclick="deleteProfile()">Delete Profile</button>
      </div>
    </div>

    <div class="card">
      <div class="card-header"><span class="card-title">Glider Specifications</span></div>
      <div class="form-group">
        <label class="form-label">Glider Model Preset</label>
        <select class="form-control" id="glider-select" onchange="applyGliderPreset(this.value)">
          <option value="LS4">LS4 Standard</option>
          <option value="Discus2">Discus 2a</option>
          <option value="Pegase">Pegase 101</option>
        </select>
      </div>
      <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 1rem;">
        <div class="form-group">
          <label class="form-label">Empty Weight (kg)</label>
          <input type="number" id="polar-wt" class="form-control" value="310" oninput="updatePolarChart(); autoSync()">
        </div>
        <div class="form-group">
          <label class="form-label">Maximum Ballast Capacity (kg)</label>
          <input type="number" id="polar-bal" class="form-control" value="450" oninput="updatePolarChart(); autoSync()">
        </div>
      </div>
    </div>
    
    <div class="card">
      <div class="card-header"><span class="card-title">Aerodynamic Polar Curve</span></div>
      <canvas id="polar-canvas"></canvas>
      <div class="chart-legend">
        <div class="legend-item"><div class="line-sample" style="background:#2563eb"></div><span>Empty Weight (<span id="wt-dry-lbl">310 kg</span>)</span></div>
        <div class="legend-item"><div class="line-sample" style="border-top: 3px dashed #d97706; background:transparent"></div><span>Ballasted Weight (<span id="wt-bal-lbl">450 kg</span>)</span></div>
        <div style="margin-left:auto; font-weight:700; color:var(--accent-blue)" id="best-ld-lbl">L/D: 40.5 @ 100 km/h</div>
      </div>
    </div>

    <div class="card">
      <div class="card-header"><span class="card-title">3-Point Aerodynamic Polar Coefficients</span></div>
      <div style="display: flex; flex-direction: column; gap: 0.75rem;">
        <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 0.75rem;">
          <div><label class="form-label">Airspeed Point 1 (km/h)</label><input type="number" id="v1" class="form-control" value="80" oninput="updatePolarChart(); autoSync()"></div>
          <div><label class="form-label">Sink Rate Point 1 (m/s)</label><input type="number" step="0.01" id="si1" class="form-control" value="-0.65" oninput="updatePolarChart(); autoSync()"></div>
        </div>
        <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 0.75rem;">
          <div><label class="form-label">Airspeed Point 2 (km/h)</label><input type="number" id="v2" class="form-control" value="105" oninput="updatePolarChart(); autoSync()"></div>
          <div><label class="form-label">Sink Rate Point 2 (m/s)</label><input type="number" step="0.01" id="si2" class="form-control" value="-0.72" oninput="updatePolarChart(); autoSync()"></div>
        </div>
        <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 0.75rem;">
          <div><label class="form-label">Airspeed Point 3 (km/h)</label><input type="number" id="v3" class="form-control" value="160" oninput="updatePolarChart(); autoSync()"></div>
          <div><label class="form-label">Sink Rate Point 3 (m/s)</label><input type="number" step="0.01" id="si3" class="form-control" value="-1.85" oninput="updatePolarChart(); autoSync()"></div>
        </div>
      </div>
      <button class="btn btn-primary" style="margin-top: 1.25rem; width: 100%;" onclick="saveProfile()">Save Full Profile to Vario</button>
    </div>
  </div>

  <!-- 4. FLIGHT LOGS -->
  <div id="tab-files" class="view-pane">
    <div class="card">
      <div class="card-header">
        <span class="card-title">Flight Logs</span>
        <button class="btn" onclick="loadFiles()">Refresh List</button>
      </div>
      <div style="overflow-x: auto;">
        <table class="file-table">
          <thead><tr><th>File Name</th><th>Size</th><th>Actions</th></tr></thead>
          <tbody id="files-tbody"><tr><td colspan="3" style="color:var(--text-muted)">Loading flight logs from SD card...</td></tr></tbody>
        </table>
      </div>
    </div>
  </div>

  <!-- 5. FIRMWARE UPDATE (OTA) -->
  <div id="tab-update" class="view-pane">
    <div class="card">
      <div class="card-header">
        <div>
          <span class="card-title">Over-The-Air (OTA) Firmware Update</span>
          <p style="font-size: 0.8rem; color: var(--text-muted); margin-top: 0.2rem;">Upload a compiled binary file (.bin) over Wi-Fi to update the L!M Vario instrument.</p>
        </div>
      </div>
      <div class="form-group" style="margin-top: 1rem;">
        <label class="form-label">Select Firmware Binary File (.bin)</label>
        <input type="file" id="ota-file" accept=".bin" class="form-control" style="padding: 0.5rem;">
      </div>
      <div id="ota-progress-container" style="display:none; margin-top:1.25rem;">
        <div style="background:#e2e8f0; border-radius:6px; height:12px; overflow:hidden;">
          <div id="ota-progress-bar" style="background:#2563eb; width:0%; height:100%; transition:width 0.2s ease;"></div>
        </div>
        <p id="ota-status" style="font-size:0.85rem; color:var(--text-main); font-weight:600; margin-top:0.5rem; text-align:center;">Uploading: 0%</p>
      </div>
      <button class="btn btn-primary" style="margin-top: 1.5rem; width: 100%; padding: 0.85rem;" onclick="uploadFirmware()">Install Firmware Update</button>
    </div>
  </div>
</main>

<!-- MODAL FOR INFOBOX METRIC SELECTION -->
<div id="metric-modal" class="modal-overlay">
  <div class="modal-content">
    <div class="modal-header">
      <span>Select InfoBox Metric</span>
      <button class="modal-close" onclick="closeMetricModal()">✕</button>
    </div>
    <div class="picker-grid">
      <button class="btn picker-btn" onclick="selectMetricValue('Instantaneous Vario')">Instantaneous Vario</button>
      <button class="btn picker-btn" onclick="selectMetricValue('Average Vario')">Average Vario</button>
      <button class="btn picker-btn" onclick="selectMetricValue('MacCready Setting')">MacCready Setting</button>
      <button class="btn picker-btn" onclick="selectMetricValue('Barometric Altitude')">Barometric Altitude</button>
      <button class="btn picker-btn" onclick="selectMetricValue('GPS Altitude')">GPS Altitude</button>
      <button class="btn picker-btn" onclick="selectMetricValue('Local Time')">Local Time</button>
      <button class="btn picker-btn" onclick="selectMetricValue('Flight Duration')">Flight Duration</button>
      <button class="btn picker-btn" onclick="selectMetricValue('Wind Direction & Speed')">Wind Direction & Speed</button>
      <button class="btn picker-btn" onclick="selectMetricValue('Altitude Gain')">Altitude Gain</button>
      <button class="btn picker-btn" onclick="selectMetricValue('Flight Level')">Flight Level</button>
      <button class="btn picker-btn" onclick="selectMetricValue('Glide Ratio')">Glide Ratio</button>
      <button class="btn picker-btn btn-danger" onclick="selectMetricValue('Disabled')">Disabled</button>
    </div>
  </div>
</div>

<!-- MODAL FOR CENTER DISPLAY SELECTION -->
<div id="center-modal" class="modal-overlay">
  <div class="modal-content">
    <div class="modal-header">
      <span>Select Center Display Mode</span>
      <button class="modal-close" onclick="closeCenterModal()">✕</button>
    </div>
    <div class="picker-grid single-col">
      <button class="btn picker-btn" onclick="selectCenterValue('Thermal Helper')">Thermal Helper</button>
      <button class="btn picker-btn" onclick="selectCenterValue('Wind Direction Indicator')">Wind Direction Indicator</button>
      <button class="btn picker-btn btn-danger" onclick="selectCenterValue('Disabled')">Disabled</button>
    </div>
  </div>
</div>

<script>
  let activeSlot = 0;
  let activeProfileIndex = 0;
  let syncTimeout = null;

  let profiles = [
    { name: 'CLUB', glider: 'LS4', wtDry: 310, wtBal: 450, v1: 80, si1: -0.65, v2: 105, si2: -0.72, v3: 160, si3: -1.85 },
    { name: 'DISC2', glider: 'Discus2', wtDry: 325, wtBal: 480, v1: 85, si1: -0.58, v2: 110, si2: -0.66, v3: 170, si3: -1.60 },
    { name: 'PEGAS', glider: 'Pegase', wtDry: 290, wtBal: 420, v1: 78, si1: -0.68, v2: 100, si2: -0.76, v3: 150, si3: -1.95 }
  ];

  function autoSync() {
    clearTimeout(syncTimeout);
    syncTimeout = setTimeout(() => { saveConfigSilent(); }, 600);
  }

  function renderProfileDropdown(selectedIdx) {
    let sel = document.getElementById('prof-select');
    if (!sel) return;
    sel.innerHTML = '';
    profiles.forEach((p, idx) => {
      let opt = document.createElement('option');
      opt.value = idx;
      opt.text = p.name + ' (' + p.glider + ')';
      if (idx === selectedIdx) opt.selected = true;
      sel.appendChild(opt);
    });
    selectProfile(selectedIdx);
  }

  function selectProfile(idx) {
    activeProfileIndex = idx;
    let p = profiles[idx];
    if (!p) return;
    document.getElementById('prof-name').value = p.name;
    document.getElementById('glider-select').value = p.glider;
    document.getElementById('polar-wt').value = p.wtDry;
    document.getElementById('polar-bal').value = p.wtBal;
    document.getElementById('v1').value = p.v1;
    document.getElementById('si1').value = p.si1;
    document.getElementById('v2').value = p.v2;
    document.getElementById('si2').value = p.si2;
    document.getElementById('v3').value = p.v3;
    document.getElementById('si3').value = p.si3;
    updatePolarChart();
    autoSync();
  }

  function createNewProfile() {
    let baseName = "NEW";
    let counter = 1;
    let candidate = baseName;
    while (profiles.some(p => p.name.toUpperCase() === candidate)) {
      candidate = "NEW" + counter;
      if (candidate.length > 5) candidate = "P" + counter;
      counter++;
    }
    let newProf = {
      name: candidate,
      glider: 'LS4',
      wtDry: 310,
      wtBal: 450,
      v1: 80, si1: -0.65,
      v2: 105, si2: -0.72,
      v3: 160, si3: -1.85
    };
    profiles.push(newProf);
    renderProfileDropdown(profiles.length - 1);
  }

  function saveProfile() {
    let nameInput = document.getElementById('prof-name');
    let newName = nameInput.value.trim().toUpperCase();
    if (!newName) {
      alert("Error: Profile name cannot be empty.");
      return;
    }
    if (newName.length > 5) {
      alert("Error: Profile name must be 5 characters maximum.");
      return;
    }
    let isDup = profiles.some((p, idx) => idx !== activeProfileIndex && p.name.toUpperCase() === newName);
    if (isDup) {
      alert(`Error: A profile named "${newName}" already exists. Each profile must have a unique name.`);
      return;
    }
    let p = profiles[activeProfileIndex];
    p.name = newName;
    p.glider = document.getElementById('glider-select').value;
    p.wtDry = parseFloat(document.getElementById('polar-wt').value) || 300;
    p.wtBal = parseFloat(document.getElementById('polar-bal').value) || 450;
    p.v1 = parseFloat(document.getElementById('v1').value) || 80;
    p.si1 = parseFloat(document.getElementById('si1').value) || -0.65;
    p.v2 = parseFloat(document.getElementById('v2').value) || 105;
    p.si2 = parseFloat(document.getElementById('si2').value) || -0.72;
    p.v3 = parseFloat(document.getElementById('v3').value) || 160;
    p.si3 = parseFloat(document.getElementById('si3').value) || -1.85;

    renderProfileDropdown(activeProfileIndex);
    saveConfigSilent();
    alert(`Complete Profile "${newName}" (settings, InfoBoxes & glider curves) saved directly to L!M Vario.`);
  }

  function deleteProfile() {
    if (profiles.length <= 1) {
      alert("Cannot delete the last remaining profile.");
      return;
    }
    let name = profiles[activeProfileIndex].name;
    if (confirm(`Delete profile "${name}"?`)) {
      profiles.splice(activeProfileIndex, 1);
      renderProfileDropdown(Math.max(0, activeProfileIndex - 1));
    }
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

  function switchTab(id) {
    document.querySelectorAll('.view-pane').forEach(el => el.classList.remove('active'));
    document.querySelectorAll('.tab-btn').forEach(el => el.classList.remove('active'));
    document.getElementById(id).classList.add('active');
    event.target.classList.add('active');
    if (id === 'tab-profiles') updatePolarChart();
  }

  function toggleAccordion(btn) {
    const item = btn.parentElement;
    item.classList.toggle('open');
  }

  function openMetricModal(idx) {
    activeSlot = idx;
    document.getElementById('metric-modal').classList.add('active');
  }

  function closeMetricModal() {
    document.getElementById('metric-modal').classList.remove('active');
  }

  function selectMetricValue(metricName) {
    let el = document.getElementById('slot-lbl-' + activeSlot);
    if (el) {
      el.innerHTML = metricName;
      el.style.color = (metricName === 'Disabled') ? '#64748b' : '#f8fafc';
    }
    closeMetricModal();
    autoSync();
  }

  function openCenterModal() {
    document.getElementById('center-modal').classList.add('active');
  }

  function closeCenterModal() {
    document.getElementById('center-modal').classList.remove('active');
  }

  function selectCenterValue(modeText) {
    let el = document.getElementById('center-val');
    if (el) {
      el.innerHTML = modeText;
      el.style.color = (modeText === 'Disabled') ? '#64748b' : '#f8fafc';
    }
    closeCenterModal();
    autoSync();
  }

  function applyGliderPreset(model) {
    if (model === 'LS4') { document.getElementById('v1').value = 80; document.getElementById('si1').value = -0.65; document.getElementById('v2').value = 105; document.getElementById('si2').value = -0.72; document.getElementById('v3').value = 160; document.getElementById('si3').value = -1.85; document.getElementById('polar-wt').value = 310; }
    else if (model === 'Discus2') { document.getElementById('v1').value = 85; document.getElementById('si1').value = -0.58; document.getElementById('v2').value = 110; document.getElementById('si2').value = -0.66; document.getElementById('v3').value = 170; document.getElementById('si3').value = -1.60; document.getElementById('polar-wt').value = 325; }
    else if (model === 'Pegase') { document.getElementById('v1').value = 78; document.getElementById('si1').value = -0.68; document.getElementById('v2').value = 100; document.getElementById('si2').value = -0.76; document.getElementById('v3').value = 150; document.getElementById('si3').value = -1.95; document.getElementById('polar-wt').value = 290; }
    updatePolarChart();
    autoSync();
  }

  function updatePolarChart() {
    const canvas = document.getElementById('polar-canvas');
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const w = canvas.width = canvas.parentElement.clientWidth - 48;
    const h = canvas.height = 240;
    const padL = 45, padR = 15, padT = 15, padB = 28;
    const chartW = w - padL - padR, chartH = h - padT - padB;
    
    ctx.clearRect(0, 0, w, h);
    ctx.strokeStyle = '#f1f5f9'; ctx.lineWidth = 1;
    ctx.fillStyle = '#64748b'; ctx.font = '10px JetBrains Mono';
    ctx.textAlign = 'right'; ctx.textBaseline = 'middle';
    
    const maxSink = 3.5;
    for (let s = 0; s <= 3.5; s += 0.5) {
      let y = padT + (s / maxSink) * chartH;
      ctx.beginPath(); ctx.moveTo(padL, y); ctx.lineTo(w - padR, y); ctx.stroke();
      ctx.fillText(-s.toFixed(1) + ' m/s', padL - 6, y);
    }
    
    ctx.textAlign = 'center'; ctx.textBaseline = 'top';
    const minV = 60, maxV = 220;
    for (let v = 60; v <= 220; v += 40) {
      let x = padL + ((v - minV) / (maxV - minV)) * chartW;
      ctx.beginPath(); ctx.moveTo(x, padT); ctx.lineTo(x, padT + chartH); ctx.stroke();
      ctx.fillText(v, x, padT + chartH + 5);
    }
    ctx.fillText('Airspeed (km/h)', padL + chartW / 2, padT + chartH + 16);

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
      if (files.length === 0) h = '<tr><td colspan="3" style="color:var(--text-muted)">No flight logs found on SD card</td></tr>';
      else files.forEach(f => {
        h += `<tr><td>${f.name}</td><td>${Math.round(f.size/1024)} KB</td><td><a href="/dl?f=${f.name}" class="btn" style="padding:0.35rem 0.65rem;text-decoration:none">Download</a> <a href="/del?f=${f.name}" class="btn btn-danger" style="padding:0.35rem 0.65rem;text-decoration:none" onclick="return confirm('Delete file?')">Delete</a></td></tr>`;
      });
      if (document.getElementById('files-tbody')) document.getElementById('files-tbody').innerHTML = h;
    }).catch(e => {});
  }

  function loadConfig() {
    fetch('/api/config').then(r => r.json()).then(c => {
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
        uvert: parseInt(document.getElementById('cfg-uvert').value)
      })
    }).catch(() => {});
  }

  function saveConfig() {
    saveConfigSilent();
    alert('Settings successfully sent and applied to L!M Vario.');
  }

  window.onload = () => { renderProfileDropdown(0); loadFiles(); loadConfig(); };
</script>
</body>
</html>)rawliteral";
