#include "web_portal.h"
#include <math.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include <stdlib.h>
#include <string.h>

#include "esp_ota_ops.h"
#include "user_azure.h"
#include "user_ota.h"
#include "wifi_config_manager.h"

#include "do_sensor.h"
#include "esp_timer.h"
#include "ph_temp.h"
#include "user_system.h"
#include "screen_disp.h"
#include "screen_menu.h"
#include "ds3231.h"
#include "filter.h"
#include "user_storage.h"

static const char *PORTAL_TAG = "web_portal";

static const char *login_html =
    "<!DOCTYPE html><html><head><meta charset='utf-8'/>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'/>"
    "<title>Login - Device Admin</title>"
    "<style>"
    "  "
    ":root{--primary:#3b82f6;--bg:#f3f4f6;--card:#ffffff;--text:#1f2937;--"
    "border:#e5e7eb;--input-bg:#ffffff;--input-text:#1f2937;--text-muted:#"
    "6b7280;}"
    "  "
    "body.dark{--bg:#0f172a;--card:#1e293b;--text:#f8fafc;--border:#334155;--"
    "input-bg:#0f172a;--input-text:#f8fafc;--text-muted:#94a3b8;}"
    "  body{font-family:'Segoe "
    "UI',Roboto,sans-serif;background:var(--bg);color:var(--text);margin:0;"
    "height:100vh;display:flex;align-items:center;justify-content:center;"
    "padding:10px;box-sizing:border-box;transition:background 0.3s,color 0.3s;}"
    "  "
    ".login-card{background:var(--card);padding:32px;border-radius:24px;box-"
    "shadow:0 10px 25px -5px rgba(0,0,0,0.1),0 8px 10px -6px "
    "rgba(0,0,0,0.1);border:1px solid "
    "var(--border);width:100%;max-width:380px;text-align:center;animation:"
    "fadeIn 0.5s ease;position:relative;}"
    "  body.dark .login-card{box-shadow:0 10px 25px -5px rgba(0,0,0,0.3),0 8px "
    "10px -6px rgba(0,0,0,0.3);}"
    "  @keyframes "
    "fadeIn{from{opacity:0;transform:translateY(-10px);}to{opacity:1;transform:"
    "translateY(0);}}"
    "  h2{margin:0 0 8px "
    "0;color:var(--primary);font-size:26px;font-weight:700;}"
    "  p{margin:0 0 24px 0;color:var(--text-muted);font-size:14px;}"
    "  .input-group{position:relative;margin-bottom:20px;text-align:left;}"
    "  "
    "label{display:block;margin-bottom:8px;font-size:13px;font-weight:600;"
    "color:var(--text-muted);letter-spacing:0.5px;}"
    "  input{width:100%;box-sizing:border-box;padding:12px "
    "16px;background:var(--input-bg);border:1px solid "
    "var(--border);border-radius:12px;color:var(--input-text);font-size:15px;"
    "outline:none;transition:all 0.25s ease;padding-right:45px;}"
    "  input:focus{border-color:var(--primary);box-shadow:0 0 0 3px "
    "rgba(59,130,246,0.25);}"
    "  "
    ".pw-toggle{position:absolute;right:8px;bottom:6px;width:32px;height:32px;"
    "background:none;border:none;cursor:pointer;opacity:0.6;font-size:16px;"
    "padding:4px;color:var(--text);}"
    "  .pw-toggle:hover{opacity:1;}"
    "  "
    "button{width:100%;padding:12px;background:var(--primary);border:none;"
    "border-radius:12px;font-weight:600;font-size:15px;cursor:pointer;"
    "transition:all 0.2s;color:#fff;box-shadow:0 4px 12px "
    "rgba(59,130,246,0.3);margin-top:8px;}"
    "  button:hover{background:#2563eb;box-shadow:0 6px 16px "
    "rgba(59,130,246,0.4);}"
    "  button:active{transform:translateY(1px);}"
    "  button:disabled{opacity:0.7;cursor:not-allowed;}"
    "  "
    "#msg{margin-top:16px;font-size:14px;font-weight:600;min-height:21px;color:"
    "#f87171;}"
    "</style></head><body>"
    "<script>(function(){if((localStorage.getItem('theme')||'light')==='dark')"
    "document.body.classList.add('dark');})();</script>"
    "<div class='login-card'>"
    "  <button id='theme_btn' onclick='toggleTheme()' "
    "style='position:absolute;right:16px;top:16px;width:auto;padding:6px "
    "10px;margin:0;font-size:15px;background:none;border:1px solid "
    "var(--border);border-radius:8px;color:var(--text);box-shadow:none;cursor:"
    "pointer;line-height:1;transition:all 0.2s;'>🌙</button>"
    "  <h2>HỆ THỐNG MEBI</h2>"
    "  <p>Nhập mật khẩu quản trị viên để tiếp tục</p>"
    "  <div class='input-group'>"
    "    <label>MẬT KHẨU</label>"
    "    <input id='pass' type='password' placeholder='Nhập mã khóa bí mật' "
    "onkeydown='if(event.key===\"Enter\")login()'/>"
    "    <button type='button' id='pw_toggle' class='pw-toggle'>👁️</button>"
    "  </div>"
    "  <button id='btn_login' onclick='login()'>ĐĂNG NHẬP</button>"
    "  <div id='msg'></div>"
    "</div>"
    "<script>"
    "  document.getElementById('pw_toggle').addEventListener('click',()=>{"
    "    const "
    "p=document.getElementById('pass');p.type=p.type==='password'?'text':'"
    "password';"
    "  });"
    "  function toggleTheme(){"
    "    const isDark=document.body.classList.toggle('dark');"
    "    localStorage.setItem('theme',isDark?'dark':'light');"
    "    updateThemeButton();"
    "  }"
    "  function updateThemeButton(){"
    "    const btn=document.getElementById('theme_btn');"
    "    if(btn){"
    "      btn.innerText=document.body.classList.contains('dark')?'☀️':'🌙';"
    "    }"
    "  }"
    "  updateThemeButton();"
    "  function login(){"
    "    const p=document.getElementById('pass').value;"
    "    const btn=document.getElementById('btn_login');"
    "    const msg=document.getElementById('msg');"
    "    if(!p){msg.innerText='Vui lòng nhập mật khẩu!';return;}"
    "    btn.disabled=true;btn.innerText='Đang xác thực...';msg.innerText='';"
    "    "
    "fetch('/login',{method:'POST',headers:{'Content-Type':'application/"
    "json'},body:JSON.stringify({password:p})})"
    "    .then(r=>{"
    "      if(r.ok) {"
    "        msg.style.color='#4ade80'; msg.innerText='Đăng nhập thành công! "
    "Đang chuyển hướng...';"
    "        setTimeout(()=>window.location.reload(),1000);"
    "      } else {"
    "        throw new Error('Sai mật khẩu!');"
    "      }"
    "    })"
    "    .catch(err=>{"
    "      msg.style.color='#f87171'; msg.innerText=err.message;"
    "      btn.disabled=false;btn.innerText='ĐĂNG NHẬP';"
    "    });"
    "  }"
    "</script></body></html>";

