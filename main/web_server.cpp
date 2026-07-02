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
   HTML UI PAYLOAD
   ============================================== */
const char HTML_UI[] = R"raw_html(
<!DOCTYPE html><html><head><meta charset="utf-8"><title>Drawbridge Controller</title><meta name="viewport" content="width=device-width, initial-scale=1">
<style>
    :root { --primary: #0ea5e9; --bg: #0f172a; --card: #1e293b; --text: #f1f5f9; }
    body { font-family: -apple-system, sans-serif; background: var(--bg); color: var(--text); padding: 15px; display: flex; flex-direction: column; align-items: center; margin:0; }
    .container { width: 100%; max-width: 500px; }
    .card { background: var(--card); padding: 25px; border-radius: 20px; border: 1px solid #334155; text-align: center; margin-bottom: 20px; }
    h2 { color: var(--primary); margin-top: 0; }
    input, select { width: 100%; padding: 12px; margin: 8px 0 20px; border-radius: 10px; border: 1px solid #475569; background: #0f172a; color: white; box-sizing: border-box; }
    button { width: 100%; padding: 15px; border: none; border-radius: 12px; font-weight: bold; cursor: pointer; color: white; margin-top:10px; transition: transform 0.1s; }
    button:active { transform: scale(0.96); opacity: 0.9; }
    .btn-green { background: #10b981; }
    .btn-red { background: #ef4444; }
    .btn-gray { background: #475569; }
    .status-bar { padding: 12px; border-radius: 10px; font-weight: bold; text-align: center; font-size: 0.85rem; border: 1px solid; text-transform: uppercase; background: #172554; color: #93c5fd; border-color: #3b82f6; margin-bottom:15px; }
    .text-sm { font-size: 0.85rem; color: #94a3b8; margin-bottom:10px; }
</style>
</head>
<body>
    <div class="container">
        <div class="status-bar">DRAWBRIDGE DASHBOARD</div>

        <!-- Controls Card -->
        <div class="card">
            <h2>Control Bridge</h2>
            <div class="text-sm"><b>Current State:</b> <span id="lastcmd" style="color:#3b82f6;">Loading...</span></div>
            
            <button class="btn-green" onclick="c('open')">OPEN BRIDGE</button>
            <button class="btn-red" onclick="c('close')">CLOSE BRIDGE</button>
            <button class="btn-gray" onclick="c('stop')">STOP</button>
            
            <p id="cstat" class="text-sm" style="margin-top:15px;"></p>
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
        function c(cmd){
            document.getElementById('cstat').innerText = 'Sending...';
            fetch('/bridge?cmd='+cmd).then(r=>r.text()).then(t=>{
                document.getElementById('cstat').innerText = 'Status: ' + t;
                setTimeout(loadStatus, 600);
            });
        }
        
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
            });
        }

        setInterval(loadStatus, 3000);
        window.onload = loadStatus;
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
    char buf[50];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char param[20];
        if (httpd_query_key_value(buf, "cmd", param, sizeof(param)) == ESP_OK) {
            execute_bridge_command(param);
            httpd_resp_sendstr(req, "Command Sent");
            return ESP_OK;
        }
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing cmd parameter");
    return ESP_FAIL;
}

static esp_err_t status_get_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "last_cmd", last_command_str);
    
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
    for(int i = 0; i < ap_count; i++) cJSON_AddItemToArray(root, cJSON_CreateString((char*)ap_info[i].ssid));
    
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
    if(json) {
        cJSON *s = cJSON_GetObjectItem(json, "ssid");
        cJSON *p = cJSON_GetObjectItem(json, "pass");
        if(s && p) {
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
        config.max_uri_handlers = 15;
        config.uri_match_fn = httpd_uri_match_wildcard;
        
        if (httpd_start(&server, &config) == ESP_OK) {
            httpd_uri_t uri_index  = { .uri = "/",       .method = HTTP_GET,  .handler = index_get_handler,  .user_ctx = NULL };
            httpd_uri_t uri_scan   = { .uri = "/scan",   .method = HTTP_GET,  .handler = scan_get_handler,   .user_ctx = NULL };
            httpd_uri_t uri_save   = { .uri = "/save",   .method = HTTP_POST, .handler = save_post_handler,  .user_ctx = NULL };
            httpd_uri_t uri_bridge = { .uri = "/bridge", .method = HTTP_GET,  .handler = bridge_get_handler, .user_ctx = NULL };
            httpd_uri_t uri_status = { .uri = "/status", .method = HTTP_GET,  .handler = status_get_handler, .user_ctx = NULL };
            httpd_uri_t uri_switch_ap   = { .uri = "/switch_to_ap",   .method = HTTP_POST, .handler = switch_ap_post_handler,   .user_ctx = NULL };
            httpd_uri_t uri_switch_wifi = { .uri = "/switch_to_wifi", .method = HTTP_POST, .handler = switch_wifi_post_handler, .user_ctx = NULL };
            
            httpd_uri_t uri_cp1 = { .uri = "/generate_204", .method = HTTP_GET, .handler = captive_portal_redirect, .user_ctx = NULL };
            httpd_uri_t uri_cp2 = { .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = captive_portal_redirect, .user_ctx = NULL };
            httpd_uri_t uri_cp3 = { .uri = "/ncsi.txt", .method = HTTP_GET, .handler = captive_portal_redirect, .user_ctx = NULL };
            httpd_uri_t uri_fallback = { .uri = "/*", .method = HTTP_GET, .handler = captive_portal_redirect, .user_ctx = NULL };
            
            httpd_register_uri_handler(server, &uri_index);
            httpd_register_uri_handler(server, &uri_scan);
            httpd_register_uri_handler(server, &uri_save);
            httpd_register_uri_handler(server, &uri_bridge);
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