// Copy this file to "secrets.h" (same folder) and fill in your values.
// secrets.h is listed in .gitignore, so it will never be committed.
#pragma once

#define SECRET_WIFI_SSID "YOUR_WIFI_NAME"
#define SECRET_WIFI_PASS "YOUR_WIFI_PASSWORD"

// Network settings. Set USE_STATIC_IP to 0 to use DHCP instead
// (then read the IP from the Serial Monitor at boot).
#define USE_STATIC_IP     1
#define PLC_IP_ADDR       192, 168, 1, 50
#define PLC_GATEWAY_ADDR  192, 168, 1, 1
#define PLC_SUBNET_MASK   255, 255, 255, 0