static const char *portal_html =
    "<!DOCTYPE html><html><head><meta charset='utf-8'/>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'/>"
    "<title>Device Config</title>"
    "<style>"
    ":root{--primary:#3b82f6;--success:#10b981;--danger:#ef4444;--bg:#f3f4f6;--"
    "card:#ffffff;--text:#1f2937;--border:#e5e7eb;--input-bg:#ffffff;--input-"
    "text:#1f2937;--text-muted:#6b7280;--sidebar-bg:#1e293b;--sidebar-text:#"
    "f8fafc;--sidebar-active:#3b82f6;}"
    "body.dark{--bg:#0f172a;--card:#1e293b;--text:#f8fafc;--border:#334155;--"
    "input-bg:#0f172a;--input-text:#f8fafc;--text-muted:#94a3b8;--sidebar-bg:#"
    "0f172a;--sidebar-text:#94a3b8;--sidebar-active:#3b82f6;}"
    "body{font-family:'Segoe "
    "UI',Roboto,Arial,sans-serif;background:var(--bg);color:var(--text);margin:"
    "0;padding:0;line-height:1.5;transition:background 0.3s,color "
    "0.3s;display:flex;min-height:100vh;}"
    ".sidebar{width:260px;background:var(--sidebar-bg);color:var(--sidebar-"
    "text);display:flex;flex-direction:column;border-right:1px solid "
    "var(--border);box-sizing:border-box;padding:24px;transition:transform "
    "0.3s ease,background 0.3s;z-index:1000;}"
    ".sidebar-header{margin-bottom:32px;}"
    ".sidebar-header "
    "h2{margin:0;color:#3b82f6;font-size:22px;font-weight:800;letter-spacing:"
    "1px;}"
    ".sidebar-header p{margin:4px 0 0;font-size:12px;opacity:0.7;}"
    ".nav-list{list-style:none;padding:0;margin:0;display:flex;flex-direction:"
    "column;gap:8px;flex:1;}"
    ".nav-item{display:flex;align-items:center;gap:12px;padding:12px "
    "16px;border-radius:10px;font-size:15px;font-weight:600;cursor:pointer;"
    "transition:all 0.25s ease;color:inherit;text-decoration:none;}"
    ".nav-item:hover{background:rgba(59,130,246,0.1);color:#3b82f6;}"
    ".nav-item.active{background:var(--sidebar-active);color:#fff;box-shadow:0 "
    "4px 12px rgba(59,130,246,0.3);}"
    ".sidebar-footer{margin-top:24px;display:flex;flex-direction:column;gap:"
    "12px;}"
    ".main-layout{flex:1;display:flex;flex-direction:column;min-height:100vh;"
    "box-sizing:border-box;}"
    ".topbar{display:none;align-items:center;justify-content:space-between;"
    "padding:16px;background:var(--card);border-bottom:1px solid "
    "var(--border);}"
    ".topbar h2{margin:0;color:#3b82f6;font-size:20px;font-weight:700;}"
    ".menu-toggle{background:none;border:none;color:var(--text);font-size:24px;"
    "cursor:pointer;padding:4px 8px;}"
    ".content-container{flex:1;padding:32px;max-width:800px;margin:0 "
    "auto;width:100%;box-sizing:border-box;}"
    ".tab-panel{display:none;animation:fadeIn 0.4s ease;}"
    ".tab-panel.active{display:block;}"
    "@keyframes "
    "fadeIn{from{opacity:0;transform:translateY(10px);}to{opacity:1;transform:"
    "translateY(0);}}"
    ".card{background:var(--card);padding:28px;border-radius:20px;box-shadow:0 "
    "10px 25px -5px rgba(0,0,0,0.05),0 8px 10px -6px "
    "rgba(0,0,0,0.03);border:1px solid var(--border);transition:background "
    "0.3s,border-color 0.3s;margin-bottom:24px;}"
    "body.dark .card{box-shadow:0 10px 25px -5px rgba(0,0,0,0.2),0 8px 10px "
    "-6px rgba(0,0,0,0.1);}"
    ".section-title{font-size:20px;font-weight:700;margin:0 0 "
    "20px;padding-bottom:10px;border-bottom:2px solid "
    "var(--border);display:flex;align-items:center;color:#3b82f6;gap:8px;}"
    "label{display:block;margin-top:16px;font-size:14px;font-weight:700;color:"
    "var(--text);}"
    "select,input,textarea{width:100%;box-sizing:border-box;padding:12px;"
    "border:1px solid "
    "var(--border);border-radius:10px;margin-top:8px;font-size:14px;transition:"
    "all 0.2s ease;background:var(--input-bg);color:var(--input-text);}"
    "select:focus,input:focus,textarea:focus{outline:none;border-color:#3b82f6;"
    "box-shadow:0 0 0 3px rgba(59,130,246,0.15);}"
    ".pw-wrap{position:relative;margin-top:8px;}"
    ".pw-wrap input{margin-top:0;padding-right:45px;}"
    ".pw-toggle{position:absolute;right:8px;top:50%;transform:translateY(-50%);"
    "width:36px;height:36px;background:none;border:none;cursor:pointer;opacity:"
    "0.6;font-size:16px;padding:4px;color:var(--text);}"
    ".pw-toggle:hover{opacity:1;}"
    ".btn-group{display:grid;grid-template-columns:1fr "
    "1fr;gap:12px;margin-top:24px;}"
    "button{width:100%;padding:12px "
    "18px;border:none;border-radius:10px;font-weight:700;font-size:14px;cursor:"
    "pointer;transition:all 0.25s ease;color:#fff;}"
    "button:active{transform:translateY(1px);}"
    "button:disabled,.loading{opacity:0.6;cursor:not-allowed;transform:none "
    "!important;}"
    ".btn-primary{background:var(--success);box-shadow:0 4px 12px "
    "rgba(16,185,129,0.3);}"
    ".btn-primary:hover:not(:disabled){background:#059669;box-shadow:0 6px "
    "16px rgba(16,185,129,0.4);}"
    ".btn-secondary{background:var(--primary);box-shadow:0 4px 12px "
    "rgba(59,130,246,0.3);}"
    ".btn-secondary:hover:not(:disabled){background:#2563eb;box-shadow:0 6px "
    "16px rgba(59,130,246,0.4);}"
    ".btn-danger{background:var(--danger);box-shadow:0 4px 12px "
    "rgba(239,68,68,0.3);}"
    ".btn-danger:hover:not(:disabled){background:#dc2626;box-shadow:0 6px 16px "
    "rgba(239,68,68,0.4);}"
    ".btn-ctrl{box-shadow:0 4px 6px rgba(0,0,0,0.1);font-size:15px;}"
    ".btn-zero{background:linear-gradient(135deg, #10b981 0%, #059669 100%);}"
    ".btn-zero:hover:not(:disabled){background:linear-gradient(135deg, #059669 "
    "0%, #047857 100%);box-shadow:0 6px 12px rgba(16,185,129,0.3);}"
    ".btn-tare{background:linear-gradient(135deg, #3b82f6 0%, #2563eb 100%);}"
    ".btn-tare:hover:not(:disabled){background:linear-gradient(135deg, #2563eb "
    "0%, #1d4ed8 100%);box-shadow:0 6px 12px rgba(59,130,246,0.3);}"
    ".btn-clear-tare{background:linear-gradient(135deg, #ef4444 0%, #dc2626 "
    "100%);}"
    ".btn-clear-tare:hover:not(:disabled){background:linear-gradient(135deg, "
    "#dc2626 0%, #b91c1c 100%);box-shadow:0 6px 12px rgba(239,68,68,0.3);}"
    "#toast{position:fixed;bottom:24px;left:50%;transform:translateX(-50%) "
    "translateY(100px);background:rgba(15,23,42,0.9);color:#fff;padding:12px "
    "24px;border-radius:50px;box-shadow:0 10px 25px -5px "
    "rgba(0,0,0,0.3);font-weight:700;font-size:14px;z-index:9999;transition:"
    "all 0.4s cubic-bezier(0.175, 0.885, 0.32, "
    "1.275);opacity:0;display:flex;align-items:center;gap:8px;}"
    "#toast.show{transform:translateX(-50%) translateY(0);opacity:1;}"
    "#toast.success{border-left:4px solid #10b981;}"
    "#toast.error{border-left:4px solid #ef4444;}"
    "body.dark #toast{background:rgba(255,255,255,0.95);color:#0f172a;}"
    ".sidebar-overlay{display:none;position:fixed;top:0;left:0;right:0;bottom:"
    "0;background:rgba(0,0,0,0.5);z-index:999;backdrop-filter:blur(4px);}"
    ".sensor-grid{display:grid;grid-template-columns:1fr "
    "1fr;gap:24px;margin-bottom:24px;}"
    "@media (max-width: 768px){"
    "body{flex-direction:column;}"
    ".sidebar{position:fixed;left:0;top:0;bottom:0;transform:translateX(-100%);"
    "box-shadow:10px 0 25px rgba(0,0,0,0.2);}"
    ".sidebar.open{transform:translateX(0);}"
    ".sidebar-overlay.open{display:block;}"
    ".topbar{display:flex;}"
    ".content-container{padding:20px;}"
    ".sensor-grid{grid-template-columns:1fr;gap:16px;}"
    "}"
    "</style></head><body>"
    "<script>(function(){if((localStorage.getItem('theme')||'light')==='dark')"
    "document.body.classList.add('dark');})();</script>"
    "<div class='sidebar' id='sidebar'>"
    "  <div class='sidebar-header'>"
    "    <h2>MEBI IoT</h2>"
    "    <p>pH / DO & Temp Monitor AP</p>"
    "  </div>"
    "  <ul class='nav-list'>"
    "    <li class='nav-item active' onclick='switchTab(\"dash\", event)'>📊 "
    "Dashboard</li>"
    "    <li class='nav-item' onclick='switchTab(\"scale\", event)'>⚖️ "
    "Calibration</li>"
    "    <li class='nav-item' onclick='switchTab(\"wifi\", event)'>📶 WiFi "
    "Setup</li>"
    "    <li class='nav-item' onclick='switchTab(\"azure\", event)'>☁️ Azure "
    "Cloud</li>"
    "    <li class='nav-item' onclick='switchTab(\"secret\", event)'>🔐 Auth "
    "Secret</li>"
    "    <li class='nav-item' onclick='switchTab(\"ota\", event)'>🔄 OTA "
    "Update</li>"
    "    <li class='nav-item' onclick='switchTab(\"lcd\", event)'>🖥️ LCD "
    "Screen</li>"
    "  </ul>"
    "  <div class='sidebar-footer'>"
    "    <button id='theme_btn' onclick='toggleTheme()' "
    "style='background:none;border:1px solid "
    "var(--border);border-radius:10px;color:var(--text);box-shadow:none;cursor:"
    "pointer;'>🌙 Dark Mode</button>"
    "    <button onclick='logout()' class='btn-danger' "
    "style='padding:10px;'>Logout</button>"
    "  </div>"
    "</div>"
    "<div class='sidebar-overlay' id='sidebar_overlay' "
    "onclick='toggleMobileSidebar()'></div>"
    "<div class='main-layout'>"
    "  <div class='topbar'>"
    "    <button class='menu-toggle' onclick='toggleMobileSidebar()'>☰</button>"
    "    <h2>MEBI IoT</h2>"
    "    <button id='theme_btn_mob' onclick='toggleTheme()' "
    "style='background:none;border:none;font-size:20px;cursor:pointer;'>🌙</"
    "button>"
    "  </div>"
    "  <div class='content-container'>"
    "    <div class='tab-panel active' id='tab-dash'>"
    "      <div class='card'>"
    "        <div class='section-title'>🖥️ System Status</div>"
    "        <div style='display:grid;grid-template-columns:1fr "
    "1fr;gap:16px;font-size:14px;text-align:left;'>"
    "          <div><strong>WiFi Status:</strong> <span id='dash_wifi_status' "
    "style='font-weight:bold;'>Checking...</span> <span id='dash_wifi_rssi' "
    "style='color:var(--text-muted);font-size:12px;'></span></div>"
    "          <div><strong>Azure IoT:</strong> <span id='dash_az_status' "
    "style='font-weight:bold;'>Checking...</span></div>"
    "          <div><strong>Free RAM:</strong> <span id='dash_ram'>0 "
    "KB</span></div>"
    "          <div><strong>Uptime:</strong> <span "
    "id='dash_uptime'>0s</span></div>"
    "          <div><strong>Firmware Version:</strong> <span "
    "id='dash_version'>Checking...</span></div>"
    "          <div><strong>Active Partition:</strong> <span "
    "id='dash_partition' style='font-weight:bold;'>Checking...</span></div>"
    "          <div><strong>OTA Status:</strong> <span id='dash_ota_status' "
    "style='font-weight:bold;'>Idle</span></div>"
    "          <div style='grid-column:span 2;'><strong>System Time:</strong> <span id='dash_system_time'>Checking...</span> <button id='btn_sync_time' class='btn-secondary' onclick='syncTime(event)' style='margin-left:12px;padding:4px 8px;font-size:11px;display:inline-block;width:auto;margin-top:0;'>Sync with PC</button></div>"
    "          <div style='grid-column:span 2;'><strong>Restart "
    "Reason:</strong> <span id='dash_reset'>None</span></div>"
    "        </div>"
    "        <hr style='border-top:1px solid var(--border);margin:20px "
    "0;border-bottom:none;'>"
    "        <div style='display:grid;grid-template-columns:1fr 1fr;gap:12px;'>"
    "          <button class='btn-danger' onclick='reboot()' "
    "style='margin:0;padding:10px;font-size:13px;'>Reboot Device</button>"
    "          <button id='btn_refresh' class='btn-secondary' "
    "onclick='updateDashboard()' "
    "style='margin:0;padding:10px;font-size:13px;'>Refresh</button>"
    "        </div>"
    "      </div>"
    "      <div class='sensor-grid'>"
    "        <div class='card' style='margin-bottom:0;'>"
    "          <div class='section-title'>🧪 pH Sensor Monitor</div>"
    "          <div "
    "style='display:flex;flex-direction:column;gap:12px;font-size:14px;text-"
    "align:left;'>"
    "            <div><strong>pH (ATC):</strong> <span id='dash_ph' "
    "style='font-weight:bold;color:#3b82f6;'>7.00</span></div>"
    "            <div><strong>Nhiệt độ pH:</strong> <span id='dash_temp' "
    "style='font-weight:bold;color:#10b981;'>25.0 °C</span></div>"
    "            <div><strong>Điện áp pH Probe:</strong> <span "
    "id='dash_vprobe'>0.00 mV</span></div>"
    "            <div><strong>Trạng thái hiệu chuẩn:</strong> <span "
    "id='dash_cal_status' style='font-weight:bold;'>Chưa hiệu "
    "chuẩn</span></div>"
    "          </div>"
    "        </div>"
    "        <div class='card' style='margin-bottom:0;'>"
    "          <div class='section-title'>💧 DO Sensor Monitor</div>"
    "          <div "
    "style='display:flex;flex-direction:column;gap:12px;font-size:14px;text-"
    "align:left;'>"
    "            <div><strong>Oxy hòa tan (DO):</strong> <span id='dash_do' "
    "style='font-weight:bold;color:#06b6d4;'>0.00 mg/L</span></div>"
    "            <div><strong>Độ bão hòa DO:</strong> <span id='dash_do_sat' "
    "style='font-weight:bold;color:#a855f7;'>0.0%</span></div>"
    "            <div><strong>Nhiệt độ DO:</strong> <span id='dash_do_temp' "
    "style='font-weight:bold;color:#eab308;'>0.0 °C</span></div>"
    "            <div><strong>Trạng thái DO:</strong> <span "
    "id='dash_do_status' style='font-weight:bold;'>Checking...</span></div>"
    "          </div>"
    "        </div>"
    "      </div>"
    "      <div class='card'>"
    "        <div class='section-title'>📡 Telemetry Watch Monitor</div>"
    "        <div style='margin-bottom:12px;font-size:14px;text-align:left;'>"
    "          <strong>Status: </strong><span id='tele_status' "
    "style='font-weight:bold;'>Checking...</span>"
    "        </div>"
    "        <div id='tele_list' "
    "style='display:flex;flex-direction:column;gap:8px;font-size:14px;text-"
    "align:left;max-height:300px;overflow-y:auto;padding:8px 0;'>"
    "          <div style='color:var(--text-muted);font-style:italic;'>No "
    "telemetry data pushed yet...</div>"
    "        </div>"
    "      </div>"
    "    </div>"
    "    <div class='tab-panel' id='tab-scale'>"
    "      <div class='card'>"
    "        <div class='section-title'>⚖️ Hiệu chuẩn cảm biến pH</div>"
    "        <div style='margin-bottom:20px;padding:16px "
    "20px;background:rgba(59,130,246,0.1);border-radius:12px;text-align:center;"
    "'>"
    "          <span "
    "style='font-size:12px;font-weight:700;color:var(--text-muted);display:"
    "block;text-transform:uppercase;letter-spacing:0.5px;'>Live pH | Nhiệt độ "
    "| Điện áp</span>"
    "          <span id='ctrl_ph' "
    "style='font-size:36px;font-weight:800;color:#3b82f6;'>7.00</span>"
    "          <span "
    "style='font-size:14px;color:var(--text-muted);display:block;margin-top:"
    "8px;'>Nhiệt độ: <span id='ctrl_temp' "
    "style='font-weight:bold;color:#10b981;'>25.0 °C</span> | Điện áp: <span "
    "id='ctrl_vprobe' style='font-weight:bold;'>0.00 mV</span></span>"
    "        </div>"
    "        <div style='display:grid;grid-template-columns:1fr 1fr;gap:16px;'>"
    "          <button id='btn_cal_7' class='btn-ctrl btn-zero' "
    "onclick='controlScale(\"CAL_7\")'>Hiệu chuẩn pH 7.00</button>"
    "          <button id='btn_cal_4' class='btn-ctrl btn-tare' "
    "onclick='controlScale(\"CAL_4\")'>Hiệu chuẩn pH 4.01</button>"
    "        </div>"
    "        <div "
    "style='margin-top:16px;font-size:13px;color:var(--text-muted);text-align:"
    "left;'>"
    "          <strong>Hướng dẫn:</strong> Nhúng đầu dò vào dung dịch chuẩn "
    "tương ứng (pH 7.00 hoặc pH 4.01), đợi trị số ổn định rồi nhấn nút hiệu "
    "chuẩn tương ứng."
    "        </div>"
    "      </div>"
    "      <div class='card'>"
    "        <div class='section-title'>⚙️ Bộ lọc kỹ thuật số (Digital Filter)</div>"
    "        <div style='margin-bottom:12px;font-size:13px;color:var(--text-muted);text-align:left;'>"
    "          Thời gian lọc trung bình (làm mịn tín hiệu pH & Nhiệt độ):"
    "        </div>"
    "        <div style='display:grid;grid-template-columns:1fr 1fr 1fr;gap:12px;margin-bottom:12px;'>"
    "          <button id='btn_filter_0' class='btn-ctrl btn-tare' onclick='setFilterLevel(0)' style='padding:8px 4px;font-size:13px;'>Low (5s)</button>"
    "          <button id='btn_filter_1' class='btn-ctrl btn-tare' onclick='setFilterLevel(1)' style='padding:8px 4px;font-size:13px;'>Mid (10s)</button>"
    "          <button id='btn_filter_2' class='btn-ctrl btn-tare' onclick='setFilterLevel(2)' style='padding:8px 4px;font-size:13px;'>High (20s)</button>"
    "        </div>"
    "        <div style='font-size:13px;color:var(--text-muted);text-align:left;line-height:1.4;'>"
    "          * <strong>Low (5s):</strong> Nhạy nhất với thay đổi, nhưng dữ liệu dao động nhiều hơn.<br>"
    "          * <strong>High (20s):</strong> Rất ổn định, nhưng phản hồi chậm hơn (độ trễ 20 giây)."
    "        </div>"
    "      </div>"
    "      <div class='card'>"
    "        <div class='section-title'>💧 Hiệu chuẩn cảm biến DO</div>"
    "        <div style='margin-bottom:20px;padding:16px "
    "20px;background:rgba(6,182,212,0.1);border-radius:12px;text-align:center;'"
    ">"
    "          <span "
    "style='font-size:12px;font-weight:700;color:var(--text-muted);display:"
    "block;text-transform:uppercase;letter-spacing:0.5px;'>Live DO | Nhiệt độ "
    "| Độ bão hòa</span>"
    "          <span id='ctrl_do' "
    "style='font-size:36px;font-weight:800;color:#06b6d4;'>0.00 mg/L</span>"
    "          <span "
    "style='font-size:14px;color:var(--text-muted);display:block;margin-top:"
    "8px;'>Nhiệt độ: <span id='ctrl_do_temp' "
    "style='font-weight:bold;color:#eab308;'>0.0 °C</span> | Độ bão hòa: <span "
    "id='ctrl_do_sat' "
    "style='font-weight:bold;color:#a855f7;'>0.0%</span></span>"
    "        </div>"
    "        <div style='display:grid;grid-template-columns:1fr 1fr;gap:16px;'>"
    "          <button id='btn_cal_do_zero' class='btn-ctrl btn-clear-tare' "
    "onclick='controlDO(\"CAL_DO_ZERO\")'>Hiệu chuẩn điểm 0 (Zero)</button>"
    "          <button id='btn_cal_do_slope' class='btn-ctrl btn-zero' "
    "onclick='controlDO(\"CAL_DO_SLOPE\")'>Hiệu chuẩn độ dốc (Slope)</button>"
    "        </div>"
    "        <label style='margin-top:16px;'>Hiệu chỉnh nhiệt độ DO "
    "(°C)</label>"
    "        <div style='display:flex;gap:12px;margin-top:8px;'>"
    "          <input id='ref_do_temp' type='number' step='0.1' "
    "placeholder='Ví dụ: 25.5' style='margin-top:0;flex:1;'/>"
    "          <button id='btn_correct_do_temp' class='btn-secondary btn-ctrl' "
    "onclick='correctDOTemp()' style='width:auto;padding:12px "
    "24px;margin:0;'>Hiệu chỉnh</button>"
    "        </div>"
    "        <label style='margin-top:16px;'>Bù độ mặn DO (PSU / ppt)</label>"
    "        <div style='display:flex;gap:12px;margin-top:8px;'>"
    "          <input id='ref_do_salinity' type='number' step='0.1' "
    "placeholder='Ví dụ: 15.5' style='margin-top:0;flex:1;'/>"
    "          <button id='btn_compensate_salinity' class='btn-secondary btn-ctrl' "
    "onclick='compensateSalinity()' style='width:auto;padding:12px "
    "24px;margin:0;'>Cập nhật</button>"
    "        </div>"
    "        <div "
    "style='margin-top:16px;font-size:13px;color:var(--text-muted);text-align:"
    "left;'>"
    "          <strong>Hướng dẫn hiệu chuẩn điểm 0:</strong> Pha dung dịch "
    "Natri Sulfit 5% (5g Sodium Sulfite vào 95ml nước sạch). Nhúng cảm biến DO "
    "vào dung dịch và chờ tối thiểu 3 phút cho giá trị đo ổn định hoàn toàn, "
    "sau đó nhấn nút hiệu chuẩn điểm 0.<br><br>"
    "          <strong>Hướng dẫn hiệu chuẩn độ dốc:</strong> Dùng môi trường "
    "không khí bão hòa hơi nước (đặt đầu dò DO trong chai hiệu chuẩn cao hơn "
    "mặt nước 2-3mm sao cho màng ẩm nhưng không dính nước). Chờ 3 phút cho giá "
    "trị đo ổn định hoàn toàn, sau đó nhấn nút hiệu chuẩn độ dốc.<br><br>"
    "          <strong>Hướng dẫn hiệu chỉnh nhiệt độ:</strong> Nhúng cảm biến "
    "DO và một nhiệt kế chuẩn vào cùng một cốc nước. Nhập nhiệt độ của nhiệt "
    "kế chuẩn vào ô trên rồi nhấn nút 'Hiệu chỉnh'."
    "        </div>"
    "        <hr style='border-top:1px dashed var(--border);margin:20px "
    "0;border-bottom:none;'>"
    "        <div "
    "style='display:flex;justify-content:space-between;align-items:center;flex-"
    "wrap:wrap;gap:12px;'>"
    "          <span "
    "style='font-size:13px;color:var(--text-muted);text-align:left;'>Khôi phục "
    "các mốc hiệu chuẩn DO về giá trị mặc định của nhà máy.</span>"
    "          <button id='btn_reset_do_sensor' class='btn-danger btn-ctrl' "
    "onclick='resetDOSensor()' style='width:auto;padding:10px "
    "20px;font-size:13px;margin:0;'>Reset Sensor</button>"
    "        </div>"
    "      </div>"
    "    </div>"
    "    <div class='tab-panel' id='tab-wifi'>"
    "      <div class='card'>"
    "        <div class='section-title'>📶 WiFi Configuration</div>"
    "        <label>Available Networks</label>"
    "        <select id=''><option value=''>Scanning...</option></select>"
    "        <label>Hidden  (Optional)</label>"
    "        <input id='ssid_manual' placeholder='Enter hidden SSID'/>"
    "        <label>Password</label>"
    "        <div class='pw-wrap'>"
    "          <input id='wifi_pass' type='password' placeholder='Enter WiFi "
    "password'/>"
    "          <button type='button' id='pw_toggle' class='pw-toggle' "
    "title='Toggle Password'>👁️</button>"
    "        </div>"
    "        <div class='btn-group'>"
    "          <button id='btn_clr_wifi' class='btn-danger' "
    "onclick='clr(\"wifi\")'>Clear WiFi</button>"
    "          <button id='btn_save_wifi' class='btn-primary' "
    "onclick='save(\"wifi\")'>Save & Reconnect</button>"
    "        </div>"
    "        <button id='btn_scan' class='btn-secondary' onclick='scan()' "
    "style='margin-top:16px;'>Scan Networks</button>"
    "      </div>"
    "    </div>"
    "    <div class='tab-panel' id='tab-azure'>"
    "      <div class='card'>"
    "        <div class='section-title'>☁️ Azure IoT Hub</div>"
    "        <label>Host Name</label>"
    "        <input id='host_name' "
    "placeholder='dev-iot-hub.azure-devices.net'/>"
    "        <label>Device ID</label>"
    "        <input id='dev_id' placeholder='my-device-1'/>"
    "        <label>Symmetric Key</label>"
    "        <input id='sym_key' placeholder='Base64 Key'/>"
    "        <label>Connection String</label>"
    "        <textarea id='conn_str' oninput='parseConnStr()' "
    "placeholder='HostName=xxx;DeviceId=yyy;SharedAccessKey=zzz' rows='3' "
    "style='font-family:monospace;'></textarea>"
    "        <div class='btn-group'>"
    "          <button id='btn_clr_azure' class='btn-danger' "
    "onclick='clr(\"azure\")'>Clear Azure</button>"
    "          <button id='btn_save_azure' class='btn-primary' "
    "onclick='save(\"azure\")'>Save Azure Config</button>"
    "        </div>"
    "      </div>"
    "    </div>"
    "    <div class='tab-panel' id='tab-secret'>"
    "      <div class='card'>"
    "        <div class='section-title'>🔐 Auth Secret</div>"
    "        <label>Secret Key</label>"
    "        <div class='pw-wrap'>"
    "          <input id='secret' type='password' placeholder='Enter new "
    "secret key'/>"
    "          <button type='button' class='pw-toggle' onclick=\"var "
    "s=document.getElementById('secret');s.type=s.type==='password'?'text':'"
    "password';\">👁️</button>"
    "        </div>"
    "        <div class='btn-group'>"
    "          <button id='btn_save_secret' class='btn-primary' "
    "onclick='save(\"secret\")'>Save Secret</button>"
    "        </div>"
    "      </div>"
    "    </div>"
    "    <div class='tab-panel' id='tab-ota'>"
    "      <div class='card'>"
    "        <div class='section-title'>🔄 Firmware OTA Update</div>"
    "        <label>Current Version</label>"
    "        <input id='ota_cur_ver' readonly "
    "style='background:rgba(0,0,0,0.05);cursor:not-allowed;'/>"
    "        <label>Update URL</label>"
    "        <input id='ota_url' "
    "placeholder='https://example.com/firmware.bin'/>"
    "        <label>OTA Status</label>"
    "        <div "
    "style='padding:12px;background:rgba(59,130,246,0.1);border-radius:10px;"
    "font-weight:bold;margin-top:8px;' id='ota_status_desc'>Idle</div>"
    "        <div class='btn-group'>"
    "          <button id='btn_start_ota' class='btn-primary' "
    "onclick='startOta()'>Start Update</button>"
    "        </div>"
    "      </div>"
    "    </div>"
    "    <div class='tab-panel' id='tab-lcd'>"
    "      <div class='card' style='text-align:center;'>"
    "        <div class='section-title'>🖥️ LCD Screen Emulator</div>"
    "        <div style='position:relative;display:inline-block;padding:12px;border-radius:12px;background-color:#0f172a;border:4px solid #334155;margin-bottom:16px;'>"
    "          <canvas id='lcd_canvas' width='128' height='64' style='display:block;width:384px;height:192px;background:#9bbc0f;border:2px solid #0f380f;image-rendering:-moz-crisp-edges;image-rendering:-webkit-crisp-edges;image-rendering:pixelated;'></canvas>"
    "        </div>"
    "        <div style='margin-top:8px;display:flex;justify-content:center;gap:12px;max-width:320px;margin-left:auto;margin-right:auto;'>"
    "          <button class='btn-secondary btn-ctrl' onclick='simulateBtn(\"esc\")' style='flex:1;padding:10px 0;margin:0;'>ESC</button>"
    "          <button class='btn-secondary btn-ctrl' onclick='simulateBtn(\"up\")' style='flex:1;padding:10px 0;margin:0;'>▲ UP</button>"
    "          <button class='btn-secondary btn-ctrl' onclick='simulateBtn(\"down\")' style='flex:1;padding:10px 0;margin:0;'>▼ DOWN</button>"
    "          <button class='btn-primary btn-ctrl' onclick='simulateBtn(\"enter\")' style='flex:1;padding:10px 0;margin:0;'>ENTER</button>"
    "        </div>"
    "        <div style='margin-top:16px;font-size:13px;color:var(--text-muted);text-align:center;'>"
    "          Bản vẽ mô phỏng LCD 128x64 thực tế trực tuyến.<br>Thao tác phím bấm ảo hoặc phím vật lý để xem thay đổi."
    "        </div>"
    "      </div>"
    "    </div>"
    "    <div id='msg'></div>"
    "  </div>"
    "</div>"
    "<div id='toast'></div>"
    "<script>"
    "function setMsg(t){document.getElementById('msg').innerText=t;}"
    "function setLoading(btn,on,text){"
    "if(on){btn.classList.add('loading');btn.disabled=true;if(text){btn."
    "dataset.ot=btn.innerText;btn.innerText=text;}}"
    "else{btn.classList.remove('loading');btn.disabled=false;if(btn.dataset.ot)"
    "{btn.innerText=btn.dataset.ot;}}"
    "}"
    "document.getElementById('pw_toggle').addEventListener('click',()=>{"
    "const "
    "p=document.getElementById('wifi_pass');p.type=p.type==='password'?'text':'"
    "password';"
    "});"
    "function scan(){"
    "const btn=document.getElementById('btn_scan');"
    "setLoading(btn,true,'Scanning...');setMsg('Scanning networks...');"
    "fetch('/scan').then(r=>r.json()).then(list=>{"
    "const sel=document.getElementById('ssid');sel.innerHTML='';"
    "list.forEach(s=>{const "
    "o=document.createElement('option');o.value=s;o.text=s;sel.add(o);});"
    "setMsg('Scan complete');"
    "}).catch(()=>setMsg('Scan failed')).finally(()=>setLoading(btn,false));"
    "}"
    "function parseConnStr(){"
    "let cs=document.getElementById('conn_str').value.trim();"
    "if(!cs)return;"
    "cs.split(';').forEach(function(part){"
    "let idx=part.indexOf('=');"
    "if(idx<=0)return;"
    "let k=part.substring(0,idx).trim();"
    "let v=part.substring(idx+1).trim();"
    "if(k==='HostName')document.getElementById('host_name').value=v;"
    "else if(k==='DeviceId')document.getElementById('dev_id').value=v;"
    "else if(k==='SharedAccessKey')document.getElementById('sym_key').value=v;"
    "});"
    "setMsg('Fields auto-filled from connection string');"
    "}"
    "function save(tgt){"
    "const btn=document.getElementById('btn_save_'+tgt);"
    "let payload={target:tgt};"
    "if(tgt==='wifi'){"
    "const sel=document.getElementById('ssid').value;"
    "const manual=document.getElementById('ssid_manual').value.trim();"
    "payload.ssid=manual.length?manual:sel;"
    "if(!payload.ssid){setMsg('Error: SSID is required');return;}"
    "payload.password=document.getElementById('wifi_pass').value;"
    "}"
    "else if(tgt==='azure'){"
    "let cs=document.getElementById('conn_str').value.trim();"
    "if(cs){"
    "cs.split(';').forEach(function(part){"
    "let idx=part.indexOf('=');"
    "if(idx<=0)return;"
    "let k=part.substring(0,idx).trim();"
    "let v=part.substring(idx+1).trim();"
    "if(k==='HostName')payload.hostName=v;"
    "else if(k==='DeviceId')payload.deviceId=v;"
    "else if(k==='SharedAccessKey')payload.symmetricKey=v;"
    "});"
    "}else{"
    "payload.hostName=document.getElementById('host_name').value.trim();"
    "payload.deviceId=document.getElementById('dev_id').value.trim();"
    "payload.symmetricKey=document.getElementById('sym_key').value.trim();"
    "}"
    "if(!payload.hostName){setMsg('Error: HostName required');return;}"
    "}"
    "else if(tgt==='secret'){"
    "payload.secret=document.getElementById('secret').value.trim();"
    "if(!payload.secret){setMsg('Error: Secret is required');return;}"
    "}"
    "setLoading(btn,true,'Saving...');"
    "fetch('/save',{method:'POST',headers:{'Content-Type':'application/"
    "json'},body:JSON.stringify(payload)})"
    ".then(r=>r.json()).then(d=>{setMsg(d.status==='saved'?'Saved '+tgt+' "
    "successfully!':d.status);})"
    ".catch(()=>setMsg('Save failed')).finally(()=>setLoading(btn,false));"
    "}"
    "function clr(tgt){"
    "if(!confirm('Are you sure you want to clear ' + tgt + ' config?')) return;"
    "const btn=document.getElementById('btn_clr_'+tgt);"
    "setLoading(btn,true,'Clearing...');"
    "fetch('/clear',{method:'POST',headers:{'Content-Type':'application/"
    "json'},body:JSON.stringify({target:tgt})})"
    ".then(r=>r.json()).then(d=>{setMsg(d.status||'Cleared');setTimeout(()=>"
    "window.location.reload(),1500);})"
    ".catch(()=>setMsg('Clear failed')).finally(()=>setLoading(btn,false));"
    "}"
    "function logout(){"
    "if(confirm('Are you sure you want to logout?')){"
    "fetch('/logout',{method:'POST'}).then(r=>{"
    "if(r.ok) window.location.reload();"
    "});"
    "}"
    "}"
    "function toggleTheme(){"
    "const isDark=document.body.classList.toggle('dark');"
    "localStorage.setItem('theme',isDark?'dark':'light');"
    "updateThemeButton();"
    "}"
    "function updateThemeButton(){"
    "const isDark=document.body.classList.contains('dark');"
    "const btn=document.getElementById('theme_btn');"
    "const btnMob=document.getElementById('theme_btn_mob');"
    "if(btn) btn.innerText=isDark?'☀️ Light Mode':'🌙 Dark Mode';"
    "if(btnMob) btnMob.innerText=isDark?'☀️':'🌙';"
    "}"
    "function switchTab(tabId,e){"
    "document.querySelectorAll('.nav-item').forEach(item=>item.classList."
    "remove('active'));"
    "if(e) e.currentTarget.classList.add('active');"
    "document.querySelectorAll('.tab-panel').forEach(p=>p.classList.remove('"
    "active'));"
    "document.getElementById('tab-'+tabId).classList.add('active');"
    "const sb=document.getElementById('sidebar');"
    "const ov=document.getElementById('sidebar_overlay');"
    "if(sb.classList.contains('open')){"
    "sb.classList.remove('open');"
    "ov.classList.remove('open');"
    "}"
    "}"
    "function toggleMobileSidebar(){"
    "document.getElementById('sidebar').classList.toggle('open');"
    "document.getElementById('sidebar_overlay').classList.toggle('open');"
    "}"
    "function showToast(msg,isSuccess){"
    "const t=document.getElementById('toast');"
    "t.innerText=msg;"
    "t.className='show '+(isSuccess?'success':'error');"
    "setTimeout(()=>t.className='',1000);"
    "}"
    "function controlScale(act){"
    "const btn=document.getElementById('btn_'+act.toLowerCase());"
    "const bZ=document.getElementById('btn_cal_7');"
    "const bT=document.getElementById('btn_cal_4');"
    "if(bZ) bZ.disabled=true;"
    "if(bT) bT.disabled=true;"
    "const ot=btn?btn.innerText:'Processing';"
    "if(btn) btn.innerText='Processing...';"
    "fetch('/api/"
    "scale_control',{method:'POST',headers:{'Content-Type':'application/"
    "json'},body:JSON.stringify({action:act})})"
    ".then(r=>r.json()).then(d=>{"
    "showToast(d.message,d.success);"
    "updateDashboard();"
    "}).catch(()=>showToast('Failed to connect to device',false))"
    ".finally(()=>{"
    "if(bZ) bZ.disabled=false;"
    "if(bT) bT.disabled=false;"
    "if(btn) btn.innerText=ot;"
    "});"
    "}"
    "function controlDO(act){"
    "const btn=document.getElementById('btn_'+act.toLowerCase());"
    "if(btn) btn.disabled=true;"
    "const ot=btn?btn.innerText:'Processing';"
    "if(btn) btn.innerText='Processing...';"
    "fetch('/api/"
    "do_control',{method:'POST',headers:{'Content-Type':'application/"
    "json'},body:JSON.stringify({action:act})})"
    ".then(r=>r.json()).then(d=>{"
    "showToast(d.message,d.success);"
    "updateDashboard();"
    "}).catch(()=>showToast('Không thể kết nối đến thiết bị',false))"
    ".finally(()=>{"
    "if(btn){btn.disabled=false;btn.innerText=ot;}"
    "});"
    "}"
    "function correctDOTemp(){"
    "const input=document.getElementById('ref_do_temp');"
    "const val=parseFloat(input.value);"
    "if(isNaN(val) || val < -10 || val > 100){"
    "showToast('Nhiệt độ không hợp lệ (từ -10 đến 100 °C)', false);"
    "return;"
    "}"
    "const btn=document.getElementById('btn_correct_do_temp');"
    "setLoading(btn,true,'Gửi...');"
    "fetch('/api/"
    "do_control',{method:'POST',headers:{'Content-Type':'application/"
    "json'},body:JSON.stringify({action:'CORRECT_DO_TEMP',value:val})})"
    ".then(r=>r.json()).then(d=>{"
    "showToast(d.message,d.success);"
    "updateDashboard();"
    "}).catch(()=>showToast('Không thể kết nối đến thiết bị',false))"
    ".finally(()=>setLoading(btn,false));"
    "}"
    "function compensateSalinity(){"
  "const input = document.getElementById('ref_do_salinity');"
  "const val = parseFloat(input.value);"
  "if(isNaN(val) || val < 0 || val > 100){"
    "showToast('Độ mặn không hợp lệ (từ 0 đến 100 PSU)', false);"
    "return;"
  "}"
  "const btn = document.getElementById('btn_compensate_salinity');"
  "setLoading(btn, true, 'Gửi...');"
  "fetch('/api/do_control', {"
    "method: 'POST',"
    "headers: { 'Content-Type': 'application/json' },"
    "body: JSON.stringify({ action: 'COMPENSATE_SALINITY', value: val })"
  "})"
  ".then(r => r.json())"
  ".then(d => {"
    "showToast(d.message, d.success);"
    "updateDashboard();"
  "})"
  ".catch(() => showToast('Không thể kết nối đến thiết bị', false))"
  ".finally(() => setLoading(btn, false));"
