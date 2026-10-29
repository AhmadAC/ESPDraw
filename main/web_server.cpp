#include "web_server.h"
#include "config.h"
#include "motor_control.h"
#include "wifi_manager.h"
#include "system_utils.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_wifi.h"

#include <string.h>
#include <stdlib.h>

static const char *TAG = "WEB_SERVER";
httpd_handle_t server = NULL;

/* ==============================================
   HTML UI PAYLOAD WITH NINTENDO GAMEPAD SUPPORT
   ============================================== */
const char HTML_UI[] = R"raw_html(
<!DOCTYPE html><html><head><meta charset="utf-8"><title>Drawbridge Controller</title><meta name="viewport" content="width=device-width, initial-scale=1">
<style>
    :root { --primary: #0ea5e9; --bg: #0f172a; --card: #1e293b; --text: #f1f5f9; }
    body { font-family: -apple-system, sans-serif; background: var(--bg); color: var(--text); padding: 15px; display: flex; flex-direction: column; align-items: center; margin:0; }
    .container { width: 100%; max-width: 500px; }
    .card { background: var(--card); padding: 22px; border-radius: 20px; border: 1px solid #334155; text-align: center; margin-bottom: 20px; box-sizing: border-box; }
    h2 { color: var(--primary); margin-top: 0; font-size: 1.3rem; }
    input[type=password], select { width: 100%; padding: 12px; margin: 8px 0 20px; border-radius: 10px; border: 1px solid #475569; background: #0f172a; color: white; box-sizing: border-box; }
    button { width: 100%; padding: 14px; border: none; border-radius: 12px; font-weight: bold; cursor: pointer; color: white; margin-top:10px; transition: transform 0.1s; }
    button:active { transform: scale(0.96); opacity: 0.9; }
    .btn-green { background: #10b981; }
    .btn-purple { background: #8b5cf6; }
    .btn-red { background: #ef4444; }
    .btn-gray { background: #475569; }
    .status-bar { padding: 12px; border-radius: 10px; font-weight: bold; text-align: center; font-size: 0.85rem; border: 1px solid; text-transform: uppercase; background: #172554; color: #93c5fd; border-color: #3b82f6; margin-bottom:15px; }
    .text-sm { font-size: 0.85rem; color: #94a3b8; margin-bottom:10px; }
    .badge-safe { font-size: 0.76rem; background: #064e3b; color: #6ee7b7; padding: 7px 10px; border-radius: 8px; border: 1px solid #059669; margin: 12px 0 16px 0; display: block; }
    
    /* Gamepad Card Styles */
    .gp-box { background: #0f172a; border: 1px solid #334155; border-radius: 12px; padding: 14px; text-align: left; margin-bottom: 12px; }
    .gp-status { display: flex; align-items: center; justify-content: space-between; font-weight: bold; font-size: 0.9rem; margin-bottom: 8px; }
    .gp-dot { width: 10px; height: 10px; border-radius: 50%; display: inline-block; margin-right: 6px; background: #ef4444; }
    .gp-dot.active { background: #10b981; box-shadow: 0 0 8px #10b981; }
    .gp-guide { font-size: 0.8rem; color: #cbd5e1; line-height: 1.6; margin: 0; padding-left: 18px; }
    .gp-hud { display: grid; grid-template-columns: repeat(4, 1fr); gap: 6px; margin-top: 10px; text-align: center; font-size: 0.75rem; font-weight: bold; }
    .hud-tag { padding: 6px 4px; border-radius: 6px; background: #334155; color: #94a3b8; }
    .hud-tag.active { background: #0ea5e9; color: white; box-shadow: 0 0 6px #0ea5e9; }
    .hud-tag.purple-active { background: #8b5cf6; color: white; box-shadow: 0 0 6px #8b5cf6; }

    input[type=range] { -webkit-appearance: none; appearance: none; width: 100%; height: 10px; border-radius: 5px; background: #334155; outline: none; margin: 12px 0; padding: 0; }
    input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; appearance: none; width: 26px; height: 26px; border-radius: 50%; background: #0ea5e9; cursor: pointer; border: 2px solid #ffffff; }
    input[type=range]::-moz-range-thumb { width: 26px; height: 26px; border-radius: 50%; background: #0ea5e9; cursor: pointer; border: 2px solid #ffffff; }
</style>
</head>
<body>
    <div class="container">
        <div class="status-bar">DRAWBRIDGE CONTROLLER DASHBOARD</div>

        <!-- Gamepad Integration Card -->
        <div class="card">
            <h2>🎮 Nintendo Controller Input</h2>
            <div class="gp-box">
                <div class="gp-status">
                    <span><span id="gp_dot" class="gp-dot"></span><span id="gp_name">Controller Disconnected</span></span>
                    <span id="gp_badge" style="font-size:0.75rem; color:#94a3b8;">Bluetooth / USB</span>
                </div>
                <p id="gp_hint" class="text-sm" style="margin-bottom:8px; color:#f59e0b;">Pair Joy-Con or Switch Pro controller to phone and press any button.</p>
                <ul class="gp-guide">
                    <li><b>[A] Button:</b> Toggle Clockwise (ON / OFF)</li>
                    <li><b>[Y] Button:</b> Toggle Anti-Clockwise (ON / OFF)</li>
                    <li><b>[Left Stick &uarr;/&darr;]:</b> Adjust Turning Speed (20% - 100%)</li>
                    <li><b>[B] Button:</b> Stop Bridge Immediately</li>
                </ul>
                <div class="gp-hud">
                    <span id="hud_a" class="hud-tag">A (CW)</span>
                    <span id="hud_y" class="hud-tag">Y (CCW)</span>
                    <span id="hud_stick" class="hud-tag">Stick: 0.0</span>
                    <span id="hud_spd" class="hud-tag">60%</span>
                </div>
            </div>
        </div>

        <!-- Controls Card -->
        <div class="card">
            <h2>Bridge Controls</h2>
            <div class="text-sm"><b>Current State:</b> <span id="lastcmd" style="color:#38bdf8; font-weight:bold;">Loading...</span></div>
            
            <div style="display:flex; justify-content:space-between; align-items:center; margin-top:15px;">
                <span style="font-weight:bold; font-size:0.9rem;">Turning Speed:</span>
                <span id="speedVal" style="font-weight:bold; color:#38bdf8;">60%</span>
            </div>

            <input type="range" id="speed" min="20" max="100" value="60" step="5" oninput="onSliderInput(this.value)" onchange="onSliderChange(this.value)">
            
            <div style="display:flex; justify-content:space-between; font-size:0.75rem; color:#94a3b8; margin-top:-8px; margin-bottom:10px;">
                <span>20% (Minimum)</span><span>60% (Default)</span><span>100% (Maximum)</span>
            </div>

            <div class="badge-safe">🛡️ Safe Power Limit: 80% Max Duty &bull; 15s Auto-Stop Protection Active</div>

            <button class="btn-green" onclick="c('toggle_cw')">TOGGLE CLOCKWISE (CW) [A]</button>
            <button class="btn-purple" onclick="c('toggle_ccw')">TOGGLE ANTI-CLOCKWISE (CCW) [Y]</button>
            <button class="btn-red" onclick="c('stop')">STOP [B]</button>
            
            <p id="cstat" class="text-sm" style="margin-top:15px; min-height: 1.2em;"></p>
        </div>

        <!-- Wi-Fi Setup Card -->
        <div class="card">
            <h2>Wi-Fi Settings</h2>
            <div id="status" class="text-sm">Ready to Scan</div>
            <button class="btn-gray" onclick="scan()">Scan Networks</button>
            
            <select id="ssid" style="margin-top:15px;"><option value="">-- Select --</option></select>
            <input type="password" id="pass" placeholder="Password">
            
            <button class="btn-green" onclick="save()">Save and Connect</button>
            
            <hr style="border-color:#334155; margin: 25px 0;">
            <div class="text-sm">Quick Boot Mode Switch</div>
            <button style="background:#f59e0b;" onclick="forceAP()">Force AP Mode</button>
            <button style="background:#10b981;" onclick="forceWiFi()">Use Saved Wi-Fi</button>
        </div>
    </div>

    <script>
        let currentMagnitude = 60;
        let isSliding = false;
        let lastBtnAState = false;
        let lastBtnYState = false;
        let lastBtnBState = false;
        let lastStickTime = 0;

        function updateSpeedDisplay(val){
            document.getElementById('speedVal').innerText = val + "%";
            document.getElementById('hud_spd').innerText = val + "%";
            let s = document.getElementById('speed');
            if (s && !isSliding) s.value = val;
        }

        function onSliderInput(val){
            isSliding = true;
            currentMagnitude = parseInt(val, 10);
            updateSpeedDisplay(currentMagnitude);
        }

        function onSliderChange(val){
            isSliding = false;
            currentMagnitude = parseInt(val, 10);
            updateSpeedDisplay(currentMagnitude);
            sendSpeed(currentMagnitude);
        }

        function sendSpeed(val){
            document.getElementById('cstat').innerText = 'Setting speed: ' + val + '%...';
            fetch('/speed?mag=' + val)
                .then(r => r.text())
                .then(() => {
                    document.getElementById('cstat').innerText = 'Speed updated: ' + val + '%';
                    setTimeout(loadStatus, 300);
                })
                .catch(() => {
                    document.getElementById('cstat').innerText = 'Speed update failed';
                });
        }

        function c(cmd){
            document.getElementById('cstat').innerText = 'Sending command...';
            fetch('/bridge?cmd='+cmd).then(r=>r.text()).then(t=>{
                document.getElementById('cstat').innerText = 'Status: ' + t;
                setTimeout(loadStatus, 400);
            });
        }

        /* ========================================================
           NINTENDO SWITCH CONTROLLER & GAMEPAD API ENGINE
           ======================================================== */
        function pollGamepads() {
            const gamepads = navigator.getGamepads ? navigator.getGamepads() : [];
            let activeGp = null;

            for (let i = 0; i < gamepads.length; i++) {
                if (gamepads[i] && gamepads[i].connected) {
                    activeGp = gamepads[i];
                    break;
                }
            }

            const dot = document.getElementById('gp_dot');
            const name = document.getElementById('gp_name');
            const hint = document.getElementById('gp_hint');
            const hudA = document.getElementById('hud_a');
            const hudY = document.getElementById('hud_y');
            const hudStick = document.getElementById('hud_stick');

            if (activeGp) {
                dot.classList.add('active');
                name.innerText = activeGp.id.length > 25 ? activeGp.id.substring(0, 22) + "..." : activeGp.id;
                hint.style.display = 'none';

                // Standard / Nintendo Switch Layout:
                // buttons[1] = physical 'A' button (East)
                // buttons[2] = physical 'Y' button (West)
                // buttons[0] = physical 'B' button (South)
                const btnA = (activeGp.buttons[1] && activeGp.buttons[1].pressed) || false;
                const btnY = (activeGp.buttons[2] && activeGp.buttons[2].pressed) || false;
                const btnB = (activeGp.buttons[0] && activeGp.buttons[0].pressed) || false;

                // Visual Indicator for Button A (Clockwise)
                if (btnA) {
                    hudA.classList.add('active');
                } else {
                    hudA.classList.remove('active');
                }

                // Visual Indicator for Button Y (Anti-Clockwise)
                if (btnY) {
                    hudY.classList.add('purple-active');
                } else {
                    hudY.classList.remove('purple-active');
                }

                // Rising-edge trigger for Button A: Toggle Clockwise (ON/OFF)
                if (btnA && !lastBtnAState) {
                    c('toggle_cw');
                }
                lastBtnAState = btnA;

                // Rising-edge trigger for Button Y: Toggle Anti-Clockwise (ON/OFF)
                if (btnY && !lastBtnYState) {
                    c('toggle_ccw');
                }
                lastBtnYState = btnY;

                // Rising-edge trigger for Button B: Immediate Stop
                if (btnB && !lastBtnBState) {
                    c('stop');
                }
                lastBtnBState = btnB;

                // Left Joystick handling (Vertical axis = axes[1], Horizontal axis = axes[0])
                const axisY = activeGp.axes.length > 1 ? activeGp.axes[1] : 0;
                const axisX = activeGp.axes.length > 0 ? activeGp.axes[0] : 0;
                
                // D-Pad Up / Down compatibility
                const dpadUp = (activeGp.buttons[12] && activeGp.buttons[12].pressed) || false;
                const dpadDown = (activeGp.buttons[13] && activeGp.buttons[13].pressed) || false;

                const DEADZONE = 0.22;
                let stickDeflection = 0;

                if (Math.abs(axisY) > DEADZONE) {
                    stickDeflection = -axisY; // Pushing up increases speed
                } else if (Math.abs(axisX) > DEADZONE) {
                    stickDeflection = axisX;  // Pushing right increases speed
                }

                hudStick.innerText = "Stick: " + stickDeflection.toFixed(1);

                const now = Date.now();
                if ((Math.abs(stickDeflection) > 0 || dpadUp || dpadDown) && (now - lastStickTime > 90)) {
                    lastStickTime = now;
                    let step = 0;
                    if (dpadUp) step = 5;
                    else if (dpadDown) step = -5;
                    else step = Math.round(stickDeflection * 5);

                    if (step !== 0) {
                        currentMagnitude = Math.min(100, Math.max(20, currentMagnitude + step));
                        updateSpeedDisplay(currentMagnitude);
                        sendSpeed(currentMagnitude);
                    }
                }
            } else {
                dot.classList.remove('active');
                name.innerText = "Controller Disconnected";
                hint.style.display = 'block';
                hudA.classList.remove('active');
                hudY.classList.remove('purple-active');
                hudStick.innerText = "Stick: N/A";
            }

            requestAnimationFrame(pollGamepads);
        }

        window.addEventListener("gamepadconnected", () => {
            console.log("Gamepad connected successfully");
        });

        function scan(){
            document.getElementById('status').innerText = 'Scanning...';
            fetch('/scan').then(r=>r.json()).then(d=>{
                let s = document.getElementById('ssid');
                s.innerHTML = '<option value="">-- Select --</option>';
                d.forEach(n=>{ s.innerHTML += '<option value="'+n+'">'+n+'</option>'; });
                document.getElementById('status').innerText = 'Found ' + d.length + ' networks';
            }).catch(() => document.getElementById('status').innerText = 'Scan Error');
        }
        
        function save(){
            let s = document.getElementById('ssid').value, p = document.getElementById('pass').value;
            if(!s) return alert('Select SSID');
            fetch('/save', { method:'POST', body:JSON.stringify({ssid:s, pass:p}) }).then(()=>{
                alert('Credentials saved! Rebooting...');
            });
        }
        
        function forceAP(){
            if(confirm("Switch to AP Mode and reboot?")) fetch('/switch_to_ap', { method: 'POST' }).then(() => alert('Rebooting...'));
        }
        
        function forceWiFi(){
            if(confirm("Switch to Wi-Fi Mode and reboot?")) fetch('/switch_to_wifi', { method: 'POST' }).then(() => alert('Rebooting...'));
        }

        function loadStatus(){
            fetch('/status').then(r=>r.json()).then(d=>{
                document.getElementById('lastcmd').innerText = d.last_cmd;
                if (!isSliding && d.target_speed !== undefined) {
                    currentMagnitude = d.target_speed;
                    updateSpeedDisplay(currentMagnitude);
                }
            }).catch(()=>{});
        }

        setInterval(loadStatus, 2500);
        window.onload = function() {
            loadStatus();
            requestAnimationFrame(pollGamepads);
        };
    </script>
</body></html>
)raw_html";

static esp_err_t index_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_UI, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t captive_portal_redirect(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t bridge_get_handler(httpd_req_t *req) {
    char buf[64];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char param[32];
        if (httpd_query_key_value(buf, "speed", param, sizeof(param)) == ESP_OK) {
            set_bridge_speed(atoi(param));
            httpd_resp_sendstr(req, "Speed Updated");
            return ESP_OK;
        }
        if (httpd_query_key_value(buf, "cmd", param, sizeof(param)) == ESP_OK) {
            execute_bridge_command(param);
            httpd_resp_sendstr(req, "Command Sent");
            return ESP_OK;
        }
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing cmd or speed parameter");
    return ESP_FAIL;
}

static esp_err_t speed_get_handler(httpd_req_t *req) {
    char buf[64];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char val[32];
        if (httpd_query_key_value(buf, "mag", val, sizeof(val)) == ESP_OK) {
            set_speed_magnitude(atoi(val));
            httpd_resp_sendstr(req, "Magnitude OK");
            return ESP_OK;
        }
        if (httpd_query_key_value(buf, "val", val, sizeof(val)) == ESP_OK ||
            httpd_query_key_value(buf, "speed", val, sizeof(val)) == ESP_OK) {
            set_bridge_speed(atoi(val));
            httpd_resp_sendstr(req, "Speed OK");
            return ESP_OK;
        }
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing val or mag parameter");
    return ESP_FAIL;
}

static esp_err_t status_get_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "last_cmd", last_command_str);
    cJSON_AddNumberToObject(root, "speed", current_speed_pct);
    cJSON_AddNumberToObject(root, "target_speed", target_speed_magnitude);
    cJSON_AddNumberToObject(root, "max_limit", MOTOR_MAX_SAFE_DUTY_PERCENT);

    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));

    free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t scan_get_handler(httpd_req_t *req) {
    wifi_scan_config_t scan_config = {};
    esp_wifi_scan_start(&scan_config, true);
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    wifi_ap_record_t *ap_info = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_count);
    esp_wifi_scan_get_ap_records(&ap_count, ap_info);

    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < ap_count; i++) {
        cJSON_AddItemToArray(root, cJSON_CreateString((char*)ap_info[i].ssid));
    }

    char* json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));

    free(ap_info);
    cJSON_Delete(root);
    free(json_str);
    return ESP_OK;
}

static esp_err_t save_post_handler(httpd_req_t *req) {
    char buf[200];
    int ret, remaining = req->content_len;
    size_t recv_len = (remaining < (int)sizeof(buf)) ? remaining : (int)sizeof(buf);
    if ((ret = httpd_req_recv(req, buf, recv_len)) <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (json) {
        cJSON *s = cJSON_GetObjectItem(json, "ssid");
        cJSON *p = cJSON_GetObjectItem(json, "pass");
        if (s && p) {
            nvs_handle_t my_handle;
            nvs_open("storage", NVS_READWRITE, &my_handle);
            nvs_set_str(my_handle, "wifi_ssid", s->valuestring);
            nvs_set_str(my_handle, "wifi_pass", p->valuestring);
            nvs_set_u8(my_handle, "force_ap", 0);
            nvs_commit(my_handle);
            nvs_close(my_handle);
        }
        cJSON_Delete(json);
        httpd_resp_sendstr(req, "OK");
        xTaskCreate(delayed_reboot_task, "reboot_task", 2048, NULL, 5, NULL);
    }
    return ESP_OK;
}

static esp_err_t switch_ap_post_handler(httpd_req_t *req) {
    nvs_handle_t my_handle;
    nvs_open("storage", NVS_READWRITE, &my_handle);
    nvs_set_u8(my_handle, "force_ap", 1);
    nvs_commit(my_handle);
    nvs_close(my_handle);
    httpd_resp_sendstr(req, "OK");
    xTaskCreate(delayed_reboot_task, "reboot_task", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static esp_err_t switch_wifi_post_handler(httpd_req_t *req) {
    nvs_handle_t my_handle;
    nvs_open("storage", NVS_READWRITE, &my_handle);
    nvs_set_u8(my_handle, "force_ap", 0);
    nvs_commit(my_handle);
    nvs_close(my_handle);
    httpd_resp_sendstr(req, "OK");
    xTaskCreate(delayed_reboot_task, "reboot_task", 2048, NULL, 5, NULL);
    return ESP_OK;
}

void start_web_server() {
    if (server == NULL) {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.max_uri_handlers = 20;
        config.uri_match_fn = httpd_uri_match_wildcard;

        if (httpd_start(&server, &config) == ESP_OK) {
            httpd_uri_t uri_index       = { .uri = "/",               .method = HTTP_GET,  .handler = index_get_handler,       .user_ctx = NULL };
            httpd_uri_t uri_scan        = { .uri = "/scan",           .method = HTTP_GET,  .handler = scan_get_handler,        .user_ctx = NULL };
            httpd_uri_t uri_save        = { .uri = "/save",           .method = HTTP_POST, .handler = save_post_handler,       .user_ctx = NULL };
            httpd_uri_t uri_bridge      = { .uri = "/bridge",         .method = HTTP_GET,  .handler = bridge_get_handler,      .user_ctx = NULL };
            httpd_uri_t uri_speed       = { .uri = "/speed",          .method = HTTP_GET,  .handler = speed_get_handler,       .user_ctx = NULL };
            httpd_uri_t uri_status      = { .uri = "/status",         .method = HTTP_GET,  .handler = status_get_handler,      .user_ctx = NULL };
            httpd_uri_t uri_switch_ap   = { .uri = "/switch_to_ap",   .method = HTTP_POST, .handler = switch_ap_post_handler,   .user_ctx = NULL };
            httpd_uri_t uri_switch_wifi = { .uri = "/switch_to_wifi", .method = HTTP_POST, .handler = switch_wifi_post_handler, .user_ctx = NULL };

            httpd_uri_t uri_cp1         = { .uri = "/generate_204",        .method = HTTP_GET, .handler = captive_portal_redirect, .user_ctx = NULL };
            httpd_uri_t uri_cp2         = { .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = captive_portal_redirect, .user_ctx = NULL };
            httpd_uri_t uri_cp3         = { .uri = "/ncsi.txt",            .method = HTTP_GET, .handler = captive_portal_redirect, .user_ctx = NULL };
            httpd_uri_t uri_fallback    = { .uri = "/*",                   .method = HTTP_GET, .handler = captive_portal_redirect, .user_ctx = NULL };

            httpd_register_uri_handler(server, &uri_index);
            httpd_register_uri_handler(server, &uri_scan);
            httpd_register_uri_handler(server, &uri_save);
            httpd_register_uri_handler(server, &uri_bridge);
            httpd_register_uri_handler(server, &uri_speed);
            httpd_register_uri_handler(server, &uri_status);
            httpd_register_uri_handler(server, &uri_switch_ap);
            httpd_register_uri_handler(server, &uri_switch_wifi);
            httpd_register_uri_handler(server, &uri_cp1);
            httpd_register_uri_handler(server, &uri_cp2);
            httpd_register_uri_handler(server, &uri_cp3);
            httpd_register_uri_handler(server, &uri_fallback);

            ESP_LOGI(TAG, "Web Server started successfully on port %d", config.server_port);
        }
    }
}

void stop_web_server() {
    if (server != NULL) {
        ESP_LOGI(TAG, "Stopping Web Server...");
        httpd_stop(server);
        server = NULL;
    }
}