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

#define TAG "BLE"
#define DEVICE_NAME "DnFA-C5"
#define LED_GPIO 2

static uint16_t cmd_chr_val_handle;
static uint16_t tlm_chr_val_handle;      
static uint8_t cmd_chr_data[32] = {0};
static uint8_t s_own_addr_type = 0;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE; 
static bool s_tlm_subscribed = false;   

// Service: 68ABF545-7BC8-49F0-BCD0-37E32B52E0AB
static const ble_uuid128_t SVC_UUID = BLE_UUID128_INIT(
    0xAB, 0xE0, 0x52, 0x2B, 0xE3, 0x37, 0xD0, 0xBC,
    0xF0, 0x49, 0xC8, 0x7B, 0x45, 0xF5, 0xAB, 0x68
);

// Control / Command: 68ABF545-7BC8-49F0-BCD0-37E32B52E0AC (phone -> ESP32)
static const ble_uuid128_t CMD_CHR_UUID = BLE_UUID128_INIT(
    0xAC, 0xE0, 0x52, 0x2B, 0xE3, 0x37, 0xD0, 0xBC,
    0xF0, 0x49, 0xC8, 0x7B, 0x45, 0xF5, 0xAB, 0x68
);

// Telemetry: 68ABF545-7BC8-49F0-BCD0-37E32B52E0AD (ESP32 -> phone)   // NEW
static const ble_uuid128_t TLM_CHR_UUID = BLE_UUID128_INIT(
    0xAD, 0xE0, 0x52, 0x2B, 0xE3, 0x37, 0xD0, 0xBC,
    0xF0, 0x49, 0xC8, 0x7B, 0x45, 0xF5, 0xAB, 0x68
);

static void start_advertising(void);
void ble_send_telemetry(const char *msg);

// tut for noobs like me: (to be continued... maybe.)
// how does this ble thing work???
//
// identification
//      static const ble_uuid128_t SVC_UUID = BLE_UUID128_INIT(...);
//      ^^ service uuid, unique id to know if this is the right ble device
//         to communicate to.
//
// this file now defines TWO characteristics under that service:
//   - CMD_CHR_UUID (0xAC): phone writes commands to us   (WRITE)
//   - TLM_CHR_UUID (0xAD): we push telemetry to the phone (NOTIFY)
// The iOS app requires BOTH to exist before it treats us as the real
// device, so if you only expose one, iOS will reject the connection.
//
// initalization starts at app_main
//      rc = nimble_port_init();   - initalize nimble stack
//      ble_svc_gap_init();        - initalize device name, appearance
//      ble_svc_gatt_init();       - initialize service discovery
//      ble_svc_gap_device_name_set("DnFA-C5");         - set device name
//      nimble_port_freertos_init(bleprph_host_task);   - start ble host stack

static int
cmd_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                   struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len == 0) return 0;
    if (len > sizeof(cmd_chr_data) - 1) len = sizeof(cmd_chr_data) - 1;

    if (ble_hs_mbuf_to_flat(ctxt->om, cmd_chr_data, len, NULL) != 0)
        return BLE_ATT_ERR_UNLIKELY;

    cmd_chr_data[len] = '\0';

    ESP_LOGI(TAG, "Received command: %s", (char *)cmd_chr_data);

    if (strcmp((char *)cmd_chr_data, "test") == 0) {
        ESP_LOGI(TAG, "Got test command, replying");
        ble_send_telemetry("worked");
    } else {
        ESP_LOGW(TAG, "Unknown command: %s", (char *)cmd_chr_data);
    }
    return 0;
}

// NEW: telemetry characteristic access callback. Notify-only chars still
// need an access_cb registered (NimBLE requires a non-NULL fn pointer);
// reads/writes to it aren't expected, so just reject them politely.
static int
tlm_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                   struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    (void)ctxt;
    return BLE_ATT_ERR_UNLIKELY;
}