"}"
    "function resetDOSensor(){"
    "if(!confirm('Cảnh báo: Lệnh này sẽ xóa toàn bộ các mốc hiệu chuẩn (Zero "
    "và Slope) đã lưu của cảm biến DO. Bạn có chắc chắn muốn khôi phục cài đặt "
    "gốc?')) return;"
    "const btn=document.getElementById('btn_reset_do_sensor');"
    "setLoading(btn,true,'Resetting...');"
    "fetch('/api/"
    "do_control',{method:'POST',headers:{'Content-Type':'application/"
    "json'},body:JSON.stringify({action:'RESET_DO_SENSOR'})})"
    ".then(r=>r.json()).then(d=>{"
    "if(d.success){"
    "alert('Đã khôi phục cài đặt gốc cảm biến DO thành công! Vui lòng tiến "
    "hành hiệu chuẩn lại Điểm 0 và Độ dốc trước khi tiếp tục đo đạc.');"
    "}else{"
    "showToast(d.message,false);"
    "}"
    "updateDashboard();"
    "}).catch(()=>showToast('Không thể kết nối đến thiết bị',false))"
    ".finally(()=>setLoading(btn,false));"
    "}"
    "function setFilterLevel(lvl){"
    "document.getElementById('btn_filter_0').disabled=true;"
    "document.getElementById('btn_filter_1').disabled=true;"
    "document.getElementById('btn_filter_2').disabled=true;"
    "fetch('/api/scale_control',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action:'SET_FILTER',level:lvl})})"
    ".then(r=>r.json()).then(d=>{"
    "showToast(d.message,d.success);"
    "updateDashboard();"
    "}).catch(()=>showToast('Không thể kết nối đến thiết bị',false))"
    ".finally(()=>{"
    "document.getElementById('btn_filter_0').disabled=false;"
    "document.getElementById('btn_filter_1').disabled=false;"
    "document.getElementById('btn_filter_2').disabled=false;"
    "});"
    "}"
    "let localUptime=0;"
    "let uptimeTimer=null;"
    "function renderUptime(){"
    "const ut=localUptime;"
    "let h=Math.floor(ut/3600), m=Math.floor((ut%3600)/60), s=ut%60;"
    "document.getElementById('dash_uptime').innerText=(h>0?h+'h "
    "':'')+(m>0?m+'m ':'')+s+'s';"
    "}"
    "function updateDashboard(){"
    "fetch('/api/system_status').then(r=>{"
    "if(r.status===401) window.location.reload();"
    "return r.json();"
    "}).then(d=>{"
    "const wifi=document.getElementById('dash_wifi_status');"
    "wifi.innerText=d.wifi.connected?'Connected "
    "('+d.wifi.ssid+')':'Disconnected';"
    "wifi.style.color=d.wifi.connected?'#10b981':'#ef4444';"
    "document.getElementById('dash_wifi_rssi').innerText=d.wifi.connected?'('+"
    "d.wifi.rssi+' dBm)':'';"
    "const az=document.getElementById('dash_az_status');"
    "az.innerText=d.azure.initialized?'Connected':'Disconnected';"
    "az.style.color=d.azure.initialized?'#10b981':'#ef4444';"
    "const phVal=Number(d.sensor.ph).toFixed(2);"
    "const tempVal=Number(d.sensor.temp).toFixed(1);"
    "const vProbeVal=Number(d.sensor.v_probe_mv).toFixed(2);"
    "const calStatus=d.sensor.is_calibrated;"
    "document.getElementById('dash_ph').innerText=phVal;"
    "document.getElementById('dash_temp').innerText=tempVal+' °C';"
    "document.getElementById('dash_vprobe').innerText=vProbeVal+' mV';"
    "const calEl=document.getElementById('dash_cal_status');"
    "calEl.innerText=calStatus?'Đã hiệu chuẩn (U7: "
    "'+Number(d.sensor.u7).toFixed(3)+', Slope: "
    "'+Number(d.sensor.slope_norm).toFixed(3)+')':'Chưa hiệu chuẩn';"
    "calEl.style.color=calStatus?'#10b981':'#ef4444';"
    "const ctrlPh=document.getElementById('ctrl_ph');"
    "if(ctrlPh){"
    "ctrlPh.innerText=phVal;"
    "document.getElementById('ctrl_temp').innerText=tempVal+' °C';"
    "document.getElementById('ctrl_vprobe').innerText=vProbeVal+' mV';"
    "for(let i=0;i<3;i++){"
    "const btn=document.getElementById('btn_filter_'+i);"
    "if(btn){"
    "if(i===d.sensor.filter_lvl){"
    "btn.style.background='var(--success)';"
    "}else{"
    "btn.style.background='var(--primary)';"
    "}"
    "}"
    "}"
    "}"
    "const doVal=Number(d.sensor.do_mg_l).toFixed(2);"
    "const doSat=Number(d.sensor.do_saturation_pct).toFixed(1);"
    "const doTemp=Number(d.sensor.do_temp_c).toFixed(1);"
    "const doValid=d.sensor.do_valid;"
    "document.getElementById('dash_do').innerText=doVal+' mg/L';"
    "document.getElementById('dash_do_sat').innerText=doSat+'%';"
    "document.getElementById('dash_do_temp').innerText=doTemp+' °C';"
    "const ctrlDo=document.getElementById('ctrl_do');"
    "if(ctrlDo){"
    "ctrlDo.innerText=doVal+' mg/L';"
    "document.getElementById('ctrl_do_temp').innerText=doTemp+' °C';"
    "document.getElementById('ctrl_do_sat').innerText=doSat+'%';"
    "}"
    "const doEl=document.getElementById('dash_do_status');"
    "if(doValid){"
    "doEl.innerText='Online';"
    "doEl.style.color='#10b981';"
    "}else{"
    "doEl.innerText='Offline (Lỗi: '+d.sensor.do_error_code+')';"
    "doEl.style.color='#ef4444';"
    "}"
    "document.getElementById('dash_ram').innerText=(d.system.free_heap/"
    "1024).toFixed(1)+' KB';"
    "localUptime=d.system.uptime;"
    "renderUptime();"
    "if(!uptimeTimer){"
    "uptimeTimer=setInterval(()=>{localUptime++;renderUptime();},1000);"
    "}"
    "document.getElementById('dash_reset').innerText=d.system.reset_reason + ' "
    "(Reset count: '+d.system.reset_count+')';"
    "document.getElementById('dash_version').innerText=d.system.version;"
    "const epoch=d.system.epoch;"
    "const sysTimeEl=document.getElementById('dash_system_time');"
    "if(sysTimeEl){"
    "if(epoch&&epoch>946684800){"
    "const date=new Date(epoch*1000);"
    "const pad=(n)=>String(n).padStart(2,'0');"
    "sysTimeEl.innerText=pad(date.getDate())+'/'+pad(date.getMonth()+1)+'/'+date.getFullYear()+' '+pad(date.getHours())+':'+pad(date.getMinutes())+':'+pad(date.getSeconds());"
    "}else{"
    "sysTimeEl.innerText='Not Synced';"
    "}"
    "}"
    "document.getElementById('dash_partition').innerText=d.system.partition||'"
    "Unknown';"
    "if(document.getElementById('ota_cur_ver')){"
    "document.getElementById('ota_cur_ver').value=d.system.version;"
    "}"
    "const otaStatus=d.system.ota_status||'Idle';"
    "const otaStatusEl=document.getElementById('dash_ota_status');"
    "otaStatusEl.innerText=otaStatus;"
    "if(otaStatus.includes('Success')){"
    "otaStatusEl.style.color='#10b981';"
    "}else if(otaStatus.includes('Failed')){"
    "otaStatusEl.style.color='#ef4444';"
    "}else "
    "if(otaStatus.includes('Downloading')||otaStatus.includes('Waiting')){"
    "otaStatusEl.style.color='#3b82f6';"
    "}else{"
    "otaStatusEl.style.color='var(--text-muted)';"
    "}"
    "if(document.getElementById('ota_status_desc')){"
    "document.getElementById('ota_status_desc').innerText=otaStatus;"
    "const btn=document.getElementById('btn_start_ota');"
    "if(otaStatus.includes('Downloading')||otaStatus.includes('Waiting')||"
    "otaStatus.includes('Success')){"
    "btn.disabled=true;"
    "if(otaStatus.includes('Downloading')){"
    "btn.innerText='Downloading...';"
    "}"
    "}else{"
    "btn.disabled=false;"
    "btn.innerText='Start Update';"
    "}"
    "}"
    "const teleActive=d.telemetry.active;"
    "const teleInterval=d.telemetry.interval;"
    "const teleStatusEl=document.getElementById('tele_status');"
    "if(teleActive){"
    "teleStatusEl.innerText='Active (Interval: '+teleInterval+'ms)';"
    "teleStatusEl.style.color='#10b981';"
    "}else{"
    "teleStatusEl.innerText='Inactive (Stopped)';"
    "teleStatusEl.style.color='#ef4444';"
    "}"
    "const teleList=document.getElementById('tele_list');"
    "const lastPushed=d.telemetry.last_pushed;"
    "if(lastPushed&&lastPushed.payload){"
    "const pl=lastPushed.payload;"
    "let html='';"
    "if(pl.Code){"
    "let codeName=pl.Code;"
    "if(pl.Code===503||pl.Code===504)codeName=pl.Code+' (Sensor Telemetry)';"
    "else if(pl.Code===502)codeName='502 (Ask Version)';"
    "else if(pl.Code===501)codeName='501 (Update Firmware)';"
    "else if(pl.Code===505)codeName='505 (Set Config)';"
    "html+='<div><strong>• Message Code:</strong> "
    "<span>'+codeName+'</span></div>';"
    "}"
    "if(pl.TimeStamp){"
    "const date=new Date(pl.TimeStamp*1000);"
    "const pad=(n)=>String(n).padStart(2,'0');"
    "const "
    "dateStr=pad(date.getDate())+'/'+pad(date.getMonth()+1)+'/"
    "'+date.getFullYear()+' "
    "'+pad(date.getHours())+':'+pad(date.getMinutes())+':'+pad(date.getSeconds("
    "));"
    "html+='<div><strong>• TimeStamp:</strong> <span>'+dateStr+'</span></div>';"
    "}"
    "if(pl.DeviceId){"
    "html+='<div><strong>• Device ID:</strong> "
    "<span>'+pl.DeviceId+'</span></div>';"
    "}"
    "if(pl.ph!==undefined){"
    "html+='<hr style=\"border-top:1px dashed var(--border);margin:8px "
    "0;border-bottom:none;\">';"
    "html+='<div><strong>• pH (ATC):</strong> <span "
    "style=\"font-weight:bold;color:#3b82f6;\">'+Number(pl.ph).toFixed(2)+'</"
    "span></div>';"
    "html+='<div><strong>• Temperature:</strong> <span "
    "style=\"font-weight:bold;color:#10b981;\">'+Number(pl.temp).toFixed(1)+' "
    "°C</span></div>';"
    "html+='<div><strong>• Probe Voltage:</strong> "
    "<span>'+Number(pl.v_probe_mv).toFixed(2)+' mV</span></div>';"
    "html+='<div><strong>• Calibrated:</strong> "
    "<span>'+(pl.is_calibrated?\"Yes\":\"No\")+'</span></div>';"
    "}else if(pl.SensorData!==undefined){"
    "const sd=pl.SensorData;"
    "const phVal=sd.ph!==undefined?sd.ph:sd.pH;"
    "const doVal=sd.do!==undefined?sd.do:sd.DO;"
    "html+='<hr style=\"border-top:1px dashed var(--border);margin:8px "
    "0;border-bottom:none;\">';"
    "html+='<div><strong>• pH (ATC):</strong> <span "
    "style=\"font-weight:bold;color:#3b82f6;\">'+(phVal!==undefined?Number("
    "phVal).toFixed(2):'N/A')+'</span></div>';"
    "html+='<div><strong>• Temperature:</strong> <span "
    "style=\"font-weight:bold;color:#10b981;\">'+(sd.temp!==undefined?Number("
    "sd.temp).toFixed(1):'N/A')+' °C</span></div>';"
    "html+='<div><strong>• Valid:</strong> "
    "<span>'+(sd.Valid!==undefined?(sd.Valid?\"Yes\":\"No\"):\"N/A\")+'</"
    "span></div>';"
    "if(doVal!==undefined){"
    "html+='<div><strong>• Oxy hòa tan (DO):</strong> <span "
    "style=\"font-weight:bold;color:#06b6d4;\">'+Number(doVal).toFixed(2)+' "
    "mg/L</span></div>';"
    "html+='<div><strong>• Độ bão hòa DO:</strong> <span "
    "style=\"font-weight:bold;color:#a855f7;\">'+(sd.do_sat!==undefined?Number("
    "sd.do_sat).toFixed(1)+'%':'N/A')+'</span></div>';"
    "html+='<div><strong>• Nhiệt độ DO:</strong> <span "
    "style=\"font-weight:bold;color:#eab308;\">'+(sd.do_temp!==undefined?"
    "Number(sd.do_temp).toFixed(1)+' °C':'N/A')+'</span></div>';"
    "html+='<div><strong>• DO Valid:</strong> "
    "<span>'+(sd.do_valid!==undefined?(sd.do_valid?\"Yes\":\"No\"):\"N/A\")+'</"
    "span></div>';"
    "}"
    "}"
    "let extraHtml='';"
    "for(const key in pl){"
    "if(key!=='Code'&&key!=='TimeStamp'&&key!=='DeviceId'&&key!=='HostName'&&"
    "key!=='Weight'&&key!=='payload'&&key!=='SensorData'){"
    "const val=typeof pl[key]==='object'?JSON.stringify(pl[key]):pl[key];"
    "extraHtml+='<div><strong>• '+key+':</strong> <span>'+val+'</span></div>';"
    "}"
    "}"
    "if(extraHtml){"
    "html+='<hr style=\"border-top:1px dashed var(--border);margin:8px "
    "0;border-bottom:none;\">'+extraHtml;"
    "}"
    "teleList.innerHTML=html;"
    "}else{"
    "teleList.innerHTML='<div "
    "style=\"color:var(--text-muted);font-style:italic;\">No telemetry data "
    "pushed yet...</div>';"
    "}"
    "}).catch(()=>{});"
    "}"
    "function reboot(){"
    "if(confirm('Are you sure you want to reboot ESP32?')){"
    "fetch('/api/reboot',{method:'POST'}).then(r=>{"
    "if(r.ok){"
    "alert('Device is rebooting... Please wait 5 seconds and reload.');"
    "window.location.reload();"
    "}"
    "});"
    "}"
    "}"
    "function syncTime(e){"
    "if(e)e.preventDefault();"
    "const btn=document.getElementById('btn_sync_time');"
    "setLoading(btn,true,'Syncing...');"
    "const epoch=Math.floor(Date.now()/1000);"
    "fetch('/api/set_time',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({epoch:epoch})})"
    ".then(r=>r.json()).then(d=>{"
    "if(d.success){showToast('Đồng bộ thời gian thành công!',true);updateDashboard();}"
    "else{showToast('Đồng bộ thất bại!',false);}"
    "}).catch(()=>showToast('Không thể kết nối đến thiết bị',false))"
    ".finally(()=>setLoading(btn,false));"
    "}"
    "function startOta(){"
    "const url=document.getElementById('ota_url').value.trim();"
    "if(!url){showToast('Vui lòng nhập URL!',false);return;}"
    "if(!confirm('Bạn có chắc chắn muốn cập nhật Firmware từ URL này?'))return;"
    "const btn=document.getElementById('btn_start_ota');"
    "setLoading(btn,true,'Starting...');"
    "fetch('/api/"
    "ota_trigger',{method:'POST',headers:{'Content-Type':'application/"
    "json'},body:JSON.stringify({url:url})})"
    ".then(r=>r.json()).then(d=>{"
    "if(d.status==='started'){showToast('Đã bắt đầu tiến trình OTA!',true);}"
    "else{showToast('Lỗi: '+d.message,false);}"
    "})"
    ".catch(()=>showToast('Không thể kết nối đến thiết bị',false))"
    ".finally(()=>setLoading(btn,false));"
    "}"
    "function updateLcdScreen(){"
    "const cv=document.getElementById('lcd_canvas');"
    "if(!cv||!document.getElementById('tab-lcd').classList.contains('active'))return;"
    "fetch('/api/screen_fb').then(r=>{"
    "if(r.status===401)window.location.reload();"
    "return r.arrayBuffer();"
    "}).then(buf=>{"
    "const view=new Uint8Array(buf);"
    "const ctx=cv.getContext('2d');"
    "const imgData=ctx.createImageData(128,64);"
    "for(let page=0;page<8;page++){"
    "for(let col=0;col<128;col++){"
    "const val=view[page*128+col];"
    "for(let bit=0;bit<8;bit++){"
    "const py=page*8+bit,px=col;"
    "const on=(val&(1<<bit))!==0;"
    "const r=on?15:155,g=on?56:188,b=on?15:15;"
    "const idx=(py*128+px)*4;"
    "imgData.data[idx]=r;imgData.data[idx+1]=g;imgData.data[idx+2]=b;imgData.data[idx+3]=255;"
    "}"
    "}"
    "}"
    "ctx.putImageData(imgData,0,0);"
    "}).catch(()=>{});"
    "}"
    "function simulateBtn(btn){"
    "fetch('/api/simulate_btn',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({button:btn})})"
    ".then(r=>r.json()).then(d=>{"
    "if(d.success)setTimeout(updateLcdScreen,50);"
    "}).catch(()=>{});"
    "}"
    "updateThemeButton();"
    "setInterval(updateDashboard,1000);"
    "setInterval(updateLcdScreen,250);"
    "updateDashboard();"
    "scan();"
    "</script></body></html>";

