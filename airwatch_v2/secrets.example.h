// Copy this file to "secrets.h" (same folder as airwatch_v2.ino) and fill it
// in. secrets.h is only on your computer: when you share airwatch_v2.ino,
// your Wi-Fi password does not go with it. Without secrets.h the sketch still
// builds; the web page (home and road) is simply left out. With only the
// WEB_ lines filled in, road access works and home mode is left out.

// Your home network, exactly as it appears (upper / lower case matters).
#define HOME_WIFI_SSID      "MyHomeWiFi"
#define HOME_WIFI_PASSWORD  "my wifi password"

// Optional but recommended: your router's 2.4 GHz address (BSSID). With it,
// someone copying your network name elsewhere cannot make the board try to
// log in to their fake network. Find it in the mark log: the WIFI line with
// your network name, e.g. "WIFI AC:91:9B:12:34:56 ... ssid="MyHomeWiFi"".
// Delete the line to accept any router with that name. A mesh (UniFi etc.)
// has one address per access point: list them all, separated by commas.
// The board joins whichever is strongest.
#define HOME_WIFI_BSSID     "AC:91:9B:12:34:56, AC:91:9B:12:34:57"

// Login for the web page (http://airwatch.local or the IP on the screen),
// also used for wireless firmware updates from arduino-cli.
#define WEB_USER            "admin"
#define WEB_PASSWORD        "change me please"

// Road access (hold the mark button 3 s within 15 s of power-up): the board
// makes its own Wi-Fi network. WPA2 needs a password of 8+ characters; if
// this line is deleted, WEB_PASSWORD is used (so make that 8+ characters).
#define ROAD_AP_PASSWORD    "road password"
// #define ROAD_AP_SSID     "airwatch"     // network name, if you want another

// Optional: send the logs home on arrival. Each time the board joins your
// home Wi-Fi it sends the mark log and alert journal to the receiver in the
// repo's server/ folder, and clears them once the receiver confirms an exact
// copy. Use the address of the machine running the receiver, and the same
// token as its AIRWATCH_TOKEN. Delete these lines to turn it off.
// #define UPLOAD_URL       "http://192.168.1.50:8077/upload"
// #define UPLOAD_TOKEN     "a long random string"
// With the airwatch-builder container running too, the board also installs
// the newest firmware from the same server on arrival. To stop that (e.g.
// while testing your own builds), uncomment:
// #define AUTO_UPDATE      0