// NEW: helper to push a telemetry string to the connected phone.
// Call this from wherever your sensor/status loop lives.
void ble_send_telemetry(const char *msg)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || !s_tlm_subscribed) {
        return; // nobody connected / not subscribed yet
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(msg, strlen(msg));
    if (!om) {
        ESP_LOGE(TAG, "telemetry mbuf alloc failed");
        return;
    }
    int rc = ble_gatts_notify_custom(s_conn_handle, tlm_chr_val_handle, om);
    if (rc != 0) {
        ESP_LOGE(TAG, "notify failed; rc=%d", rc);
    }
}

static const struct ble_gatt_chr_def s_chrs[] = {
    {
        .uuid = &CMD_CHR_UUID.u,
        .access_cb = cmd_chr_access_cb,
        .val_handle = &cmd_chr_val_handle,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {
        // NEW: telemetry characteristic, matches iOS's telemetryCharUUID
        .uuid = &TLM_CHR_UUID.u,
        .access_cb = tlm_chr_access_cb,
        .val_handle = &tlm_chr_val_handle,
        .flags = BLE_GATT_CHR_F_NOTIFY,
    },
    { 0 },
};

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &SVC_UUID.u,
        .characteristics = s_chrs,
    },
    { 0 },
};

// NEW: GAP event handler. Previously ble_gap_adv_start() was passed a NULL
// callback, so the firmware never knew about connects/disconnects/subscribes
// and could never restart advertising or send notifications.
static int
gap_event_handler(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "Connected; conn_handle=%d", s_conn_handle);
        } else {
            ESP_LOGE(TAG, "Connect failed; status=%d", event->connect.status);
            start_advertising();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected; reason=%d", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_tlm_subscribed = false;
        start_advertising(); // resume advertising so the phone can reconnect
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == tlm_chr_val_handle) {
            s_tlm_subscribed = event->subscribe.cur_notify;
            ESP_LOGI(TAG, "Telemetry subscribe state: %d", s_tlm_subscribed);
        }
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU update: conn_handle=%d mtu=%d",
                 event->mtu.conn_handle, event->mtu.value);
        return 0;

    default:
        return 0;
    }
}

static void
start_advertising(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&SVC_UUID;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "error setting advertisement data; rc=%d", rc);
        return;
    }

    struct ble_hs_adv_fields rsp = {0};
    rsp.name = (uint8_t *)DEVICE_NAME;
    rsp.name_len = strlen(DEVICE_NAME);
    rsp.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0) {
        ESP_LOGE(TAG, "error setting scan response; rc=%d", rc);
        return;
    }

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = BLE_GAP_ADV_FAST_INTERVAL1_MIN;
    adv_params.itvl_max = BLE_GAP_ADV_FAST_INTERVAL1_MAX;

    // pass gap_event_handler instead of NULL so connect/disconnect/
    // subscribe events actually reach us.
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event_handler, NULL);
    if (rc == 0 || rc == BLE_HS_EALREADY) {
        ESP_LOGI(TAG, "Advertising as %s", DEVICE_NAME);
    } else {
        ESP_LOGE(TAG, "error enabling advertisement; rc=%d", rc);
    }
}

static void
bleprph_on_reset(int reason)
{
    ESP_LOGE(TAG, "Resetting state; reason=%d", reason);
}

static void
bleprph_on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed; rc=%d", rc);
        return;
    }

    ESP_LOGI(TAG, "BLE synced, starting advertising");
    start_advertising();
}

static void
bleprph_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void
app_main(void)
{
    int rc;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    rc = nimble_port_init();
    ESP_LOGI(TAG, "nimble_port_init rc=%d", rc);
    if (rc != 0) return;

    ble_hs_cfg.reset_cb = bleprph_on_reset;
    ble_hs_cfg.sync_cb = bleprph_on_sync;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed; rc=%d", rc);
        return;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed; rc=%d", rc);
        return;
    }

    ble_svc_gap_device_name_set(DEVICE_NAME);
    ble_att_set_preferred_mtu(247);

    nimble_port_freertos_init(bleprph_host_task);
    ESP_LOGI(TAG, "Host task started, waiting for sync");
}