static esp_err_t portal_get_handler(httpd_req_t *req) {
  // httpd_resp_set_type(req, "text/html");
  // return httpd_resp_send(req, portal_html, HTTPD_RESP_USE_STRLEN);

  if (!is_authenticated(req)) {
    // Chưa đăng nhập -> Trả về giao diện Login
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, login_html, HTTPD_RESP_USE_STRLEN);
  }

  // Đã đăng nhập -> Hiển thị trang cấu hình bình thường
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, portal_html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t scan_get_handler(httpd_req_t *req) {
  if (!is_authenticated(req)) {
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"unauthorized\"}",
                    HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  wifi_config_manager_prepare_scan();
  wifi_scan_config_t scan_config = {0};
  esp_err_t err = esp_wifi_scan_start(&scan_config, true);
  if (err != ESP_OK) {
    ESP_LOGE(PORTAL_TAG, "Scan start failed: %s", esp_err_to_name(err));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "scan failed");
    return ESP_FAIL;
  }

  uint16_t ap_num = 0;
  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_num));
  wifi_ap_record_t *ap_records = calloc(ap_num, sizeof(wifi_ap_record_t));
  if (ap_records == NULL) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem");
    return ESP_FAIL;
  }

  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_num, ap_records));

  cJSON *arr = cJSON_CreateArray();
  for (uint16_t i = 0; i < ap_num; i++) {
    if (ap_records[i].ssid[0] == '\0') {
      continue;
    }
    cJSON_AddItemToArray(arr,
                         cJSON_CreateString((const char *)ap_records[i].ssid));
  }

  char *out = cJSON_PrintUnformatted(arr);
  cJSON_Delete(arr);
  free(ap_records);

  if (out == NULL) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json fail");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
  free(out);
  return ESP_OK;
}

