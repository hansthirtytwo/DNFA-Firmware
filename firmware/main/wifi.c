#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_att.h"
#include "main.h"


#define TAG "WIFI"
// too lazy, copy pasted imports




// deauth var




static const char *getSecurity(wifi_auth_mode_t authmode)
{
    switch (authmode) {
        case WIFI_AUTH_OPEN:            return "OPEN";
        case WIFI_AUTH_WEP:             return "WEP";
        case WIFI_AUTH_WPA_PSK:         return "WPA";
        case WIFI_AUTH_WPA2_PSK:        return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ENT";
        case WIFI_AUTH_WPA3_PSK:        return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3";
        case WIFI_AUTH_WAPI_PSK:        return "WAPI";
        default:                        return "UNKNOWN";
    }
}

// WiFi SSIDs are arbitrary byte arrays. Dumping them raw into JSON produces
// invalid UTF-8 that iOS String(data:encoding:.utf8) rejects.  This escapes
// every byte outside printable ASCII (0x20-0x7E) plus " and \ into \uXXXX
// so the JSON is always pure ASCII.
static int json_escape_ssid(char *dst, size_t dst_size,
                            const uint8_t *ssid, size_t ssid_len)
{
    size_t j = 0;
    for (size_t i = 0; i < ssid_len && j + 6 < dst_size; i++) {
        uint8_t c = ssid[i];
        if (c == '"') {
            dst[j++] = '\\'; dst[j++] = '"';
        } else if (c == '\\') {
            dst[j++] = '\\'; dst[j++] = '\\';
        } else if (c >= 0x20 && c <= 0x7E) {
            dst[j++] = (char)c;
        } else {
            j += snprintf(dst + j, dst_size - j, "\\u%04x", c);
        }
    }
    dst[j] = '\0';
    return (int)j;
}




static void scan_wifi_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "scan_wifi_task: starting WiFi scan");

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true
    };

    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Scan failed: %s", esp_err_to_name(err));
        ble_send_telemetry("{\"rows\":[]}");
        vTaskDelay(pdMS_TO_TICKS(20));
        ble_send_telemetry("__END__");
        vTaskDelete(NULL);
        return;
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);

    if (ap_count == 0) {
        ble_send_telemetry("{\"rows\":[]}");
        vTaskDelay(pdMS_TO_TICKS(20));
        ble_send_telemetry("__END__");
        vTaskDelete(NULL);
        return;
    }

    wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (!ap_records) {
        ESP_LOGE(TAG, "malloc failed");
        ble_send_telemetry("{\"rows\":[]}");
        vTaskDelay(pdMS_TO_TICKS(20));
        ble_send_telemetry("__END__");
        vTaskDelete(NULL);
        return;
    }

    esp_wifi_scan_get_ap_records(&ap_count, ap_records);

    static char scan_json[1024];
    size_t offset = 0;
    offset += snprintf(scan_json + offset, sizeof(scan_json) - offset, "{\"rows\":[");

    bool first = true;
    for (int j = 0; j < ap_count; j++) {
        char safe_ssid[256];
        json_escape_ssid(safe_ssid, sizeof(safe_ssid),
                         ap_records[j].ssid, strlen((char *)ap_records[j].ssid));

         // new lesson: always make mac addr into string
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 ap_records[j].bssid[0], ap_records[j].bssid[1], ap_records[j].bssid[2],
                 ap_records[j].bssid[3], ap_records[j].bssid[4], ap_records[j].bssid[5]);

        char entry[384];
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", ap_records[j].primary);

        int len = snprintf(entry, sizeof(entry),
                            "%s{\"ssid\":\"%s\",\"rssi\":%d,\"sec\":\"%s\",\"channel\":\"%s\",\"mac\":\"%s\"}",
                            first ? "" : ",",
                            safe_ssid,
                            ap_records[j].rssi,
                            getSecurity(ap_records[j].authmode),
                            buf,
                            mac_str); // <--- FIXED: Use the formatted string

        if (offset + len < sizeof(scan_json) - 1) {
            offset += snprintf(scan_json + offset, sizeof(scan_json) - offset, "%s", entry);
            first = false;
        } else {
            ESP_LOGW(TAG, "scan_json buffer full, truncating");
            break;
        }
    }

    snprintf(scan_json + offset, sizeof(scan_json) - offset, "]}");
    free(ap_records);

    ESP_LOGI(TAG, "Sending scan results (%d bytes total)", (int)strlen(scan_json));

    // BLE notifications are single ATT PDUs — max payload = ATT_MTU - 3.
    // Use the negotiated MTU so chunks always fit regardless of peer.
    uint16_t mtu = ble_att_mtu(ble_get_conn_handle());
    size_t CHUNK_SIZE = (mtu > 6) ? (mtu - 3) : 20;
    ESP_LOGI(TAG, "Negotiated MTU=%d, using chunk size=%d", (int)mtu, (int)CHUNK_SIZE);

    size_t json_len = strlen(scan_json);
    size_t sent_bytes = 0;

    while (sent_bytes < json_len) {
        char chunk[CHUNK_SIZE + 1];
        size_t bytes_to_send = json_len - sent_bytes;

        if (bytes_to_send > CHUNK_SIZE) {
            bytes_to_send = CHUNK_SIZE;
        }

        memcpy(chunk, scan_json + sent_bytes, bytes_to_send);
        chunk[bytes_to_send] = '\0';

        ble_send_telemetry(chunk);
        sent_bytes += bytes_to_send;
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    vTaskDelay(pdMS_TO_TICKS(20));
    ble_send_telemetry("__END__");

    vTaskDelete(NULL);
}


int
cmd_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                   struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len == 0) return 0;
    if (len > CMD_CHR_BUF_SIZE - 1) len = CMD_CHR_BUF_SIZE - 1;

    if (ble_hs_mbuf_to_flat(ctxt->om, get_cmd_chr_data(), len, NULL) != 0)
        return BLE_ATT_ERR_UNLIKELY;

    get_cmd_chr_data()[len] = '\0';

    ESP_LOGI(TAG, "Received command: %s", (char *)get_cmd_chr_data());

    if (strcmp((char *)get_cmd_chr_data(), "test") == 0) {
        ESP_LOGI(TAG, "Got test command, replying");
        ble_send_telemetry("worked");
    } else if (strcmp((char *)get_cmd_chr_data(), "scan_wifi") == 0) {
        ESP_LOGI(TAG, "Got scan_wifi command");
        xTaskCreate(scan_wifi_task, "wifi_scan", 4096, NULL, 5, NULL);
        return 0;

        
    } else {
        ESP_LOGW(TAG, "Unknown command: %s", (char *)get_cmd_chr_data());
    }
    return 0;
}






// deauth

