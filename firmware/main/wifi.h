#ifndef WIFI_H
#define WIFI_H
#include "host/ble_hs.h" 

int cmd_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg);


#endif