static esp_err_t save_post_handler(httpd_req_t *req) {
  if (!is_authenticated(req)) {
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"unauthorized\"}",
                    HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  char buf[512];
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no body");
    return ESP_FAIL;
  }
  buf[ret] = '\0';

  cJSON *root = cJSON_Parse(buf);
  if (root == NULL) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
    return ESP_FAIL;
  }

  const cJSON *tgt = cJSON_GetObjectItem(root, "target");
  bool ok = false;

  if (cJSON_IsString(tgt)) {
    if (strcmp(tgt->valuestring, "wifi") == 0) {
      const cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
      const cJSON *pass = cJSON_GetObjectItem(root, "password");
      if (cJSON_IsString(ssid)) {
        const char *pass_str = (cJSON_IsString(pass)) ? pass->valuestring : "";
        ok = wifi_config_manager_save(ssid->valuestring, pass_str);
        if (ok)
          wifi_config_manager_schedule_connect();
      }
    } else if (strcmp(tgt->valuestring, "azure") == 0) {
      const cJSON *hostName = cJSON_GetObjectItem(root, "hostName");
      const cJSON *deviceId = cJSON_GetObjectItem(root, "deviceId");
      const cJSON *symmetricKey = cJSON_GetObjectItem(root, "symmetricKey");

      if (cJSON_IsString(hostName) && cJSON_IsString(deviceId) &&
          cJSON_IsString(symmetricKey)) {
        if (strlen(hostName->valuestring) > 0) {
          ok = azure_config_manager_save(hostName->valuestring,
                                         deviceId->valuestring,
                                         symmetricKey->valuestring);
          if (ok) {
            memset(IoTHubHandle.hostName, 0, sizeof(IoTHubHandle.hostName));
            memset(IoTHubHandle.deviceId, 0, sizeof(IoTHubHandle.deviceId));
            memset(IoTHubHandle.symmetricKey, 0,
                   sizeof(IoTHubHandle.symmetricKey));

            strncpy(IoTHubHandle.hostName, hostName->valuestring,
                    sizeof(IoTHubHandle.hostName) - 1);
            strncpy(IoTHubHandle.deviceId, deviceId->valuestring,
                    sizeof(IoTHubHandle.deviceId) - 1);
            strncpy(IoTHubHandle.symmetricKey, symmetricKey->valuestring,
                    sizeof(IoTHubHandle.symmetricKey) - 1);

            IoTHubHandle.isNeedReinit = true;
          }
        }
      }
    } else if (strcmp(tgt->valuestring, "secret") == 0) {
      const cJSON *sec = cJSON_GetObjectItem(root, "secret");
      if (cJSON_IsString(sec) && strlen(sec->valuestring) > 0) {
        ok = auth_secret_save(sec->valuestring);
      }
    }
  }

  cJSON_Delete(root);

  cJSON *resp = cJSON_CreateObject();
  cJSON_AddStringToObject(resp, "status", ok ? "saved" : "failed");
  char *out = cJSON_PrintUnformatted(resp);
  cJSON_Delete(resp);

  if (out == NULL) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "resp fail");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
  free(out);

  return ESP_OK;
}

