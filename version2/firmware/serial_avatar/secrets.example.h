// secrets.example.h — checked-in template. Copy to secrets.h and fill in
// real values. The real secrets.h is gitignored (see /.gitignore).

#pragma once

#define WIFI_SSID    "your-hotspot-name"
#define WIFI_PASS    "your-hotspot-password"

#define SERVER_HOST  "192.168.x.x"      // LAN IP of laptop running server.py
#define SERVER_PORT  8000

// 1 for station A, 2 for station B. Flash each board with its own number.
#define ME_STICKER   1