static esp_err_t clear_post_handler(httpd_req_t *req) {
  if (!is_authenticated(req)) {
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"unauthorized\"}",
                    HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  char buf[128];
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0)
    return ESP_FAIL;
  buf[ret] = '\0';

  cJSON *root = cJSON_Parse(buf);
  if (!root)
    return ESP_FAIL;

  const cJSON *tgt = cJSON_GetObjectItem(root, "target");
  bool ok = false;
  if (cJSON_IsString(tgt)) {
    if (strcmp(tgt->valuestring, "wifi") == 0)
      ok = wifi_config_manager_clear();
    else if (strcmp(tgt->valuestring, "azure") == 0)
      ok = azure_config_manager_clear();
  }
  cJSON_Delete(root);

  cJSON *resp = cJSON_CreateObject();
  cJSON_AddStringToObject(resp, "status", ok ? "cleared" : "failed");
  char *out = cJSON_PrintUnformatted(resp);
  cJSON_Delete(resp);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
  free(out);
  return ESP_OK;
}

static esp_err_t login_post_handler(httpd_req_t *req) {
  char buf[128];
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no body");
    return ESP_FAIL;
  }
  buf[ret] = '\0';

  cJSON *root = cJSON_Parse(buf);
  if (root == NULL) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
    return ESP_FAIL;
  }

  const cJSON *pass = cJSON_GetObjectItem(root, "password");
  bool ok = false;
  if (cJSON_IsString(pass)) {
    // char stored[AUTH_SECRET_MAX_LEN] = {0};
    // auth_secret_load(stored, sizeof(stored));
    // printf("\n[DEBUG LOGIN] Nhập: '%s' (Độ dài: %d) | NVS lưu: '%s' (Độ dài:
    // %d)\n\n",
    //        pass->valuestring, strlen(pass->valuestring), stored,
    //        strlen(stored));

    ok = auth_secret_verify(pass->valuestring);
  }
  cJSON_Delete(root);

  if (ok) {
    char cookie_str[128];
    char secret[AUTH_SECRET_MAX_LEN] = {0};
    auth_secret_load(secret, sizeof(secret));

    // Tạo chuỗi Cookie chứa mật khẩu đã băm/hoặc thô (trong ESP32 ta dùng thẳng
    // secret cho đơn giản)
    snprintf(cookie_str, sizeof(cookie_str),
             "session=%s; Path=/; Max-Age=3600; HttpOnly", secret);

    httpd_resp_set_hdr(req, "Set-Cookie", cookie_str);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"success\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  } else {
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"fail\",\"message\":\"Sai mật khẩu!\"}",
                    HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
}

static esp_err_t logout_post_handler(httpd_req_t *req) {
  // Để đăng xuất, ta xóa cookie bằng cách set Max-Age=0
  httpd_resp_set_hdr(req, "Set-Cookie",
                     "session=; Path=/; Max-Age=0; HttpOnly");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, "{\"status\":\"logged_out\"}", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t system_status_get_handler(httpd_req_t *req) {
  // Xác thực quyền truy cập
  if (!is_authenticated(req)) {
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"unauthorized\"}",
                    HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  cJSON *root = cJSON_CreateObject();

  // 1. Thông tin WiFi
  cJSON *wifi = cJSON_CreateObject();
  cJSON_AddBoolToObject(wifi, "connected", Sys_Info.isWifiConnected);
  if (Sys_Info.isWifiConnected) {
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
      cJSON_AddStringToObject(wifi, "ssid", (char *)ap_info.ssid);
      cJSON_AddNumberToObject(wifi, "rssi", ap_info.rssi);
    } else {
      cJSON_AddStringToObject(wifi, "ssid", "Unknown");
      cJSON_AddNumberToObject(wifi, "rssi", 0);
    }
  } else {
    cJSON_AddStringToObject(wifi, "ssid", "Disconnected");
    cJSON_AddNumberToObject(wifi, "rssi", -100);
  }
  cJSON_AddItemToObject(root, "wifi", wifi);

  // 2. Thông tin Cloud Azure
  cJSON *azure = cJSON_CreateObject();
  cJSON_AddBoolToObject(azure, "initialized", IoTHubHandle.isAzureInitialized);
  cJSON_AddStringToObject(azure, "host", IoTHubHandle.hostName);
  cJSON_AddStringToObject(azure, "device_id", IoTHubHandle.deviceId);
  cJSON_AddItemToObject(root, "azure", azure);

  // 3. Thông số Hệ thống (OS Specs)
  cJSON *sys = cJSON_CreateObject();
  cJSON_AddNumberToObject(sys, "free_heap", esp_get_free_heap_size());
  cJSON_AddNumberToObject(sys, "uptime",
                          esp_timer_get_time() / 1000000ULL); // Trả về giây
  cJSON_AddStringToObject(sys, "version", VERSION);
  cJSON_AddNumberToObject(sys, "epoch", (double)time(NULL));
  cJSON_AddNumberToObject(sys, "reset_count", reset_count);

  // Đọc nguyên nhân reset
  esp_reset_reason_t reason = esp_reset_reason();
  const char *reason_str = "Unknown";
  switch (reason) {
  case ESP_RST_POWERON:
    reason_str = "Power-on Reset";
    break;
  case ESP_RST_EXT:
    reason_str = "External Reset";
    break;
  case ESP_RST_SW:
    reason_str = "Software Reset";
    break;
  case ESP_RST_PANIC:
    reason_str = "Software Panic";
    break;
  case ESP_RST_INT_WDT:
    reason_str = "Interrupt Watchdog";
    break;
  case ESP_RST_TASK_WDT:
    reason_str = "Task Watchdog";
    break;
  case ESP_RST_DEEPSLEEP:
    reason_str = "Deepsleep Reset";
    break;
  case ESP_RST_BROWNOUT:
    reason_str = "Brownout Reset";
    break;
  default:
    break;
  }
  cJSON_AddStringToObject(sys, "reset_reason", reason_str);
  cJSON_AddStringToObject(sys, "ota_status", User_Ota_Get_Status_String());
  const esp_partition_t *running = esp_ota_get_running_partition();
  cJSON_AddStringToObject(sys, "partition",
                          (running != NULL) ? running->label : "Unknown");
  cJSON_AddItemToObject(root, "system", sys);

  // 4. Trạng thái cảm biến pH & Nhiệt độ
  cJSON *sensor = cJSON_CreateObject();
  PH_Temp_Sensor_Status_t status = Get_Sensor_Status();
  cJSON_AddNumberToObject(sensor, "ph", status.ph);
  cJSON_AddNumberToObject(sensor, "temp", status.temperature);
  cJSON_AddNumberToObject(sensor, "v_probe_mv", status.v_probe_mv);
  cJSON_AddBoolToObject(sensor, "is_calibrated", status.is_calibrated);
  cJSON_AddNumberToObject(sensor, "filter_lvl", g_filter_level);
  cJSON_AddNumberToObject(sensor, "ph7_voltage_mv", status.ph7_voltage_mv);
  cJSON_AddNumberToObject(sensor, "ph7_temp_c", status.ph7_temp_c);
  cJSON_AddNumberToObject(sensor, "ph4_voltage_mv", status.ph4_voltage_mv);
  cJSON_AddNumberToObject(sensor, "ph4_temp_c", status.ph4_temp_c);
  cJSON_AddNumberToObject(sensor, "slope_norm", status.slope_norm);
  cJSON_AddNumberToObject(sensor, "u7", status.u7);
  cJSON_AddNumberToObject(sensor, "do_mg_l", status.do_mg_l);
  cJSON_AddNumberToObject(sensor, "do_temp_c", status.do_temp_c);
  cJSON_AddNumberToObject(sensor, "do_saturation_pct",
                          status.do_saturation_pct);
  cJSON_AddBoolToObject(sensor, "do_valid", status.do_valid);
  cJSON_AddNumberToObject(sensor, "do_error_code", status.do_error_code);
  cJSON_AddItemToObject(root, "sensor", sensor);

  // 5. Telemetry Watch Monitor data
  cJSON *telemetry_status = cJSON_CreateObject();
  cJSON_AddBoolToObject(telemetry_status, "active",
                        User_Azure_Get_Telemetry_Active());
  cJSON_AddNumberToObject(telemetry_status, "interval",
                          User_Azure_Get_Telemetry_Interval());
  cJSON *last_tele_obj = NULL;
  if (g_last_telemetry_payload[0] != '\0') {
    last_tele_obj = cJSON_Parse(g_last_telemetry_payload);
  }
  if (last_tele_obj != NULL) {
    cJSON_AddItemToObject(telemetry_status, "last_pushed", last_tele_obj);
  } else {
    cJSON_AddNullToObject(telemetry_status, "last_pushed");
  }
  cJSON_AddItemToObject(root, "telemetry", telemetry_status);

  char *out = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
  free(out);

  return ESP_OK;
}

static esp_err_t scale_control_post_handler(httpd_req_t *req) {
  if (!is_authenticated(req)) {
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"message\":\"Chưa đăng nhập!\"}",
                    HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  char buf[128];
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no body");
    return ESP_FAIL;
  }
  buf[ret] = '\0';

  cJSON *root = cJSON_Parse(buf);
  if (root == NULL) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
    return ESP_FAIL;
  }

  const cJSON *action = cJSON_GetObjectItem(root, "action");
  if (!cJSON_IsString(action)) {
    cJSON_Delete(root);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                        "action field missing or not string");
    return ESP_FAIL;
  }

  const char *status_msg = "Hành động không xác định";
  bool success = false;
  PH_Temp_Sensor_Status_t status = Get_Sensor_Status();

  if (strcmp(action->valuestring, "CAL_7") == 0) {
    bool ok = Calibrate_PH_Point(7.00f, status.v_probe_mv, status.temperature, 2);
    if (ok) {
      status_msg = "Hiệu chuẩn pH 7.00 thành công!";
      success = true;
    } else {
      status_msg = "Hiệu chuẩn pH 7.00 thất bại! Vui lòng kiểm tra lại điện áp "
                   "điện cực.";
    }
  } else if (strcmp(action->valuestring, "CAL_4") == 0) {
    bool ok = Calibrate_PH_Point(4.00f, status.v_probe_mv, status.temperature, 2);
    if (ok) {
      status_msg = "Hiệu chuẩn pH 4.01 thành công!";
      success = true;
    } else {
      status_msg = "Hiệu chuẩn pH 4.01 thất bại! Vui lòng kiểm tra lại điện áp "
                   "điện cực.";
    }
  } else if (strcmp(action->valuestring, "SET_FILTER") == 0) {
    const cJSON *level = cJSON_GetObjectItem(root, "level");
    if (level != NULL && cJSON_IsNumber(level)) {
      int lvl = level->valueint;
      if (lvl >= 0 && lvl < 3) {
        g_filter_level = (filter_level_t)lvl;
        Nvs_Write_Number("filter_lvl", (uint32_t)g_filter_level);
        update_system_filters_level(g_filter_level);
        status_msg = "Đặt mức lọc số thành công!";
        success = true;
      } else {
        status_msg = "Mức lọc không hợp lệ (yêu cầu 0, 1 hoặc 2)!";
      }
    } else {
      status_msg = "Thiếu trường level!";
    }
  } else {
    status_msg = "Hành động không hỗ trợ!";
  }

  cJSON_Delete(root);

  cJSON *resp = cJSON_CreateObject();
  cJSON_AddBoolToObject(resp, "success", success);
  cJSON_AddStringToObject(resp, "message", status_msg);
  char *out = cJSON_PrintUnformatted(resp);
  cJSON_Delete(resp);

  if (out == NULL) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "resp fail");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
  free(out);
  return ESP_OK;
}

static esp_err_t do_control_post_handler(httpd_req_t *req) {
  if (!is_authenticated(req)) {
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":false,\"message\":\"Chưa đăng nhập!\"}",
                    HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  char buf[128];
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no body");
    return ESP_FAIL;
  }
  buf[ret] = '\0';

  cJSON *root = cJSON_Parse(buf);
  if (root == NULL) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
    return ESP_FAIL;
  }

  const cJSON *action = cJSON_GetObjectItem(root, "action");
  if (!cJSON_IsString(action)) {
    cJSON_Delete(root);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                        "action field missing or not string");
    return ESP_FAIL;
  }

  const char *status_msg = "Hành động không xác định";
  bool success = false;

  if (strcmp(action->valuestring, "CAL_DO_ZERO") == 0) {
    esp_err_t err = do_sensor_calibrate_zero();
    if (err == ESP_OK) {
      status_msg = "Hiệu chuẩn điểm 0 thành công!";
      success = true;
    } else {
      status_msg =
          "Hiệu chuẩn điểm 0 thất bại! Vui lòng kiểm tra lại kết nối cảm biến.";
    }
  } else if (strcmp(action->valuestring, "CAL_DO_SLOPE") == 0) {
    esp_err_t err = do_sensor_calibrate_slope();
    if (err == ESP_OK) {
      status_msg = "Hiệu chuẩn độ dốc thành công!";
      success = true;
    } else {
      status_msg =
          "Hiệu chuẩn độ dốc thất bại! Vui lòng kiểm tra lại kết nối cảm biến.";
    }
  } else if (strcmp(action->valuestring, "CORRECT_DO_TEMP") == 0) {
    const cJSON *val = cJSON_GetObjectItem(root, "value");
    if (cJSON_IsNumber(val)) {
      float temp_val = (float)val->valuedouble;
      esp_err_t err = do_sensor_correct_temp(temp_val);
      if (err == ESP_OK) {
        status_msg = "Hiệu chỉnh nhiệt độ thành công!";
        success = true;
      } else {
        status_msg = "Hiệu chỉnh nhiệt độ thất bại! Vui lòng kiểm tra lại kết "
                     "nối cảm biến.";
      }
    } else {
      status_msg = "Giá trị nhiệt độ không hợp lệ!";
    }
  } else if (strcmp(action->valuestring, "RESET_DO_SENSOR") == 0) {
    esp_err_t err = do_sensor_reset();
    if (err == ESP_OK) {
      status_msg = "Khôi phục cài đặt gốc thành công!";
      success = true;
    } else {
      status_msg = "Khôi phục cài đặt gốc thất bại! Vui lòng kiểm tra lại kết "
                   "nối cảm biến.";
    }
  }
  else if (strcmp(action->valuestring, "RESET_DO_SENSOR") == 0) {
        esp_err_t err = do_sensor_reset();
        if (err == ESP_OK) {
            status_msg = "Khôi phục cài đặt gốc thành công!";
            success = true;
        } else {
            status_msg = "Khôi phục cài đặt gốc thất bại! Vui lòng kiểm tra lại kết nối cảm biến.";
        }
    } 
    /* --- BẮT ĐẦU PHẦN THÊM MỚI --- */
    else if (strcmp(action->valuestring, "COMPENSATE_SALINITY") == 0) {
        const cJSON *val = cJSON_GetObjectItem(root, "value");
        if (cJSON_IsNumber(val)) {
            float salinity_val = (float)val->valuedouble;
            esp_err_t err = do_sensor_set_salinity(salinity_val);
            if (err == ESP_OK) {
                status_msg = "Cập nhật bù độ mặn thành công!";
                success = true;
            } else {
                status_msg = "Cập nhật bù độ mặn thất bại! Vui lòng kiểm tra lại kết nối cảm biến.";
            }
        } else {
            status_msg = "Giá trị độ mặn không hợp lệ!";
        }
    }
  else {
    status_msg = "Hành động không hỗ trợ!";
  }

  cJSON_Delete(root);

  cJSON *resp = cJSON_CreateObject();
  cJSON_AddBoolToObject(resp, "success", success);
  cJSON_AddStringToObject(resp, "message", status_msg);
  char *out = cJSON_PrintUnformatted(resp);
  cJSON_Delete(resp);

  if (out == NULL) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "resp fail");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
  free(out);
  return ESP_OK;
}

static esp_err_t reboot_post_handler(httpd_req_t *req) {
  if (!is_authenticated(req)) {
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"unauthorized\"}",
                    HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, "{\"status\":\"rebooting\"}", HTTPD_RESP_USE_STRLEN);

  // Đợi 1 giây để phản hồi HTTP gửi đi thành công rồi mới reset
  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();
  return ESP_OK;
}

static esp_err_t ota_trigger_post_handler(httpd_req_t *req) {
  if (!is_authenticated(req)) {
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"unauthorized\"}",
                    HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  char buf[512];
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no body");
    return ESP_FAIL;
  }
  buf[ret] = '\0';

  cJSON *root = cJSON_Parse(buf);
  if (root == NULL) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
    return ESP_FAIL;
  }

  const cJSON *url = cJSON_GetObjectItem(root, "url");
  bool ok = false;
  if (cJSON_IsString(url) && strlen(url->valuestring) > 0) {
    User_Ota_Trigger(url->valuestring);
    ok = true;
  }

  cJSON_Delete(root);

  cJSON *resp = cJSON_CreateObject();
  cJSON_AddStringToObject(resp, "status", ok ? "started" : "failed");
  if (!ok) {
    cJSON_AddStringToObject(resp, "message", "Invalid URL");
  }
  char *out = cJSON_PrintUnformatted(resp);
  cJSON_Delete(resp);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
  free(out);

  return ESP_OK;
}

static esp_err_t screen_fb_get_handler(httpd_req_t *req) {
  if (!is_authenticated(req)) {
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"unauthorized\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  uint8_t buffer[1024];
  LCD_GetFramebuffer(buffer);

  httpd_resp_set_type(req, "application/octet-stream");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_send(req, (const char *)buffer, sizeof(buffer));
  return ESP_OK;
}

static esp_err_t simulate_btn_post_handler(httpd_req_t *req) {
  if (!is_authenticated(req)) {
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"unauthorized\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  char buf[64];
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no body");
    return ESP_FAIL;
  }
  buf[ret] = '\0';

  cJSON *root = cJSON_Parse(buf);
  if (root == NULL) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
    return ESP_FAIL;
  }

  const cJSON *btn = cJSON_GetObjectItem(root, "button");
  bool ok = false;
  if (cJSON_IsString(btn)) {
    ok = menu_simulate_press(btn->valuestring);
  }
  cJSON_Delete(root);

  cJSON *resp = cJSON_CreateObject();
  cJSON_AddBoolToObject(resp, "success", ok);
  char *out = cJSON_PrintUnformatted(resp);
  cJSON_Delete(resp);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
  free(out);
  return ESP_OK;
}

static esp_err_t set_time_post_handler(httpd_req_t *req) {
  if (!is_authenticated(req)) {
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"unauthorized\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  char buf[128];
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no body");
    return ESP_FAIL;
  }
  buf[ret] = '\0';

  cJSON *root = cJSON_Parse(buf);
  if (root == NULL) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
    return ESP_FAIL;
  }

  const cJSON *epoch_item = cJSON_GetObjectItem(root, "epoch");
  bool success = false;
  if (cJSON_IsNumber(epoch_item)) {
    time_t t = (time_t)epoch_item->valuedouble;
    struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
    settimeofday(&tv, NULL);

    struct tm tm_info;
    localtime_r(&t, &tm_info);
    esp_err_t err = ds3231_set_time(&tm_info);
    if (err == ESP_OK) {
      success = true;
      ESP_LOGI(PORTAL_TAG, "Da dong bo thoi gian tu Web Portal: %04d-%02d-%02d %02d:%02d:%02d",
               tm_info.tm_year + 1900, tm_info.tm_mon + 1, tm_info.tm_mday,
               tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec);
    } else {
      ESP_LOGE(PORTAL_TAG, "Dong bo RTC that bai: %s", esp_err_to_name(err));
    }
  }

  cJSON_Delete(root);

  cJSON *resp = cJSON_CreateObject();
  cJSON_AddBoolToObject(resp, "success", success);
  char *out = cJSON_PrintUnformatted(resp);
  cJSON_Delete(resp);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
  free(out);
  return ESP_OK;
}

void web_portal_register_handlers(httpd_handle_t server) {
  static httpd_uri_t portal = {.uri = "/",
                               .method = HTTP_GET,
                               .handler = portal_get_handler,
                               .user_ctx = NULL};

  static httpd_uri_t scan = {.uri = "/scan",
                             .method = HTTP_GET,
                             .handler = scan_get_handler,
                             .user_ctx = NULL};

  static httpd_uri_t save = {.uri = "/save",
                             .method = HTTP_POST,
                             .handler = save_post_handler,
                             .user_ctx = NULL};

  static httpd_uri_t clear = {.uri = "/clear",
                              .method = HTTP_POST,
                              .handler = clear_post_handler,
                              .user_ctx = NULL};

  // 1. KHAI BÁO THÊM 2 URI DƯỚI ĐÂY:
  static httpd_uri_t login = {.uri = "/login",
                              .method = HTTP_POST,
                              .handler = login_post_handler,
                              .user_ctx = NULL};
  static httpd_uri_t logout = {.uri = "/logout",
                               .method = HTTP_POST,
                               .handler = logout_post_handler,
                               .user_ctx = NULL};

  static httpd_uri_t sys_status = {.uri = "/api/system_status",
                                   .method = HTTP_GET,
                                   .handler = system_status_get_handler,
                                   .user_ctx = NULL};

  static httpd_uri_t sys_reboot = {.uri = "/api/reboot",
                                   .method = HTTP_POST,
                                   .handler = reboot_post_handler,
                                   .user_ctx = NULL};

  static httpd_uri_t sys_scale_control = {.uri = "/api/scale_control",
                                          .method = HTTP_POST,
                                          .handler = scale_control_post_handler,
                                          .user_ctx = NULL};

  static httpd_uri_t sys_do_control = {.uri = "/api/do_control",
                                       .method = HTTP_POST,
                                       .handler = do_control_post_handler,
                                       .user_ctx = NULL};

  static httpd_uri_t ota_trigger = {.uri = "/api/ota_trigger",
                                    .method = HTTP_POST,
                                    .handler = ota_trigger_post_handler,
                                    .user_ctx = NULL};

  static httpd_uri_t sys_screen_fb = {.uri = "/api/screen_fb",
                                      .method = HTTP_GET,
                                      .handler = screen_fb_get_handler,
                                      .user_ctx = NULL};

  static httpd_uri_t sys_simulate_btn = {.uri = "/api/simulate_btn",
                                         .method = HTTP_POST,
                                         .handler = simulate_btn_post_handler,
                                         .user_ctx = NULL};

  static httpd_uri_t sys_set_time = {.uri = "/api/set_time",
                                     .method = HTTP_POST,
                                     .handler = set_time_post_handler,
                                     .user_ctx = NULL};

  httpd_register_uri_handler(server, &portal);
  httpd_register_uri_handler(server, &scan);
  httpd_register_uri_handler(server, &save);
  httpd_register_uri_handler(server, &clear);

  // 2. ĐĂNG KÝ VỚI SERVER:
  httpd_register_uri_handler(server, &login);
  httpd_register_uri_handler(server, &logout);
  httpd_register_uri_handler(server, &sys_status);
  httpd_register_uri_handler(server, &sys_reboot);
  httpd_register_uri_handler(server, &sys_scale_control);
  httpd_register_uri_handler(server, &sys_do_control);
  httpd_register_uri_handler(server, &ota_trigger);
  httpd_register_uri_handler(server, &sys_screen_fb);
  httpd_register_uri_handler(server, &sys_simulate_btn);
  httpd_register_uri_handler(server, &sys_set_time);
  ESP_LOGI(PORTAL_TAG, "Web portal handlers registered");
}
