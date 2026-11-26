/*
 * AIRWATCH v2 - passive 2.4 GHz Wi-Fi + Bluetooth LE monitor
 * ==========================================================
 * Listens only. Never transmits, associates, or sends BLE scan requests.
 *
 * BOARDS
 *   Seeed XIAO ESP32-C3 or XIAO ESP32-S3 (same wiring on both: buzzer D2,
 *   NeoPixel D3, OLED SDA D4 / SCL D5). Neither board has an antenna switch:
 *   the U.FL connector is the only antenna, so nothing needs enabling here.
 *
 * LIBRARIES (Arduino Library Manager)
 *   Adafruit NeoPixel, Adafruit GFX Library, Adafruit SSD1306,
 *   NimBLE-Arduino 2.x (by h2zero)
 *
 * ARDUINO IDE SETTINGS
 *   Board:  XIAO_ESP32C3  or  XIAO_ESP32S3   (the chip is detected at build
 *   time and the right GPIO numbers are picked automatically)
 *   Built against esp32 core 3.3.12 + NimBLE-Arduino 2.5.x.
 *   Storage layout: partitions.csv next to this file (3.9 MB for marks,
 *     two 2 MB firmware slots) is used automatically. The notes below are for
 *     building without it.
 *   Partition scheme:
 *     C3: "No OTA (2MB APP/2MB SPIFFS)"  (arduino-cli: add
 *         :PartitionScheme=no_ota to the board name). The build does not
 *         comfortably fit the C3's default 1.2 MB app slot, and this layout
 *         gives the mark log about 2 MB.
 *     S3: leave the default (3MB APP/1.5MB SPIFFS). It has no "No OTA"
 *         option and does not need one.
 *   Keep the same scheme on every flash: changing it wipes the mark log.
 *
 * WHAT YOU HEAR  (works with an active buzzer; BUZZER_IS_PASSIVE adds pitch)
 *   short-short        once at power-on: buzzer works, monitor is running
 *   short-short-LONG   new HIGH target  (police-specific signature, direct)
 *   short-short        new LOW target   (broad vendor / in-vehicle router /
 *                                        traffic addressed to a HIGH prefix)
 *   single tick        new ECHO / MINOR target (traffic addressed to a LOW
 *                      prefix, or a drone: Remote ID broadcast or Skydio)
 *   tick-tick ...      heartbeat while a HIGH target is in range
 *   tick ...           heartbeat while a LOW target is in range
 *                      heartbeat rate = proximity: ~2 s apart when weak,
 *                      ~4 per second when strong
 *   (silence)          nothing in range; it only beeps for a target
 *
 *   one LONG beep      mark saved: "I see police"      (tap the button)
 *   two LONG beeps     mark saved: "nothing is there"  (hold ~1 second)
 *   falling 3 beeps    alert sounds muted     (three quick taps)
 *   rising 3 beeps     alert sounds back on   (three quick taps again)
 *
 * GPS (optional)  ATGM336H, NEO-6M, NEO-M8N, GT-U7: anything sending NMEA at
 *   9600 baud. Wire GPS TX -> D7, VCC -> 3V3, GND -> GND (GPS RX stays
 *   unconnected). With a fix, marks and the alert journal get the real date
 *   and time (UTC; the web page shows local time) and where you were; each
 *   alerted device remembers where its signal was strongest. No GPS wired =
 *   everything works as before.
 *
 * ALERT JOURNAL
 *   Every alert (anything that beeped or ticked) is saved as one line when
 *   it ends: when, what, strongest signal, how long, where. It also carries
 *   tl= (its strongest signal in each second, up to 3 min) and from=/to=
 *   (where it was first and last heard), which the log server charts. No button
 *   needed. It is capped at 256 KB (about 1,400 alerts); the oldest half is
 *   dropped when full, so it never crowds out marks. 'j' prints it; the web
 *   page shows it under Marks -> Alert journal. ERASE wipes it with the marks.
 *
 * MARK BUTTON  (the web page's "?" has the same list)
 *   tap                  mark "police are there"   (saved ~0.4 s after release)
 *   hold ~1 s            mark "nothing is there"
 *   3 quick taps         mute / unmute alert sounds until the power is cut
 *                        (marks still beep; screen shows a crossed speaker)
 *   hold 3 s in the first 15 s after power-up: road access (see below)
 *   Wire a push button between D1 and GND, or use the board's own BOOT
 *   button. Each press saves everything heard in the last 30 seconds -
 *   matched or not, Wi-Fi and BLE - plus whatever was alerting, to the
 *   board's flash. Anything that alerted in the last 90 seconds is listed
 *   too (EARLIER lines), so a late press still records what the beep was.
 *   Marks survive power loss.
 *   Each line carries what is needed to build a new signature later: full
 *   network name, WPS maker / model, the vendor codes in the beacon (vie=),
 *   who the device was talking to (peer=), how long it was in range
 *   (first=), and for Bluetooth the complete raw advert (adv=).
 *   Wi-Fi lines also list up to 6 network names the device searched for
 *   (probes="a|b"): a cruiser's laptop or radio looking for its agency's
 *   networks is a strong clue even when its router is off.
 *   BACKGROUND SAMPLES: every 5 min while driving, a silent snapshot (same
 *   lines, header #SAMPLE) goes to /samples.log for the log server's
 *   signature finder to compare police sightings against.
 *   Phone-type Bluetooth (random-address Apple / Microsoft / Samsung /
 *   Google-Android adverts that match no signature) is left out to keep
 *   marks readable.
 *     tap          = "I see police"      (saw=POLICE in the log)
 *     hold ~1 s    = "nothing is there"  (saw=NOTHING in the log)
 *   The mark also records by itself whether the detector was alerting
 *   (type=DURING_ALERT, with what it was alerting on) or silent
 *   (type=NO_ALERT). Together: POLICE + NO_ALERT = missed, NOTHING +
 *   DURING_ALERT = false alarm, POLICE + DURING_ALERT = confirmed hit.
 *   Pressing again within 10 seconds saves a short follow-up that adds
 *   only what is new or clearly stronger since the previous mark; nothing
 *   already saved is repeated or overwritten. (So a fumbled tap can be
 *   corrected by holding straight after: both are kept, the later one counts.)
 *   To read marks back: plug into a computer, open the serial monitor,
 *   type l and press Enter.
 *
 * NOT ALERTED ON
 *   Fire / EMS / transit / school fleets. A router whose network name
 *   matches ignoredSsidTargets is silenced, along with its sibling radios
 *   and the devices talking to it. A police-specific (HIGH) signature
 *   always wins over the ignore list.
 *
 * QUIET TARGETS
 *   A Cradlepoint still using its factory network name for an E-series
 *   branch router or an IBR200 kiosk gateway ("E110-dbf": model, dash,
 *   three hex digits; the models are listed in quietSsidTargets) is silent:
 *   no sound, LED or screen alert. It is still saved in marks (tier=QUIET)
 *   and in the alert journal, for review. Its sibling radios are treated
 *   the same. Any police-specific (HIGH) signature on the same
 *   device lifts the cap. Vehicle models such as IBR900 are not affected.
 *
 * HOME WI-FI MAINTENANCE MODE  (needs secrets.h - copy secrets.example.h)
 *   When the board has heard your home network for HOME_SETTLE_MS (3 s)
 *   it stops Wi-Fi listening, joins your network and serves a web page at
 *   http://airwatch.local (or the IP shown on screen): status, the mark
 *   log (view, filter, download, erase), matched devices, and wireless
 *   firmware updates. Bluetooth detection and the mark button keep working.
 *   It reboots back into passive listening when the home network is out of
 *   reach or very weak for about 5 s (you drove off; a link that drops and
 *   comes back counts as leaving unless it stays up 10 s), as soon as the
 *   GPS shows you driving (10+ mph for 3 s), when a join has not worked
 *   within 20 s, after MAINT_IDLE_MS (15 min) without a page request, or when
 *   you press Exit on the page. With the GPS showing you driving it does not
 *   enter home mode at all.
 *   After any of these (or a crash) it will not open the page again by
 *   itself until it has not heard your home network for HOME_RELEASE_MS
 *   (5 min), i.e. you drove away and came back. A power cycle also re-arms
 *   it.
 *   With UPLOAD_URL / UPLOAD_TOKEN in secrets.h it first sends the mark log
 *   and alert journal to the receiver in server/ and clears them once the
 *   receiver confirms an identical copy (README: Sending logs home).
 *   HOME_WIFI_BSSID may list several access points (a mesh), separated by
 *   commas; it joins whichever of them is strongest.
 *   The phone must be able to reach the board: on a UniFi "IoT" network
 *   with client isolation, or a phone on a different VLAN, the page will
 *   not open. The console logs every page request ("[WEB] ..."), so no
 *   lines at all means the requests never reached the board.
 *   This is the only time the board transmits, and it does so at reduced
 *   power (WEB_TX_POWER_*): full power can brown out a weak USB supply.
 *
 * RESTARTS AND CRASHES
 *   Every boot reports why the board last restarted (console, Status page,
 *   'r' for the history). After a crash it also shows where in the code it
 *   happened (pc= / bt= addresses: send those plus the build's .elf file
 *   for decoding). "power dipped (brownout)" means the supply sagged: add a
 *   470 uF capacitor across 5V and GND at the XIAO, or use a better cable /
 *   charger.
 *   On the page you can also add a note to any mark, or correct a fumbled
 *   tap / hold ("police were there" / "nothing was there"). These are saved
 *   as #NOTE lines in the mark log, so the serial 'l' dump shows them too.
 *
 * ROAD ACCESS (needs WEB_USER / WEB_PASSWORD in secrets.h)
 *   Within 15 s of power-up, press and hold the mark button for 3 s (until
 *   it beeps three times; the screen shows HOLD=WEB during that window).
 *   The board makes its own Wi-Fi network "airwatch" (password: ROAD_AP_
 *   PASSWORD, or WEB_PASSWORD if that is 8+ characters). Join it from the
 *   phone: Android shows "Sign in to network" - tap it and the page opens
 *   (or open http://192.168.4.1 in Chrome; if that hangs, Android is sending
 *   it over mobile data: tap "Stay connected" on the no-internet prompt or
 *   turn mobile data off for a moment). No web login on the road: the Wi-Fi
 *   password already keeps others out. Same page as at home. It goes back to listening on
 *   Exit or after 15 min idle. Use the D1 button or press BOOT only AFTER
 *   power is on: BOOT held while power is applied starts the chip's own
 *   USB flashing mode instead.
 *
 *   Wireless upload from the computer instead of USB:
 *     arduino-cli upload -p <IP on screen> --protocol network \
 *       --fqbn esp32:esp32:XIAO_ESP32S3:PSRAM=opi ~/airwatch_v2
 *   (asks for WEB_PASSWORD; add --upload-field password=... to skip that)
 *
 * SERIAL COMMANDS (115200, type the command then press Enter)
 *   l           print the saved mark log
 *   j           print the alert journal
 *   r           restart / crash history
 *   ERASE       erase the mark log (all caps)
 *   s           per-signature hit counters      d   dump tracked devices
 *   t           turn the 2-second performance telemetry on / off (off at boot)
 *   0/1/2/3     channel dwell 150 (experimental) / 250 / 500 / 1000 ms
 *   w / x       enter / leave home maintenance mode now (for testing)
 *   a           start road access (own Wi-Fi network) now
 *   m           mute / unmute alert sounds
 *   led         LED test: red, green, blue, white, then the alert colours
 *   h           help
 *
 * SCREEN (large text, two rows)
 *   Idle, top row:     W = Wi-Fi devices and B = Bluetooth devices heard in
 *                      the last 30 seconds. Updates every second; if either
 *                      sits at 0 that radio (or the antenna) has a problem.
 *                      "DROP n" replaces it for 30 s after frames were
 *                      lost to a full queue (e.g. during a flash write).
 *   Idle, bottom row:  the most recent alert and how long ago it ended
 *                      ("MAYBE 3m"), for 15 minutes. Otherwise M = marks
 *                      saved this drive, L = how full the mark storage is;
 *                      offload and ERASE before 100%.
 *   Alert, top row:    POLICE / MAYBE / MINOR, then signal strength in dBm
 *                      (closer to 0 = nearer; "echo" = heard only indirectly)
 *   Alert, small rows: what matched, then W<channel> or BLE and x<targets>
 */

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_system.h>
#include <esp_err.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <string.h>
#include <ctype.h>
#include <algorithm>
#include <esp_core_dump.h>

// Home Wi-Fi details live in secrets.h, next to this file, so they are never
// part of this sketch when it is shared. No secrets.h = no maintenance mode.
#if __has_include("secrets.h")
  #include "secrets.h"
#endif
#if defined(WEB_USER) && defined(WEB_PASSWORD)
  #define WEB_UI 1          // web page: road access always, home mode if below
#else
  #define WEB_UI 0
#endif
#if WEB_UI && defined(HOME_WIFI_SSID) && defined(HOME_WIFI_PASSWORD)
  #define HOME_SYNC 1
#else
  #define HOME_SYNC 0
#endif
#ifndef ROAD_AP_SSID
  #define ROAD_AP_SSID "airwatch"
#endif
#if WEB_UI && !defined(ROAD_AP_PASSWORD)
  #define ROAD_AP_PASSWORD WEB_PASSWORD
#endif
#if WEB_UI
  #include <WebServer.h>
  #include <DNSServer.h>
  #include <ESPmDNS.h>
  #include <ArduinoOTA.h>
  #include <Update.h>
#endif

// ============================================================
// BUILD OPTIONS
// ============================================================

// 1 = also scan Bluetooth LE advertisements (Axon body cams, TASERs, holster
//     sensors, etc.). Needs the NimBLE-Arduino 2.x library.
#define ENABLE_BLE_SCAN        1

// Set to 0 to isolate GPIO load / brownout transients from buzzer coil
#define ENABLE_BUZZER_HARDWARE 1

// 0 = active buzzer (beeps when the pin is HIGH; what v1 assumed)
// 1 = passive piezo (driven with a tone; adds a different pitch per tier)
#define BUZZER_IS_PASSIVE      0

// 1 = mark button + persistent log in the board's flash (LittleFS)
#define ENABLE_MARK_LOG        1

// 1 = read a GPS module on D7 (harmless when none is wired)
#define ENABLE_GPS             1

#if ENABLE_BLE_SCAN
  #include <NimBLEDevice.h>
#endif
#if ENABLE_MARK_LOG
  #include <FS.h>
  #include <LittleFS.h>
#endif
// Upload on arrival: with UPLOAD_URL in secrets.h, the board sends its mark
// log and alert journal to the receiver in server/ each time it joins the
// home Wi-Fi, and deletes them once the receiver confirms a matching copy.
#if HOME_SYNC && ENABLE_MARK_LOG && defined(UPLOAD_URL)
  #define AUTO_UPLOAD 1
  #include <HTTPClient.h>
  #ifndef UPLOAD_TOKEN
    #define UPLOAD_TOKEN ""
  #endif
#else
  #define AUTO_UPLOAD 0
#endif

// ============================================================
// HARDWARE PIN DEFINITIONS
// ============================================================
// The XIAO variants declare D2..D5 as constants, not macros, so they cannot
// be tested with #ifdef. Select raw GPIO numbers by chip instead.
#if defined(AIRWATCH_CUSTOM_PINS)
  // define BUZZER_PIN / LED_PIN / I2C_SDA / I2C_SCL yourself before this point
#elif CONFIG_IDF_TARGET_ESP32S3      // XIAO ESP32-S3: D2=3 D3=4 D4=5 D5=6
  #define BUZZER_PIN  3
  #define LED_PIN     4
  #define I2C_SDA     5
  #define I2C_SCL     6
  #define MARK_BUTTON_PIN 2          // D1, button to GND
  #define BOOT_BUTTON_PIN 0          // on-board BOOT button
  #define GPS_RX_PIN  44             // D7 <- GPS TX
  #define GPS_TX_PIN  43             // D6 (not needed; leave GPS RX unconnected)
#elif CONFIG_IDF_TARGET_ESP32C3      // XIAO ESP32-C3: D2=4 D3=5 D4=6 D5=7
  #define BUZZER_PIN  4
  #define LED_PIN     5
  #define I2C_SDA     6
  #define I2C_SCL     7
  #define MARK_BUTTON_PIN 3          // D1, button to GND
  #define BOOT_BUTTON_PIN 9          // on-board BOOT button
  #define GPS_RX_PIN  20             // D7 <- GPS TX
  #define GPS_TX_PIN  21             // D6
#else
  #error "Unsupported chip: define AIRWATCH_CUSTOM_PINS and the four pin macros"
#endif

#define NUM_LEDS          1
#define SCREEN_WIDTH      128
#define SCREEN_HEIGHT     32
#define OLED_RESET        -1
#define SCREEN_ADDRESS    0x3C

// ============================================================
// TUNING & MEMORY CONSTANTS (STATIC ALLOCATION)
// ============================================================

// --- Presence / alert timing ---
// A target counts as "in range" until it has been silent this long.
constexpr uint32_t PRESENCE_TIMEOUT_MS        = 8000;
// Silent for this long = a new encounter next time (announced again).
constexpr uint32_t ENCOUNTER_GAP_MS           = 30000;
// Minimum spacing between "new target" jingles of the same tier.
constexpr uint32_t ALERT_RETRIGGER_MS         = 1500;
// Heartbeat rate follows signal strength between these two bounds.
constexpr int      HEARTBEAT_RSSI_FAR_DBM     = -90;
constexpr int      HEARTBEAT_RSSI_NEAR_DBM    = -45;
constexpr uint32_t HEARTBEAT_PERIOD_FAR_MS    = 2000;
constexpr uint32_t HEARTBEAT_PERIOD_NEAR_MS   = 250;
// A target that has been continuously present this long is probably parked
// next to you (or riding with you). LOW targets stop heart-beating; HIGH
// targets drop to one reminder every LINGER_REMINDER_MS. 0 = never quieten.
constexpr uint32_t LINGER_MS                  = 120000;
constexpr uint32_t LINGER_REMINDER_MS         = 10000;
// A new LOW/ECHO Wi-Fi target that was first heard through a data or probe
// frame waits this long before it may alert, so its access point's beacon
// has a chance to reveal a Fire/EMS/transit network name first. Beacons,
// BLE and every HIGH signature alert immediately.
constexpr uint32_t FLEET_CHECK_GRACE_MS       = 1000;
// Re-print a serial detection line for the same device at most this often.
constexpr uint32_t DETECTION_RELOG_MS         = 30000;

constexpr uint32_t DISPLAY_ALERT_REFRESH_MS   = 200;
constexpr uint32_t DISPLAY_IDLE_REFRESH_MS    = 1000;
constexpr uint32_t OLED_I2C_HZ                = 400000;
constexpr uint32_t STATS_INTERVAL_MS          = 2000;
constexpr uint32_t CHANNEL_HOP_RETRY_MS       = 50;

// Callback-side early RSSI filter (-100 = effectively off)
constexpr bool    ENABLE_CAPTURE_RSSI_FLOOR   = true;
constexpr int8_t  CAPTURE_RSSI_MIN            = -100;

// Channel plan. 1/6/11 carry most traffic and get 3/4 of the time; 3 and 9
// sit between them so access points on 2-5 and 7-10 are still heard.
constexpr uint8_t SCAN_CHANNELS[] = {1, 6, 11, 3, 1, 6, 11, 9};
constexpr size_t NUM_SCAN_CHANNELS = sizeof(SCAN_CHANNELS) / sizeof(SCAN_CHANNELS[0]);

// BLE passive scan duty: listen WINDOW out of every INTERVAL. The rest of
// the airtime stays with the Wi-Fi sniffer (one shared 2.4 GHz radio).
constexpr uint16_t BLE_SCAN_INTERVAL_MS       = 120;
constexpr uint16_t BLE_SCAN_WINDOW_MS         = 30;
constexpr uint32_t BLE_WATCHDOG_MS            = 5000;
// Self-healing: if a radio delivers nothing at all for this long, re-arm it.
// Total silence is normal on an empty road, and re-arming is harmless there.
constexpr uint32_t WIFI_STALL_REARM_MS        = 60000;
constexpr uint32_t BLE_STALL_RESTART_MS       = 120000;

// Static Allocation Limits
// Only frames that already matched a signature are queued, so this stays
// nearly empty; 10 x 556 bytes is plenty and frees RAM for the mark table.
constexpr size_t MGMT_IE_QUEUE_SIZE           = 10;
constexpr size_t HEADER_QUEUE_SIZE            = 64;
constexpr size_t BLE_QUEUE_SIZE               = 24;
constexpr size_t RAW_IE_MAX                   = 512;
// Preserve up to four trailer bytes so application-side parsing can decide
// whether the ESP-IDF delivery included an FCS instead of discarding it early.
constexpr size_t RAW_IE_CAPTURE_MAX           = RAW_IE_MAX + 4;
constexpr size_t DEVICE_CACHE_SIZE            = 64;
constexpr size_t MAX_EVENTS_PER_LOOP          = 32;
constexpr size_t AIRWATCH_MAX_SSID_LEN        = 32;
constexpr size_t MAX_WPS_LEN                  = 32;
constexpr size_t MAX_BLE_NAME_LEN             = 31;

// --- Mark log ---
constexpr uint32_t MARK_WINDOW_MS             = 30000;   // how far back a mark reaches
// A mark also lists alerts that ended shortly before the press (EARLIER
// lines), for a press that comes a little late. Kept short: at driving speed
// anything older is far behind you.
constexpr uint32_t MARK_EARLIER_MS            = 90000;
// The screen's "last alert" line and the web page's device list look back
// further: that is for review, not for marks.
constexpr uint32_t MARK_HISTORY_MS            = 15UL * 60UL * 1000UL;
// Distinct transmitters remembered. With PSRAM (XIAO ESP32-S3: add
// :PSRAM=opi to the board name) the table lives there and is four times
// larger; without it, 512 x ~150 bytes = ~75 KB comes from the normal heap.
// If "Heap" in the t telemetry ever gets low (under ~20000) on a board
// without PSRAM, drop RECENT_TABLE_SIZE_RAM to 384.
constexpr size_t   RECENT_TABLE_SIZE_RAM      = 512;
constexpr size_t   RECENT_TABLE_SIZE_PSRAM    = 2048;
constexpr size_t   OBS_QUEUE_SIZE             = 64;
constexpr uint32_t OBS_THROTTLE_MS            = 1000;    // aim: one report per transmitter per second
constexpr size_t   OBS_THROTTLE_SLOTS         = 256;
constexpr size_t   OBS_TEXT_LEN               = 32;      // SSID / BLE name characters kept (the full length)
constexpr size_t   OBS_DETAIL_LEN             = 64;      // BLE: raw advert (31). Wi-Fi: WPS maker/model/name
constexpr size_t   OBS_MAX_VENDOR_OUIS        = 4;       // distinct vendor-IE codes kept per Wi-Fi device
constexpr size_t   RECENT_PROBES              = 6;       // network names a device searched for (PSRAM only)
// Background samples: every SAMPLE_INTERVAL_MS while driving, a silent
// snapshot like a mark (no sound, no button) goes to its own file. They are
// the "normal traffic" baseline the log server's signature finder compares
// police sightings against.
constexpr uint32_t SAMPLE_INTERVAL_MS         = 5UL * 60UL * 1000UL;
constexpr float    SAMPLE_MIN_MPH             = 10.0f;   // with a GPS fix: only while moving
constexpr size_t   SAMPLES_FILE_MAX           = 1536UL * 1024UL;   // stop adding past this until uploaded
constexpr uint32_t MARK_DEBOUNCE_MS           = 40;
constexpr uint32_t MARK_LONG_PRESS_MS         = 800;     // held this long = "nothing is there"
constexpr uint32_t MARK_LOCKOUT_MS            = 1000;
// Taps closer together than this belong to one gesture: 1-2 taps = a
// "police" mark, 3+ taps = mute / unmute.
constexpr uint32_t MULTI_TAP_GAP_MS           = 400;    // ignore repeat presses this long
// A press this soon after the previous mark is a follow-up: it saves only
// transmitters that are new, or this much stronger, since they were last saved.
constexpr uint32_t MARK_FOLLOWUP_MS           = 10000;
constexpr int      MARK_FOLLOWUP_RSSI_DB      = 6;
// A press counts as "during an alert" if one was showing this recently.
constexpr uint32_t MARK_ALERT_RECENT_MS       = 15000;
constexpr uint32_t MARK_NOTICE_MS             = 3000;    // how long the OLED confirms a mark
// Marks are refused once the flash file system has less than this free, so a
// mark is never cut off half-written. The usable size follows the partition
// scheme automatically (about 2 MB with "No OTA", about 1.4 MB by default).
constexpr size_t   MARK_LOG_RESERVE_BYTES     = 64 * 1024;
#define MARK_LOG_PATH   "/marks.log"
#define JOURNAL_PATH    "/alerts.log"
#define JOURNAL_OLD_PATH "/alerts.1.log"
// Alert journal: two files of at most this size; when the newer one fills,
// the older one is deleted. So at most 2x this, oldest alerts dropped first.
constexpr size_t   JOURNAL_FILE_MAX           = 128 * 1024;
#define BOOT_COUNT_PATH "/boot.txt"
#define SAMPLES_PATH    "/samples.log"
// Copies set aside while uploading on arrival (see uploadOnArrival)
#define MARKS_PENDING_PATH      "/marks.pending"
#define ALERTS_PENDING_OLD_PATH "/alerts.pending.1"
#define ALERTS_PENDING_PATH     "/alerts.pending"
#define SAMPLES_PENDING_PATH    "/samples.pending"

// --- Home Wi-Fi maintenance mode (see header) ---
constexpr uint32_t HOME_SETTLE_MS             = 3000;    // home heard this long before joining
constexpr uint32_t HOME_GAP_MS                = 15000;   // silent this long = no longer "at home"
constexpr int8_t   HOME_MIN_RSSI              = -85;     // ignore it when this faint
constexpr uint32_t HOME_RELEASE_MS            = 5UL * 60UL * 1000UL;  // away this long re-arms it
constexpr uint32_t MAINT_JOIN_TIMEOUT_MS      = 20000;   // could not join in this long: give up
constexpr uint32_t MAINT_RECOVER_MS           = 10000;   // link must be back this long to cancel leaving
constexpr float    HOME_DRIVING_MPH           = 10.0f;   // GPS: at least this fast ...
constexpr uint32_t HOME_DRIVING_MS            = 3000;    // ... for this long = driving: not at home
constexpr uint32_t MAINT_LOST_MS              = 5000;    // home network gone / very weak this long: leave
constexpr int      HOME_LEAVE_RSSI            = -88;     // this weak counts as gone (driving away)
constexpr uint32_t ROAD_WINDOW_MS             = 15000;   // road access: hold the button within this of power-up
constexpr uint32_t ROAD_HOLD_MS               = 3000;    // ...for this long
constexpr uint32_t MAINT_IDLE_MS              = 15UL * 60UL * 1000UL; // no page request this long: leave
constexpr char     DEVICE_HOSTNAME[]          = "airwatch";
// Transmit power while the web page is up. The sniffer never transmits;
// joining Wi-Fi does, in bursts of 300+ mA at full power, which can make a
// car USB supply sag far enough to reset the board (brownout). Home: enough
// to reach the house from the driveway. Road: the phone is next to it.
#define WEB_TX_POWER_HOME  WIFI_POWER_13dBm
#define WEB_TX_POWER_ROAD  WIFI_POWER_7dBm
#define RESET_LOG_PATH     "/resets.log"
constexpr size_t   RESET_LOG_MAX              = 8 * 1024;

constexpr int RSSI_LED_MIN                    = -95;
constexpr int RSSI_LED_MAX                    = -35;

// ============================================================
// TARGET SIGNATURE DATABASE
// ============================================================

enum TargetCategory : uint8_t {
  CAT_HIGH_CONF = 0,   // vendor/pattern that is specific to law enforcement
  CAT_LOW_CONF  = 1,   // broad vendor, in-vehicle router, or unconfirmed
  CAT_MINOR     = 2    // worth a single tick only, never more (drones)
};                     // lower value = stronger

enum MatchMode : uint8_t {
  MATCH_EXACT,
  MATCH_PREFIX,
  MATCH_SUBSTRING,
  MATCH_WORD,        // pattern with no letter directly before or after it
  MATCH_TOKEN_END    // pattern with no letter directly after it
};

// Alert tier of a tracked device. Higher = stronger evidence.
enum AlertTier : uint8_t {
  TIER_NONE = 0,
  TIER_ECHO = 1,   // only seen as the *receiver* of frames, LOW-category prefix
  TIER_LOW  = 2,   // transmitted a LOW signature, or receiver with HIGH prefix
  TIER_HIGH = 3    // transmitted a HIGH signature
};

// Heartbeat applies to this tier and above (ECHO gets its one tick only).
constexpr uint8_t HEARTBEAT_MIN_TIER = TIER_LOW;

enum LabelRank : uint8_t {
  LABEL_RANK_NONE      = 0,
  LABEL_RANK_ECHO      = 1, // secondary match (address seen as receiver)
  LABEL_RANK_LOW       = 2, // any low-confidence signature
  LABEL_RANK_HIGH_PRF  = 3, // high-confidence MAC prefix (hardware OUI)
  LABEL_RANK_HIGH_WPS  = 4, // high-confidence WPS descriptor
  LABEL_RANK_HIGH_BLE  = 5, // high-confidence BLE company ID / service / name
  LABEL_RANK_HIGH_SSID = 6  // high-confidence SSID (explicit network name)
};

enum ReasonCode : uint16_t {
  REASON_NONE           = 0x0000,
  REASON_PREFIX_HIGH    = 0x0001,
  REASON_PREFIX_LOW     = 0x0002,
  REASON_SSID_HIGH      = 0x0004,
  REASON_SSID_LOW       = 0x0008,
  REASON_WPS_HIGH       = 0x0010,
  REASON_WPS_LOW        = 0x0020,
  REASON_DIRECTED_PROBE = 0x0040,
  REASON_WILDCARD_PROBE = 0x0080,
  REASON_ECHO           = 0x0100,
  REASON_DATA_FRAME     = 0x0200,
  REASON_BLE_ADDR       = 0x0400,
  REASON_BLE_COMPANY    = 0x0800,
  REASON_BLE_SERVICE    = 0x1000,
  REASON_BLE_NAME       = 0x2000,
  REASON_FLEET_SSID     = 0x4000,  // broadcast a Fire/EMS/transit network name
  REASON_DRONE          = 0x8000   // drone: Remote ID broadcast or drone-maker address
};

enum SourceRadio : uint8_t {
  SRC_WIFI = 0,
  SRC_BLE  = 1
};

struct MacPrefixTarget {
  uint64_t prefix;
  uint8_t  prefixBits;     // 24 (MA-L), 28 (MA-M) or 36 (MA-S)
  const char* label;
  TargetCategory category;
};

struct SSIDTarget {
  const char* pattern;
  const char* label;
  TargetCategory category;
  MatchMode mode;
};

struct IgnoredSSIDTarget {
  const char* pattern;
  MatchMode mode;
};

struct WPSTarget {
  const char* pattern;
  const char* label;
  TargetCategory category;
  MatchMode mode;
};

struct BleCompanyTarget {
  uint16_t companyId;      // Bluetooth SIG company identifier
  const char* label;
  TargetCategory category;
};

struct BleServiceTarget {
  uint16_t uuid16;         // 16-bit service UUID
  const char* label;
  TargetCategory category;
};

struct BleNameTarget {
  const char* pattern;     // upper-case substring of the advertised name
  const char* label;
  TargetCategory category;
};

// Every prefix below was checked against nmap's copy of the IEEE registry
// (Nov 2026); the trailing comment is the registered owner. The 25 entries
// from v1 that are registered to unrelated companies (Intel, HP,
// Apple, Nintendo, Huawei, Cisco, Microsoft, Lenovo, the shared IEEE blocks
// 70:B3:D5 and 00:50:C2, ...) are gone: they could not match police gear.
const MacPrefixTarget prefixTargets[] = {
  // --- HIGH: vendors that sell essentially only to law enforcement ---
  {0x0025DFULL,    24, "Axon",             CAT_HIGH_CONF},  // Axon Enterprise
  {0x001D96ULL,    24, "WatchGuard Video", CAT_HIGH_CONF},  // WatchGuard Video
  {0xFC019EULL,    24, "Vievu Cam",        CAT_HIGH_CONF},  // Vievu
  {0x0023BDULL,    24, "Digital Ally",     CAT_HIGH_CONF},  // Digital Ally
  {0x001BBEULL,    24, "ICOP Digital",     CAT_HIGH_CONF},  // Icop Digital
  {0x0022AFULL,    24, "Safety Vision",    CAT_HIGH_CONF},  // Safety Vision
  {0xE41E0ABULL,   28, "Safety Vision",    CAT_HIGH_CONF},  // Safety Vision (MA-M)   [new]
  {0x0009BCULL,    24, "Utility BodyWorn", CAT_HIGH_CONF},  // Utility
  {0x0016EDULL,    24, "Utility Inc",      CAT_HIGH_CONF},  // Utility
  {0x3873EA0ULL,   28, "L3 Mobile-Vision", CAT_HIGH_CONF},  // L-3 Mobile-Vision (MA-M) [new]
  {0x58E876CULL,   28, "Kustom Signals",   CAT_HIGH_CONF},  // Kustom Signals (MA-M)  [new]
  {0x1C8259DULL,   28, "Stalker Radar",    CAT_HIGH_CONF},  // Applied Concepts (MA-M) [new]
  {0x6C1811ULL,    24, "Decatur Radar",    CAT_HIGH_CONF},  // Decatur Electronics

  // --- LOW: right vendor, but they also sell far beyond policing ---
  // Motorola Solutions (radios, body cams, also business radios / cameras)
  {0x00047DULL,    24, "Motorola Solutions", CAT_LOW_CONF},
  {0x001885ULL,    24, "Motorola Solutions", CAT_LOW_CONF},
  {0x001F92ULL,    24, "Motorola Solutions", CAT_LOW_CONF},
  {0x4CCC34ULL,    24, "Motorola Solutions", CAT_LOW_CONF},
  {0x10746FULL,    24, "Motorola Solutions", CAT_LOW_CONF},
  {0xB8E28CULL,    24, "Motorola Solutions", CAT_LOW_CONF},
  {0x002075ULL,    24, "Motorola Comms",     CAT_LOW_CONF},  // Motorola Communication Israel
  {0x9C862BULL,    24, "Motorola Solutions", CAT_LOW_CONF},  // Motorola Solutions Malaysia  [registry]

  // Unconfirmed Axon addresses listed by SquachWatch-CYD. E4:05:40 is not in
  // the IEEE registry at all; 28:24:FF belongs to WNC, a contract maker that
  // builds for many brands. LOW so the logs can show whether they earn it.
  {0xE40540ULL,    24, "Axon? (unreg.)",     CAT_LOW_CONF},
  {0x2824FFULL,    24, "Axon? (WNC)",        CAT_LOW_CONF},

  // Rugged terminals / in-car video
  {0x001987ULL,    24, "Panasonic Mobile",   CAT_LOW_CONF},  // Panasonic Mobile Communications
  {0x008045ULL,    24, "Panasonic",          CAT_LOW_CONF},  // Matsushita Electric
  {0x54CD10ULL,    24, "Panasonic Mobile",   CAT_LOW_CONF},  // Panasonic Mobile Comms   [registry]
  {0xD8B12AULL,    24, "Panasonic Mobile",   CAT_LOW_CONF},  // Panasonic Mobile Comms   [registry]
  {0xB8208EULL,    24, "Panasonic Connect",  CAT_LOW_CONF},  // Toughbook maker          [registry]
  {0x4C364EULL,    24, "Panasonic Connect",  CAT_LOW_CONF},  // Toughbook maker          [registry]
  {0xBC3E0BULL,    24, "Panasonic Connect",  CAT_LOW_CONF},  // Toughbook maker          [registry]
  {0xD42DC5ULL,    24, "i-PRO Camera",       CAT_LOW_CONF},  // i-PRO                  [new]
  {0x9C83BFULL,    24, "PRO-Vision Video",   CAT_LOW_CONF},  // PRO-Vision             [new]
  {0x001570ULL,    24, "Zebra Device",       CAT_LOW_CONF},  // Zebra Technologies
  {0xD0E40BULL,    24, "Vievu? (unverif)",   CAT_LOW_CONF},  // registered to "Wearable"; unconfirmed

  // Radios, tactical comms, ALPR, sirens, docks
  {0x0017F3ULL,    24, "Harris Comms",       CAT_LOW_CONF},
  {0x0006ECULL,    24, "Harris Comms",       CAT_LOW_CONF},
  {0x0000C3ULL,    24, "Harris Comms",       CAT_LOW_CONF},
  {0x000268ULL,    24, "Harris Govt",        CAT_LOW_CONF},
  {0x001027ULL,    24, "L-3 Comms",          CAT_LOW_CONF},  // L-3 Communications East
  {0xA47C1FULL,    24, "Cobham Tactical",    CAT_LOW_CONF},
  {0x0CBF15ULL,    24, "Genetec ALPR",       CAT_LOW_CONF},
  {0x00BF15ULL,    24, "Genetec ALPR",       CAT_LOW_CONF},
  {0x0040DEULL,    24, "ELSAG ALPR",         CAT_LOW_CONF},  // Elsag Datamat          [new]
  {0x70B3D51C5ULL, 36, "ELSAG ALPR",         CAT_LOW_CONF},  // Elsag (MA-S)           [new]
  {0x00173DULL,    24, "Neology ALPR",       CAT_LOW_CONF},  // Neology                [new]
  {0x083C030ULL,   28, "Federal Signal",     CAT_LOW_CONF},  // Federal Signal SSG (MA-M) [new]
  {0x001F9CULL,    24, "Havis Dock",         CAT_LOW_CONF},  // Havis                  [new]
  {0x70B3D5983ULL, 36, "Havis Dock",         CAT_LOW_CONF},  // Havis (MA-S)           [new]
  {0x68DA73BULL,   28, "Gamber-Johnson",     CAT_LOW_CONF},  // Gamber-Johnson (MA-M)  [new]
  {0x8C1F6442BULL, 36, "Gamber-Johnson",     CAT_LOW_CONF},  // MA-S blocks           [registry]
  {0x8C1F64CB5ULL, 36, "Gamber-Johnson",     CAT_LOW_CONF},
  {0x8C1F6404CULL, 36, "Gamber-Johnson",     CAT_LOW_CONF},
  {0x70B3D5178ULL, 36, "Gamber-Johnson",     CAT_LOW_CONF},
  {0x70B3D5DBCULL, 36, "Gamber-Johnson",     CAT_LOW_CONF},
  {0x70B3D5E34ULL, 36, "Gamber-Johnson",     CAT_LOW_CONF},

  // In-vehicle cellular routers (also buses, utility trucks, kiosks)
  {0x003044ULL,    24, "Cradlepoint GW",     CAT_LOW_CONF},
  {0x00E01CULL,    24, "Cradlepoint GW",     CAT_LOW_CONF},
  {0x00A0D5ULL,    24, "Sierra AirLink",     CAT_LOW_CONF},
  {0x28A331ULL,    24, "Sierra AirLink",     CAT_LOW_CONF},
  {0x50139DULL,    24, "Sierra AirLink",     CAT_LOW_CONF},
  {0x64CE6EULL,    24, "Sierra AirLink",     CAT_LOW_CONF},
  {0xCC934AULL,    24, "Sierra AirLink",     CAT_LOW_CONF},
  {0x84DB2FULL,    24, "Sierra AirLink",     CAT_LOW_CONF},
  {0x00116EULL,    24, "Peplink Router",     CAT_LOW_CONF},
  {0x6CA3D3ULL,    24, "Peplink Router",     CAT_LOW_CONF},  // Peplink International   [registry]
  {0xD413F8ULL,    24, "Peplink Router",     CAT_LOW_CONF},
  {0x1056CAULL,    24, "Peplink Router",     CAT_LOW_CONF},
  {0x001ADDULL,    24, "Pepwave Router",     CAT_LOW_CONF},  // PePWave                [new]
  {0xA8C0EAULL,    24, "Pepwave Router",     CAT_LOW_CONF},  // Pepwave Limited        [new]

  // --- MINOR: one tick only ---
  {0x381D14ULL,    24, "Skydio Drone",       CAT_MINOR}      // Skydio: police drone programs
};
constexpr size_t NUM_PREFIX_TARGETS = sizeof(prefixTargets) / sizeof(prefixTargets[0]);

// Patterns are compared against the upper-cased SSID.
const SSIDTarget ssidTargets[] = {
  {"AXON",        "Axon Fleet/Body",   CAT_HIGH_CONF, MATCH_PREFIX},
  {"WATCHGUARD",  "WatchGuard Video",  CAT_HIGH_CONF, MATCH_PREFIX},
  {"VISTA",       "Motorola Vista",    CAT_LOW_CONF,  MATCH_PREFIX},    // LOW: "Vista..." apartments, hotels
  {"POLICE",      "Police Network",    CAT_HIGH_CONF, MATCH_SUBSTRING},
  {"PATROL",      "Patrol Unit",       CAT_LOW_CONF,  MATCH_SUBSTRING},   // LOW: "Paw Patrol", security patrols
  {"GETAC",       "Getac Unit",        CAT_HIGH_CONF, MATCH_PREFIX},
  {"MDT-",        "Mobile Terminal",   CAT_HIGH_CONF, MATCH_PREFIX},
  {"COBAN",       "Coban Unit",        CAT_HIGH_CONF, MATCH_PREFIX},
  {"PANCAR",      "Panasonic Fleet",   CAT_HIGH_CONF, MATCH_PREFIX},
  {"L3-MOBILE",   "L3 MobileVision",   CAT_HIGH_CONF, MATCH_PREFIX},
  {"FIRSTNET",    "FirstNet Uplink",   CAT_LOW_CONF,  MATCH_PREFIX},
  {"CRADLEPOINT", "Cradlepoint",       CAT_LOW_CONF,  MATCH_PREFIX},
  {"CRADLPT",     "Cradlepoint",       CAT_LOW_CONF,  MATCH_PREFIX},
  {"SIERRA",      "Sierra AirLink",    CAT_LOW_CONF,  MATCH_PREFIX},
  {"AIRLINK",     "Sierra AirLink",    CAT_LOW_CONF,  MATCH_PREFIX},
  // Seen on the access points of a Virginia State Police station (marks,
  // Nov 2026): "COVA-VASP" next to "Virginia_Hotspot" on the same radios.
  {"COVA-VASP",   "VA State Police",   CAT_HIGH_CONF, MATCH_SUBSTRING},
  {"VASP",        "VA State Police?",  CAT_LOW_CONF,  MATCH_WORD},
  {"VIRGINIA_HOTSPOT", "VA State Hotspot", CAT_LOW_CONF, MATCH_PREFIX},
  {"VSP",         "State Police",      CAT_LOW_CONF,  MATCH_PREFIX},
  {"SHERIFF",     "Sheriff Dept",      CAT_LOW_CONF,  MATCH_SUBSTRING},
  {"STATE_POL",   "State Police",      CAT_LOW_CONF,  MATCH_PREFIX},
  // Confirmed on a marked Henrico PD truck (drive 73, Nov 2026): its
  // Cradlepoint broadcast "GRTC-MT Wifi" and "GRTC-Radio", and its Motorola
  // radio looked for "GRTC-Radio". The names also catch the second network,
  // whose derived address the Cradlepoint prefix does not. LOW until more
  // sightings show these are always police.
  {"GRTC-MT",     "GRTC/Henrico unit", CAT_LOW_CONF,  MATCH_PREFIX},
  {"GRTC-RADIO",  "GRTC/Henrico unit", CAT_LOW_CONF,  MATCH_EXACT},
  // [new] Cradlepoint factory SSIDs are "<model>-<3 hex digits>"
  {"IBR6",        "Cradlepoint IBR",   CAT_LOW_CONF,  MATCH_PREFIX},
  {"IBR9",        "Cradlepoint IBR",   CAT_LOW_CONF,  MATCH_PREFIX},
  {"IBR1",        "Cradlepoint IBR",   CAT_LOW_CONF,  MATCH_PREFIX},
  {"R1900-",      "Cradlepoint R1900", CAT_LOW_CONF,  MATCH_PREFIX},
  {"R920-",       "Cradlepoint R920",  CAT_LOW_CONF,  MATCH_PREFIX},
  {"R980-",       "Cradlepoint R980",  CAT_LOW_CONF,  MATCH_PREFIX},
  {"R2100-",      "Cradlepoint R2100", CAT_LOW_CONF,  MATCH_PREFIX},
  // [new] educated guesses, unconfirmed in the field
  {"ARBITRATOR",  "Arbitrator Video",  CAT_LOW_CONF,  MATCH_SUBSTRING},
  {"BODYWORN",    "BodyWorn Cam",      CAT_LOW_CONF,  MATCH_SUBSTRING}
};
constexpr size_t NUM_SSID_TARGETS = sizeof(ssidTargets) / sizeof(ssidTargets[0]);

// Non-law-enforcement fleets (Fire, EMS, transit, schools) that often use
// Cradlepoint, Sierra AirLink, or Peplink routers. Compared against the
// upper-cased SSID. A router broadcasting one of these is silenced unless
// the same frame also carries a HIGH (police-specific) signature.
//
// Short patterns use WORD / TOKEN_END so they cannot fire inside ordinary
// words: "EMS" must stand alone ("EMS-4", "COUNTY EMS", "EMS12") and so no
// longer matches "SYSTEMS" or "ITEMS"; "MEDIC" matches "MEDIC7" and
// "PARAMEDIC" but not "MEDICAL". A run-together name such as "HenricoEMS"
// is NOT caught by the WORD rule: add it here as its own line if you see it.
const IgnoredSSIDTarget ignoredSsidTargets[] = {
  {"FIRE",      MATCH_SUBSTRING},   // e.g. "HenricoFire"
  {"RESCUE",    MATCH_SUBSTRING},
  {"AMBULANCE", MATCH_SUBSTRING},
  {"TRANSIT",   MATCH_SUBSTRING},
  {"SCHOOL",    MATCH_SUBSTRING},
  {"EMS",       MATCH_WORD},
  {"EMT",       MATCH_WORD},
  {"BUS",       MATCH_WORD},
  {"MEDIC",     MATCH_TOKEN_END},
  {"ENGINE",    MATCH_TOKEN_END},   // "ENGINE12", not "ENGINEERING"
  {"LADDER",    MATCH_TOKEN_END}
};
constexpr size_t NUM_IGNORED_SSID_TARGETS = sizeof(ignoredSsidTargets) / sizeof(ignoredSsidTargets[0]);

// Factory network names that cap a LOW match at QUIET (silent: logged and
// journaled only). Upper case. The whole name must be this prefix followed by
// exactly three hex digits - Cradlepoint's default "<model>-<last 3 of MAC>".
// These are the E-series branch / office models; vehicle models (IBR900,
// IBR1700, R1900 ...) are deliberately not listed. To alert normally on one
// of these again, delete its line.
// Fixed routers you have reviewed (the log server's Fixed routers tab has a
// copy button for each): treated like quiet-list names, i.e. silent but
// still logged, along with their sibling radios. A police-specific (HIGH)
// signature on one still alerts. The all-zero row is a placeholder.
const uint8_t quietMacTargets[][6] = {
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};
constexpr size_t NUM_QUIET_MAC_TARGETS = sizeof(quietMacTargets) / sizeof(quietMacTargets[0]);

const char* const quietSsidTargets[] = {
  "IBR200-",   // kiosk / signage / ATM gateway, not a vehicle router
  "MBR1200B-", // old small-office router, not a vehicle model
  "E100-",
  "E102-",
  "E110-",
  "E300-",
  "E3000-"
};
constexpr size_t NUM_QUIET_SSID_TARGETS = sizeof(quietSsidTargets) / sizeof(quietSsidTargets[0]);
constexpr char QUIET_LABEL[] = "Cradlepoint E-series";

// Bluetooth company IDs of phone / laptop / watch makers. Adverts from a
// random address carrying one of these, and matching no signature, are not
// written to the mark log.
const uint16_t blePhoneCompanyIds[] = {
  0x004C,   // Apple
  0x0006,   // Microsoft
  0x0075    // Samsung
};
constexpr size_t NUM_BLE_PHONE_COMPANY_IDS = sizeof(blePhoneCompanyIds) / sizeof(blePhoneCompanyIds[0]);

// Google / Android service UUIDs (Fast Pair, Nearby, Find My Device network).
// Random-address adverts whose first service is one of these, and that match
// no signature, are phones, earbuds and trackers: not written to the mark log.
const uint16_t blePhoneServiceIds[] = {
  0xFCF1, 0xFEF3, 0xFE9F, 0xFE2C, 0xFEAA
};
constexpr size_t NUM_BLE_PHONE_SERVICE_IDS = sizeof(blePhoneServiceIds) / sizeof(blePhoneServiceIds[0]);

// Not a registered Bluetooth service. Cheap fitness bands advertise it, and
// one of them was caught using TASER's company ID (false HIGH alert, Nov
// 2026). A company-ID-only match that also carries this stays LOW ("maybe").
constexpr uint16_t BLE_WEARABLE_SERVICE = 0x0AF0;

// Drone Remote ID (ASTM F3411, required on most US drones): over Bluetooth,
// service data for UUID 0xFFFA with app code 0x0D; over Wi-Fi, a beacon
// vendor element with OUI FA:0B:BC, type 0x0D. Logged and given one tick
// (MINOR), never more: hobby drones send it too.
constexpr uint16_t BLE_REMOTE_ID_SERVICE = 0xFFFA;
constexpr uint8_t  REMOTE_ID_APP_CODE    = 0x0D;
constexpr uint32_t WIFI_REMOTE_ID_OUI    = 0xFA0BBC;
const char* const  DRONE_LABEL           = "Drone (Remote ID)";

// Axon body camera: puts the text BWCDEVICE in its own Bluetooth advert
// (seen reversed, inside a little-endian 128-bit UUID). Address-independent,
// so it still works when the camera changes its address. From SquachWatch-CYD.
const char* const  AXON_BWC_TAG          = "BWCDEVICE";
const char* const  AXON_BWC_LABEL        = "Axon Body Cam";

const WPSTarget wpsTargets[] = {
  {"CRADLEPOINT", "Cradlepoint HW",    CAT_LOW_CONF,  MATCH_SUBSTRING},
  {"SIERRA",      "Sierra AirLink HW", CAT_LOW_CONF,  MATCH_SUBSTRING},
  {"AIRLINK",     "Sierra AirLink HW", CAT_LOW_CONF,  MATCH_SUBSTRING},
  {"PEPWAVE",     "Pepwave HW",        CAT_LOW_CONF,  MATCH_SUBSTRING},
  {"PEPLINK",     "Peplink HW",        CAT_LOW_CONF,  MATCH_SUBSTRING},
  {"GETAC",       "Getac System HW",   CAT_HIGH_CONF, MATCH_SUBSTRING},
  {"TOUGHBOOK",   "Panasonic Toughbook",CAT_HIGH_CONF, MATCH_SUBSTRING},
  {"TOUGHPAD",    "Panasonic Toughpad",CAT_HIGH_CONF, MATCH_SUBSTRING},
  {"PANASONIC",   "Panasonic HW",      CAT_LOW_CONF,  MATCH_SUBSTRING},   // LOW: TVs, cameras, printers
  {"MOTOROLA",    "Motorola Device HW",CAT_LOW_CONF,  MATCH_SUBSTRING},   // LOW: Motorola-brand home gateways
  {"AXON",        "Axon System HW",    CAT_HIGH_CONF, MATCH_SUBSTRING},
  {"WATCHGUARD",  "WatchGuard HW",     CAT_HIGH_CONF, MATCH_SUBSTRING}
};
constexpr size_t NUM_WPS_TARGETS = sizeof(wpsTargets) / sizeof(wpsTargets[0]);

// --- Bluetooth LE signatures ---
// 0x034D is registered to TASER International (Axon) in the Bluetooth SIG
// company list. The three service UUIDs are the ones the AXEOFF / OUI-SPY
// projects attribute to Axon/TASER. Motorola Solutions is a guess that its
// body cams and radios use their own company ID; confirm with captures.
const BleCompanyTarget bleCompanyTargets[] = {
  {0x034D, "Axon/TASER BLE",   CAT_HIGH_CONF},
  {0x04EC, "Motorola Sol BLE", CAT_LOW_CONF}
};
constexpr size_t NUM_BLE_COMPANY_TARGETS = sizeof(bleCompanyTargets) / sizeof(bleCompanyTargets[0]);

const BleServiceTarget bleServiceTargets[] = {
  {0xFC81, "Axon BLE",       CAT_HIGH_CONF},
  {0xFE6B, "Axon/TASER BLE", CAT_HIGH_CONF},
  {0xFE6C, "Axon/TASER BLE", CAT_HIGH_CONF}
};
constexpr size_t NUM_BLE_SERVICE_TARGETS = sizeof(bleServiceTargets) / sizeof(bleServiceTargets[0]);

const BleNameTarget bleNameTargets[] = {
  {"AXON",  "Axon BLE",  CAT_HIGH_CONF},
  {"TASER", "TASER BLE", CAT_HIGH_CONF},
  // LOW: these names are not unique to police gear
  {"WATCHGUARD", "WatchGuard BLE", CAT_LOW_CONF},
  {"GETAC",      "Getac BLE",      CAT_LOW_CONF},
  {"COBAN",      "Coban BLE",      CAT_LOW_CONF},
  {"STALKER",    "Stalker BLE",    CAT_LOW_CONF},
  {"BODYWORN",   "BodyWorn BLE",   CAT_LOW_CONF}
};
constexpr size_t NUM_BLE_NAME_TARGETS = sizeof(bleNameTargets) / sizeof(bleNameTargets[0]);

// BleEvent carries matches as a one-byte prefix index and 8-bit masks.
static_assert(NUM_PREFIX_TARGETS < 255,       "prefix table too large for BleEvent.prefixIdx");
static_assert(NUM_BLE_COMPANY_TARGETS <= 8,   "BLE company table too large for BleEvent.companyMask");
static_assert(NUM_BLE_SERVICE_TARGETS <= 8,   "BLE service table too large for BleEvent.serviceMask");
static_assert(NUM_BLE_NAME_TARGETS <= 8,      "BLE name table too large for BleEvent.nameMask");

// ============================================================
// DATA STRUCTURES
// ============================================================

struct HeaderEvent {
  uint8_t  addr1[6];
  uint8_t  addr2[6];
  uint8_t  addr3[6];
  uint8_t  addr4[6];       // Valid only for ToDS=1 + FromDS=1 data frames
  int8_t   rssi;           // RSSI always belongs to addr2 (the RF transmitter)
  uint8_t  channel;
  uint8_t  frameType;      // WIFI_PKT_MGMT or WIFI_PKT_DATA
  uint8_t  subtype;
  uint8_t  dsFlags;        // bit0 = ToDS, bit1 = FromDS
  bool     hasAddr4;
};
static_assert(sizeof(HeaderEvent) <= 40, "HeaderEvent unexpectedly large");

struct MgmtIeEvent {
  HeaderEvent header;
  uint16_t    ieAvailableLen;
  uint16_t    ieLen;
  uint8_t     ieData[RAW_IE_CAPTURE_MAX];
};
static_assert(sizeof(MgmtIeEvent) <= 560, "MgmtIeEvent exceeds safe boundary limit");

struct BleEvent {
  uint8_t     mac[6];      // most-significant byte first, like a Wi-Fi MAC
  int8_t      rssi;
  uint8_t     category;    // TargetCategory of the best match
  uint8_t     labelRank;
  uint16_t    reasons;
  uint8_t     prefixIdx;   // 0xFF if none (counted safely in loop())
  uint8_t     companyMask; // bitmask of matched bleCompanyTargets
  uint8_t     serviceMask; // bitmask of matched bleServiceTargets
  uint8_t     nameMask;    // bitmask of matched bleNameTargets
  bool        soloCompany; // HIGH only because of a company ID: confirm with a 2nd advert
  const char* label;
};

// --- Mark log: a compact note that "this transmitter was just heard" ---
enum ObsKind : uint8_t {
  OBS_BEACON     = 0x01,
  OBS_PROBE_REQ  = 0x02,
  OBS_PROBE_RESP = 0x04,
  OBS_DATA       = 0x08,
  OBS_MGMT_OTHER = 0x10,
  OBS_BLE        = 0x20,   // BLE advert, random address
  OBS_BLE_PUBLIC = 0x40,   // BLE advert, public (vendor-assigned) address
  OBS_HAS_CID    = 0x80    // bleCompany is valid
};
constexpr uint8_t OBS_RADIO_BLE = OBS_BLE | OBS_BLE_PUBLIC;   // set = heard on Bluetooth

struct Observation {
  uint8_t  mac[6];
  uint8_t  kind;           // ObsKind bits
  int8_t   rssi;
  uint8_t  channel;        // 0 for BLE
  uint16_t bleCompany;
  uint16_t bleService;     // first 16-bit service UUID seen, 0 if none
  char     text[OBS_TEXT_LEN + 1];   // SSID (Wi-Fi) or advertised name (BLE)
  uint8_t  detailLen;      // BLE: bytes of raw advert in detail[]. Wi-Fi: unused
  uint8_t  ouiCount;       // Wi-Fi: vendor-IE codes in ouis[]
  bool     hasPeer;        // Wi-Fi: peer[] is valid
  uint8_t  peer[6];        // Wi-Fi: the unicast address it was sending to
  uint8_t  ouis[OBS_MAX_VENDOR_OUIS * 3];
  uint8_t  detail[OBS_DETAIL_LEN];   // BLE: raw advert. Wi-Fi: WPS text, NUL-terminated
};

struct RecentEntry {
  bool     valid;
  uint8_t  mac[6];
  uint8_t  kinds;
  int8_t   bestRssi;
  uint8_t  channel;
  uint16_t reports;        // distinct clock seconds in which it was heard
  uint32_t lastSeen;
  uint32_t lastCountedSec; // the clock second most recently counted in reports
  uint32_t firstSeen;      // start of the current stay in range
  uint16_t bleCompany;
  uint16_t bleService;
  char     text[OBS_TEXT_LEN + 1];
  uint8_t  detailLen;      // BLE: bytes of raw advert in detail[]. Wi-Fi: unused
  uint8_t  ouiCount;       // Wi-Fi: vendor-IE codes in ouis[]
  bool     hasPeer;        // Wi-Fi: peer[] is valid
  uint8_t  peer[6];        // Wi-Fi: the unicast address it was sending to
  uint8_t  ouis[OBS_MAX_VENDOR_OUIS * 3];
  uint8_t  detail[OBS_DETAIL_LEN];   // BLE: raw advert. Wi-Fi: WPS text, NUL-terminated
  uint8_t  probeCount;     // names in recentProbes[slot]
  bool     marked;         // already written to the log during this stay in range
  int8_t   markedRssi;     // bestRssi when it was last written
  uint32_t markedAt;       // when it was last written
};

// Small always-in-RAM companion of recentTable[], one per slot, holding
// everything the per-frame search and the 1 Hz screen count need. The big
// entries (which may live in slower PSRAM) are only touched on a match.
struct RecentIndex {
  uint32_t key;            // 0 = slot free; else address hash, bit 0 = Bluetooth
  uint32_t lastSeen;
  int8_t   bestRssi;
};

// Per-task "did I already report this address in the last second" memory.
struct ObsThrottle {
  uint32_t key[OBS_THROTTLE_SLOTS];
  uint32_t at[OBS_THROTTLE_SLOTS];
};

// Declared here before any function implementations so the Arduino preprocessor
// does not generate forward prototypes referencing an undefined type.
struct IeParseResult {
  char             ssid[AIRWATCH_MAX_SSID_LEN + 1];
  uint8_t          ssidLen;
  bool             ssidIeSeen;
  bool             wildcardSsid;
  const WPSTarget* wpsMatch;
  size_t           wpsMatchIdx;
  int16_t          wpsStart;
  int16_t          wpsEnd;
  bool             structurallyComplete;
  size_t           bytesConsumed;
  uint16_t         ieCount;
  bool             remoteId;        // drone Remote ID vendor element present
};

// One observation of a target, from either radio.
struct Sighting {
  uint8_t     mac[6];
  uint8_t     src;         // SourceRadio
  uint8_t     tier;        // AlertTier this observation supports
  bool        viaEcho;     // target was the receiver, not the transmitter
  bool        hasRssi;     // rssi describes the target itself
  bool        ssidSeen;    // came from a beacon / probe response (network name was checked)
  bool        quiet;       // that network name is on the quiet list
  bool        needsSecond; // HIGH evidence that counts as LOW until heard twice
  const char* ssid;        // network name it broadcast (printable), or nullptr
  int8_t      rssi;
  uint8_t     channel;     // Wi-Fi channel, 0 for BLE
  uint16_t    reasons;
  const char* label;
  uint8_t     labelRank;
};

struct TrackedDevice {
  bool        valid;
  bool        suppressed;      // true if identified as Fire/EMS/non-police
  bool        quietCap;        // quiet-list router: LOW evidence counts as one tick only
  uint8_t     mac[6];
  uint8_t     src;             // radio of the most recent direct sighting
  uint8_t     tier;            // best tier this encounter
  uint8_t     announcedTier;   // best tier already announced this encounter
  bool        echoOnly;        // never heard transmitting this encounter
  bool        hasRssi;
  int16_t     smoothedRssiFp8;
  uint8_t     channel;
  uint32_t    firstSeen;       // start of this encounter
  uint32_t    lastSeen;
  uint32_t    lastDirectSeen;
  uint32_t    lastLogAt;
  uint32_t    holdUntil;       // may not alert before this (fleet-check grace)
  uint16_t    sightings;
  uint16_t    reasonMask;
  const char* label;
  uint8_t     labelRank;
  char        ssid[AIRWATCH_MAX_SSID_LEN + 1];  // network name it broadcast, if heard
  int8_t      bestRssi;        // strongest direct signal this encounter (-127 = none)
  bool        journaled;       // this encounter's alert is already in the journal
  bool        hasPos;          // posLat/posLon valid: where its signal was strongest
  float       posLat;
  float       posLon;
  uint8_t     posMph;          // our speed there (stationary router vs. something pacing you)
  bool        hasTrail;        // fromLat/Lon and toLat/Lon valid
  float       fromLat;         // where its signal was first heard (with a GPS fix) ...
  float       fromLon;
  float       toLat;           // ... and last heard
  float       toLon;
};

// Signal timeline: for each tracked device, its strongest signal in each
// second of the encounter (index 0 = first heard), up to TIMELINE_SECONDS.
// 0 = not heard that second. Written to the journal and marks as tl=, so the
// review page can chart how the signal rose and fell.
constexpr size_t TIMELINE_SECONDS = 180;
int8_t* deviceTimeline = nullptr;       // DEVICE_CACHE_SIZE x TIMELINE_SECONDS (PSRAM when present)

struct Note {
  uint16_t freqHz;   // used only when BUZZER_IS_PASSIVE
  uint16_t onMs;
  uint16_t gapMs;
};

constexpr size_t AUDIO_MAX_NOTES = 4;

struct AudioState {
  Note     notes[AUDIO_MAX_NOTES];
  uint8_t  count;
  uint8_t  index;
  uint8_t  priority;
  bool     active;
  bool     sounding;
  uint32_t phaseEnd;
};

// What the OLED / LED should show right now.
struct AlertView {
  bool     active;
  uint8_t  tier;
  bool     echoOnly;
  bool     hasRssi;
  int8_t   rssi;
  uint8_t  channel;
  uint8_t  src;
  uint8_t  count;      // targets currently in range
  bool     quiet;      // lead target is a quiet-list router
  char     label[24];
};

struct DisplaySnapshot {
  bool    inAlert;
  bool    muted;
  uint8_t tier;
  bool    echoOnly;
  bool    hasRssi;
  uint8_t channel;
  uint8_t src;
  uint8_t count;
  int8_t  rssi;
  char    label[24];

  bool equals(const DisplaySnapshot& other) const {
    if (inAlert != other.inAlert || muted != other.muted) return false;
    if (inAlert) {
      return (tier == other.tier) &&
             (echoOnly == other.echoOnly) &&
             (hasRssi == other.hasRssi) &&
             (channel == other.channel) &&
             (src == other.src) &&
             (count == other.count) &&
             (rssi == other.rssi) &&
             (strncmp(label, other.label, sizeof(label)) == 0);
    }
    return (channel == other.channel);
  }
};

// ============================================================
// ALERT SOUNDS
// ============================================================
// Rhythm carries the meaning so an active buzzer works; pitch is a bonus.

const Note JINGLE_BOOT[]      = {{2000, 60, 60}, {2600, 60, 0}};
const Note JINGLE_NEW_HIGH[]  = {{2200, 70, 45}, {2800, 70, 45}, {3400, 200, 0}};
const Note JINGLE_NEW_LOW[]   = {{1600, 80, 60}, {2000, 80, 0}};
const Note JINGLE_NEW_ECHO[]  = {{1000, 35, 0}};
const Note JINGLE_BEAT_HIGH[] = {{2800, 30, 70}, {2800, 30, 0}};
const Note JINGLE_BEAT_LOW[]  = {{1600, 30, 0}};
const Note JINGLE_MARK[]      = {{1200, 300, 0}};                  // one long beep: mark saved
const Note JINGLE_MUTE[]      = {{3000, 60, 50}, {2400, 60, 50}, {1800, 140, 0}}; // falling: muted
const Note JINGLE_UNMUTE[]    = {{1800, 60, 50}, {2400, 60, 50}, {3000, 140, 0}}; // rising: sound on
const Note JINGLE_ROAD[]      = {{1800, 60, 60}, {2400, 60, 60}, {3000, 60, 0}}; // three rising: road access starting
const Note JINGLE_MARK_NOTHING[] = {{1200, 300, 150}, {1200, 300, 0}}; // two long beeps: "nothing is there"

constexpr uint8_t AUDIO_PRIO_BEAT = 1;
constexpr uint8_t AUDIO_PRIO_ECHO = 2;
constexpr uint8_t AUDIO_PRIO_LOW  = 3;
constexpr uint8_t AUDIO_PRIO_HIGH = 4;
constexpr uint8_t AUDIO_PRIO_BOOT = 5;

// ============================================================
// STATIC BUFFERS & GLOBAL STATE
// ============================================================

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

Adafruit_SSD1306 display(
    SCREEN_WIDTH,
    SCREEN_HEIGHT,
    &Wire,
    OLED_RESET,
    OLED_I2C_HZ,
    OLED_I2C_HZ
);

QueueHandle_t mgmtIeQueue = nullptr;
QueueHandle_t headerQueue = nullptr;
QueueHandle_t bleQueue    = nullptr;
StaticQueue_t mgmtIeQueueControl;
StaticQueue_t headerQueueControl;
StaticQueue_t bleQueueControl;
alignas(4) uint8_t mgmtIeQueueStorage[MGMT_IE_QUEUE_SIZE * sizeof(MgmtIeEvent)];
alignas(4) uint8_t headerQueueStorage[HEADER_QUEUE_SIZE * sizeof(HeaderEvent)];
alignas(4) uint8_t bleQueueStorage[BLE_QUEUE_SIZE * sizeof(BleEvent)];

TrackedDevice deviceCache[DEVICE_CACHE_SIZE];

// Mark log state
QueueHandle_t obsQueue = nullptr;
StaticQueue_t obsQueueControl;
alignas(4) uint8_t obsQueueStorage[OBS_QUEUE_SIZE * sizeof(Observation)];
RecentEntry* recentTable = nullptr;     // recentCap entries, allocated in setup()
RecentIndex* recentIndex = nullptr;
uint16_t*    recentOrder = nullptr;     // scratch for sorting a mark
// Network names each recent device searched for (probe requests), up to
// RECENT_PROBES per device. PSRAM only; without it, just one (text).
typedef char ProbeNames[RECENT_PROBES][OBS_TEXT_LEN + 1];
ProbeNames*  recentProbes = nullptr;
size_t       recentCap   = 0;
bool         recentInPsram = false;
bool         dumpInProgress = false;    // mark log is printing: hold other serial output
ObsThrottle wifiObsThrottle{};     // touched only by the Wi-Fi task
ObsThrottle bleObsThrottle{};      // touched only by the BLE host task
volatile uint32_t obsDrops         = 0;
bool     markLogReady              = false;
uint8_t  markLogPercent            = 0;     // how full the mark storage is
bool     telemetryOn               = false; // 2-second serial stats ('t' toggles)
volatile uint32_t wifiFramesSeen   = 0;     // every frame the sniffer delivers
uint32_t wifiRearms                = 0;
uint32_t bleRestarts               = 0;
uint32_t bootCount                 = 0;
uint16_t markCount                 = 0;     // marks saved since power-on
uint32_t noticeUntil               = 0;
bool     noticeActive              = false;
bool     noticeDirty               = false;   // text changed: redraw now
char     noticeLine1[22]           = "";
char     noticeLine2[22]           = "";
DisplaySnapshot lastRenderedState{};
AlertView alertView{};
// The most recent alert, kept after it ends so a mark can still name it.
AlertView lastAlert{};
bool      lastAlertValid = false;
uint32_t  lastAlertAt    = 0;      // last moment it was still showing
AudioState audio{};
// Alert sounds muted (3 taps). Kept across the board's own reboots (leaving
// the web page), cleared when the power is cut.
bool     audioMuted = false;
RTC_NOINIT_ATTR uint32_t muteMagic;
constexpr uint32_t MUTE_MAGIC = 0x5EE0FF00UL;

// First-octet bitmap of the prefix table: lets the capture callback reject
// almost every address with one lookup instead of walking the whole table.
uint8_t prefixFirstOctetMap[32] = {0};

// Signature counters (modified exclusively on the loop() task for thread safety)
uint32_t prefixTargetHits[NUM_PREFIX_TARGETS]          = {0};
uint32_t ssidTargetHits[NUM_SSID_TARGETS]              = {0};
uint32_t wpsTargetHits[NUM_WPS_TARGETS]                = {0};
uint32_t bleCompanyTargetHits[NUM_BLE_COMPANY_TARGETS] = {0};
uint32_t bleServiceTargetHits[NUM_BLE_SERVICE_TARGETS] = {0};
uint32_t bleNameTargetHits[NUM_BLE_NAME_TARGETS]       = {0};
uint32_t suppressedDevicesTotal                        = 0;

uint32_t channelHopMs              = 250;

// Counters written from the Wi-Fi / BLE tasks, read from loop(). They are
// statistics only, so a torn read is harmless.
volatile uint32_t mgmtFramesSeen        = 0;  // probe req / probe resp / beacon inspected
volatile uint32_t mgmtFramesFiltered    = 0;  // ...and dropped as uninteresting
volatile uint32_t selectedFrames        = 0;
volatile uint32_t totalDrops            = 0;
volatile uint32_t selectedMgmtIeFrames  = 0;
volatile uint32_t selectedHeaderFrames  = 0;
volatile uint32_t mgmtIeDrops           = 0;
volatile uint32_t headerDrops           = 0;
volatile uint32_t ieFramesOverCaptureLimit = 0;
volatile uint32_t bleAdvertsSeen        = 0;
volatile uint32_t bleMatches            = 0;
volatile uint32_t bleAxonBwcHits        = 0;     // Axon body-cam tag adverts
volatile uint32_t bleRemoteIdHits       = 0;     // drone Remote ID adverts (Bluetooth)
volatile uint32_t bleDrops              = 0;

volatile uint32_t ieDistribution[3][6]  = {{0}};
volatile uint16_t maxIeObserved[3]      = {0};
volatile uint32_t wpsHitsCaptured       = 0;
volatile uint16_t maxWpsStartObserved   = 0;
volatile uint16_t maxWpsEndObserved     = 0;
volatile uint32_t ssidIeSeenFrames      = 0;
volatile uint32_t wildcardSsidFrames    = 0;
volatile uint32_t wildcardProbeFrames   = 0;

uint16_t mgmtQueueWatermarkBoot         = 0;
uint16_t mgmtQueueWatermarkInterval     = 0;
uint16_t headerQueueWatermarkBoot       = 0;
uint16_t headerQueueWatermarkInterval   = 0;

uint32_t cacheEvictions            = 0;
uint32_t sightingCount             = 0;
uint32_t encounterCount[4]         = {0};   // new encounters by AlertTier
uint32_t heartbeatCount            = 0;
uint32_t prefixHitsTotal           = 0;
uint32_t ssidHitsTotal             = 0;
uint32_t wpsHitsTotal              = 0;
uint32_t channelErrors             = 0;
uint32_t fcsTrimmedIeFrames        = 0;
uint32_t malformedIeFrames         = 0;
bool     displayReady              = false;
bool     bleReady                  = false;

// Home Wi-Fi maintenance mode
volatile uint32_t homeBeaconCount  = 0;     // written by the Wi-Fi task
volatile int8_t   homeBeaconRssi   = 0;
bool     maintActive               = false; // web mode: home Wi-Fi joined/joining, or road network up
bool     maintRoad                 = false; // web mode is the road network (own access point)
bool     maintConnected            = false;
uint32_t maintStartedAt            = 0;
uint32_t maintLastRequestAt        = 0;
bool     homeLocked                = false; // stay out until away from home a while
// Survives a software reboot (not a power cut): set when we reboot out of
// maintenance on purpose, so it does not re-enter while still parked at home.
RTC_NOINIT_ATTR uint32_t maintLockMagic;
constexpr uint32_t MAINT_LOCK_MAGIC = 0xA17E5EEDUL;
// Set while the web page is up, so a crash there is recognised on reboot.
RTC_NOINIT_ATTR uint32_t webActiveMagic;
constexpr uint32_t WEB_ACTIVE_MAGIC = 0x3EBACE00UL;
char     lastResetText[200]        = "";
bool     lastResetBad              = false;   // crash, freeze or brownout
bool     crashedInWeb              = false;   // ...while the web page was up
constexpr size_t HOME_BSSID_MAX     = 8;
uint8_t  homeBssidList[HOME_BSSID_MAX][6];      // HOME_WIFI_BSSID, parsed once in setup()
uint8_t  homeBssidCount            = 0;
bool     homeBssidFilter           = false;     // a list was given: only those access points count
// Web diagnostics. Wi-Fi events arrive on another task: it only sets these,
// loop() prints them.
volatile uint32_t webEvtFlags      = 0;       // bit 0 phone joined, 1 got IP, 2 left, 3 home link lost
volatile uint8_t  webEvtMac[6]     = {0};
volatile uint32_t webEvtIp         = 0;
volatile uint16_t webEvtReason     = 0;
uint32_t webRequests               = 0;
uint32_t webAuthFails              = 0;

// Alert scheduling
bool     trackingActive            = false;
uint32_t nextHeartbeatAt           = 0;
uint32_t lastAnnounceAt[4]         = {0};
bool     announcedOnce[4]          = {false};

volatile uint8_t currentChannel    = 0;
size_t   channelIndex              = 0;

// ============================================================
// CONSOLE: everything printed goes to USB serial AND a ring buffer the web
// page's Console tab reads, so the console works without a computer.
// ============================================================
constexpr size_t CONSOLE_RING_PSRAM = 64 * 1024;   // ~600 lines of history
constexpr size_t CONSOLE_RING_RAM   = 8 * 1024;
char*    consoleRing      = nullptr;
size_t   consoleRingSize  = 0;
uint32_t consoleWritten   = 0;    // total bytes ever written (ring position = this % size)

class ConsoleTee : public Print {
 public:
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* buf, size_t len) override {
    if (consoleRing) {
      for (size_t i = 0; i < len; ++i) consoleRing[(consoleWritten + i) % consoleRingSize] = static_cast<char>(buf[i]);
      consoleWritten += len;
    }
    Serial.write(buf, len);
    return len;
  }
  void flush() override { Serial.flush(); }
};
ConsoleTee Console;

static void consoleBegin() {
  if (psramFound()) consoleRing = static_cast<char*>(ps_malloc(CONSOLE_RING_PSRAM));
  if (consoleRing) {
    consoleRingSize = CONSOLE_RING_PSRAM;
  } else {
    consoleRing = static_cast<char*>(malloc(CONSOLE_RING_RAM));
    consoleRingSize = consoleRing ? CONSOLE_RING_RAM : 0;
  }
}

// ============================================================
// GPS: NMEA reader (RMC for time / position / speed, GGA for satellites)
// ============================================================
constexpr uint32_t GPS_BAUD          = 9600;
constexpr uint32_t GPS_FIX_FRESH_MS  = 5000;    // a position older than this is not used
constexpr float    GPS_MAX_HDOP      = 8.0f;    // worse = cold-start / parking-deck guess, not used

struct GpsState {
  bool     fix;            // last RMC said "valid"
  double   lat;
  double   lon;
  float    speedMph;
  uint8_t  sats;
  float    hdop;           // from GGA; 0 = field empty
  uint32_t ggaAt;          // millis() of the last GGA
  uint32_t fixAt;          // millis() of the last valid position
  uint32_t sentenceAt;     // millis() of the last good sentence (module alive)
  uint32_t sentences;
  uint32_t badChecksums;
  bool     clockSet;       // system clock set from GPS since boot
};
GpsState gps{};

static inline bool gpsSeen(uint32_t now)      { return gps.sentences && static_cast<int32_t>(now - gps.sentenceAt) < 3000; }
// Ages are signed so a `now` taken just before a sentence arrived reads as 0.
static inline bool gpsHdopOk(uint32_t now) {
  if (!gps.ggaAt || static_cast<int32_t>(now - gps.ggaAt) >= 3000) return true;   // module sends no GGA
  return gps.hdop > 0.0f && gps.hdop < GPS_MAX_HDOP;
}
static inline bool gpsFixFresh(uint32_t now) {
  return gps.fix && gps.fixAt && static_cast<int32_t>(now - gps.fixAt) < static_cast<int32_t>(GPS_FIX_FRESH_MS) &&
         gpsHdopOk(now);
}
// The clock survives the board's own reboots; after a power cut it is
// wrong (1970) until the GPS sets it.
static inline bool clockValid()               { return time(nullptr) > 1735689600; }   // after 2025-01-01

static void isoTime(char* out, size_t cap, time_t t) {
  struct tm tmv;
  gmtime_r(&t, &tmv);
  strftime(out, cap, "%Y-%m-%dT%H:%M:%SZ", &tmv);
}

// " time=... lat=... lon=... mph=..." with whatever is known. agoMs moves
// the time back (e.g. to when an alert started). pos = add the position.
static void gpsTag(char* out, size_t cap, uint32_t now, uint32_t agoMs, bool pos) {
  size_t n = 0;
  out[0] = '\0';
  if (clockValid()) {
    char t[24];
    isoTime(t, sizeof(t), time(nullptr) - static_cast<time_t>(agoMs / 1000UL));
    n += snprintf(out + n, cap - n, " time=%s", t);
  }
  if (!pos || n >= cap) return;
  if (gpsFixFresh(now)) {
    snprintf(out + n, cap - n, " lat=%.5f lon=%.5f mph=%.0f", gps.lat, gps.lon, gps.speedMph);
  } else if (ENABLE_GPS) {
    snprintf(out + n, cap - n, " gps=%s", gpsSeen(now) ? "nofix" : "none");
  }
}

// Where (and how fast) we were for a tracked device. Only called with a fresh fix.
static inline int8_t* timelineOf(const TrackedDevice& d);

// First and last position it was heard from directly (with a fresh fix).
static inline void noteDeviceTrail(TrackedDevice& d) {
  if (!d.hasTrail) { d.hasTrail = true; d.fromLat = gps.lat; d.fromLon = gps.lon; }
  d.toLat = gps.lat;
  d.toLon = gps.lon;
}

static inline void setDevicePos(TrackedDevice& d) {
  d.hasPos = true;
  d.posLat = gps.lat;
  d.posLon = gps.lon;
  d.posMph = static_cast<uint8_t>(constrain(lroundf(gps.speedMph), 0L, 255L));
}

#if ENABLE_GPS
static int nmeaHex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

static double nmeaCoord(const char* v, const char* hemi) {
  const double x = atof(v);
  const int deg = static_cast<int>(x / 100.0);
  double d = deg + (x - deg * 100.0) / 60.0;
  if (hemi[0] == 'S' || hemi[0] == 'W') d = -d;
  return d;
}

// Days from 1970-01-01 to a calendar date (no time zone involved).
static int64_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const int64_t yoe = y - era * 400;
  const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

static void gpsSetClock(const char* hhmmss, const char* ddmmyy) {
  if (strlen(hhmmss) < 6 || strlen(ddmmyy) != 6) return;
  for (int i = 0; i < 6; ++i)
    if (!isdigit(static_cast<unsigned char>(hhmmss[i])) || !isdigit(static_cast<unsigned char>(ddmmyy[i]))) return;
  auto two = [](const char* p) { return (p[0] - '0') * 10 + (p[1] - '0'); };
  const int hh = two(hhmmss), mi = two(hhmmss + 2), ss = two(hhmmss + 4);
  const int dd = two(ddmmyy), mo = two(ddmmyy + 2), yy = two(ddmmyy + 4);
  if (mo < 1 || mo > 12 || dd < 1 || dd > 31 || hh > 23 || mi > 59 || ss > 60 || yy < 25) return;
  const time_t t = static_cast<time_t>(daysFromCivil(2000 + yy, mo, dd) * 86400LL + hh * 3600 + mi * 60 + ss);
  const long long drift = static_cast<long long>(time(nullptr)) - static_cast<long long>(t);
  if (!gps.clockSet || drift > 2 || drift < -2) {
    struct timeval tv = {t, 0};
    settimeofday(&tv, nullptr);
    if (!gps.clockSet) {
      char iso[24];
      isoTime(iso, sizeof(iso), t);
      Console.printf("[GPS] clock set from GPS: %s\n", iso);
    }
    gps.clockSet = true;
  }
}

static void gpsHandleSentence(char* line) {
  // Checksum: XOR of everything between '$' and '*'.
  char* star = strchr(line, '*');
  if (!star || strlen(star) < 3) { ++gps.badChecksums; return; }
  uint8_t sum = 0;
  for (const char* q = line + 1; q < star; ++q) sum ^= static_cast<uint8_t>(*q);
  const int hi = nmeaHex(star[1]), lo = nmeaHex(star[2]);
  if (hi < 0 || lo < 0 || sum != ((hi << 4) | lo)) { ++gps.badChecksums; return; }
  *star = '\0';

  char* f[20];
  size_t nf = 0;
  f[nf++] = line;
  for (char* q = line; *q && nf < 20; ++q) {
    if (*q == ',') { *q = '\0'; f[nf++] = q + 1; }
  }
  if (strlen(f[0]) != 6) return;
  const char* type = f[0] + 3;       // "$GNRMC" -> "RMC", any talker (GP, GN, BD, GL)
  gps.sentenceAt = millis();
  ++gps.sentences;

  if (strcmp(type, "RMC") == 0 && nf >= 10) {
    gps.fix = (f[2][0] == 'A');
    if (gps.fix && f[3][0] && f[5][0]) {
      gps.lat      = nmeaCoord(f[3], f[4]);
      gps.lon      = nmeaCoord(f[5], f[6]);
      gps.speedMph = static_cast<float>(atof(f[7]) * 1.15078);   // knots -> mph
      gps.fixAt    = millis();
      gpsSetClock(f[1], f[9]);
    }
  } else if (strcmp(type, "GGA") == 0 && nf >= 9) {
    gps.sats = static_cast<uint8_t>(atoi(f[7]));
    gps.hdop  = static_cast<float>(atof(f[8]));
    gps.ggaAt = millis();
  }
}
#endif  // ENABLE_GPS

// Called every loop() pass: read whatever the GPS has sent.
static void gpsPoll() {
#if ENABLE_GPS
  static char   line[100];
  static size_t len = 0;
  while (Serial1.available() > 0) {
    const char c = static_cast<char>(Serial1.read());
    if (c == '$') len = 0;
    if (c == '\r' || c == '\n') {
      if (len > 6 && line[0] == '$') { line[len] = '\0'; gpsHandleSentence(line); }
      len = 0;
    } else if (len < sizeof(line) - 1) {
      line[len++] = c;
    } else {
      len = 0;                     // overlong: not NMEA, start over
    }
  }
#endif
}

// ============================================================
// INLINE UTILITIES & MAC CLASSIFICATION
// ============================================================

static inline bool timeReached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

// Atomic: some counters are bumped from both the Wi-Fi and the Bluetooth
// task, which can run on different cores.
static inline void bump(volatile uint32_t& counter) {
  __atomic_fetch_add(&counter, 1U, __ATOMIC_RELAXED);
}

static inline uint64_t macToUint64(const uint8_t mac[6]) {
  return (static_cast<uint64_t>(mac[0]) << 40) |
         (static_cast<uint64_t>(mac[1]) << 32) |
         (static_cast<uint64_t>(mac[2]) << 24) |
         (static_cast<uint64_t>(mac[3]) << 16) |
         (static_cast<uint64_t>(mac[4]) << 8)  |
          static_cast<uint64_t>(mac[5]);
}

static inline bool macMatches(const uint8_t a[6], const uint8_t b[6]) {
  return memcmp(a, b, 6) == 0;
}

static inline bool isGroupMac(const uint8_t mac[6]) {
  return (mac[0] & 0x01) != 0;
}

static inline int16_t rssiToFp8(int8_t rssi) {
  return static_cast<int16_t>(static_cast<int32_t>(rssi) * 256);
}

static inline int8_t fp8ToRssi(int16_t fp8) {
  return static_cast<int8_t>(fp8 / 256);
}

static inline bool serialReady() {
  // On the native USB port this is true only while a monitor is attached,
  // so periodic prints never stall the loop when running from a charger.
  return static_cast<bool>(Serial);
}

// For the [DET] lines: always (they are kept for the web console), except
// while the mark log is being printed, so they cannot land in the middle of it.
static inline bool detLogReady() {
  extern bool dumpInProgress;
  return !dumpInProgress;
}

// Temporarily enables blocking USB CDC writes when executing interactive serial
// commands (like 'l', 's', 'd') so large dumps never drop bytes or splice lines.
static inline void setSerialBlockingMode(bool blocking) {
#if ARDUINO_USB_CDC_ON_BOOT
  // Only block while a computer is actually listening; otherwise a command
  // sent from the web console would stall on a full, unread USB buffer.
  if (blocking) {
    if (serialReady()) Serial.setTxTimeoutMs(200);
  } else {
    Serial.flush();
    Serial.setTxTimeoutMs(0);
  }
#else
  (void)blocking;
#endif
}

static void normalizeStringUpper(char* str) {
  for (; *str; ++str) {
    *str = static_cast<char>(toupper(static_cast<unsigned char>(*str)));
  }
}

static const char* tierName(uint8_t tier) {
  switch (tier) {
    case TIER_HIGH: return "HIGH";
    case TIER_LOW:  return "LOW";
    case TIER_ECHO: return "ECHO";
    default:        return "NONE";
  }
}

// ============================================================
// SIGNATURE DATABASE VALIDATOR & MATCHERS
// ============================================================

static inline uint8_t prefixFirstOctet(const MacPrefixTarget& t) {
  return static_cast<uint8_t>((t.prefix >> (t.prefixBits - 8)) & 0xFF);
}

static bool validateSignatureDatabases() {
  bool valid = true;
  memset(prefixFirstOctetMap, 0, sizeof(prefixFirstOctetMap));

  for (size_t i = 0; i < NUM_PREFIX_TARGETS; ++i) {
    const MacPrefixTarget& t = prefixTargets[i];
    const bool validBits = (t.prefixBits == 24 || t.prefixBits == 28 || t.prefixBits == 36);
    if (!validBits) {
      Console.printf("[ERR] Prefix target %u (%s): invalid bit length %u\n",
                    static_cast<unsigned>(i), t.label ? t.label : "<null>", t.prefixBits);
      valid = false;
      continue;
    }

    const uint64_t maxPrefixVal = (1ULL << t.prefixBits) - 1ULL;
    if (t.prefix > maxPrefixVal) {
      Console.printf("[ERR] Prefix target %u (%s): prefix 0x%llX overflows %u bits\n",
                    static_cast<unsigned>(i), t.label ? t.label : "<null>",
                    static_cast<unsigned long long>(t.prefix), t.prefixBits);
      valid = false;
    }

    if (!t.label || !*t.label) {
      Console.printf("[ERR] Prefix target %u: empty label\n", static_cast<unsigned>(i));
      valid = false;
    }

    const uint8_t first = prefixFirstOctet(t);
    if (first & 0x01) {
      Console.printf("[ERR] Prefix target %u (%s): group-address prefix\n",
                    static_cast<unsigned>(i), t.label ? t.label : "<null>");
      valid = false;
    }
    prefixFirstOctetMap[first >> 3] |= static_cast<uint8_t>(1U << (first & 7));
  }

  for (size_t i = 0; i < NUM_SSID_TARGETS; ++i) {
    const SSIDTarget& t = ssidTargets[i];
    if (!t.pattern || !*t.pattern || !t.label || !*t.label) {
      valid = false;
    }
  }

  for (size_t i = 0; i < NUM_IGNORED_SSID_TARGETS; ++i) {
    const IgnoredSSIDTarget& t = ignoredSsidTargets[i];
    if (!t.pattern || !*t.pattern) {
      valid = false;
    }
  }

  for (size_t i = 0; i < NUM_WPS_TARGETS; ++i) {
    const WPSTarget& t = wpsTargets[i];
    if (!t.pattern || !*t.pattern || !t.label || !*t.label) {
      valid = false;
    }
  }

  for (size_t i = 0; i < NUM_BLE_NAME_TARGETS; ++i) {
    const BleNameTarget& t = bleNameTargets[i];
    if (!t.pattern || !*t.pattern || !t.label || !*t.label) {
      valid = false;
    }
  }

  return valid;
}

static const MacPrefixTarget* matchPrefix(uint64_t mac48, size_t* matchedIndex = nullptr) {
  const MacPrefixTarget* best = nullptr;
  size_t bestIndex = 0;
  uint8_t bestBits = 0;

  for (size_t i = 0; i < NUM_PREFIX_TARGETS; ++i) {
    const auto& target = prefixTargets[i];
    const uint64_t extracted = mac48 >> (48 - target.prefixBits);

    if (extracted == target.prefix && target.prefixBits > bestBits) {
      best = &target;
      bestIndex = i;
      bestBits = target.prefixBits;
    }
  }

  if (best && matchedIndex) *matchedIndex = bestIndex;
  return best;
}

// Prefix lookup for a unicast address, with the one-lookup fast reject.
static inline const MacPrefixTarget* matchEligiblePrefixMac(const uint8_t mac[6],
                                                             size_t* matchedIndex = nullptr) {
  if (!mac || isGroupMac(mac)) return nullptr;
  if (!(prefixFirstOctetMap[mac[0] >> 3] & (1U << (mac[0] & 7)))) return nullptr;
  return matchPrefix(macToUint64(mac), matchedIndex);
}

// A router's extra networks (VAPs) are often sent from a derived address:
// its own with the locally-administered bit set in the first byte (and
// sometimes a counter), e.g. Cradlepoint 00:30:44:.. -> 02:/06:30:44:..
// For beacons / probe responses from such an address, try the router
// vendors whose block starts 00: (Cradlepoint, Sierra, Peplink...), LOW only.
// Never for probe requests: phones use random addresses for those.
static const MacPrefixTarget* matchDerivedApPrefix(const uint8_t mac[6], size_t* matchedIndex = nullptr) {
  if (!mac || isGroupMac(mac) || (mac[0] & 0x03) != 0x02) return nullptr;
  uint8_t base[6];
  memcpy(base, mac, 6);
  base[0] = 0x00;
  size_t idx = 0;
  const MacPrefixTarget* t = matchEligiblePrefixMac(base, &idx);
  if (!t || t->category != CAT_LOW_CONF || t->prefixBits != 24) return nullptr;
  if (matchedIndex) *matchedIndex = idx;
  return t;
}

// One pattern test, shared by the SSID, ignore-list and WPS matchers.
// text must already be upper-cased.
static bool patternMatches(const char* text, const char* pattern, MatchMode mode) {
  switch (mode) {
    case MATCH_EXACT:
      return strcmp(text, pattern) == 0;
    case MATCH_PREFIX:
      return strncmp(text, pattern, strlen(pattern)) == 0;
    case MATCH_SUBSTRING:
      return strstr(text, pattern) != nullptr;
    case MATCH_WORD:
    case MATCH_TOKEN_END: {
      const size_t n = strlen(pattern);
      for (const char* p = strstr(text, pattern); p; p = strstr(p + 1, pattern)) {
        const char after = p[n];
        const bool letterAfter  = (after >= 'A' && after <= 'Z');
        const bool letterBefore = (p > text) && (p[-1] >= 'A' && p[-1] <= 'Z');
        if (!letterAfter && (mode == MATCH_TOKEN_END || !letterBefore)) return true;
      }
      return false;
    }
  }
  return false;
}

static const SSIDTarget* matchSSID(const char* ssid, size_t* matchedIndex = nullptr) {
  if (!ssid || !*ssid) return nullptr;

  for (size_t i = 0; i < NUM_SSID_TARGETS; ++i) {
    if (patternMatches(ssid, ssidTargets[i].pattern, ssidTargets[i].mode)) {
      if (matchedIndex) *matchedIndex = i;
      return &ssidTargets[i];
    }
  }
  return nullptr;
}

static bool matchIgnoredSSID(const char* ssid) {
  if (!ssid || !*ssid) return false;

  for (size_t i = 0; i < NUM_IGNORED_SSID_TARGETS; ++i) {
    if (patternMatches(ssid, ignoredSsidTargets[i].pattern, ignoredSsidTargets[i].mode)) return true;
  }
  return false;
}

// True for an (upper-cased) factory network name on the quiet list.
static bool matchQuietSSID(const char* ssid) {
  if (!ssid || !*ssid) return false;
  for (size_t i = 0; i < NUM_QUIET_SSID_TARGETS; ++i) {
    const size_t n = strlen(quietSsidTargets[i]);
    if (strncmp(ssid, quietSsidTargets[i], n) != 0) continue;
    const char* tail = ssid + n;
    if (strlen(tail) == 3 && isxdigit(static_cast<unsigned char>(tail[0])) &&
        isxdigit(static_cast<unsigned char>(tail[1])) &&
        isxdigit(static_cast<unsigned char>(tail[2]))) return true;
  }
  return false;
}

// Decides what an (upper-cased) SSID means. Police patterns are checked
// first: a HIGH police match always wins, so "County Police Systems" or
// "FIRE-POLICE" can never be swallowed by the ignore list. Otherwise an
// ignore-list hit marks the network as a non-police fleet and cancels any
// LOW match (e.g. "FIRSTNET-EMS 12").
static const SSIDTarget* classifySsid(const char* ssid, bool& ignored, size_t* matchedIndex = nullptr) {
  ignored = false;
  const SSIDTarget* match = matchSSID(ssid, matchedIndex);
  if (match && match->category == CAT_HIGH_CONF) return match;
  if (matchIgnoredSSID(ssid)) {
    ignored = true;
    return nullptr;
  }
  return match;
}

static const WPSTarget* matchWPS(const char* wps, size_t* matchedIndex = nullptr) {
  if (!wps || !*wps) return nullptr;

  for (size_t i = 0; i < NUM_WPS_TARGETS; ++i) {
    if (patternMatches(wps, wpsTargets[i].pattern, wpsTargets[i].mode)) {
      if (matchedIndex) *matchedIndex = i;
      return &wpsTargets[i];
    }
  }
  return nullptr;
}

// ============================================================
// IE DESERIALIZER (used by both the capture callback and loop())
// ============================================================

static void initIeParseResult(IeParseResult& out) {
  memset(&out, 0, sizeof(out));
  out.wpsMatch = nullptr;
  out.wpsMatchIdx = 0;
  out.wpsStart = -1;
  out.wpsEnd = -1;
  out.structurallyComplete = true;
}

static void parseRawIEsSingle(const uint8_t* data, size_t len, IeParseResult& out) {
  initIeParseResult(out);
  if (!data || len == 0) return;

  size_t offset = 0;
  while (offset + 2 <= len) {
    const uint8_t tagId  = data[offset];
    const uint8_t tagLen = data[offset + 1];

    if (offset + 2 + tagLen > len) {
      out.structurallyComplete = false;
      out.bytesConsumed = offset;
      return;
    }

    const uint8_t* tagPayload = data + offset + 2;
    ++out.ieCount;

    if (tagId == 0 && !out.ssidIeSeen) {
      out.ssidIeSeen = true;
      out.wildcardSsid = (tagLen == 0);
      if (tagLen > 0) {
        const size_t copyLen = (tagLen > AIRWATCH_MAX_SSID_LEN) ? AIRWATCH_MAX_SSID_LEN : tagLen;
        memcpy(out.ssid, tagPayload, copyLen);
        out.ssid[copyLen] = '\0';
        out.ssidLen = static_cast<uint8_t>(copyLen);
      }
    } else if (tagId == 221 && tagLen >= 4) {
      const uint32_t vendorOui = (static_cast<uint32_t>(tagPayload[0]) << 16) |
                                 (static_cast<uint32_t>(tagPayload[1]) << 8)  |
                                  static_cast<uint32_t>(tagPayload[2]);

      if (vendorOui == WIFI_REMOTE_ID_OUI && tagPayload[3] == REMOTE_ID_APP_CODE) out.remoteId = true;

      // Wi-Fi Alliance WPS: OUI 00:50:F2 Subtype 0x04
      if (vendorOui == 0x0050F2 && tagPayload[3] == 0x04) {
        if (out.wpsStart == -1) {
          out.wpsStart = static_cast<int16_t>(offset);
          out.wpsEnd   = static_cast<int16_t>(offset + 2 + tagLen);
        }

        size_t subOffset = 4;
        while (subOffset + 4 <= tagLen) {
          const uint16_t attrType = (static_cast<uint16_t>(tagPayload[subOffset]) << 8) |
                                     tagPayload[subOffset + 1];
          const uint16_t attrLen  = (static_cast<uint16_t>(tagPayload[subOffset + 2]) << 8) |
                                     tagPayload[subOffset + 3];

          if (subOffset + 4 + attrLen > tagLen) break;
          const uint8_t* attrVal = tagPayload + subOffset + 4;

          const bool isRelevantAttr = (attrType == 0x1011 || // Device Name
                                       attrType == 0x1021 || // Manufacturer
                                       attrType == 0x1023 || // Model Name
                                       attrType == 0x1024);  // Model Number

          if (isRelevantAttr && attrLen > 0) {
            char tempAttr[MAX_WPS_LEN + 1];
            const size_t copyLen = (attrLen > MAX_WPS_LEN) ? MAX_WPS_LEN : attrLen;
            memcpy(tempAttr, attrVal, copyLen);
            tempAttr[copyLen] = '\0';
            normalizeStringUpper(tempAttr);

            size_t hitIdx = 0;
            const WPSTarget* match = matchWPS(tempAttr, &hitIdx);
            if (match) {
              if (!out.wpsMatch ||
                  (match->category == CAT_HIGH_CONF && out.wpsMatch->category != CAT_HIGH_CONF)) {
                out.wpsMatch    = match;
                out.wpsMatchIdx = hitIdx;
              }
            }
          }
          subOffset += 4 + attrLen;
        }
      }
    }

    offset += 2 + tagLen;
  }

  out.bytesConsumed = offset;
  out.structurallyComplete = (offset == len);
}

static void parseRawIEsFlexible(const uint8_t* data, size_t len, bool allowFcsTrim,
                                IeParseResult& out, bool& usedFcsTrim) {
  IeParseResult full{};
  parseRawIEsSingle(data, len, full);
  usedFcsTrim = false;

  // Trimming the 4-byte FCS can only help a frame that did not parse cleanly.
  if (full.structurallyComplete || !allowFcsTrim || len < 4) {
    out = full;
    return;
  }

  IeParseResult trimmed{};
  parseRawIEsSingle(data, len - 4, trimmed);

  bool chooseTrimmed = false;
  if (trimmed.structurallyComplete && !full.structurallyComplete) {
    chooseTrimmed = true;
  } else if (!trimmed.structurallyComplete && !full.structurallyComplete &&
             trimmed.bytesConsumed > full.bytesConsumed) {
    chooseTrimmed = true;
  }

  if (chooseTrimmed) {
    out = trimmed;
    usedFcsTrim = true;
  } else {
    out = full;
  }
}

// ============================================================
// BUZZER + NON-BLOCKING NOTE SEQUENCER
// ============================================================

static void buzzerOn(uint16_t freqHz) {
#if ENABLE_BUZZER_HARDWARE
  #if BUZZER_IS_PASSIVE
    tone(BUZZER_PIN, freqHz);
  #else
    (void)freqHz;
    digitalWrite(BUZZER_PIN, HIGH);
  #endif
#else
  (void)freqHz;
#endif
}

static void buzzerOff() {
#if ENABLE_BUZZER_HARDWARE
  #if BUZZER_IS_PASSIVE
    noTone(BUZZER_PIN);
  #else
    digitalWrite(BUZZER_PIN, LOW);
  #endif
#endif
}

// Long blocking jobs (saving a mark, log downloads, uploads) keep audioTick()
// from running, so an active buzzer caught mid-note would stay on: stop first.
static void audioStop() {
  buzzerOff();
  audio.active = false;
}

static uint32_t jingleDurationMs(const Note* notes, uint8_t count) {
  uint32_t total = 0;
  for (uint8_t i = 0; i < count; ++i) total += notes[i].onMs + notes[i].gapMs;
  return total;
}

// Starts a jingle unless something more important is already playing.
static bool audioPlay(const Note* notes, uint8_t count, uint8_t priority, uint32_t now) {
  if (!notes || count == 0) return false;
  // Muted: alert jingles and heartbeats stay silent. Boot, mark and mode
  // sounds (AUDIO_PRIO_BOOT) still play, so button presses are confirmed.
  if (audioMuted && priority < AUDIO_PRIO_BOOT) return false;
  if (audio.active && priority < audio.priority) return false;

  if (audio.active && audio.sounding) buzzerOff();

  const uint8_t n = (count > AUDIO_MAX_NOTES) ? AUDIO_MAX_NOTES : count;
  for (uint8_t i = 0; i < n; ++i) audio.notes[i] = notes[i];
  audio.count    = n;
  audio.index    = 0;
  audio.priority = priority;
  audio.active   = true;
  audio.sounding = true;
  audio.phaseEnd = now + audio.notes[0].onMs;
  buzzerOn(audio.notes[0].freqHz);
  return true;
}

// Serviced from loop() only.
static void audioTick(uint32_t now) {
  if (!audio.active || !timeReached(now, audio.phaseEnd)) return;

  if (audio.sounding) {
    buzzerOff();
    audio.sounding = false;
    audio.phaseEnd = now + audio.notes[audio.index].gapMs;
    if (audio.notes[audio.index].gapMs != 0) return;
  }

  ++audio.index;
  if (audio.index >= audio.count) {
    audio.active = false;
    return;
  }

  audio.sounding = true;
  audio.phaseEnd = now + audio.notes[audio.index].onMs;
  buzzerOn(audio.notes[audio.index].freqHz);
}

// ============================================================
// DIAGNOSTIC PRINT HELPERS
// ============================================================

static void formatReasonCodes(uint16_t mask, char* out, size_t cap) {
  if (!out || cap == 0) return;
  out[0] = '\0';
  strlcat(out, "[", cap);
  if (mask & REASON_PREFIX_HIGH)    strlcat(out, "PRF_HI|", cap);
  if (mask & REASON_PREFIX_LOW)     strlcat(out, "PRF_LO|", cap);
  if (mask & REASON_SSID_HIGH)      strlcat(out, "SSID_HI|", cap);
  if (mask & REASON_SSID_LOW)       strlcat(out, "SSID_LO|", cap);
  if (mask & REASON_WPS_HIGH)       strlcat(out, "WPS_HI|", cap);
  if (mask & REASON_WPS_LOW)        strlcat(out, "WPS_LO|", cap);
  if (mask & REASON_DIRECTED_PROBE) strlcat(out, "PROBE|", cap);
  if (mask & REASON_WILDCARD_PROBE) strlcat(out, "WILD_PRB|", cap);
  if (mask & REASON_BLE_ADDR)       strlcat(out, "BLE_ADDR|", cap);
  if (mask & REASON_BLE_COMPANY)    strlcat(out, "BLE_CID|", cap);
  if (mask & REASON_BLE_SERVICE)    strlcat(out, "BLE_SVC|", cap);
  if (mask & REASON_BLE_NAME)       strlcat(out, "BLE_NAME|", cap);
  if (mask & REASON_FLEET_SSID)     strlcat(out, "FLEET_SSID|", cap);
  if (mask & REASON_DRONE)          strlcat(out, "DRONE|", cap);
  if (mask & REASON_ECHO)           strlcat(out, "ECHO|", cap);
  if (mask & REASON_DATA_FRAME)     strlcat(out, "DATA", cap);
  strlcat(out, "]", cap);
}

static void printReasonCodes(uint16_t mask) {
  char text[112];
  formatReasonCodes(mask, text, sizeof(text));
  Console.print(text);
}

// Tier as shown to a person: a capped quiet-list router reads QUIET.
static const char* shownTierName(uint8_t tier, bool quiet) {
  return (quiet && tier == TIER_ECHO) ? "QUIET" : tierName(tier);
}

static void printDeviceLine(const char* tag, const TrackedDevice& d, uint32_t now) {
  Console.printf("%s %02X:%02X:%02X:%02X:%02X:%02X %-5s %-4s ",
                tag, d.mac[0], d.mac[1], d.mac[2], d.mac[3], d.mac[4], d.mac[5],
                d.suppressed ? "IGN" : shownTierName(d.tier, d.quietCap),
                (d.src == SRC_BLE) ? "BLE" : "WIFI");
  if (d.hasRssi) Console.printf("rssi=%d ", fp8ToRssi(d.smoothedRssiFp8));
  else           Console.print(F("rssi=-- "));
  if (d.src == SRC_WIFI) Console.printf("ch=%u ", d.channel);
  Console.printf("age=%lums seen=%u \"%s\" ",
                static_cast<unsigned long>(now - d.lastSeen),
                static_cast<unsigned>(d.sightings),
                d.label ? d.label : "");
  if (d.ssid[0]) Console.printf("ssid=\"%s\" ", d.ssid);
  printReasonCodes(d.reasonMask);
  Console.println();
}

// ============================================================
// TARGET TRACKER
// ============================================================

#if ENABLE_MARK_LOG
static void journalAlert(const TrackedDevice& d, uint32_t now, bool ongoing = false, const char* ended = nullptr);
static void journalFlushOngoing();
#endif

static inline int8_t* timelineOf(const TrackedDevice& d) {
  if (!deviceTimeline) return nullptr;
  return deviceTimeline + static_cast<size_t>(&d - deviceCache) * TIMELINE_SECONDS;
}

static inline void timelineNote(const TrackedDevice& d, int8_t rssi, uint32_t now) {
  int8_t* t = timelineOf(d);
  if (!t || rssi >= 0) return;
  const uint32_t i = (now - d.firstSeen) / 1000UL;
  if (i >= TIMELINE_SECONDS) return;
  if (t[i] == 0 || rssi > t[i]) t[i] = rssi;
}

// " tl=-80,-78,,-71 from=lat,lon to=lat,lon" (empty = not heard that second)
static void printTimeline(Print& out, const TrackedDevice& d) {
  if (const int8_t* t = timelineOf(d)) {
    int last = -1;
    for (int i = 0; i < static_cast<int>(TIMELINE_SECONDS); ++i) if (t[i]) last = i;
    if (last >= 0) {
      out.print(" tl=");
      for (int i = 0; i <= last; ++i) {
        if (i) out.print(',');
        if (t[i]) out.print(static_cast<int>(t[i]));
      }
    }
  }
  if (d.hasTrail) out.printf(" from=%.5f,%.5f to=%.5f,%.5f", d.fromLat, d.fromLon, d.toLat, d.toLon);
}

static void startEncounter(TrackedDevice& d, const uint8_t mac[6], uint32_t now) {
#if ENABLE_MARK_LOG
  // The slot is being reused (evicted from a full cache, or after a long
  // stall): an alert in it that never reached the journal goes there now.
  if (d.valid && !d.journaled && !d.suppressed && d.announcedTier != TIER_NONE) {
    d.journaled = true;
    journalAlert(d, now, (now - d.lastSeen) < PRESENCE_TIMEOUT_MS);
  }
#endif
  memset(&d, 0, sizeof(d));
  if (int8_t* t = timelineOf(d)) memset(t, 0, TIMELINE_SECONDS);
  d.valid           = true;
  d.suppressed      = false;
  memcpy(d.mac, mac, 6);
  d.tier            = TIER_NONE;
  d.announcedTier   = TIER_NONE;
  d.echoOnly        = true;
  d.hasRssi         = false;
  d.smoothedRssiFp8 = rssiToFp8(-100);
  d.firstSeen       = now;
  d.lastSeen        = now;
  d.lastLogAt       = now - DETECTION_RELOG_MS;
  d.holdUntil       = now;
  d.label           = nullptr;
  d.labelRank       = LABEL_RANK_NONE;
  d.bestRssi        = -127;
}

// Finds the entry for this MAC, or claims one. newEncounter is set when the
// device is new or has been silent for ENCOUNTER_GAP_MS.
static TrackedDevice* resolveDevice(const uint8_t mac[6], uint32_t now, bool& newEncounter) {
  newEncounter = false;

  for (size_t i = 0; i < DEVICE_CACHE_SIZE; ++i) {
    TrackedDevice& d = deviceCache[i];
    if (d.valid && macMatches(d.mac, mac)) {
      if ((now - d.lastSeen) >= ENCOUNTER_GAP_MS) {
        startEncounter(d, mac, now);
        newEncounter = true;
      }
      return &d;
    }
  }

  newEncounter = true;

  // A free slot first. Then a stale one (silent for ENCOUNTER_GAP_MS),
  // preferring one past MARK_HISTORY_MS, which nothing reads any more, and
  // otherwise the one silent longest: recent alerts stay for EARLIER lines
  // and the web page.
  size_t stale = DEVICE_CACHE_SIZE;
  uint32_t staleAge = 0;
  for (size_t i = 0; i < DEVICE_CACHE_SIZE; ++i) {
    TrackedDevice& d = deviceCache[i];
    if (!d.valid) {
      startEncounter(d, mac, now);
      return &d;
    }
    const uint32_t age = now - d.lastSeen;
    if (age >= ENCOUNTER_GAP_MS && (stale == DEVICE_CACHE_SIZE || age > staleAge)) {
      stale = i;
      staleAge = age;
    }
  }
  if (stale < DEVICE_CACHE_SIZE) {
    startEncounter(deviceCache[stale], mac, now);
    return &deviceCache[stale];
  }

  // Cache full of live entries: evict suppressed devices first, then lowest tier, then stalest.
  size_t   victimIndex = 0;
  uint8_t  minimumTier = 255;
  uint32_t maximumAge  = 0;

  for (size_t i = 0; i < DEVICE_CACHE_SIZE; ++i) {
    const TrackedDevice& d = deviceCache[i];
    const uint8_t effectiveTier = d.suppressed ? static_cast<uint8_t>(TIER_NONE) : d.tier;
    const uint32_t age = now - d.lastSeen;
    if (effectiveTier < minimumTier) {
      minimumTier = effectiveTier;
      maximumAge  = age;
      victimIndex = i;
    } else if (effectiveTier == minimumTier && age > maximumAge) {
      maximumAge  = age;
      victimIndex = i;
    }
  }

  ++cacheEvictions;
  startEncounter(deviceCache[victimIndex], mac, now);
  return &deviceCache[victimIndex];
}

// Marks a device as a non-police fleet (e.g. Fire/EMS) so its router OUI
// does not trigger or continue LOW/ECHO alerts during this encounter.
// Records what was heard from a silenced device too, so its mark log line
// shows signal, channel, network name and why it was silenced.
// event: the frame that caused it. ssidText: that frame's network name if
// it was the device's own (beacon / probe response), else nullptr.
static bool macsAreSiblings(const uint8_t a[6], const uint8_t b[6]);

// Silences one tracked device as a non-police fleet. If it had already
// alerted, that alert goes into the journal first (marked ended=fleet), so
// the review still shows what beeped and why it stopped.
static void silenceTracked(TrackedDevice& d, const char* reasonLabel, uint32_t now) {
#if ENABLE_MARK_LOG
  if (!d.journaled && d.announcedTier != TIER_NONE) {
    d.journaled = true;
    journalAlert(d, now, (now - d.lastSeen) < PRESENCE_TIMEOUT_MS, "fleet");
  }
#endif
  d.suppressed = true;
  d.tier       = TIER_NONE;
  if (reasonLabel) d.label = reasonLabel;
  ++suppressedDevicesTotal;
  if (detLogReady()) printDeviceLine("[DET] IGN ", d, now);
}

static void suppressDevice(const uint8_t mac[6], const char* reasonLabel, const HeaderEvent& event,
                           const char* ssidText, uint16_t reasons, uint32_t now) {
  if (!mac || isGroupMac(mac)) return;
  bool newEncounter = false;
  TrackedDevice* d = resolveDevice(mac, now, newEncounter);
  if (!d) return;

  d->lastSeen = now;
  if (d->sightings < 0xFFFF) ++d->sightings;
  d->reasonMask |= reasons;
  if (macMatches(mac, event.addr2)) {          // it transmitted this frame: the signal is its own
    const int16_t rawFp8 = rssiToFp8(event.rssi);
    if (!d->hasRssi) { d->smoothedRssiFp8 = rawFp8; d->hasRssi = true; }
    else d->smoothedRssiFp8 = static_cast<int16_t>(d->smoothedRssiFp8 +
                              (static_cast<int32_t>(rawFp8) - d->smoothedRssiFp8) / 2);
    d->echoOnly       = false;
    d->lastDirectSeen = now;
    d->src            = SRC_WIFI;
    d->channel        = event.channel;
    if (ssidText && ssidText[0]) {
      strncpy(d->ssid, ssidText, sizeof(d->ssid) - 1);
      d->ssid[sizeof(d->ssid) - 1] = '\0';
    }
  } else if (!d->hasRssi) {
    d->src     = SRC_WIFI;
    d->channel = event.channel;
  }

  if (!d->suppressed && d->tier < TIER_HIGH) {
    silenceTracked(*d, reasonLabel, now);
    // Its sibling radios already in the cache go quiet now too, not only
    // when they next transmit: an alert on 00:30:44:... stops the moment its
    // router's 02:30:44:... fleet network is heard.
    for (size_t i = 0; i < DEVICE_CACHE_SIZE; ++i) {
      TrackedDevice& o = deviceCache[i];
      if (&o != d && o.valid && !o.suppressed && o.tier < TIER_HIGH &&
          (now - o.lastSeen) < ENCOUNTER_GAP_MS && macsAreSiblings(o.mac, d->mac)) {
        silenceTracked(o, reasonLabel, now);
      }
    }
  }
}

// True when two addresses look like radios of the same physical router:
// same address apart from a small step in the last byte, or apart from the
// first byte (how multi-SSID access points derive their extra BSSIDs).
static bool macsAreSiblings(const uint8_t a[6], const uint8_t b[6]) {
  const int diff = static_cast<int>(a[5]) - static_cast<int>(b[5]);
  if (memcmp(a, b, 5) == 0) return diff != 0 && diff >= -7 && diff <= 7;
  if (memcmp(a + 1, b + 1, 4) != 0) return false;
  // Derived (locally administered) BSSID: first byte differs, last byte the
  // same or a small step away (some drivers change both).
  return (a[0] != b[0]) && ((a[0] | b[0]) & 0x02) && diff >= -7 && diff <= 7;
}

static bool isQuietMac(const uint8_t mac[6]) {
  for (size_t i = 0; i < NUM_QUIET_MAC_TARGETS; ++i) {
    const uint8_t* q = quietMacTargets[i];
    if (!(q[0] | q[1] | q[2] | q[3] | q[4] | q[5])) continue;     // placeholder
    if (memcmp(q, mac, 6) == 0 || macsAreSiblings(q, mac)) return true;
  }
  return false;
}

// Is this address (or a sibling radio of it) currently silenced as a
// Fire/EMS/transit fleet?
static bool isIgnoredFleetMac(const uint8_t mac[6], uint32_t now) {
  if (!mac || isGroupMac(mac)) return false;
  for (size_t i = 0; i < DEVICE_CACHE_SIZE; ++i) {
    const TrackedDevice& d = deviceCache[i];
    if (!d.valid || !d.suppressed) continue;
    if ((now - d.lastSeen) >= ENCOUNTER_GAP_MS) continue;
    if (macMatches(d.mac, mac) || macsAreSiblings(d.mac, mac)) return true;
  }
  return false;
}

// Is a sibling radio of this address currently capped as a quiet-list router?
static bool hasQuietSibling(const uint8_t mac[6], uint32_t now) {
  for (size_t i = 0; i < DEVICE_CACHE_SIZE; ++i) {
    const TrackedDevice& d = deviceCache[i];
    if (!d.valid || !d.quietCap) continue;
    if ((now - d.lastSeen) >= ENCOUNTER_GAP_MS) continue;
    if (macsAreSiblings(d.mac, mac)) return true;
  }
  return false;
}

// Caps one device at QUIET. A device that already has HIGH evidence is left alone.
static void applyQuietCap(TrackedDevice& d, uint32_t now) {
  if (d.quietCap || d.suppressed || d.tier >= TIER_HIGH) return;
  d.quietCap = true;
  d.label     = QUIET_LABEL;
  d.labelRank = LABEL_RANK_LOW;
  if (d.tier == TIER_LOW) {
    d.tier = TIER_ECHO;          // already announced as LOW: the heartbeat just stops
    if (detLogReady()) printDeviceLine("[DET] QUIET", d, now);
  }
}

static void announceDevice(TrackedDevice& d, uint32_t now) {
  if (d.suppressed) return;
  const uint8_t tier = d.tier;
  d.announcedTier = tier;
  if (!d.hasPos && gpsFixFresh(now)) setDevicePos(d);
  if (tier == TIER_NONE || tier > TIER_HIGH) return;
  if (d.quietCap) return;          // quiet-list router: logged and journaled, never heard

  const bool rateOk = !announcedOnce[tier] ||
                      ((now - lastAnnounceAt[tier]) >= ALERT_RETRIGGER_MS);
  if (!rateOk) return;   // the heartbeat still covers it

  const Note* jingle = JINGLE_NEW_ECHO;
  uint8_t count      = sizeof(JINGLE_NEW_ECHO) / sizeof(Note);
  uint8_t priority   = AUDIO_PRIO_ECHO;
  if (tier == TIER_HIGH) {
    jingle   = JINGLE_NEW_HIGH;
    count    = sizeof(JINGLE_NEW_HIGH) / sizeof(Note);
    priority = AUDIO_PRIO_HIGH;
  } else if (tier == TIER_LOW) {
    jingle   = JINGLE_NEW_LOW;
    count    = sizeof(JINGLE_NEW_LOW) / sizeof(Note);
    priority = AUDIO_PRIO_LOW;
  }

  if (audioPlay(jingle, count, priority, now)) {
    lastAnnounceAt[tier] = now;
    announcedOnce[tier]  = true;
    // Let the jingle finish before the heartbeat takes over.
    nextHeartbeatAt = now + jingleDurationMs(jingle, count) + 250;
  }
}

// Single entry point for every detection, from either radio.
static void noteSighting(const Sighting& s, uint32_t now) {
  bool newEncounter = false;
  TrackedDevice* d = resolveDevice(s.mac, now, newEncounter);
  if (!d) return;

  d->lastSeen = now;
  // Heard again after its alert was journaled (went quiet 8 s, came back
  // within the encounter): journal it again when it ends, so a comeback or a
  // later upgrade to HIGH is not lost. The server keeps the latest line.
  if (d->journaled && !d->suppressed) d->journaled = false;

  // If this device was suppressed (e.g. broadcasting a Fire/EMS SSID), ignore
  // subsequent LOW/ECHO router OUI matches unless a HIGH police signature appears.
  if (d->suppressed) {
    if (s.tier < TIER_HIGH) return;
    d->suppressed = false;
  }

  ++sightingCount;
  if (d->sightings < 0xFFFF) ++d->sightings;
  d->reasonMask |= s.reasons;

  // Quiet list (see quietSsidTargets). A HIGH signature always lifts the cap.
  if (s.tier >= TIER_HIGH) {
    d->quietCap = false;
  } else if (!d->quietCap && d->tier < TIER_HIGH) {
    if (s.quiet) {
      applyQuietCap(*d, now);
      for (size_t i = 0; i < DEVICE_CACHE_SIZE; ++i) {       // its other radios too
        TrackedDevice& o = deviceCache[i];
        if (&o != d && o.valid && macsAreSiblings(o.mac, d->mac) &&
            (now - o.lastSeen) < ENCOUNTER_GAP_MS) applyQuietCap(o, now);
      }
    } else if (newEncounter && hasQuietSibling(d->mac, now)) {
      applyQuietCap(*d, now);
    }
  }

  // Fleet-check grace (see FLEET_CHECK_GRACE_MS). Anything that proves the
  // network name was checked, or any HIGH signature, releases it at once.
  if (s.tier >= TIER_HIGH || s.ssidSeen || s.src == SRC_BLE) {
    d->holdUntil = now;
  } else if (newEncounter) {
    d->holdUntil = now + FLEET_CHECK_GRACE_MS;
  }

  if (!s.viaEcho) d->echoOnly = false;

  if (s.hasRssi) {
    const int16_t rawFp8 = rssiToFp8(s.rssi);
    if (!d->hasRssi) {
      d->smoothedRssiFp8 = rawFp8;
      d->hasRssi = true;
    } else {
      // Light smoothing: sightings can be seconds apart on a drive-by.
      const int32_t delta = static_cast<int32_t>(rawFp8) -
                            static_cast<int32_t>(d->smoothedRssiFp8);
      d->smoothedRssiFp8 = static_cast<int16_t>(d->smoothedRssiFp8 + delta / 2);
    }
    d->lastDirectSeen = now;
    d->src            = s.src;
    d->channel        = s.channel;
    // Remember where its signal was strongest (closest to it).
    if (s.rssi > d->bestRssi) {
      d->bestRssi = s.rssi;
      if (gpsFixFresh(now)) setDevicePos(*d);
    } else if (!d->hasPos && gpsFixFresh(now)) {
      setDevicePos(*d);
    }
    timelineNote(*d, s.rssi, now);
    if (gpsFixFresh(now)) noteDeviceTrail(*d);
  } else if (!d->hasRssi) {
    d->src     = s.src;
    d->channel = s.channel;
  }

  if (s.label && !d->quietCap && (s.labelRank > d->labelRank || d->label == nullptr)) {
    d->label     = s.label;
    d->labelRank = s.labelRank;
  }
  if (s.ssid && s.ssid[0]) {
    strncpy(d->ssid, s.ssid, sizeof(d->ssid) - 1);
    d->ssid[sizeof(d->ssid) - 1] = '\0';
  }

  uint8_t effectiveTier = (d->quietCap && s.tier == TIER_LOW)
      ? static_cast<uint8_t>(TIER_ECHO) : s.tier;
  // Evidence that needs confirming alerts at once as LOW ("maybe") and
  // becomes HIGH on the second sighting, so nothing is delayed.
  if (s.needsSecond && effectiveTier == TIER_HIGH && d->sightings < 2) effectiveTier = TIER_LOW;
  const bool upgraded = (effectiveTier > d->tier);
  if (upgraded) d->tier = effectiveTier;

  if (newEncounter && d->tier <= TIER_HIGH) ++encounterCount[d->tier];

  // The "new target" jingle is played from alertTick() once any grace ends.

  if (detLogReady() &&
      (newEncounter || upgraded || (now - d->lastLogAt) >= DETECTION_RELOG_MS)) {
    d->lastLogAt = now;
    printDeviceLine(newEncounter ? "[DET] NEW " : (upgraded ? "[DET] UP  " : "[DET] SEEN"),
                    *d, now);
  }
}

// True when a should lead over b: tier, then a real RSSI, then stronger RSSI.
static bool deviceOutranks(const TrackedDevice& a, const TrackedDevice& b) {
  if (a.tier != b.tier) return a.tier > b.tier;
  if (a.hasRssi != b.hasRssi) return a.hasRssi;
  return a.smoothedRssiFp8 > b.smoothedRssiFp8;
}

// Decides what to show and when to beat. Runs every loop() pass.

static void alertTick(uint32_t now) {
  const TrackedDevice* displayLead = nullptr;
  const TrackedDevice* audioLead   = nullptr;
  bool    audioLeadLingering = false;
  uint8_t freshCount = 0;

  for (size_t i = 0; i < DEVICE_CACHE_SIZE; ++i) {
    TrackedDevice& d = deviceCache[i];
#if ENABLE_MARK_LOG
    // An alert that has ended goes into the journal once.
    if (d.valid && !d.journaled && !d.suppressed && d.announcedTier != TIER_NONE &&
        (now - d.lastSeen) >= PRESENCE_TIMEOUT_MS) {
      d.journaled = true;
      journalAlert(d, now);
    }
#endif
    if (!d.valid || d.suppressed || d.tier == TIER_NONE) continue;
    if ((now - d.lastSeen) >= PRESENCE_TIMEOUT_MS) continue;
    if (!timeReached(now, d.holdUntil)) continue;      // still in fleet-check grace

    if (d.tier > d.announcedTier) announceDevice(d, now);
    if (d.quietCap) continue;        // silent: no screen, LED or heartbeat either

    if (freshCount < 255) ++freshCount;
    if (!displayLead || deviceOutranks(d, *displayLead)) displayLead = &d;

    if (d.tier < HEARTBEAT_MIN_TIER) continue;
    const bool lingering = (LINGER_MS != 0) && ((now - d.firstSeen) >= LINGER_MS);
    if (lingering && d.tier < TIER_HIGH) continue;
    if (!audioLead || deviceOutranks(d, *audioLead)) {
      audioLead = &d;
      audioLeadLingering = lingering;
    }
  }

  if (!displayLead) {
    alertView.active = false;
    alertView.count  = 0;
    trackingActive = false;   // going quiet is silent: beeps only mean a target
    return;
  }

  trackingActive      = true;
  alertView.active    = true;
  alertView.tier      = displayLead->tier;
  alertView.echoOnly  = displayLead->echoOnly;
  alertView.hasRssi   = displayLead->hasRssi;
  alertView.rssi      = fp8ToRssi(displayLead->smoothedRssiFp8);
  alertView.channel   = displayLead->channel;
  alertView.src       = displayLead->src;
  alertView.count     = freshCount;
  alertView.quiet     = displayLead->quietCap;
  strncpy(alertView.label, displayLead->label ? displayLead->label : "",
          sizeof(alertView.label) - 1);
  alertView.label[sizeof(alertView.label) - 1] = '\0';

  lastAlert      = alertView;
  lastAlertValid = true;
  lastAlertAt    = now;

  if (!audioLead || !timeReached(now, nextHeartbeatAt)) return;

  uint32_t period = HEARTBEAT_PERIOD_FAR_MS;
  if (audioLead->hasRssi) {
    const int rssi = constrain(static_cast<int>(fp8ToRssi(audioLead->smoothedRssiFp8)),
                               HEARTBEAT_RSSI_FAR_DBM, HEARTBEAT_RSSI_NEAR_DBM);
    period = static_cast<uint32_t>(map(rssi, HEARTBEAT_RSSI_FAR_DBM, HEARTBEAT_RSSI_NEAR_DBM,
                                       HEARTBEAT_PERIOD_FAR_MS, HEARTBEAT_PERIOD_NEAR_MS));
  }
  if (audioLeadLingering && period < LINGER_REMINDER_MS) period = LINGER_REMINDER_MS;

  if (!audio.active) {
    const bool high = (audioLead->tier == TIER_HIGH);
    audioPlay(high ? JINGLE_BEAT_HIGH : JINGLE_BEAT_LOW,
              high ? sizeof(JINGLE_BEAT_HIGH) / sizeof(Note)
                   : sizeof(JINGLE_BEAT_LOW) / sizeof(Note),
              AUDIO_PRIO_BEAT, now);
    ++heartbeatCount;
  }
  nextHeartbeatAt = now + period;
}

// ============================================================
// MARK LOG: remember everything heard recently, save it on a button press
// ============================================================

static void sanitizeText(char* dst, size_t cap, const uint8_t* src, size_t len) {
  const size_t n = (len < cap - 1) ? len : cap - 1;
  for (size_t i = 0; i < n; ++i) {
    const uint8_t c = src[i];
    dst[i] = (c >= 0x20 && c < 0x7F && c != '"') ? static_cast<char>(c) : '.';
  }
  dst[n] = '\0';
}

// True if this address has not been reported within OBS_THROTTLE_MS.
// salt separates kinds of frame from one address, so a router's beacons
// (which carry its name and vendor details) are not starved by its data.
static bool obsThrottlePass(ObsThrottle& t, const uint8_t mac[6], uint32_t now, uint32_t salt = 0) {
  uint32_t key = (static_cast<uint32_t>(mac[2]) << 24) | (static_cast<uint32_t>(mac[3]) << 16) |
                 (static_cast<uint32_t>(mac[4]) << 8)  |  static_cast<uint32_t>(mac[5]);
  key ^= ((static_cast<uint32_t>(mac[0]) << 8) | mac[1]) * 2654435761UL;
  key ^= salt;
  if (key == 0) key = 1;
  const size_t slot = (key ^ (key >> 7) ^ (key >> 15)) % OBS_THROTTLE_SLOTS;
  if (t.key[slot] == key && (now - t.at[slot]) < OBS_THROTTLE_MS) return false;
  t.key[slot] = key;
  t.at[slot]  = now;
  return true;
}

// Wi-Fi task: note the transmitter of any frame, matched or not.
// Appends printable text to the WPS summary in o.detail, "/"-separated.
static void obsAppendWpsText(Observation& o, const uint8_t* src, size_t len, size_t maxChars) {
  char* dst = reinterpret_cast<char*>(o.detail);
  size_t used = strlen(dst);
  if (len == 0 || used + 2 >= OBS_DETAIL_LEN) return;
  if (used > 0) dst[used++] = '/';
  size_t room = OBS_DETAIL_LEN - 1 - used;
  if (room > maxChars) room = maxChars;
  const size_t n = (len < room) ? len : room;
  for (size_t i = 0; i < n; ++i) {
    const uint8_t c = src[i];
    dst[used + i] = (c >= 0x20 && c < 0x7F && c != '"' && c != '/') ? static_cast<char>(c) : '.';
  }
  dst[used + n] = '\0';
}

// Wi-Fi task: pulls the identifying details out of a beacon / probe frame's
// information elements for the mark log - the vendor codes it carries and
// what its WPS block says the device is.
static void obsExtractIeDetail(Observation& o, const uint8_t* ies, size_t len) {
  size_t offset = 0;
  while (offset + 2 <= len) {
    const uint8_t tagId  = ies[offset];
    const uint8_t tagLen = ies[offset + 1];
    if (offset + 2 + tagLen > len) return;
    const uint8_t* p = ies + offset + 2;

    if (tagId == 221 && tagLen >= 3) {
      const bool microsoft = (p[0] == 0x00 && p[1] == 0x50 && p[2] == 0xF2);
      if (!microsoft) {                       // 00:50:F2 (WPA/WMM/WPS) is on nearly everything
        bool known = false;
        for (uint8_t i = 0; i < o.ouiCount; ++i) {
          if (memcmp(&o.ouis[i * 3], p, 3) == 0) { known = true; break; }
        }
        if (!known && o.ouiCount < OBS_MAX_VENDOR_OUIS) {
          memcpy(&o.ouis[o.ouiCount * 3], p, 3);
          ++o.ouiCount;
        }
      } else if (tagLen >= 4 && p[3] == 0x04 && o.detail[0] == 0) {   // WPS
        const uint8_t* mfr = nullptr;  size_t mfrLen = 0;
        const uint8_t* model = nullptr; size_t modelLen = 0;
        const uint8_t* num = nullptr;  size_t numLen = 0;
        const uint8_t* name = nullptr; size_t nameLen = 0;
        size_t sub = 4;
        while (sub + 4 <= tagLen) {
          const uint16_t attrType = static_cast<uint16_t>((p[sub] << 8) | p[sub + 1]);
          const uint16_t attrLen  = static_cast<uint16_t>((p[sub + 2] << 8) | p[sub + 3]);
          if (sub + 4 + attrLen > tagLen) break;
          const uint8_t* v = p + sub + 4;
          if (attrType == 0x1021)      { mfr = v;   mfrLen = attrLen; }
          else if (attrType == 0x1023) { model = v; modelLen = attrLen; }
          else if (attrType == 0x1024) { num = v;   numLen = attrLen; }
          else if (attrType == 0x1011) { name = v;  nameLen = attrLen; }
          sub += 4 + attrLen;
        }
        obsAppendWpsText(o, mfr, mfrLen, 16);       // maker / model / number / device name
        obsAppendWpsText(o, model, modelLen, 16);
        obsAppendWpsText(o, num, numLen, 12);
        obsAppendWpsText(o, name, nameLen, OBS_DETAIL_LEN);
      }
    }
    offset += 2 + static_cast<size_t>(tagLen);
  }
}

static void observeWifiFrame(const HeaderEvent& h, const uint8_t* ssid, size_t ssidLen,
                             const uint8_t* ies = nullptr, size_t iesLen = 0) {
#if ENABLE_MARK_LOG
  if (isGroupMac(h.addr2)) return;
  // Salt: beacons separately from data, and each different searched-for name
  // separately, so a burst of probes for several networks is not cut to one.
  uint32_t salt = ies ? 0x9E3779B9UL : 0;
  if (h.subtype == 0x04 && ssid && ssidLen) {
    for (size_t i = 0; i < ssidLen; ++i) salt = (salt ^ ssid[i]) * 16777619UL;
  }
  if (!obsThrottlePass(wifiObsThrottle, h.addr2, millis(), salt)) return;

  Observation o{};
  memcpy(o.mac, h.addr2, 6);
  if (h.frameType == WIFI_PKT_DATA)   o.kind = OBS_DATA;
  else if (h.subtype == 0x08)         o.kind = OBS_BEACON;
  else if (h.subtype == 0x04)         o.kind = OBS_PROBE_REQ;
  else if (h.subtype == 0x05)         o.kind = OBS_PROBE_RESP;
  else                                o.kind = OBS_MGMT_OTHER;
  o.rssi    = h.rssi;
  o.channel = h.channel;
  if (ssid && ssidLen > 0) sanitizeText(o.text, sizeof(o.text), ssid, ssidLen);
  if (ies && iesLen > 0) obsExtractIeDetail(o, ies, iesLen);

  // Who it was talking to: the receiver if that is one device, otherwise the
  // network (BSSID) the frame belongs to.
  if (!isGroupMac(h.addr1) && !macMatches(h.addr1, h.addr2)) {
    memcpy(o.peer, h.addr1, 6); o.hasPeer = true;
  } else if (!isGroupMac(h.addr3) && !macMatches(h.addr3, h.addr2)) {
    memcpy(o.peer, h.addr3, 6); o.hasPeer = true;
  }

  if (xQueueSend(obsQueue, &o, 0) != pdTRUE) bump(obsDrops);
#else
  (void)h; (void)ssid; (void)ssidLen; (void)ies; (void)iesLen;
#endif
}

// loop(): fold one observation into the table of recently heard transmitters.
static void recentNote(const Observation& o, uint32_t now) {
  const bool ble = (o.kind & OBS_RADIO_BLE) != 0;
  // A device that uses one address on both radios gets one entry per radio.
  uint32_t key = (static_cast<uint32_t>(o.mac[2]) << 24) | (static_cast<uint32_t>(o.mac[3]) << 16) |
                 (static_cast<uint32_t>(o.mac[4]) << 8)  |  static_cast<uint32_t>(o.mac[5]);
  key ^= ((static_cast<uint32_t>(o.mac[0]) << 8) | o.mac[1]) * 2654435761UL;
  key = (key << 1) | (ble ? 1U : 0U);
  if (key == 0) key = 2;

  size_t found = recentCap;
  size_t spare = 0;
  uint32_t spareAge = 0;
  bool haveSpare = false;

  for (size_t i = 0; i < recentCap; ++i) {
    const RecentIndex& x = recentIndex[i];
    if (x.key == key && macMatches(recentTable[i].mac, o.mac)) { found = i; break; }
    const uint32_t age = x.key ? (now - x.lastSeen) : 0xFFFFFFFFUL;
    if (!haveSpare || age > spareAge) { spare = i; spareAge = age; haveSpare = true; }
  }

  const bool isNew = (found == recentCap);
  const size_t slot = isNew ? spare : found;   // new transmitter: the free or stalest slot
  RecentEntry* e = &recentTable[slot];
  RecentIndex& x = recentIndex[slot];
  if (isNew || (now - x.lastSeen) >= MARK_WINDOW_MS) {   // new, or stale: start over
    memset(e, 0, sizeof(*e));
    if (recentProbes) memset(recentProbes[slot], 0, sizeof(ProbeNames));
    e->valid     = true;
    memcpy(e->mac, o.mac, 6);
    e->bestRssi  = o.rssi;
    e->firstSeen = now;
    x.key        = key;
  }

  e->lastSeen = now;
  x.lastSeen  = now;
  e->kinds   |= o.kind;
  if (o.rssi > e->bestRssi) e->bestRssi = o.rssi;
  x.bestRssi  = e->bestRssi;
  if (o.channel) e->channel = o.channel;
  // Count each clock second once, however many reports arrive in it. The
  // per-task throttle is only best-effort (hash slots collide when ~100
  // devices are in range), so the real limit is enforced here.
  const uint32_t sec = now / 1000UL;
  if (e->reports == 0 || sec != e->lastCountedSec) {
    if (e->reports < 0xFFFF) ++e->reports;
    e->lastCountedSec = sec;
  }
  if (o.kind & OBS_HAS_CID) e->bleCompany = o.bleCompany;
  if (o.bleService) e->bleService = o.bleService;
  if (o.text[0]) { strncpy(e->text, o.text, sizeof(e->text) - 1); e->text[sizeof(e->text) - 1] = '\0'; }
  // Every different network name it searches for (a cruiser laptop or radio
  // looking for its agency's networks is one of the best clues there is).
  if (recentProbes && (o.kind & OBS_PROBE_REQ) && o.text[0] && e->probeCount < RECENT_PROBES) {
    bool known = false;
    for (uint8_t i = 0; i < e->probeCount && !known; ++i) known = (strcmp(recentProbes[slot][i], o.text) == 0);
    if (!known) {
      strncpy(recentProbes[slot][e->probeCount], o.text, OBS_TEXT_LEN);
      recentProbes[slot][e->probeCount][OBS_TEXT_LEN] = '\0';
      ++e->probeCount;
    }
  }
  if (o.hasPeer) { memcpy(e->peer, o.peer, 6); e->hasPeer = true; }
  if (o.ouiCount) { memcpy(e->ouis, o.ouis, sizeof(e->ouis)); e->ouiCount = o.ouiCount; }
  if (o.kind & (OBS_BLE | OBS_BLE_PUBLIC)) {
    // keep the fullest advert seen (a device may alternate short and long ones)
    if (o.detailLen >= e->detailLen && o.detailLen > 0) {
      memcpy(e->detail, o.detail, sizeof(e->detail)); e->detailLen = o.detailLen;
    }
  } else if (o.detail[0]) {
    memcpy(e->detail, o.detail, sizeof(e->detail));
  }
}

static void showNotice(const char* line1, const char* line2, uint32_t now) {
  strncpy(noticeLine1, line1, sizeof(noticeLine1) - 1); noticeLine1[sizeof(noticeLine1) - 1] = '\0';
  strncpy(noticeLine2, line2, sizeof(noticeLine2) - 1); noticeLine2[sizeof(noticeLine2) - 1] = '\0';
  noticeUntil  = now + MARK_NOTICE_MS;
  noticeActive = true;
  noticeDirty  = true;
}

#if HOME_SYNC
static void maintEnter(uint32_t now, const char* why);
#endif
#if WEB_UI
static void maintExit(const char* why, bool lock);
static void roadEnter(uint32_t now);
#endif
static void printResetLog();
static void serviceCore();   // one pass of listening / alerting / hopping (defined near loop())

#if HOME_SYNC
// True once a fresh GPS fix has shown HOME_DRIVING_MPH or more for
// HOME_DRIVING_MS: you are driving, so this is not "at home". Without a GPS
// (or a fix) it is always false and the Wi-Fi signal decides.
static bool homeDriving(uint32_t now) {
  static uint32_t fastSince = 0;
  if (!gpsFixFresh(now) || gps.speedMph < HOME_DRIVING_MPH) { fastSince = 0; return false; }
  if (fastSince == 0) fastSince = now;
  return (now - fastSince) >= HOME_DRIVING_MS;
}
#endif

// Road access offer: the first ROAD_WINDOW_MS after power-up only. Latched
// shut, so it cannot come back when millis() wraps after 49.7 days.
static bool roadWindowOpen(uint32_t now) {
  static bool closed = false;
  if (!closed && now >= ROAD_WINDOW_MS) closed = true;
  return !closed;
}

// 3 taps, the 'm' command or the web page (works without the mark log too)
static void toggleMute(uint32_t now) {
  audioMuted = !audioMuted;
  muteMagic  = audioMuted ? MUTE_MAGIC : 0;
  if (audioMuted && audio.active) { buzzerOff(); audio.active = false; }
  if (audioMuted) audioPlay(JINGLE_MUTE, sizeof(JINGLE_MUTE) / sizeof(Note), AUDIO_PRIO_BOOT, now);
  else            audioPlay(JINGLE_UNMUTE, sizeof(JINGLE_UNMUTE) / sizeof(Note), AUDIO_PRIO_BOOT, now);
  showNotice(audioMuted ? "MUTED" : "SOUND ON", audioMuted ? "3 taps = sound on" : "3 taps = mute", now);
  Console.println(audioMuted ? F("[SND] alert sounds muted") : F("[SND] alert sounds on"));
  lastRenderedState = DisplaySnapshot{};
}

// HOME_WIFI_BSSID parsing (needs only HOME_SYNC)
#if HOME_SYNC
static int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  return (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
}

// "AA:BB:CC:DD:EE:FF, 11-22-33-44-55-66" -> homeBssidList. Any separators
// inside an address are fine; commas, spaces or semicolons split addresses.
static void parseHomeBssids(const char* list) {
  homeBssidCount = 0;
  homeBssidFilter = false;
  uint8_t mac[6];
  int digits = 0;
  bool bad = false;
  for (const char* p = list;; ++p) {
    const char c = *p;
    if (c == '\0' || c == ',' || c == ';' || c == ' ') {
      if (digits == 12) {
        homeBssidFilter = true;
        if (homeBssidCount < HOME_BSSID_MAX) memcpy(homeBssidList[homeBssidCount++], mac, 6);
        else bad = true;
      } else if (digits) {
        bad = true;
      }
      digits = 0;
      if (c == '\0') break;
      continue;
    }
    const int v = hexNibble(c);
    if (v < 0) continue;                       // ':' '-' '.'
    if (digits < 12) {
      if (digits % 2 == 0) mac[digits / 2] = static_cast<uint8_t>(v << 4);
      else                 mac[digits / 2] |= static_cast<uint8_t>(v);
    }
    ++digits;
  }
  if (bad) {
    homeBssidFilter = true;                    // stay strict: a typo must not widen the match
    Console.printf("[HOME] HOME_WIFI_BSSID: could not read all of \"%s\" (max %u addresses)\n",
                   list, static_cast<unsigned>(HOME_BSSID_MAX));
  }
  if (homeBssidCount)
    Console.printf("[HOME] home access points: %u from HOME_WIFI_BSSID\n", static_cast<unsigned>(homeBssidCount));
}
#endif

#if ENABLE_MARK_LOG

static size_t markLogFreeBytes() {
  const size_t total = LittleFS.totalBytes();
  const size_t used  = LittleFS.usedBytes();
  return (total > used) ? (total - used) : 0;
}

// Refreshes the "LOG:nn%" figure. 100% means no further marks will be saved.
static void markLogRefreshUsage() {
  if (!markLogReady) { markLogPercent = 0; return; }
  const size_t total = LittleFS.totalBytes();
  const size_t usable = (total > MARK_LOG_RESERVE_BYTES) ? (total - MARK_LOG_RESERVE_BYTES) : 0;
  const size_t used  = LittleFS.usedBytes();
  if (usable == 0 || used >= usable) { markLogPercent = 100; return; }
  markLogPercent = static_cast<uint8_t>((static_cast<uint64_t>(used) * 100ULL) / usable);
}

static void markLogBegin() {
  markLogReady = LittleFS.begin(true);   // formats the data partition on first use
  if (!markLogReady) return;
  markLogRefreshUsage();

  File f = LittleFS.open(BOOT_COUNT_PATH, FILE_READ);
  if (f) {
    bootCount = static_cast<uint32_t>(f.parseInt());
    f.close();
  }
  ++bootCount;
  f = LittleFS.open(BOOT_COUNT_PATH, FILE_WRITE);
  if (f) {
    f.printf("%lu\n", static_cast<unsigned long>(bootCount));
    f.close();
  }
}

// Appends one mark to the log:
//   #MARK header (saw = POLICE or NOTHING, from the button; type =
//   DURING_ALERT or NO_ALERT, from the detector; what was leading the alert)
//   one WIFI/BLE line per transmitter heard in the last MARK_WINDOW_MS
//   one ALERT line per device the detector was tracking in that window
// A follow-up mark (followUp = true) writes only the transmitters that are
// new, or clearly stronger, since they were last written; "written" is how
// many lines that was. Returns false if nothing could be written.
// Collects small prints and hands them to the file in 2 KB pieces.
class BufferedFileWriter : public Print {
 public:
  explicit BufferedFileWriter(File& f) : file_(f) {}
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* buf, size_t len) override {
    for (size_t done = 0; done < len;) {
      if (used_ == sizeof(buf_)) flush();
      const size_t n = std::min(len - done, sizeof(buf_) - used_);
      memcpy(buf_ + used_, buf + done, n);
      used_ += n;
      done  += n;
    }
    return len;
  }
  void flush() override {
    if (used_) file_.write(buf_, used_);
    used_ = 0;
  }
  void close() { flush(); file_.close(); }
 private:
  File&  file_;
  size_t used_ = 0;
  static uint8_t buf_[2048];   // static: keeps 2 KB off the loop task's stack
};
uint8_t BufferedFileWriter::buf_[2048];

static bool saveMark(uint32_t now, bool sawNothing, bool followUp, bool sample,
                     uint16_t& heard, uint16_t& written, bool& logFull) {
  heard = 0;
  written = 0;
  logFull = false;
  if (!markLogReady) return false;

  if (markLogFreeBytes() <= MARK_LOG_RESERVE_BYTES) {
    logFull = true;
    markLogRefreshUsage();     // idle screen switches to LOG:FULL
    return false;
  }

  audioStop();   // the write below blocks loop() (the mark jingle plays after)

  File file = LittleFS.open(sample ? SAMPLES_PATH : MARK_LOG_PATH, FILE_APPEND);
  if (!file) return false;
  if (sample && file.size() >= SAMPLES_FILE_MAX) { file.close(); return false; }   // full until uploaded
  BufferedFileWriter f(file);   // a few big writes instead of ~15 tiny ones per device

  // Strongest signal first: whatever you just drove past is near the top.
  // (Collect, then one O(n log n) sort: a full 2048-entry table stays quick.
  // Ties keep table order, same as the old insertion sort.)
  uint16_t* order = recentOrder;
  for (size_t i = 0; i < recentCap; ++i) {
    if (!recentIndex[i].key || (now - recentIndex[i].lastSeen) >= MARK_WINDOW_MS) continue;
    order[heard++] = static_cast<uint16_t>(i);
  }
  std::sort(order, order + heard, [](uint16_t a, uint16_t b) {
    const int ra = recentIndex[a].bestRssi, rb = recentIndex[b].bestRssi;
    return ra != rb ? ra > rb : a < b;
  });

  const bool duringAlert = alertView.active ||
      (lastAlertValid && (now - lastAlertAt) < MARK_ALERT_RECENT_MS);
  static uint16_t sampleCount = 0;
  if (sample) {
    f.printf("#SAMPLE drive=%lu n=%u type=%s uptime=%lus heard=%u obs_dropped=%lu",
             static_cast<unsigned long>(bootCount), static_cast<unsigned>(++sampleCount),
             duringAlert ? "DURING_ALERT" : "NO_ALERT",
             static_cast<unsigned long>(now / 1000UL), static_cast<unsigned>(heard),
             static_cast<unsigned long>(obsDrops));
  } else {
    ++markCount;
    f.printf("#MARK drive=%lu mark=%u saw=%s type=%s uptime=%lus heard=%u obs_dropped=%lu",
             static_cast<unsigned long>(bootCount), static_cast<unsigned>(markCount),
             sawNothing ? "NOTHING" : "POLICE",
             duringAlert ? "DURING_ALERT" : "NO_ALERT",
             static_cast<unsigned long>(now / 1000UL), static_cast<unsigned>(heard),
             static_cast<unsigned long>(obsDrops));
  }
  if (followUp) f.printf(" followup_of=%u", static_cast<unsigned>(markCount - 1));
  if (heard >= recentCap) f.print(" table_full=yes");
  {
    char tag[100];
    gpsTag(tag, sizeof(tag), now, 0, true);
    f.print(tag);
  }
  if (alertView.active) {
    f.printf(" alerting=%s lead=\"%s\" in_range=%u\n",
             shownTierName(alertView.tier, alertView.quiet),
             alertView.label, static_cast<unsigned>(alertView.count));
  } else if (lastAlertValid && (now - lastAlertAt) < MARK_EARLIER_MS) {
    // Nothing is alerting now, but something was moments ago.
    f.printf(" alerting=NONE last_alert=%s last_lead=\"%s\" ended_ago=%lums\n",
             shownTierName(lastAlert.tier, lastAlert.quiet), lastAlert.label,
             static_cast<unsigned long>(now - lastAlertAt));
  } else {
    f.print(" alerting=NONE\n");
  }

  for (size_t n = 0; n < heard; ++n) {
    RecentEntry& r = recentTable[order[n]];

    // Follow-up: skip what an earlier mark already holds, unless it has
    // come clearly closer since.
    if (followUp && r.marked && (now - r.markedAt) < MARK_WINDOW_MS &&
        static_cast<int>(r.bestRssi) < static_cast<int>(r.markedRssi) + MARK_FOLLOWUP_RSSI_DB) continue;
    if (!sample) {               // samples leave the follow-up bookkeeping alone
      r.marked     = true;
      r.markedAt   = now;
      r.markedRssi = r.bestRssi;
    }
    ++written;

    const bool ble = (r.kinds & (OBS_BLE | OBS_BLE_PUBLIC)) != 0;
    f.printf("%s %02X:%02X:%02X:%02X:%02X:%02X ", ble ? "BLE " : "WIFI",
             r.mac[0], r.mac[1], r.mac[2], r.mac[3], r.mac[4], r.mac[5]);
    if (ble) {
      f.print((r.kinds & OBS_BLE_PUBLIC) ? "addr=public " : "addr=random ");
    } else {
      f.print("frames=");
      if (r.kinds & OBS_BEACON)     f.print("B");   // beacon
      if (r.kinds & OBS_PROBE_REQ)  f.print("P");   // probe request
      if (r.kinds & OBS_PROBE_RESP) f.print("R");   // probe response
      if (r.kinds & OBS_DATA)       f.print("D");   // data
      if (r.kinds & OBS_MGMT_OTHER) f.print("M");   // other management
      f.printf(" ch=%u ", r.channel);
    }
    f.printf("rssi=%d secs=%u ago=%lums first=%lums", r.bestRssi, static_cast<unsigned>(r.reports),
             static_cast<unsigned long>(now - r.lastSeen),
             static_cast<unsigned long>(now - r.firstSeen));
    if (r.text[0])             f.printf(" %s=\"%s\"", ble ? "name" : "ssid", r.text);
    if (!ble && r.probeCount && recentProbes) {           // every name it searched for
      const ProbeNames& pn = recentProbes[&r - recentTable];
      f.print(" probes=\"");
      for (uint8_t i = 0; i < r.probeCount; ++i) {
        if (i) f.print('|');
        for (const char* c = pn[i]; *c; ++c) f.print(*c == '|' ? '/' : *c);
      }
      f.print('"');
    }
    if (r.kinds & OBS_HAS_CID) f.printf(" cid=%04X", r.bleCompany);
    if (r.bleService)          f.printf(" svc=%04X", r.bleService);
    if (ble) {
      if (r.detailLen) {                                   // the whole raw advert
        f.print(" adv=");
        for (uint8_t i = 0; i < r.detailLen && i < OBS_DETAIL_LEN; ++i) f.printf("%02X", r.detail[i]);
      }
    } else {
      if (r.mac[0] & 0x02) f.print(" mac=private");        // randomised, not a vendor address
      if (r.hasPeer) f.printf(" peer=%02X:%02X:%02X:%02X:%02X:%02X",
                              r.peer[0], r.peer[1], r.peer[2], r.peer[3], r.peer[4], r.peer[5]);
      if (r.detail[0]) f.printf(" wps=\"%s\"", reinterpret_cast<const char*>(r.detail));
      if (r.ouiCount) {
        f.print(" vie=");
        for (uint8_t i = 0; i < r.ouiCount && i < OBS_MAX_VENDOR_OUIS; ++i) {
          f.printf("%s%02X%02X%02X", i ? "," : "", r.ouis[i * 3], r.ouis[i * 3 + 1], r.ouis[i * 3 + 2]);
        }
      }
    }

    for (size_t k = 0; k < DEVICE_CACHE_SIZE; ++k) {      // did the detector already know it?
      const TrackedDevice& d = deviceCache[k];
      if (d.valid && macMatches(d.mac, r.mac) && (now - d.lastSeen) < MARK_WINDOW_MS) {
        if (d.suppressed) f.print(" alerted=IGNORED_FLEET");
        else              f.printf(" alerted=%s", shownTierName(d.tier, d.quietCap));
        break;
      }
    }
    f.print("\n");
  }

  // Everything the detector itself was tracking, including devices it only
  // knows from traffic addressed to them (those never appear above).
  for (size_t k = 0; k < DEVICE_CACHE_SIZE; ++k) {
    const TrackedDevice& d = deviceCache[k];
    if (!d.valid || (now - d.lastSeen) >= MARK_EARLIER_MS) continue;
    // ALERT = seen within the mark window; EARLIER = older, up to 90 seconds.
    const bool current = (now - d.lastSeen) < MARK_WINDOW_MS;

    char why[112];
    formatReasonCodes(d.reasonMask, why, sizeof(why));
    f.printf("%s %02X:%02X:%02X:%02X:%02X:%02X tier=%s src=%s ",
             current ? "ALERT" : "EARLIER", d.mac[0], d.mac[1], d.mac[2], d.mac[3], d.mac[4], d.mac[5],
             d.suppressed ? "IGNORED_FLEET" : shownTierName(d.tier, d.quietCap),
             (d.src == SRC_BLE) ? "BLE" : "WIFI");
    if (d.hasRssi) f.printf("rssi=%d ", fp8ToRssi(d.smoothedRssiFp8));
    else           f.print("rssi=-- ");
    if (d.src == SRC_WIFI) f.printf("ch=%u ", d.channel);
    f.printf("ago=%lums seen=%u in_range_for=%lus beeped=%s label=\"%s\"",
             static_cast<unsigned long>(now - d.lastSeen), static_cast<unsigned>(d.sightings),
             static_cast<unsigned long>((d.lastSeen - d.firstSeen) / 1000UL),
             (d.announcedTier != TIER_NONE) ? "yes" : "no",
             d.label ? d.label : "");
    if (d.ssid[0]) f.printf(" ssid=\"%s\"", d.ssid);
    if (d.bestRssi > -127) f.printf(" best=%d", d.bestRssi);
    if (d.hasPos) f.printf(" at=%.5f,%.5f mph=%u", d.posLat, d.posLon, static_cast<unsigned>(d.posMph));
    printTimeline(f, d);
    f.printf(" why=%s\n", why);
  }

  f.close();
  markLogRefreshUsage();
  return true;
}

static void printMarkLog() {
  if (!markLogReady) { Console.println(F("[LOG] storage not available")); return; }
  File f = LittleFS.open(MARK_LOG_PATH, FILE_READ);
  if (!f || f.size() == 0) {
    Console.println(F("[LOG] no marks saved"));
    if (f) f.close();
    return;
  }

  setSerialBlockingMode(true);
  Console.printf("\n===== MARK LOG BEGIN (%lu bytes, storage %u%% full) =====\n",
                static_cast<unsigned long>(f.size()), static_cast<unsigned>(markLogPercent));
  Console.flush();

  // The dump can take many seconds. Keep listening, alerting and hopping
  // channels meanwhile; only the [DET] serial lines are held back.
  dumpInProgress = true;
  static uint8_t chunk[256];
  while (f.available()) {
    const size_t n = f.read(chunk, sizeof(chunk));
    if (n == 0) break;
    Serial.write(chunk, n);        // straight to USB: too big for the web console
    Serial.flush();
    if (!serialReady()) break;     // monitor went away: stop instead of stalling
    serviceCore();
  }
  f.close();
  dumpInProgress = false;
  Console.println(F("===== MARK LOG END ====="));
  setSerialBlockingMode(false);
}

// Follow-up state for marks; ERASE clears it so the next mark is a full one.
static uint32_t lastMarkAt = 0;
static bool     everMarked = false;

static void eraseMarkLog() {
  if (!markLogReady) { Console.println(F("[LOG] storage not available")); return; }
  for (size_t i = 0; i < recentCap; ++i) recentTable[i].marked = false;
  everMarked = false;
  LittleFS.remove(MARK_LOG_PATH);
  LittleFS.remove(JOURNAL_PATH);
  LittleFS.remove(JOURNAL_OLD_PATH);
  LittleFS.remove(SAMPLES_PATH);
  for (const char* p : {MARKS_PENDING_PATH, ALERTS_PENDING_OLD_PATH, ALERTS_PENDING_PATH, SAMPLES_PENDING_PATH})
    LittleFS.remove(p);   // copies left by a failed upload
  markLogRefreshUsage();
  Console.println(F("[LOG] mark log, alert journal and background samples erased"));
}

// One line per finished alert:
//   A drive=27 up=312 time=2026-11-21T17:42:10Z tier=LOW src=WIFI mac=...
//     best=-70 ch=6 dur=12 seen=30 label="..." ssid="..." at=lat,lon mph=43 why=[...]
// time = when it started; at = where its signal was strongest, mph = our
// speed right there.
static void journalAlert(const TrackedDevice& d, uint32_t now, bool ongoing, const char* ended) {
  if (!markLogReady || markLogFreeBytes() <= MARK_LOG_RESERVE_BYTES * 2) return;   // marks come first
  File f = LittleFS.open(JOURNAL_PATH, FILE_APPEND);
  if (!f) return;
  if (f.size() >= JOURNAL_FILE_MAX) {          // full: the older half goes
    f.close();
    LittleFS.remove(JOURNAL_OLD_PATH);
    LittleFS.rename(JOURNAL_PATH, JOURNAL_OLD_PATH);
    f = LittleFS.open(JOURNAL_PATH, FILE_APPEND);
    if (!f) return;
  }
  BufferedFileWriter w(f);                      // one write instead of ~10 small ones
  char tag[100];
  gpsTag(tag, sizeof(tag), now, now - d.firstSeen, false);
  char why[112];
  formatReasonCodes(d.reasonMask, why, sizeof(why));
  w.printf("A drive=%lu up=%lu%s tier=%s src=%s mac=%02X:%02X:%02X:%02X:%02X:%02X",
           static_cast<unsigned long>(bootCount), static_cast<unsigned long>(d.firstSeen / 1000UL), tag,
           shownTierName(d.tier, d.quietCap), (d.src == SRC_BLE) ? "BLE" : "WIFI",
           d.mac[0], d.mac[1], d.mac[2], d.mac[3], d.mac[4], d.mac[5]);
  if (d.bestRssi > -127) w.printf(" best=%d", d.bestRssi);
  else                   w.print(" best=--");
  if (d.src == SRC_WIFI) w.printf(" ch=%u", d.channel);
  w.printf(" dur=%lu seen=%u label=\"%s\"",
           static_cast<unsigned long>((d.lastSeen - d.firstSeen) / 1000UL),
           static_cast<unsigned>(d.sightings), d.label ? d.label : "");
  if (d.ssid[0]) w.printf(" ssid=\"%s\"", d.ssid);
  if (d.hasPos) w.printf(" at=%.5f,%.5f mph=%u", d.posLat, d.posLon, static_cast<unsigned>(d.posMph));
  if (audioMuted) w.print(" muted=yes");
  if (ongoing)    w.print(" ongoing=yes");      // still in range (reboot, or its slot was needed)
  if (ended)      w.printf(" ended=%s", ended); // e.g. fleet: silenced as Fire/EMS mid-alert
  printTimeline(w, d);
  w.printf(" why=%s\n", why);
  w.close();
  markLogRefreshUsage();
}

// Before a deliberate reboot: alerts still in range would otherwise never
// reach the journal (they are written once they have gone quiet).
static void journalFlushOngoing() {
  const uint32_t now = millis();
  for (size_t i = 0; i < DEVICE_CACHE_SIZE; ++i) {
    TrackedDevice& d = deviceCache[i];
    if (d.valid && !d.journaled && !d.suppressed && d.announcedTier != TIER_NONE) {
      d.journaled = true;
      journalAlert(d, now, (now - d.lastSeen) < PRESENCE_TIMEOUT_MS);
    }
  }
}

static void printJournal() {
  if (!markLogReady) { Console.println(F("[LOG] storage not available")); return; }
  setSerialBlockingMode(true);
  Console.println(F("\n===== ALERT JOURNAL BEGIN (oldest first) ====="));
  dumpInProgress = true;
  static uint8_t chunk[256];
  for (const char* path : {JOURNAL_OLD_PATH, JOURNAL_PATH}) {
    File f = LittleFS.open(path, FILE_READ);
    if (!f) continue;
    while (f.available()) {
      const size_t n = f.read(chunk, sizeof(chunk));
      if (n == 0) break;
      Serial.write(chunk, n);
      Serial.flush();
      if (!serialReady()) break;
      serviceCore();
    }
    f.close();
  }
  dumpInProgress = false;
  Console.println(F("===== ALERT JOURNAL END ====="));
  setSerialBlockingMode(false);
}

static void takeMark(uint32_t now, bool sawNothing) {
  if (everMarked && (now - lastMarkAt) < MARK_LOCKOUT_MS) return;
  const bool followUp = everMarked && (now - lastMarkAt) < MARK_FOLLOWUP_MS;

  uint16_t heard = 0;
  uint16_t written = 0;
  bool logFull = false;
  char line1[22];
  char line2[22];
  if (saveMark(now, sawNothing, followUp, false, heard, written, logFull)) {
    everMarked = true;
    lastMarkAt = now;
    snprintf(line1, sizeof(line1), "#%u %s", static_cast<unsigned>(markCount),
             sawNothing ? "NOTHING" : "POLICE");
    if (followUp) snprintf(line2, sizeof(line2), "added: %u new", static_cast<unsigned>(written));
    else          snprintf(line2, sizeof(line2), "saved: %u devices", static_cast<unsigned>(written));
    if (sawNothing) {
      audioPlay(JINGLE_MARK_NOTHING, sizeof(JINGLE_MARK_NOTHING) / sizeof(Note), AUDIO_PRIO_BOOT, now);
    } else {
      audioPlay(JINGLE_MARK, sizeof(JINGLE_MARK) / sizeof(Note), AUDIO_PRIO_BOOT, now);
    }
  } else {
    snprintf(line1, sizeof(line1), "NOT SAVED");
    snprintf(line2, sizeof(line2), logFull ? "LOG FULL: then ERASE" : "flash storage failed");
  }
  showNotice(line1, line2, now);
}

// Background sample every SAMPLE_INTERVAL_MS: not in the first 2 minutes
// (still in the driveway), not in web mode, and with a GPS fix only while
// moving. Silent: nothing on screen, no sound.
static void sampleTick(uint32_t now) {
  static uint32_t lastSample = 0;
  if (maintActive || now < 120000UL) return;
  if (lastSample && !timeReached(now, lastSample + SAMPLE_INTERVAL_MS)) return;
  if (gpsFixFresh(now) && gps.speedMph < SAMPLE_MIN_MPH) return;   // stopped: wait until moving
  lastSample = now;
  uint16_t heard = 0, written = 0;
  bool full = false;
  if (saveMark(now, false, false, true, heard, written, full) && detLogReady())
    Console.printf("[LOG] background sample: %u devices\n", static_cast<unsigned>(written));
}

// Debounced button on D1 or the BOOT button (both switch to GND).
//   tap (released before MARK_LONG_PRESS_MS) = "police" mark, saved once no
//     further tap follows within MULTI_TAP_GAP_MS; 2 taps = still one mark
//   3+ quick taps = mute / unmute
//   held to MARK_LONG_PRESS_MS = "nothing is there" mark (at once)
//   first ROAD_WINDOW_MS after power-up: held ROAD_HOLD_MS = road access;
//     a shorter hold there is decided on release
// A stuck button produces exactly one mark.
#if WEB_UI
static void roadEnter(uint32_t now);
#endif
static void markButtonTick(uint32_t now) {
  static bool     lastRaw     = false;
  static bool     pressed     = false;   // debounced state
  static bool     fired       = false;   // this press already did something
  static bool     roadPress   = false;   // press began inside the road window
  static uint32_t rawSince    = 0;
  static uint32_t pressStart  = 0;
  static uint8_t  taps        = 0;
  static uint32_t lastRelease = 0;

  const bool raw = (digitalRead(MARK_BUTTON_PIN) == LOW) || (digitalRead(BOOT_BUTTON_PIN) == LOW);
  if (raw != lastRaw) {
    lastRaw  = raw;
    rawSince = now;
    return;
  }
  if ((now - rawSince) < MARK_DEBOUNCE_MS) return;

  if (raw && !pressed) {
    pressed    = true;
    fired      = false;
    pressStart = rawSince;
#if WEB_UI
    roadPress  = !maintActive && taps == 0 && roadWindowOpen(now) && (pressStart < ROAD_WINDOW_MS);
#endif
  } else if (raw && pressed && !fired) {
    const uint32_t held = now - pressStart;
#if WEB_UI
    if (roadPress) {
      if (held >= ROAD_HOLD_MS) { fired = true; taps = 0; roadEnter(now); }
      return;
    }
#endif
    if (held >= MARK_LONG_PRESS_MS) {
      fired = true;
      taps  = 0;                                  // a hold ends any tap sequence
      takeMark(now, /*sawNothing=*/true);
    }
  } else if (!raw && pressed) {
    pressed = false;
    if (!fired) {
      if (roadPress && (now - pressStart) >= MARK_LONG_PRESS_MS) {
        taps = 0;
        takeMark(now, /*sawNothing=*/true);
      } else {
        if (taps < 255) ++taps;
        lastRelease = now;
      }
    }
  } else if (!raw && !pressed && taps > 0 && (now - lastRelease) >= MULTI_TAP_GAP_MS) {
    if (taps >= 3) toggleMute(now);
    else           takeMark(now, /*sawNothing=*/false);
    taps = 0;
  }
  (void)roadPress;
}

#endif  // ENABLE_MARK_LOG

// ============================================================
// PROMISCUOUS HANDLER (Wi-Fi Task Context)
// ============================================================

static bool fillHeaderEvent(HeaderEvent& event, const uint8_t* payload,
                            uint16_t sigLen, wifi_promiscuous_pkt_type_t type,
                            int8_t rssi, uint8_t channel) {
  if (!payload || sigLen < 24) return false;

  memset(&event, 0, sizeof(event));
  memcpy(event.addr1, payload + 4, 6);
  memcpy(event.addr2, payload + 10, 6);
  memcpy(event.addr3, payload + 16, 6);

  event.rssi      = rssi;
  event.channel   = channel;
  event.frameType = static_cast<uint8_t>(type);
  event.subtype   = static_cast<uint8_t>((payload[0] & 0xF0) >> 4);
  event.dsFlags   = static_cast<uint8_t>(payload[1] & 0x03);

  if (type == WIFI_PKT_DATA && event.dsFlags == 0x03) {
    if (sigLen < 30) return false;
    memcpy(event.addr4, payload + 24, 6);
    event.hasAddr4 = true;
  }

  return true;
}

static bool headerHasTargetPrefix(const HeaderEvent& event) {
  if (matchEligiblePrefixMac(event.addr2)) return true; // RF transmitter / TA
  if (matchEligiblePrefixMac(event.addr1)) return true; // receiver / RA
  if (matchEligiblePrefixMac(event.addr3)) return true; // BSSID, DA, or SA by DS bits
  if (event.hasAddr4 && matchEligiblePrefixMac(event.addr4)) return true; // WDS SA
  return false;
}

static void recordIeStatistics(uint8_t subtype, size_t avail, const IeParseResult& parsed) {
  int subIdx = -1;
  if (subtype == 0x04)      subIdx = 0;
  else if (subtype == 0x05) subIdx = 1;
  else if (subtype == 0x08) subIdx = 2;

  if (subIdx >= 0) {
    const uint16_t len = static_cast<uint16_t>(min(avail, static_cast<size_t>(0xFFFF)));
    if (len > maxIeObserved[subIdx]) maxIeObserved[subIdx] = len;
    int bucket = 5;
    if      (len <= 128) bucket = 0;
    else if (len <= 160) bucket = 1;
    else if (len <= 192) bucket = 2;
    else if (len <= 224) bucket = 3;
    else if (len <= 255) bucket = 4;
    bump(ieDistribution[subIdx][bucket]);
  }

  if (parsed.ssidIeSeen) bump(ssidIeSeenFrames);
  if (parsed.wildcardSsid) {
    bump(wildcardSsidFrames);
    if (subtype == 0x04) bump(wildcardProbeFrames);
  }

  if (parsed.wpsStart >= 0) {
    bump(wpsHitsCaptured);
    if (static_cast<uint16_t>(parsed.wpsStart) > maxWpsStartObserved) {
      maxWpsStartObserved = static_cast<uint16_t>(parsed.wpsStart);
    }
    if (static_cast<uint16_t>(parsed.wpsEnd) > maxWpsEndObserved) {
      maxWpsEndObserved = static_cast<uint16_t>(parsed.wpsEnd);
    }
  }
}

void wifi_promiscuous_handler(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;

  const wifi_promiscuous_pkt_t* pkt = reinterpret_cast<wifi_promiscuous_pkt_t*>(buf);
  if (!pkt || pkt->rx_ctrl.rx_state != 0 || pkt->rx_ctrl.sig_len < 24) return;

  // Early RSSI gating: drop fringe/distant frames before parsing headers or copying IEs
  if (ENABLE_CAPTURE_RSSI_FLOOR && pkt->rx_ctrl.rssi < CAPTURE_RSSI_MIN) {
    return;
  }

  const uint16_t sigLen = pkt->rx_ctrl.sig_len;
  const uint8_t* payload = pkt->payload;
  if (!payload) return;

  bump(wifiFramesSeen);     // proof of life for the sniffer watchdog

  HeaderEvent header{};
  if (!fillHeaderEvent(header, payload, sigLen, type,
                       pkt->rx_ctrl.rssi, pkt->rx_ctrl.channel)) {
    return;
  }

  const bool prefixHit = headerHasTargetPrefix(header) ||
      (type == WIFI_PKT_MGMT && (header.subtype == 0x08 || header.subtype == 0x05) &&
       matchDerivedApPrefix(header.addr2));

  if (type == WIFI_PKT_MGMT) {
    const bool needsIePayload = (header.subtype == 0x04 ||
                                 header.subtype == 0x05 ||
                                 header.subtype == 0x08);

    if (needsIePayload) {
      const size_t offset = (header.subtype == 0x04) ? 24 : 36;
      if (sigLen < offset) return;

      const size_t avail  = static_cast<size_t>(sigLen) - offset;
      const size_t capLen = min(avail, RAW_IE_CAPTURE_MAX);

      // Decide here, in the Wi-Fi task, whether this frame is worth queueing.
      // v1 copied every beacon it heard (556 bytes each), which overflowed the
      // queue in busy areas and dropped frames at random - targets included.
      IeParseResult parsed{};
      parseRawIEsSingle(payload + offset, capLen, parsed);

      observeWifiFrame(header, reinterpret_cast<const uint8_t*>(parsed.ssid), parsed.ssidLen,
                       payload + offset, capLen);

#if HOME_SYNC
      // Is this my home network's beacon? (exact name, optional router address)
      if (header.subtype == 0x08 && parsed.ssidLen == sizeof(HOME_WIFI_SSID) - 1 &&
          memcmp(parsed.ssid, HOME_WIFI_SSID, parsed.ssidLen) == 0 && header.rssi >= HOME_MIN_RSSI) {
        bool bssidOk = true;
        if (homeBssidFilter) {                // optional list of the home access points
          bssidOk = false;
          for (uint8_t i = 0; i < homeBssidCount && !bssidOk; ++i)
            bssidOk = (memcmp(homeBssidList[i], header.addr2, 6) == 0);
        }
        if (bssidOk) {
          homeBeaconRssi = header.rssi;
          bump(homeBeaconCount);
        }
      }
#endif

      bool ssidHit = false;
      bool ignoredSsid = false;
      if (parsed.ssidLen > 0) {
        normalizeStringUpper(parsed.ssid);
        ssidHit = (classifySsid(parsed.ssid, ignoredSsid) != nullptr);
      }

      bump(mgmtFramesSeen);
      recordIeStatistics(header.subtype, avail, parsed);
      if (avail > RAW_IE_CAPTURE_MAX) bump(ieFramesOverCaptureLimit);

      // Ignored (Fire/EMS/transit) networks always go through to loop(), even
      // from an address we do not track: multi-SSID routers send them from a
      // derived BSSID (02:30:44:... next to the Cradlepoint 00:30:44:...), and
      // silencing that one silences its sibling radios too.
      if (!prefixHit && !ssidHit && !ignoredSsid && !parsed.remoteId && parsed.wpsMatch == nullptr) {
        bump(mgmtFramesFiltered);
        return;
      }

      // Static: this callback only ever runs on the Wi-Fi task, one frame at a
      // time, so the 556-byte event does not need to live on that task's stack.
      static MgmtIeEvent event;
      event.header = header;
      event.ieAvailableLen = static_cast<uint16_t>(min(avail, static_cast<size_t>(0xFFFF)));
      event.ieLen = static_cast<uint16_t>(capLen);
      if (event.ieLen > 0) {
        memcpy(event.ieData, payload + offset, event.ieLen);
      }

      bump(selectedFrames);
      bump(selectedMgmtIeFrames);
      if (xQueueSend(mgmtIeQueue, &event, 0) != pdTRUE) {
        bump(totalDrops);
        bump(mgmtIeDrops);
      }
      return;
    }
  }

  observeWifiFrame(header, nullptr, 0);

  if (!prefixHit) return;

  bump(selectedFrames);
  bump(selectedHeaderFrames);
  if (xQueueSend(headerQueue, &header, 0) != pdTRUE) {
    bump(totalDrops);
    bump(headerDrops);
  }
}

// ============================================================
// WI-FI EVIDENCE EVALUATION (loop context)
// ============================================================

static inline void considerLabel(Sighting& s, const char* label, uint8_t rank) {
  if (label && rank > s.labelRank) {
    s.label     = label;
    s.labelRank = rank;
  }
}

// The target's address appeared as a receiver / BSSID / WDS source: something
// nearby is talking to it even though we did not hear it transmit.
static void reportEcho(const uint8_t mac[6], const MacPrefixTarget* prefixMatch,
                       const HeaderEvent& event, uint32_t now) {
  Sighting s{};
  memcpy(s.mac, mac, 6);
  s.src     = SRC_WIFI;
  s.viaEcho = true;
  s.hasRssi = false;           // RSSI belongs to the transmitter, not to this address
  s.rssi    = event.rssi;
  s.channel = event.channel;
  s.reasons = REASON_ECHO;
  if (prefixMatch->category == CAT_HIGH_CONF) {
    s.tier     = TIER_LOW;     // traffic addressed to a police-specific device
    s.reasons |= REASON_PREFIX_HIGH;
  } else {
    s.tier     = TIER_ECHO;
    s.reasons |= REASON_PREFIX_LOW;
  }
  if (event.frameType == WIFI_PKT_DATA) s.reasons |= REASON_DATA_FRAME;
  considerLabel(s, prefixMatch->label, LABEL_RANK_ECHO);
  noteSighting(s, now);
}

static void evaluateAddressEvidence(const HeaderEvent& event,
                                    const SSIDTarget* ssidMatch, size_t matchedSsidIdx,
                                    bool isIgnoredSsid, bool isQuietSsid,
                                    const WPSTarget* wpsMatch, size_t matchedWpsIdx,
                                    bool isWildcardProbe,
                                    const char* ssidText,
                                    uint32_t now, bool isRemoteId = false) {
  size_t matchedPrefixIdx2 = 0;
  const MacPrefixTarget* prefixMatch2 = matchEligiblePrefixMac(event.addr2, &matchedPrefixIdx2);
  if (!prefixMatch2 && event.frameType == WIFI_PKT_MGMT && (event.subtype == 0x08 || event.subtype == 0x05))
    prefixMatch2 = matchDerivedApPrefix(event.addr2, &matchedPrefixIdx2);   // a router's extra network

  size_t matchedPrefixIdx1 = 0;
  const MacPrefixTarget* prefixMatch1 = matchEligiblePrefixMac(event.addr1, &matchedPrefixIdx1);

  size_t matchedPrefixIdx3 = 0;
  const MacPrefixTarget* prefixMatch3 = matchEligiblePrefixMac(event.addr3, &matchedPrefixIdx3);

  size_t matchedPrefixIdx4 = 0;
  const MacPrefixTarget* prefixMatch4 = event.hasAddr4
      ? matchEligiblePrefixMac(event.addr4, &matchedPrefixIdx4)
      : nullptr;

  // If this is a beacon or probe response for an ignored SSID (e.g. "HenricoFire"),
  // suppress the AP's MAC so its generic router OUI (Cradlepoint/Sierra/Peplink)
  // does not trigger LOW/ECHO alerts on data or management frames.
  if (isIgnoredSsid && event.frameType == WIFI_PKT_MGMT &&
      (event.subtype == 0x08 || event.subtype == 0x05)) {
    const bool hasHighHardware =
        (prefixMatch2 && prefixMatch2->category == CAT_HIGH_CONF) ||
        (wpsMatch && wpsMatch->category == CAT_HIGH_CONF);
    if (!hasHighHardware) {
      const uint16_t why = REASON_FLEET_SSID | (prefixMatch2 ? REASON_PREFIX_LOW : 0) |
                           (wpsMatch ? REASON_WPS_LOW : 0);
      suppressDevice(event.addr2, "Ignored Fleet", event, ssidText, why, now);
      if (!macMatches(event.addr3, event.addr2)) {
        suppressDevice(event.addr3, "Ignored Fleet", event, nullptr, REASON_FLEET_SSID, now);
      }
      return;
    }
  }

  // Traffic to, from or through a router that is already silenced as a
  // non-police fleet (or through one of its sibling radios): the laptops,
  // cameras and extra SSIDs riding on a fire truck's router are not police
  // either. Silence them too - unless something in the frame is HIGH.
  if (isIgnoredFleetMac(event.addr2, now) || isIgnoredFleetMac(event.addr1, now) ||
      isIgnoredFleetMac(event.addr3, now)) {
    const bool anyHigh =
        (prefixMatch2 && prefixMatch2->category == CAT_HIGH_CONF) ||
        (prefixMatch1 && prefixMatch1->category == CAT_HIGH_CONF) ||
        (prefixMatch3 && prefixMatch3->category == CAT_HIGH_CONF) ||
        (prefixMatch4 && prefixMatch4->category == CAT_HIGH_CONF) ||
        (ssidMatch    && ssidMatch->category    == CAT_HIGH_CONF) ||
        (wpsMatch     && wpsMatch->category     == CAT_HIGH_CONF);
    if (!anyHigh) {
      // Rides on a silenced router: not its own fleet name, so no FLEET_SSID.
      const bool ownName = (event.frameType == WIFI_PKT_MGMT &&
                            (event.subtype == 0x08 || event.subtype == 0x05));
      if (prefixMatch2 || ssidMatch || wpsMatch)
        suppressDevice(event.addr2, "Ignored Fleet", event, ownName ? ssidText : nullptr,
                       prefixMatch2 ? REASON_PREFIX_LOW : 0, now);
      if (prefixMatch1) suppressDevice(event.addr1, "Ignored Fleet", event, nullptr, REASON_PREFIX_LOW | REASON_ECHO, now);
      if (prefixMatch3) suppressDevice(event.addr3, "Ignored Fleet", event, nullptr, REASON_PREFIX_LOW | REASON_ECHO, now);
      if (prefixMatch4) suppressDevice(event.addr4, "Ignored Fleet", event, nullptr, REASON_PREFIX_LOW | REASON_ECHO, now);
      return;
    }
  }

  if (prefixMatch2) { ++prefixHitsTotal; prefixTargetHits[matchedPrefixIdx2]++; }
  if (prefixMatch1) { ++prefixHitsTotal; prefixTargetHits[matchedPrefixIdx1]++; }
  if (prefixMatch3) { ++prefixHitsTotal; prefixTargetHits[matchedPrefixIdx3]++; }
  if (prefixMatch4) { ++prefixHitsTotal; prefixTargetHits[matchedPrefixIdx4]++; }
  if (ssidMatch)    { ++ssidHitsTotal;   ssidTargetHits[matchedSsidIdx]++; }
  if (wpsMatch)     { ++wpsHitsTotal;    wpsTargetHits[matchedWpsIdx]++; }

  // --- The transmitter itself: one frame is enough. ---
  if ((prefixMatch2 || ssidMatch || wpsMatch || isRemoteId) && !isGroupMac(event.addr2)) {
    Sighting s{};
    memcpy(s.mac, event.addr2, 6);
    s.src     = SRC_WIFI;
    s.viaEcho = false;
    s.hasRssi = true;
    s.rssi    = event.rssi;
    s.channel = event.channel;
    s.ssidSeen = (event.frameType == WIFI_PKT_MGMT &&
                  (event.subtype == 0x08 || event.subtype == 0x05));
    s.quiet    = (isQuietSsid && s.ssidSeen) || isQuietMac(event.addr2);
    s.ssid     = s.ssidSeen ? ssidText : nullptr;   // its own name, not one it is asking for

    bool high = false;
    bool low  = false;    // otherwise only MINOR evidence (drone): one tick
    if (prefixMatch2) {
      if (prefixMatch2->category == CAT_HIGH_CONF) {
        high = true;
        s.reasons |= REASON_PREFIX_HIGH;
        considerLabel(s, prefixMatch2->label, LABEL_RANK_HIGH_PRF);
      } else if (prefixMatch2->category == CAT_MINOR) {
        s.reasons |= REASON_PREFIX_LOW | REASON_DRONE;
        considerLabel(s, prefixMatch2->label, LABEL_RANK_ECHO);
      } else {
        low = true;
        s.reasons |= REASON_PREFIX_LOW;
        considerLabel(s, prefixMatch2->label, LABEL_RANK_LOW);
      }
    }
    if (isRemoteId) {
      s.reasons |= REASON_DRONE;
      considerLabel(s, DRONE_LABEL, LABEL_RANK_ECHO);
    }
    if (ssidMatch) {
      if (ssidMatch->category == CAT_HIGH_CONF) {
        high = true;
        s.reasons |= REASON_SSID_HIGH;
        considerLabel(s, ssidMatch->label, LABEL_RANK_HIGH_SSID);
      } else {
        low = true;
        s.reasons |= REASON_SSID_LOW;
        considerLabel(s, ssidMatch->label, LABEL_RANK_LOW);
      }
      if (event.frameType == WIFI_PKT_MGMT && event.subtype == 0x04) {
        s.reasons |= REASON_DIRECTED_PROBE;   // a client asking for a target network
      }
    }
    if (wpsMatch) {
      if (wpsMatch->category == CAT_HIGH_CONF) {
        high = true;
        s.reasons |= REASON_WPS_HIGH;
        considerLabel(s, wpsMatch->label, LABEL_RANK_HIGH_WPS);
      } else {
        low = true;
        s.reasons |= REASON_WPS_LOW;
        considerLabel(s, wpsMatch->label, LABEL_RANK_LOW);
      }
    }
    if (isWildcardProbe && prefixMatch2)  s.reasons |= REASON_WILDCARD_PROBE;
    if (event.frameType == WIFI_PKT_DATA) s.reasons |= REASON_DATA_FRAME;

    s.tier = high ? TIER_HIGH : low ? TIER_LOW : TIER_ECHO;
    noteSighting(s, now);
  }

  // --- Target addresses that appear as receiver / BSSID / WDS source. ---
  if (prefixMatch1 && !macMatches(event.addr1, event.addr2)) {
    reportEcho(event.addr1, prefixMatch1, event, now);
  }

  if (prefixMatch3 &&
      !macMatches(event.addr3, event.addr2) &&
      !macMatches(event.addr3, event.addr1)) {
    reportEcho(event.addr3, prefixMatch3, event, now);
  }

  if (prefixMatch4 &&
      !macMatches(event.addr4, event.addr2) &&
      !macMatches(event.addr4, event.addr1) &&
      !macMatches(event.addr4, event.addr3)) {
    reportEcho(event.addr4, prefixMatch4, event, now);
  }
}

static void evaluateHeaderEvent(const HeaderEvent& event, uint32_t now) {
  evaluateAddressEvidence(event, nullptr, 0, /*isIgnoredSsid=*/false, /*isQuietSsid=*/false,
                          nullptr, 0, /*isWildcardProbe=*/false, /*ssidText=*/nullptr, now);
}

static void evaluateMgmtIeEvent(const MgmtIeEvent& event, uint32_t now) {
  const HeaderEvent& header = event.header;

  IeParseResult parsed{};
  bool usedFcsTrim = false;
  if (event.ieLen > 0) {
    const bool allowFcsTrim = (event.ieAvailableLen == event.ieLen);
    parseRawIEsFlexible(event.ieData, event.ieLen, allowFcsTrim,
                        parsed, usedFcsTrim);

    if (usedFcsTrim) ++fcsTrimmedIeFrames;
    if (!parsed.structurallyComplete) ++malformedIeFrames;
  }

  char ssidText[AIRWATCH_MAX_SSID_LEN + 1] = "";     // as broadcast, made printable
  if (parsed.ssidLen > 0) {
    sanitizeText(ssidText, sizeof(ssidText),
                 reinterpret_cast<const uint8_t*>(parsed.ssid), parsed.ssidLen);
    normalizeStringUpper(parsed.ssid);
  }

  bool isIgnoredSsid = false;
  size_t matchedSsidIdx = 0;
  const SSIDTarget* ssidMatch = (parsed.ssidLen > 0)
      ? classifySsid(parsed.ssid, isIgnoredSsid, &matchedSsidIdx)
      : nullptr;

  const bool isWildcardProbe = (header.subtype == 0x04 && parsed.wildcardSsid);
  const bool isQuietSsid = (parsed.ssidLen > 0) && !isIgnoredSsid && matchQuietSSID(parsed.ssid);

  evaluateAddressEvidence(header,
                          ssidMatch, matchedSsidIdx,
                          isIgnoredSsid, isQuietSsid,
                          parsed.wpsMatch, parsed.wpsMatchIdx,
                          isWildcardProbe,
                          ssidText,
                          now, parsed.remoteId && header.subtype == 0x08);
}

// ============================================================
// BLUETOOTH LE PASSIVE SCAN
// ============================================================

static void evaluateBleEvent(const BleEvent& event, uint32_t now) {
  // Thread-safe hit counter updates in loop() context
  if (event.prefixIdx < NUM_PREFIX_TARGETS) {
    ++prefixHitsTotal;
    prefixTargetHits[event.prefixIdx]++;
  }
  for (size_t i = 0; i < NUM_BLE_COMPANY_TARGETS; ++i) {
    if (event.companyMask & (1U << i)) bleCompanyTargetHits[i]++;
  }
  for (size_t i = 0; i < NUM_BLE_SERVICE_TARGETS; ++i) {
    if (event.serviceMask & (1U << i)) bleServiceTargetHits[i]++;
  }
  for (size_t i = 0; i < NUM_BLE_NAME_TARGETS; ++i) {
    if (event.nameMask & (1U << i)) bleNameTargetHits[i]++;
  }

  Sighting s{};
  memcpy(s.mac, event.mac, 6);
  s.src       = SRC_BLE;
  s.viaEcho   = false;
  s.hasRssi   = true;
  s.rssi      = event.rssi;
  s.channel   = 0;
  s.reasons   = event.reasons;
  s.label     = event.label;
  s.labelRank = event.labelRank;
  s.tier      = (event.category == CAT_HIGH_CONF) ? TIER_HIGH
              : (event.category == CAT_LOW_CONF)  ? TIER_LOW : TIER_ECHO;
  s.needsSecond = event.soloCompany;
  noteSighting(s, now);
}

#if ENABLE_BLE_SCAN

// Folds one signature match into the event being built for this advert.
static inline void bleConsider(BleEvent& ev, bool& matched, const char* label,
                               TargetCategory category, uint16_t reason) {
  const uint8_t rank = (category == CAT_HIGH_CONF) ? LABEL_RANK_HIGH_BLE
                     : (category == CAT_LOW_CONF)  ? LABEL_RANK_LOW : LABEL_RANK_ECHO;
  if (!matched || rank > ev.labelRank) {
    ev.label     = label;
    ev.labelRank = rank;
  }
  // The event starts as MINOR; the strongest signature in the advert wins
  // (lower category value = stronger).
  if (category < ev.category) ev.category = category;
  ev.reasons |= reason;
  matched = true;
}

static inline void bleCheckService(BleEvent& ev, bool& matched, uint16_t uuid16) {
  for (size_t i = 0; i < NUM_BLE_SERVICE_TARGETS; ++i) {
    if (bleServiceTargets[i].uuid16 == uuid16) {
      ev.serviceMask |= static_cast<uint8_t>(1U << i);
      bleConsider(ev, matched, bleServiceTargets[i].label,
                  bleServiceTargets[i].category, REASON_BLE_SERVICE);
    }
  }
}

// Runs in the NimBLE host task for every advertisement heard: match and
// enqueue only. No Serial, no audio, no heap, no shared counter mutations.
static void bleEvaluateAdvert(const NimBLEAdvertisedDevice* dev) {
  if (!dev) return;
  bump(bleAdvertsSeen);

  const NimBLEAddress& addr = dev->getAddress();
  const uint8_t* le = addr.getVal();          // least-significant byte first
  if (!le) return;

  BleEvent ev{};
  for (int i = 0; i < 6; ++i) ev.mac[i] = le[5 - i];
  ev.rssi      = static_cast<int8_t>(dev->getRSSI());
  ev.category  = CAT_MINOR;
  ev.prefixIdx = 0xFF;
  bool matched = false;
  bool wearableService = false;

  // For the mark log: first company ID / service UUID / name in this advert.
  Observation obs{};
  memcpy(obs.mac, ev.mac, 6);
  obs.kind = (addr.getType() == BLE_ADDR_PUBLIC) ? OBS_BLE_PUBLIC : OBS_BLE;
  obs.rssi = ev.rssi;

  // 1) Vendor prefix - only meaningful for public (IEEE-assigned) addresses.
  if (addr.getType() == BLE_ADDR_PUBLIC) {
    size_t idx = 0;
    const MacPrefixTarget* t = matchEligiblePrefixMac(ev.mac, &idx);
    if (t) {
      ev.prefixIdx = static_cast<uint8_t>(idx);
      bleConsider(ev, matched, t->label, t->category,
                  REASON_BLE_ADDR | (t->category == CAT_MINOR ? REASON_DRONE : 0));
    }
  }

  // 2) Walk the advertising payload: [len][type][data...] repeated.
  const std::vector<uint8_t>& payload = dev->getPayload();
  const size_t total = payload.size();
  size_t pos = 0;
  while (pos + 1 < total) {
    const uint8_t fieldLen = payload[pos];
    if (fieldLen == 0 || pos + 1 + fieldLen > total) break;
    const uint8_t  adType = payload[pos + 1];
    const uint8_t* data   = &payload[pos + 2];
    const size_t   dataLen = fieldLen - 1;

    switch (adType) {
      case 0xFF: {   // manufacturer specific data: company ID, little-endian
        if (dataLen >= 2) {
          const uint16_t companyId = static_cast<uint16_t>(data[0] | (data[1] << 8));
          if (!(obs.kind & OBS_HAS_CID)) { obs.bleCompany = companyId; obs.kind |= OBS_HAS_CID; }
          for (size_t i = 0; i < NUM_BLE_COMPANY_TARGETS; ++i) {
            if (bleCompanyTargets[i].companyId == companyId) {
              ev.companyMask |= static_cast<uint8_t>(1U << i);
              bleConsider(ev, matched, bleCompanyTargets[i].label,
                          bleCompanyTargets[i].category, REASON_BLE_COMPANY);
            }
          }
        }
        break;
      }
      case 0x02:     // incomplete list of 16-bit service UUIDs
      case 0x03: {   // complete list of 16-bit service UUIDs
        for (size_t i = 0; i + 1 < dataLen; i += 2) {
          const uint16_t uuid16 = static_cast<uint16_t>(data[i] | (data[i + 1] << 8));
          if (!obs.bleService) obs.bleService = uuid16;
          if (uuid16 == BLE_WEARABLE_SERVICE) wearableService = true;
          bleCheckService(ev, matched, uuid16);
        }
        break;
      }
      case 0x16: {   // service data, 16-bit UUID first
        if (dataLen >= 2) {
          const uint16_t uuid16 = static_cast<uint16_t>(data[0] | (data[1] << 8));
          if (!obs.bleService) obs.bleService = uuid16;
          if (uuid16 == BLE_WEARABLE_SERVICE) wearableService = true;
          if (uuid16 == BLE_REMOTE_ID_SERVICE && dataLen >= 3 && data[2] == REMOTE_ID_APP_CODE) {
            bump(bleRemoteIdHits);
            bleConsider(ev, matched, DRONE_LABEL, CAT_MINOR, REASON_DRONE);
          }
          bleCheckService(ev, matched, uuid16);
        }
        break;
      }
      // The same 16-bit IDs can also arrive in their long forms: a 32-bit
      // UUID with zero upper half, or a 128-bit UUID on the Bluetooth base
      // (0000xxxx-0000-1000-8000-00805F9B34FB). Axon's are checked there too.
      case 0x04:     // incomplete list of 32-bit service UUIDs
      case 0x05:     // complete list of 32-bit service UUIDs
      case 0x20: {   // service data, 32-bit UUID first
        const size_t step = (adType == 0x20) ? dataLen : 4;
        for (size_t i = 0; i + 4 <= dataLen && step >= 4; i += step) {
          if (data[i + 2] || data[i + 3]) continue;
          const uint16_t uuid16 = static_cast<uint16_t>(data[i] | (data[i + 1] << 8));
          if (!obs.bleService) obs.bleService = uuid16;
          bleCheckService(ev, matched, uuid16);
        }
        break;
      }
      case 0x06:     // incomplete list of 128-bit service UUIDs
      case 0x07:     // complete list of 128-bit service UUIDs
      case 0x21: {   // service data, 128-bit UUID first
        static const uint8_t kBase[12] = {0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00,
                                          0x00, 0x80, 0x00, 0x10, 0x00, 0x00};
        const size_t step = (adType == 0x21) ? dataLen : 16;
        for (size_t i = 0; i + 16 <= dataLen && step >= 16; i += step) {
          if (memcmp(data + i, kBase, 12) != 0 || data[i + 14] || data[i + 15]) continue;
          const uint16_t uuid16 = static_cast<uint16_t>(data[i + 12] | (data[i + 13] << 8));
          if (!obs.bleService) obs.bleService = uuid16;
          bleCheckService(ev, matched, uuid16);
        }
        break;
      }
      case 0x08:     // shortened local name
      case 0x09: {   // complete local name
        if (!obs.text[0]) sanitizeText(obs.text, sizeof(obs.text), data, dataLen);
        char name[MAX_BLE_NAME_LEN + 1];
        const size_t copyLen = (dataLen > MAX_BLE_NAME_LEN) ? MAX_BLE_NAME_LEN : dataLen;
        memcpy(name, data, copyLen);
        name[copyLen] = '\0';
        normalizeStringUpper(name);
        for (size_t i = 0; i < NUM_BLE_NAME_TARGETS; ++i) {
          if (strstr(name, bleNameTargets[i].pattern) != nullptr) {
            ev.nameMask |= static_cast<uint8_t>(1U << i);
            bleConsider(ev, matched, bleNameTargets[i].label,
                        bleNameTargets[i].category, REASON_BLE_NAME);
          }
        }
        break;
      }
      default:
        break;
    }
    pos += 1 + static_cast<size_t>(fieldLen);
  }

  // 3) Axon body-camera tag anywhere in the raw advert, forwards or reversed.
  if (total >= 9) {
    const uint8_t* raw = payload.data();
    for (size_t i = 0; i + 9 <= total; ++i) {
      bool fw = true, bw = true;
      for (size_t k = 0; k < 9 && (fw || bw); ++k) {
        if (raw[i + k] != static_cast<uint8_t>(AXON_BWC_TAG[k]))     fw = false;
        if (raw[i + k] != static_cast<uint8_t>(AXON_BWC_TAG[8 - k])) bw = false;
      }
      if (fw || bw) {
        bump(bleAxonBwcHits);
        bleConsider(ev, matched, AXON_BWC_LABEL, CAT_HIGH_CONF, REASON_BLE_SERVICE);
        break;
      }
    }
  }

  // A HIGH match that rests on the company ID alone (no Axon service, name
  // or address prefix) is the one signature a stray device can fake by
  // accident. It still alerts on the first advert, but as LOW; a second
  // advert makes it HIGH. With the fitness-band service present it stays LOW.
  if (matched && ev.category == CAT_HIGH_CONF &&
      (ev.reasons & (REASON_BLE_SERVICE | REASON_BLE_NAME)) == 0 &&
      !(ev.prefixIdx != 0xFF && prefixTargets[ev.prefixIdx].category == CAT_HIGH_CONF)) {
    if (wearableService) {
      ev.category  = CAT_LOW_CONF;
      ev.labelRank = LABEL_RANK_LOW;
    } else {
      ev.soloCompany = true;
    }
  }

#if ENABLE_MARK_LOG
  obs.detailLen = static_cast<uint8_t>((total < OBS_DETAIL_LEN) ? total : OBS_DETAIL_LEN);
  if (obs.detailLen) memcpy(obs.detail, payload.data(), obs.detailLen);
  bool phoneAdvert = false;
  if (!matched && addr.getType() != BLE_ADDR_PUBLIC) {
    if (obs.kind & OBS_HAS_CID) {
      for (size_t i = 0; i < NUM_BLE_PHONE_COMPANY_IDS; ++i) {
        if (blePhoneCompanyIds[i] == obs.bleCompany) { phoneAdvert = true; break; }
      }
    }
    for (size_t i = 0; i < NUM_BLE_PHONE_SERVICE_IDS && !phoneAdvert; ++i) {
      if (blePhoneServiceIds[i] == obs.bleService) phoneAdvert = true;
    }
  }
  if (!phoneAdvert && obsThrottlePass(bleObsThrottle, obs.mac, millis())) {
    if (xQueueSend(obsQueue, &obs, 0) != pdTRUE) bump(obsDrops);
  }
#endif

  if (!matched) return;

  bump(bleMatches);
  if (xQueueSend(bleQueue, &ev, 0) != pdTRUE) {
    bump(bleDrops);
  }
}

class AirwatchBleCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
    bleEvaluateAdvert(advertisedDevice);
  }
};

AirwatchBleCallbacks bleCallbacks;

static bool bleStartScan() {
  NimBLEScan* scan = NimBLEDevice::getScan();
  if (!scan) return false;
  // duration 0 = scan until stopped; restart=true clears any stale state
  return scan->start(0, false, true);
}

static bool bleBegin() {
  if (!NimBLEDevice::init("")) return false;

  NimBLEScan* scan = NimBLEDevice::getScan();
  if (!scan) return false;

  scan->setScanCallbacks(&bleCallbacks, /*wantDuplicates=*/true);
  scan->setActiveScan(false);                 // passive: never sends SCAN_REQ
  scan->setInterval(BLE_SCAN_INTERVAL_MS);
  scan->setWindow(BLE_SCAN_WINDOW_MS);
  scan->setDuplicateFilter(0);                // we want every repeat for RSSI/presence
  scan->setMaxResults(0);                     // callback only; keep no result list
  return bleStartScan();
}

// Restarts the scan if the controller stops it on its own, or if it claims
// to be scanning but has delivered nothing for BLE_STALL_RESTART_MS.
static void bleWatchdog(uint32_t now) {
  static uint32_t lastCheck    = 0;
  static uint32_t lastAdverts  = 0;
  static uint32_t lastProgress = 0;
  if (!bleReady || !timeReached(now, lastCheck + BLE_WATCHDOG_MS)) return;
  lastCheck = now;

  NimBLEScan* scan = NimBLEDevice::getScan();
  if (!scan) return;

  const uint32_t adverts = bleAdvertsSeen;
  if (adverts != lastAdverts) {
    lastAdverts  = adverts;
    lastProgress = now;
  }

  if (!scan->isScanning()) {
    bleStartScan();
    ++bleRestarts;
    lastProgress = now;
  } else if ((now - lastProgress) >= BLE_STALL_RESTART_MS) {
    scan->stop();
    bleStartScan();
    ++bleRestarts;
    lastProgress = now;
  }
}

#endif  // ENABLE_BLE_SCAN

// ============================================================
// WI-FI SNIFFER WATCHDOG
// ============================================================

// If the sniffer has delivered no frame of any kind for WIFI_STALL_REARM_MS,
// switch promiscuous mode off and on again. On an empty road this fires now
// and then for no reason and costs nothing; if the driver really did stall,
// it brings detection back without a power cycle.
static void wifiWatchdog(uint32_t now) {
  static uint32_t lastCheck    = 0;
  static uint32_t lastFrames   = 0;
  static uint32_t lastProgress = 0;
  if (!timeReached(now, lastCheck + 5000)) return;
  lastCheck = now;

  const uint32_t frames = wifiFramesSeen;
  if (frames != lastFrames) {
    lastFrames   = frames;
    lastProgress = now;
    return;
  }
  if ((now - lastProgress) < WIFI_STALL_REARM_MS) return;

  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(&wifi_promiscuous_handler);
  esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
  if (esp_wifi_set_promiscuous(true) != ESP_OK) ++channelErrors;
  ++wifiRearms;
  lastProgress = now;
}

// ============================================================
// HARDWARE OUTPUT & DISPLAY
// ============================================================

// LED test ('led' command / web button): red, green, blue, white, then the
// alert colours, ~0.8 s each, then back to normal. Runs from loop(), so
// detection carries on while it plays.
struct LedTestStep { uint8_t r, g, b; const char* name; };
const LedTestStep LED_TEST_STEPS[] = {
  {255, 0, 0, "red"}, {0, 255, 0, "green"}, {0, 0, 255, "blue"}, {255, 255, 255, "white"},
  {255, 0, 0, "alert: POLICE (red)"}, {255, 127, 0, "alert: MAYBE (amber)"}, {0, 0, 40, "alert: MINOR (dim blue)"},
};
constexpr uint8_t  LED_TEST_COUNT   = sizeof(LED_TEST_STEPS) / sizeof(LED_TEST_STEPS[0]);
constexpr uint32_t LED_TEST_STEP_MS = 800;
bool     ledTestActive = false;
uint32_t ledTestStart  = 0;
uint8_t  ledTestShown  = 0xFF;

static void updateLed() {
  if (ledTestActive) return;               // the test owns the LED for a few seconds
  if (!alertView.active) {
    strip.setPixelColor(0, 0, 0, 0);
  } else {
    int brightness = 40;
    if (alertView.hasRssi) {
      brightness = constrain(static_cast<int>(map(alertView.rssi, RSSI_LED_MIN, RSSI_LED_MAX, 40, 255)),
                             40, 255);
    }
    if (alertView.tier == TIER_HIGH) {
      strip.setPixelColor(0, brightness, 0, 0);               // red
    } else if (alertView.tier == TIER_LOW) {
      strip.setPixelColor(0, brightness, brightness / 2, 0);  // amber
    } else {
      strip.setPixelColor(0, 0, 0, 40);                       // dim blue (echo)
    }
  }
  strip.show();
}

static void ledTestBegin(uint32_t now) {
  ledTestActive = true;
  ledTestStart  = now;
  ledTestShown  = 0xFF;
  Console.printf("[LED] test on GPIO%d: red, green, blue, white, then the alert colours (%.1f s)\n",
                 static_cast<int>(LED_PIN), LED_TEST_COUNT * LED_TEST_STEP_MS / 1000.0f);
  Console.println(F("[LED] red and green swapped? change NEO_GRB to NEO_RGB. Nothing at all? check 5V/GND, "
                    "the data wire to D3 and that it goes to DIN (not DOUT)"));
}

static void ledTestTick(uint32_t now) {
  if (!ledTestActive) return;
  const uint32_t step = (now - ledTestStart) / LED_TEST_STEP_MS;
  if (step >= LED_TEST_COUNT) {
    ledTestActive = false;
    Console.println(F("[LED] test done"));
    updateLed();                           // back to whatever the alert state says
    return;
  }
  if (step == ledTestShown) return;
  ledTestShown = static_cast<uint8_t>(step);
  const LedTestStep& s = LED_TEST_STEPS[step];
  strip.setPixelColor(0, s.r, s.g, s.b);
  strip.show();
  Console.printf("[LED] %s\n", s.name);
}

// How many distinct Wi-Fi and Bluetooth devices were heard within the mark window.
static void countRecentDevices(uint32_t now, uint16_t& wifi, uint16_t& ble) {
  wifi = 0; ble = 0;
  for (size_t i = 0; i < recentCap; ++i) {
    const RecentIndex& x = recentIndex[i];
    if (!x.key || (now - x.lastSeen) >= MARK_WINDOW_MS) continue;
    if (x.key & 1U) ++ble; else ++wifi;
  }
}

// Crossed-out speaker, 11 x 8 px, top-left corner at (x, y).
static void drawMuteIcon(int16_t x, int16_t y) {
  display.fillRect(x, y + 2, 3, 4, SSD1306_WHITE);
  display.fillTriangle(x + 2, y + 4, x + 5, y, x + 5, y + 7, SSD1306_WHITE);
  display.drawLine(x + 7, y + 1, x + 10, y + 6, SSD1306_WHITE);
  display.drawLine(x + 10, y + 1, x + 7, y + 6, SSD1306_WHITE);
}

static void refreshDisplay(bool forceRedraw) {
  if (noticeActive) {            // mark confirmation takes over the screen briefly
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(strlen(noticeLine1) <= 10 ? 2 : 1);   // 10 large characters fit
    display.setCursor(0, 0);
    display.println(noticeLine1);
    display.setTextSize(1);
    display.setCursor(0, 20);
    display.println(noticeLine2);
    display.display();
    updateLed();
    return;
  }

  DisplaySnapshot current{};
  current.inAlert  = alertView.active;
  current.muted    = audioMuted;
  current.channel  = alertView.active ? alertView.channel : currentChannel;
  if (alertView.active) {
    current.tier     = alertView.tier;
    current.echoOnly = alertView.echoOnly;
    current.hasRssi  = alertView.hasRssi;
    current.src      = alertView.src;
    current.count    = alertView.count;
    current.rssi     = alertView.rssi;
    strncpy(current.label, alertView.label, sizeof(current.label) - 1);
    current.label[sizeof(current.label) - 1] = '\0';
  }

  if (!forceRedraw && current.equals(lastRenderedState)) return;
  lastRenderedState = current;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // The 128x32 panel holds two rows of 10 large characters, or one large
  // row plus two small rows of 21.
  if (alertView.active) {
    const char* word = (alertView.tier == TIER_HIGH) ? "POLICE"
                     : (alertView.tier == TIER_LOW)  ? "MAYBE" : "MINOR";
    display.setTextSize(2);
    display.setCursor(0, 0);
    if (alertView.hasRssi && !alertView.echoOnly) display.printf("%-6s%4d", word, alertView.rssi);
    else                                          display.printf("%-6secho", word);

    display.setTextSize(1);
    display.setCursor(0, 16);
    display.println(alertView.label);

    display.setCursor(0, 24);
    if (alertView.src == SRC_BLE) display.print(F("BLE "));
    else                          display.printf("W%u ", alertView.channel);
    display.printf("x%u", alertView.count);
    if (audioMuted) drawMuteIcon(116, 24);
  } else if (maintActive) {
    const uint32_t now = millis();
    display.setTextSize(2);
    display.setCursor(0, 0);
    if (maintRoad) {
      display.print(F("ROAD WEB"));
      display.setTextSize(1);
      display.setCursor(0, 16);
      display.printf("%s %s", ROAD_AP_SSID, WiFi.softAPIP().toString().c_str());
    } else {
      display.print(maintConnected ? F("HOME WEB") : F("JOINING"));
      display.setTextSize(1);
      display.setCursor(0, 16);
      if (maintConnected) display.print(WiFi.localIP());
      else                display.print(F("home Wi-Fi..."));
    }
    display.setCursor(0, 24);
    const uint32_t idle = timeReached(now, maintLastRequestAt) ? now - maintLastRequestAt : 0;
    const uint32_t left = (idle < MAINT_IDLE_MS) ? (MAINT_IDLE_MS - idle) / 60000UL + 1 : 0;
    display.printf("BLE on, back in %lum", static_cast<unsigned long>(left));
  } else {
    display.setTextSize(2);
    display.setCursor(0, 0);
    const uint32_t now = millis();
    // DROP n only while frames were lost in the last 30 s (e.g. a flash
    // write); otherwise back to the live W/B counts.
    static uint32_t dropsSeen = 0, dropAt = 0;
    const uint32_t drops = totalDrops;
    if (drops != dropsSeen) { dropsSeen = drops; dropAt = now; }
    if (drops > 0 && (now - dropAt) < 30000) {
      display.printf("DROP %lu", static_cast<unsigned long>(drops % 100000UL));
    } else {
      uint16_t wifi = 0, ble = 0;
      countRecentDevices(now, wifi, ble);
      display.printf("W%u B%u", static_cast<unsigned>(wifi), static_cast<unsigned>(ble));
    }
    if (audioMuted) drawMuteIcon(116, 0);
    if (gpsFixFresh(now)) {                  // GPS fix: small G under the mute spot
      display.setTextSize(1);
      display.setCursor(122, 8);
      display.print('G');
      display.setTextSize(2);
    }
    display.setCursor(0, 16);
    if (WEB_UI && roadWindowOpen(now)) {
      display.print(F("HOLD=WEB"));            // road access offer, first 15 s only
    } else if (ENABLE_MARK_LOG && !markLogReady) {
      display.print(F("LOG FAIL"));
    } else if (ENABLE_MARK_LOG && markLogPercent >= 100) {
      display.print(F("LOG FULL"));
    } else if (lastAlertValid && (now - lastAlertAt) < MARK_HISTORY_MS) {
      // What the last beep was, and how long ago it ended.
      const char* word = (lastAlert.tier == TIER_HIGH) ? "POLICE"
                       : (lastAlert.tier == TIER_LOW)  ? "MAYBE" : "MINOR";
      const uint32_t ago = (now - lastAlertAt) / 1000UL;
      if (ago < 60) display.printf("%s %lus", word, static_cast<unsigned long>(ago));
      else          display.printf("%s %lum", word, static_cast<unsigned long>(ago / 60UL));
    } else {
      display.printf("M%u L%u%%", static_cast<unsigned>(markCount % 1000U),
                     static_cast<unsigned>(markLogPercent));
    }
  }

  display.display();
  updateLed();
}

// ============================================================
// DIAGNOSTICS & SERIAL COMMANDS
// ============================================================

static void printSignatureTelemetry() {
  setSerialBlockingMode(true);
  Console.println(F("\n=== PER-SIGNATURE TELEMETRY ==="));
  Console.println(F("--- MAC Prefixes (Wi-Fi + BLE public addresses) ---"));
  for (size_t i = 0; i < NUM_PREFIX_TARGETS; ++i) {
    if (prefixTargetHits[i] > 0) {
      Console.printf("  [0x%llX/%u] %-18s : %lu hits\n",
                    static_cast<unsigned long long>(prefixTargets[i].prefix),
                    prefixTargets[i].prefixBits,
                    prefixTargets[i].label,
                    static_cast<unsigned long>(prefixTargetHits[i]));
    }
  }
  Console.println(F("--- SSIDs ---"));
  for (size_t i = 0; i < NUM_SSID_TARGETS; ++i) {
    if (ssidTargetHits[i] > 0) {
      Console.printf("  %-20s (%-16s) : %lu hits\n",
                    ssidTargets[i].pattern, ssidTargets[i].label,
                    static_cast<unsigned long>(ssidTargetHits[i]));
    }
  }
  Console.printf("  Suppressed Non-LE Fleets (Fire/EMS/Transit): %lu devices\n",
                static_cast<unsigned long>(suppressedDevicesTotal));
  Console.println(F("--- WPS Descriptors ---"));
  for (size_t i = 0; i < NUM_WPS_TARGETS; ++i) {
    if (wpsTargetHits[i] > 0) {
      Console.printf("  %-20s (%-16s) : %lu hits\n",
                    wpsTargets[i].pattern, wpsTargets[i].label,
                    static_cast<unsigned long>(wpsTargetHits[i]));
    }
  }
  Console.println(F("--- BLE ---"));
  for (size_t i = 0; i < NUM_BLE_COMPANY_TARGETS; ++i) {
    if (bleCompanyTargetHits[i] > 0) {
      Console.printf("  company 0x%04X       (%-16s) : %lu hits\n",
                    bleCompanyTargets[i].companyId, bleCompanyTargets[i].label,
                    static_cast<unsigned long>(bleCompanyTargetHits[i]));
    }
  }
  for (size_t i = 0; i < NUM_BLE_SERVICE_TARGETS; ++i) {
    if (bleServiceTargetHits[i] > 0) {
      Console.printf("  service 0x%04X       (%-16s) : %lu hits\n",
                    bleServiceTargets[i].uuid16, bleServiceTargets[i].label,
                    static_cast<unsigned long>(bleServiceTargetHits[i]));
    }
  }
  if (bleAxonBwcHits)  Console.printf("  BWCDEVICE tag        (%-16s) : %lu adverts\n", AXON_BWC_LABEL,
                                      static_cast<unsigned long>(bleAxonBwcHits));
  if (bleRemoteIdHits) Console.printf("  Remote ID 0xFFFA     (%-16s) : %lu adverts\n", "Drone",
                                      static_cast<unsigned long>(bleRemoteIdHits));
  for (size_t i = 0; i < NUM_BLE_NAME_TARGETS; ++i) {
    if (bleNameTargetHits[i] > 0) {
      Console.printf("  name %-15s (%-16s) : %lu hits\n",
                    bleNameTargets[i].pattern, bleNameTargets[i].label,
                    static_cast<unsigned long>(bleNameTargetHits[i]));
    }
  }

  Console.println(F("\n=== IE LENGTH DISTRIBUTION MATRIX (all frames inspected) ==="));
  Console.println(F("Subtype        <=128  129-160 161-192 193-224 225-255  >255   MaxLen"));

  const char* labels[] = {"Probe Req ", "Probe Resp", "Beacon    "};
  for (size_t i = 0; i < 3; ++i) {
    Console.printf("%-12s: %6lu  %6lu  %6lu  %6lu  %6lu  %6lu   %4u B\n",
                  labels[i],
                  static_cast<unsigned long>(ieDistribution[i][0]),
                  static_cast<unsigned long>(ieDistribution[i][1]),
                  static_cast<unsigned long>(ieDistribution[i][2]),
                  static_cast<unsigned long>(ieDistribution[i][3]),
                  static_cast<unsigned long>(ieDistribution[i][4]),
                  static_cast<unsigned long>(ieDistribution[i][5]),
                  static_cast<unsigned>(maxIeObserved[i]));
  }
  Console.printf("Frames with a WPS IE: %lu | Deepest WPS: start @ %u B, end @ %u B\n",
                static_cast<unsigned long>(wpsHitsCaptured),
                static_cast<unsigned>(maxWpsStartObserved),
                static_cast<unsigned>(maxWpsEndObserved));
  Console.printf("SSID IE seen: %lu | Zero-length SSID: %lu | Wildcard probes: %lu\n",
                static_cast<unsigned long>(ssidIeSeenFrames),
                static_cast<unsigned long>(wildcardSsidFrames),
                static_cast<unsigned long>(wildcardProbeFrames));
  Console.printf("IE parser FCS-trim selections: %lu | Malformed/incomplete parses: %lu\n",
                static_cast<unsigned long>(fcsTrimmedIeFrames),
                static_cast<unsigned long>(malformedIeFrames));
  Console.println(F("--------------------------------------\n"));
  setSerialBlockingMode(false);
}

static void printTrackedDevices(uint32_t now) {
  setSerialBlockingMode(true);
  Console.println(F("\n=== TRACKED DEVICES ==="));
  size_t shown = 0;
  for (size_t i = 0; i < DEVICE_CACHE_SIZE; ++i) {
    const TrackedDevice& d = deviceCache[i];
    if (!d.valid || (now - d.lastSeen) >= ENCOUNTER_GAP_MS) continue;
    printDeviceLine("[DEV]", d, now);
    ++shown;
  }
  if (shown == 0) Console.println(F("  (none in the last 30 s)"));
  Console.println(F("-----------------------\n"));
  setSerialBlockingMode(false);
}

// One command, from the serial monitor or the web console.
static void runCommand(const char* cmd, bool fromWeb) {
  if (fromWeb) Console.printf("> %s\n", cmd);
  if (strcmp(cmd, "0") == 0) {
    channelHopMs = 150;  Console.println(F("[CONF] Dwell: 150 ms (experimental: faster sweep, fewer beacons per stop)"));
  } else if (strcmp(cmd, "1") == 0) {
    channelHopMs = 250;  Console.println(F("[CONF] Dwell: 250 ms"));
  } else if (strcmp(cmd, "2") == 0) {
    channelHopMs = 500;  Console.println(F("[CONF] Dwell: 500 ms"));
  } else if (strcmp(cmd, "3") == 0) {
    channelHopMs = 1000; Console.println(F("[CONF] Dwell: 1000 ms"));
  } else if (strcasecmp(cmd, "s") == 0) {
    printSignatureTelemetry();
  } else if (strcasecmp(cmd, "d") == 0) {
    printTrackedDevices(millis());
  } else if (strcasecmp(cmd, "t") == 0) {
    telemetryOn = !telemetryOn;
    Console.println(telemetryOn ? F("[CONF] Telemetry on (t to stop)") : F("[CONF] Telemetry off"));
#if ENABLE_MARK_LOG
  } else if (strcasecmp(cmd, "l") == 0) {
    if (fromWeb) Console.println(F("[LOG] The mark log is on the Marks tab (too big for the console)"));
    else         printMarkLog();
  } else if (strcasecmp(cmd, "j") == 0) {
    if (fromWeb) Console.println(F("[LOG] The alert journal is on the Marks tab"));
    else         printJournal();
  } else if (strcmp(cmd, "ERASE") == 0) {
    eraseMarkLog();
  } else if (strcasecmp(cmd, "e") == 0) {
    Console.println(F("[LOG] Type ERASE (all caps) to erase the mark log"));
#endif
#if HOME_SYNC
  } else if (strcasecmp(cmd, "w") == 0) {
    if (!maintActive) maintEnter(millis(), "command");
#endif
#if WEB_UI
  } else if (strcasecmp(cmd, "a") == 0) {
    if (!maintActive) roadEnter(millis());
  } else if (strcasecmp(cmd, "x") == 0) {
    if (maintActive) maintExit("command", true);
#endif
  } else if (strcasecmp(cmd, "r") == 0) {
    printResetLog();
  } else if (strcasecmp(cmd, "led") == 0) {
    ledTestBegin(millis());
  } else if (strcasecmp(cmd, "m") == 0) {
    toggleMute(millis());
  } else if (strcasecmp(cmd, "h") == 0 || strcmp(cmd, "?") == 0) {
    Console.println(F("[HELP] l mark log | j alert journal | ERASE wipe marks+journal | s signatures | d devices | t telemetry on/off | m mute | led LED test | r restarts | 0/1/2/3 dwell | w home web | a road web | x leave web"));
  } else {
    Console.printf("[CMD] unknown command \"%s\" (h = help)\n", cmd);
  }
}

static void processSerialCommands() {
  static char cmdBuf[16];
  static size_t cmdLen = 0;

  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n') {
      if (cmdLen > 0) {
        cmdBuf[cmdLen] = '\0';
        runCommand(cmdBuf, false);
        cmdLen = 0;
      }
    } else if (cmdLen + 1 < sizeof(cmdBuf)) {
      cmdBuf[cmdLen++] = c;
    }
  }
}

static void outputDiagnostics(uint32_t now) {
  static uint32_t lastStatsTime  = 0;
  static uint32_t lastTotalRx    = 0;
  static uint32_t lastTotalDrops = 0;
  static uint32_t lastMgmtSeen   = 0;
  static uint32_t lastBleSeen    = 0;

  if (!timeReached(now, lastStatsTime + STATS_INTERVAL_MS)) return;

  const uint32_t elapsedMs = now - lastStatsTime;
  lastStatsTime = now;

  const uint32_t rxDelta   = selectedFrames - lastTotalRx;
  const uint32_t dropDelta = totalDrops - lastTotalDrops;
  const uint32_t mgmtDelta = mgmtFramesSeen - lastMgmtSeen;
  const uint32_t bleDelta  = bleAdvertsSeen - lastBleSeen;
  lastTotalRx    = selectedFrames;
  lastTotalDrops = totalDrops;
  lastMgmtSeen   = mgmtFramesSeen;
  lastBleSeen    = bleAdvertsSeen;

  const uint16_t mgmtMark = mgmtQueueWatermarkInterval;
  const uint16_t hdrMark  = headerQueueWatermarkInterval;
  mgmtQueueWatermarkInterval   = 0;
  headerQueueWatermarkInterval = 0;

  // Off unless asked for with 't', so the mark log dump is not buried in it.
  if (!telemetryOn) return;

  const uint32_t pps     = elapsedMs ? (rxDelta * 1000UL) / elapsedMs : 0;
  const uint32_t dpps    = elapsedMs ? (dropDelta * 1000UL) / elapsedMs : 0;
  const uint32_t mgmtPps = elapsedMs ? (mgmtDelta * 1000UL) / elapsedMs : 0;
  const uint32_t blePps  = elapsedMs ? (bleDelta * 1000UL) / elapsedMs : 0;

  size_t activeDevices = 0;
  for (size_t i = 0; i < DEVICE_CACHE_SIZE; ++i) {
    if (deviceCache[i].valid && (now - deviceCache[i].lastSeen) < ENCOUNTER_GAP_MS) {
      ++activeDevices;
    }
  }

  // Blocking writes for this block: with the usual zero timeout, whole lines
  // were dropped whenever the USB buffer was momentarily full.
  setSerialBlockingMode(true);
  Console.println(F("=== SYSTEM PERFORMANCE TELEMETRY ==="));
  Console.printf("Dwell: %lu ms | Queued: %lu pps | Drops: %lu pps (Tot: %lu)\n",
                static_cast<unsigned long>(channelHopMs),
                static_cast<unsigned long>(pps),
                static_cast<unsigned long>(dpps),
                static_cast<unsigned long>(totalDrops));
  Console.printf("Mgmt inspected: %lu pps | filtered out: %lu of %lu total\n",
                static_cast<unsigned long>(mgmtPps),
                static_cast<unsigned long>(mgmtFramesFiltered),
                static_cast<unsigned long>(mgmtFramesSeen));
  Console.printf("MgmtQ: %u/%u P:%u/%u D:%lu | HeaderQ: %u/%u P:%u/%u D:%lu\n",
                static_cast<unsigned>(uxQueueMessagesWaiting(mgmtIeQueue)),
                static_cast<unsigned>(MGMT_IE_QUEUE_SIZE),
                mgmtMark, mgmtQueueWatermarkBoot,
                static_cast<unsigned long>(mgmtIeDrops),
                static_cast<unsigned>(uxQueueMessagesWaiting(headerQueue)),
                static_cast<unsigned>(HEADER_QUEUE_SIZE),
                hdrMark, headerQueueWatermarkBoot,
                static_cast<unsigned long>(headerDrops));
  Console.printf("BLE: %s | adverts: %lu /s | matches: %lu | drops: %lu\n",
                bleReady ? "scanning" : "off",
                static_cast<unsigned long>(blePps),
                static_cast<unsigned long>(bleMatches),
                static_cast<unsigned long>(bleDrops));
  Console.printf("Selected MgmtIE/Header: %lu/%lu | Cache: %u/%u (evictions %lu)\n",
                static_cast<unsigned long>(selectedMgmtIeFrames),
                static_cast<unsigned long>(selectedHeaderFrames),
                static_cast<unsigned>(activeDevices),
                static_cast<unsigned>(DEVICE_CACHE_SIZE),
                static_cast<unsigned long>(cacheEvictions));
  Console.printf("Heap: %u bytes | Encounters H/L/E: %lu/%lu/%lu | Heartbeats: %lu\n",
                static_cast<unsigned>(esp_get_free_internal_heap_size()),
                static_cast<unsigned long>(encounterCount[TIER_HIGH]),
                static_cast<unsigned long>(encounterCount[TIER_LOW]),
                static_cast<unsigned long>(encounterCount[TIER_ECHO]),
                static_cast<unsigned long>(heartbeatCount));
  Console.printf("IE over limit: %lu | Channel errors: %lu | Re-arms Wi-Fi/BLE: %lu/%lu\n",
                static_cast<unsigned long>(ieFramesOverCaptureLimit),
                static_cast<unsigned long>(channelErrors),
                static_cast<unsigned long>(wifiRearms),
                static_cast<unsigned long>(bleRestarts));
  Console.printf("Mark log: %s, %u%% full, %u marks this drive | obs dropped: %lu | table: %u in %s\n",
                markLogReady ? "ready" : "off", static_cast<unsigned>(markLogPercent),
                static_cast<unsigned>(markCount), static_cast<unsigned long>(obsDrops),
                static_cast<unsigned>(recentCap), recentInPsram ? "PSRAM" : "RAM");
#if HOME_SYNC
  Console.printf("Home Wi-Fi: %s | beacons %lu, last %d dBm%s\n",
                maintActive ? (maintRoad ? "road network up" : maintConnected ? "joined, web page up" : "joining") : "listening",
                static_cast<unsigned long>(homeBeaconCount), static_cast<int>(homeBeaconRssi),
                homeLocked ? " | locked until away 5 min" : "");
#endif
#if ENABLE_GPS
  if (!gpsSeen(now)) {
    Console.println(F("GPS: nothing on D7 (no module, or wiring)"));
  } else {
    char t[24] = "not set";
    if (clockValid()) isoTime(t, sizeof(t), time(nullptr));
    if (gpsFixFresh(now)) {
      Console.printf("GPS: fix, %u sats, hdop %.1f | %.5f,%.5f | %.0f mph | %s\n",
                     gps.sats, gps.hdop, gps.lat, gps.lon, gps.speedMph, t);
    } else if (gps.fix && !gpsHdopOk(now)) {
      Console.printf("GPS: rough fix ignored (hdop %.1f, %u sats) | clock %s\n", gps.hdop, gps.sats, t);
    } else {
      Console.printf("GPS: searching, %u sats | clock %s | bad checksums %lu\n",
                     gps.sats, t, static_cast<unsigned long>(gps.badChecksums));
    }
  }
#endif
  if (alertView.active) {
    Console.printf("In range: %u | Lead: %s \"%s\"\n",
                  static_cast<unsigned>(alertView.count),
                  tierName(alertView.tier), alertView.label);
  }
  Console.println(F("------------------------------------"));
  setSerialBlockingMode(false);
}

// ============================================================
// WEB PAGE: HOME WI-FI MAINTENANCE MODE AND ROAD ACCESS
// ============================================================
#if WEB_UI

WebServer webServer(80);

static const char WEB_UI_HTML[] = R"UIHTML(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="light dark">
<title>AirWatch</title>
<style>
:root{--bg:#f6f7f9;--card:#fff;--fg:#1b1f24;--mute:#6a737d;--line:#e1e4e8;--acc:#0b6bcb;--accfg:#fff;
--red:#c62828;--redbg:#fdecea;--amb:#a15c00;--ambbg:#fff4e0;--grn:#1b7a3a;--grnbg:#e7f6ec;--gry:#59636e;--grybg:#eef0f2}
@media (prefers-color-scheme:dark){:root:not([data-theme=light]){--bg:#0f1216;--card:#181c21;--fg:#e6e9ec;--mute:#9aa4ae;--line:#2a3038;
--acc:#5aa9ff;--accfg:#08111c;--red:#ff7b72;--redbg:#3a1714;--amb:#f0b35a;--ambbg:#35260d;--grn:#5fd38a;--grnbg:#12301d;--gry:#a3acb5;--grybg:#22282f}}
:root[data-theme=dark]{--bg:#0f1216;--card:#181c21;--fg:#e6e9ec;--mute:#9aa4ae;--line:#2a3038;
--acc:#5aa9ff;--accfg:#08111c;--red:#ff7b72;--redbg:#3a1714;--amb:#f0b35a;--ambbg:#35260d;--grn:#5fd38a;--grnbg:#12301d;--gry:#a3acb5;--grybg:#22282f}
*{box-sizing:border-box}body{margin:0;overflow-x:hidden;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
header{display:flex;flex-wrap:wrap;align-items:center;gap:8px 12px;padding:12px 16px;background:var(--card);border-bottom:1px solid var(--line);position:sticky;top:0;z-index:2}
h1{font-size:18px;margin:0}.pill{white-space:nowrap;font-size:12px;padding:2px 8px;border-radius:99px;background:var(--grybg);color:var(--gry)}
.hb{margin-left:auto;border:1px solid var(--line);background:none;color:var(--mute);border-radius:99px;width:26px;height:24px;font:600 13px system-ui;cursor:pointer}
#theme{border:1px solid var(--line);background:none;color:var(--mute);border-radius:99px;padding:2px 10px;font:12px system-ui;cursor:pointer}
nav{display:flex;gap:2px;width:100%;overflow-x:auto}nav button{flex:1;min-width:0;white-space:nowrap;border:0;background:none;color:var(--mute);padding:6px 10px;border-radius:6px;font:inherit;cursor:pointer}
nav button.on{background:var(--grybg);color:var(--fg);font-weight:600}
@media (max-width:480px){nav button{padding:6px 2px;font-size:14px}}@media (min-width:700px){nav{width:auto;margin-left:8px}nav button{flex:none}}
main{max-width:980px;margin:0 auto;padding:16px}section{display:none}section.on{display:block}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:10px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px}
.k{font-size:12px;color:var(--mute)}.card.bad{border-color:var(--red);background:var(--redbg)}.card.bad .v{font-size:14px;color:var(--red)}.v{font-size:20px;font-weight:600;margin-top:2px;word-break:break-word}
.row{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin:12px 0}
button.b,a.b{border:1px solid var(--line);background:var(--card);color:var(--fg);padding:7px 12px;border-radius:8px;font:inherit;cursor:pointer;text-decoration:none}
button.p{background:var(--acc);border-color:var(--acc);color:var(--accfg)}button.d{color:var(--red);border-color:var(--red)}
button.sm{padding:4px 10px;font-size:13px}button.sel{outline:2px solid var(--acc)}
input[type=text],input[type=search]{font:inherit;padding:7px 10px;border:1px solid var(--line);border-radius:8px;background:var(--card);color:var(--fg);min-width:0;flex:1 1 200px}
details{background:var(--card);border:1px solid var(--line);border-radius:10px;margin:8px 0;overflow:hidden}
summary{padding:10px 12px;cursor:pointer;display:flex;flex-wrap:wrap;gap:6px 10px;align-items:center}
.tag{font-size:12px;font-weight:600;padding:2px 8px;border-radius:6px;white-space:nowrap}
.t-miss{background:var(--redbg);color:var(--red)}.t-false{background:var(--ambbg);color:var(--amb)}
.t-hit{background:var(--grnbg);color:var(--grn)}.t-none{background:var(--grybg);color:var(--gry)}
.meta{color:var(--mute);font-size:13px}.note{font-size:13px;padding:4px 12px;color:var(--fg)}.note b{color:var(--mute);font-weight:500}
.tools{display:flex;flex-wrap:wrap;gap:6px;align-items:center;padding:8px 12px;border-top:1px solid var(--line)}
pre{margin:0;padding:8px 12px;overflow-x:auto;font:12px/1.5 ui-monospace,SFMono-Regular,Menlo,monospace;border-top:1px solid var(--line)}
pre .al{color:var(--red);font-weight:600}pre .ig{color:var(--mute)}pre .ea{color:var(--amb)}pre .hl{background:var(--ambbg)}pre a{color:inherit;text-decoration:underline dotted}
h3{font-size:15px;margin:18px 0 4px}.seg .on{background:var(--grybg);font-weight:600}.mono{font:12px ui-monospace,Menlo,monospace}
.sum{display:flex;flex-wrap:wrap;gap:6px;margin:6px 0 4px}
table{width:100%;border-collapse:collapse;background:var(--card);border:1px solid var(--line);border-radius:10px;overflow:hidden;font-size:13px}
th,td{text-align:left;padding:7px 9px;border-bottom:1px solid var(--line);vertical-align:top}th{color:var(--mute);font-weight:600}
.scroll{overflow-x:auto}pre.console{background:var(--card);border:1px solid var(--line);border-radius:10px;height:55vh;overflow:auto;white-space:pre}.msg{color:var(--mute);margin:10px 0}progress{width:100%;height:10px}code{font-size:12px;word-break:break-all}
</style></head><body>
<header><h1>AirWatch</h1><span class="pill" id="mode">…</span><button id="helpb" class="hb" title="How to use">?</button><button id="theme" title="Theme">Auto</button>
<nav><button data-s="st" class="on">Status</button><button data-s="mk">Marks</button><button data-s="dv">Devices</button><button data-s="cn">Console</button><button data-s="fw">Firmware</button></nav></header>
<main>
<section id="st" class="on">
 <div class="grid" id="cards"></div>
 <div class="row"><button class="b p" id="exit">Exit &amp; resume listening</button><button class="b" id="mute">Mute sounds</button><button class="b" id="reboot">Reboot</button><button class="b" data-c="led" title="Cycles red, green, blue, white, then the alert colours">Test LED</button></div>
 <p class="msg" id="stmsg">While this page is open the board stays here. Wi-Fi detection is paused; Bluetooth detection and the mark button keep working.</p>
</section>
<section id="mk">
 <p class="msg" id="mkRev" hidden>Marks and alerts are sent to your server each time the board gets home, so this page only shows what is new since then. Full history, map and analysis: <a target="_blank" rel="noopener" href="#">review page</a>.</p>
 <div class="row seg"><button class="b sm on" data-v="mkList">All marks</button><button class="b sm" data-v="mkJr">Alert journal</button><button class="b sm" data-v="mkRep">Repeat offenders</button></div>
 <div id="mkList">
 <div class="row"><button class="b" id="reload">Reload</button><a class="b" href="/marks.log?dl=1">Download</a>
 <input type="search" id="q" placeholder="Filter (MAC, name, ALERT, note…)"><button class="b d" id="erase">Erase…</button></div>
 <div class="sum" id="sum"></div><div class="msg" id="mkmsg">Loading…</div><div id="marks"></div>
 </div>
 <div id="mkJr" hidden>
  <div class="row"><button class="b" id="jreload">Reload</button><a class="b" href="/alerts.log?dl=1">Download</a>
  <input type="search" id="jq" placeholder="Filter (name, MAC, tier…)"></div>
  <div class="sum" id="jsum"></div><div class="msg" id="jmsg">Loading…</div>
  <h3>Most frequent</h3><p class="msg">Devices that alerted on the most different drives.</p>
  <div class="scroll"><table><thead><tr><th>Device</th><th>Alerts</th><th>Drives</th><th>Last</th></tr></thead><tbody id="jtop"></tbody></table></div>
  <h3>Every alert, newest first</h3>
  <div class="scroll"><table><thead><tr><th>When</th><th>What</th><th>Signal</th><th>Lasted</th></tr></thead><tbody id="jall"></tbody></table></div>
 </div>
 <div id="mkRep" hidden>
  <h3>Alerted in more than one mark</h3>
  <p class="msg">Each device that was alerting (ALERT / EARLIER lines) in two or more marks, and what you said was there each time. Corrections and notes count.</p>
  <div class="scroll"><table><thead><tr><th>Device</th><th>Marks</th><th>You said</th></tr></thead><tbody id="repA"></tbody></table></div>
  <h3>Heard at police sightings on different drives</h3>
  <p class="msg">Any transmitter, alerting or not, heard in marks where police were there (Missed / Confirmed) on two or more different drives. Ones that are also heard when nothing was there are usually houses along your route. Bluetooth phones change address, so they rarely repeat.</p>
  <div class="scroll"><table><thead><tr><th>Transmitter</th><th>Police drives</th><th>Other drives</th><th>Best signal</th></tr></thead><tbody id="repP"></tbody></table></div>
 </div>
</section>
<section id="dv"><div class="row"><span class="msg" style="margin:0">Devices the detector matched in the last 15 minutes · updates every 5 s</span></div>
 <div class="scroll"><table><thead><tr><th>Tier</th><th>What</th><th>Name</th><th>Signal</th><th>Seen</th><th>Address</th><th>Why</th></tr></thead><tbody id="dvt"></tbody></table></div></section>
<section id="cn">
 <div class="row"><button class="b sm" data-c="t">Telemetry on/off</button><button class="b sm" data-c="d">Tracked devices</button>
 <button class="b sm" data-c="s">Signature hits</button><button class="b sm" data-c="h">Help</button>
 <label class="meta"><input type="checkbox" id="follow" checked> Follow</label><button class="b sm" id="cclear">Clear view</button></div>
 <pre id="con" class="console"></pre>
 <form class="row" id="cform"><input type="text" id="cin" placeholder="Command (h = help)" maxlength="15" autocomplete="off" autocapitalize="off"><button class="b p">Send</button></form>
 <p class="msg">Same output and commands as the USB serial monitor, including detections from before this page opened (the last ~600 lines). The mark log itself is on the Marks tab.</p>
</section>
<section id="hp">
 <div class="card"><h3 style="margin-top:0">The button</h3>
 <table><tbody>
 <tr><td><b>Tap</b></td><td>Mark: <b>police are there</b>. One long beep, about half a second after you let go.</td></tr>
 <tr><td><b>Hold ~1 s</b></td><td>Mark: <b>nothing is there</b>. Two long beeps.</td></tr>
 <tr><td><b>Press again within 10 s</b></td><td>A follow-up mark: adds only what is new or clearly closer. Both are kept; on this page you can correct either one.</td></tr>
 <tr><td><b>Three quick taps</b></td><td><b>Mute / unmute</b> alert sounds. Falling beeps = muted, rising = sound on. The screen shows a crossed-out speaker. Stays muted until the power is cut; marks still beep.</td></tr>
 <tr><td><b>Hold 3 s in the first 15 s after power-up</b></td><td>(screen shows HOLD=WEB) Road access: the board makes its own Wi-Fi network for this page. Three rising beeps. Press BOOT only after power is on.</td></tr>
 </tbody></table></div>
 <div class="card" style="margin-top:10px"><h3 style="margin-top:0">What you hear</h3>
 <table><tbody>
 <tr><td>short-short-LONG</td><td>new POLICE target (police-specific signature)</td></tr>
 <tr><td>short-short</td><td>new MAYBE target (vehicle router, broad vendor) · also once at power-on</td></tr>
 <tr><td>single tick</td><td>new MINOR or QUIET target (heard indirectly, or a branch-office router)</td></tr>
 <tr><td>tick-tick … / tick …</td><td>heartbeat while a POLICE / MAYBE target is in range: faster = closer</td></tr>
 </tbody></table></div>
 <div class="card" style="margin-top:10px"><h3 style="margin-top:0">Screen</h3>
 <p class="msg" style="margin:0">Idle: <b>W</b>/<b>B</b> = Wi-Fi / Bluetooth devices heard in the last 30 s; bottom row = last alert and how long ago, or <b>M</b> marks this drive and <b>L</b> storage used. Alerting: POLICE / MAYBE / MINOR and signal in dBm (closer to 0 = nearer, "echo" = heard indirectly), then what matched and how many targets are in range. HOME WEB / ROAD WEB = this page is up.</p></div>
 <div class="card" style="margin-top:10px"><h3 style="margin-top:0">Alert journal and GPS</h3>
 <p class="msg" style="margin:0">Every alert is saved on its own when it ends (Marks → Alert journal), no button needed. It keeps about 1,400 alerts and drops the oldest. With a GPS on D7, marks and alerts get the real time and a map link; each alert's map point is where its signal was strongest.</p></div>
 <div class="card" style="margin-top:10px"><h3 style="margin-top:0">Web page</h3>
 <p class="msg" style="margin:0">At home it opens by itself after your Wi-Fi has been in view for a few seconds: http://airwatch.local or the address on the screen. It goes back to listening about 5 s after you drive out of range (at once if the GPS shows you driving), if joining fails for 20 s, on Exit, or after 15 min idle. After that it stays out of home mode until you have been away 5 min, or the power is cycled. Console commands: h help · t telemetry (includes GPS) · r restart / crash history · d devices · s signature hits · m mute · led LED test · 1/2/3 dwell · x leave.</p></div>
</section>
<section id="fw">
 <div class="card"><div class="k">Running build</div><div class="v" id="build">…</div></div>
 <p class="msg">Make the file on your computer with<br><code>arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3:PSRAM=opi --export-binaries ~/airwatch_v2</code><br>
 then pick <code>build/esp32.esp32.XIAO_ESP32S3/airwatch_v2.ino.bin</code> (not the bootloader, partitions or merged file). Download the marks first if you want a copy.</p>
 <div class="row"><input type="file" id="bin" accept=".bin"><button class="b p" id="up">Upload &amp; install</button></div>
 <progress id="prog" max="100" value="0" hidden></progress><div class="msg" id="fwmsg"></div>
</section>
</main>
<script>
const $=s=>document.querySelector(s);
const el=(t,c,x)=>{const e=document.createElement(t);if(c)e.className=c;if(x!=null)e.textContent=x;return e};
const store={get(k){try{return localStorage.getItem(k)}catch(e){return null}},set(k,v){try{localStorage.setItem(k,v)}catch(e){}}};
// ---- theme ----
const TH=['auto','light','dark'];let th=store.get('theme')||'auto';
function applyTheme(){if(th=='auto')document.documentElement.removeAttribute('data-theme');else document.documentElement.dataset.theme=th;
 $('#theme').textContent=th[0].toUpperCase()+th.slice(1)}
$('#theme').onclick=()=>{th=TH[(TH.indexOf(th)+1)%3];store.set('theme',th);applyTheme()};applyTheme();
// ---- tabs ----
let tab='st',dvTimer=null,cnTimer=null;
function show(s){if(!document.getElementById(s))s='st';tab=s;document.querySelectorAll('nav button').forEach(x=>x.classList.toggle('on',x.dataset.s==s));
 document.querySelectorAll('section').forEach(x=>x.classList.toggle('on',x.id==s));store.set('tab',s);
 if(s=='mk'&&!loaded)loadMarks();clearInterval(dvTimer);clearInterval(cnTimer);if(s=='cn'){pollCon();cnTimer=setInterval(pollCon,1500)}if(s=='dv'){loadDev();dvTimer=setInterval(loadDev,5000)}}
document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>show(b.dataset.s));
$('#helpb').onclick=()=>show(tab=='hp'?'st':'hp');
const fmtWhen=iso=>new Date(iso).toLocaleString([], {weekday:'short',month:'short',day:'numeric',hour:'numeric',minute:'2-digit'});
const mapUrl=(la,lo)=>'https://maps.google.com/?q='+la+','+lo;
const fmtS=s=>s<90?s+' s':s<5400?Math.round(s/60)+' min':(s/3600).toFixed(1)+' h';
// ---- status ----
async function status(){try{const j=await (await fetch('/status.json')).json();if(j.review){const e=$('#mkRev');e.hidden=false;e.querySelector('a').href=j.review}
 $('#mode').textContent=j.mode=='road'?'On the road · own network':'Home · Wi-Fi '+j.homeRssi+' dBm';$('#build').textContent=j.build;
 $('#stmsg').textContent='While this page is open the board stays here. Wi-Fi detection is paused; Bluetooth detection and the mark button keep working. '+
  (j.mode=='road'?'It goes back to listening when you press Exit or after the idle time runs out.':'It goes back to listening a few seconds after you drive out of Wi-Fi range, when you press Exit, or after the idle time runs out.');
 const a=j.alert.active?(j.alert.tier+' · '+j.alert.label+(j.alert.rssi!=null?' · '+j.alert.rssi+' dBm':'')):'none';
 const l=j.last?(j.last.tier+' · '+j.last.label+' · '+fmtS(j.last.ago)+' ago'):'none in 15 min';
 $('#mute').textContent=j.muted?'Turn sound on':'Mute sounds';
 const gp=j.gps||{};const gpsTxt=!gp.seen?'Not connected':gp.fix?'Fix · '+gp.sats+' sats'+(gp.spd!=null?' · '+gp.spd+' mph':''):'Searching · '+gp.sats+' sats';
 const c=[['Sound',j.muted?'Muted':'On'],['GPS',gpsTxt],['Clock',j.clock?new Date(j.clock).toLocaleString([], {dateStyle:'medium',timeStyle:'short'}):'Not set (needs a GPS fix)'],['Drive','#'+j.drive],['Marks this drive',j.marks],['Mark storage',j.logPct+'% · '+Math.round(j.logBytes/1024)+' KB'],
 ['Alert now',a],['Last alert',l],['Heard (30 s)','Wi-Fi '+j.wifi+' · BLE '+j.ble],['Uptime',fmtS(j.uptime)],
 ['Back to listening in',fmtS(j.idleLeft)+' idle'],['Address',j.ip],['Free memory',Math.round(j.heap/1024)+' KB'],
 ['BLE adverts',j.bleAdverts],['Obs dropped',j.obsDrops],['Last restart',j.lastReset||'?',j.lastResetBad]];
 const g=$('#cards');g.textContent='';c.forEach(([k,v,bad])=>{const d=el('div','card'+(bad?' bad':''));d.append(el('div','k',k),el('div','v',String(v)));g.append(d)});
}catch(e){$('#mode').textContent='offline'}}
status();setInterval(status,3000);
$('#exit').onclick=async()=>{if(!confirm('Leave and go back to listening? The board reboots.'))return;
 await fetch('/exit',{method:'POST',headers:{'X-AirWatch':'1'}});document.body.innerHTML='<main><p>Back to listening. This page will stop responding.</p></main>'};
$('#mute').onclick=async()=>{await fetch('/mute',{method:'POST',headers:{'X-AirWatch':'1'}});status()};
$('#reboot').onclick=async()=>{if(!confirm('Reboot? If you are still home it comes back here in about 15 seconds.'))return;await fetch('/reboot',{method:'POST',headers:{'X-AirWatch':'1'}})};
// ---- marks ----
let loaded=false,MARKS=[],NOTES={};
const KINDS={miss:['Missed','t-miss'],false:['False alarm','t-false'],hit:['Confirmed hit','t-hit'],none:['Nothing, quiet','t-none']};
function kindOf(m){const n=NOTES[m.id]||[];let saw=(m.h.match(/ saw=(\w+)/)||[])[1];const ty=(m.h.match(/ type=(\w+)/)||[])[1];
 n.forEach(x=>{if(x.saw)saw=x.saw});
 if(!saw&&ty=='MISSED')return'miss';if(!saw&&ty=='FALSE_ALARM')return'false';
 const alerting=ty=='DURING_ALERT'||ty=='FALSE_ALARM';
 if(saw=='POLICE')return alerting?'hit':'miss';return alerting?'false':'none'}
async function loadMarks(){loaded=true;$('#mkmsg').textContent='Loading…';
 try{const t=await (await fetch('/marks.log')).text();MARKS=[];NOTES={};let cur=null;
 t.split('\n').forEach(L=>{if(L.startsWith('#MARK')){const g=r=>(L.match(r)||[])[1];cur={h:L,lines:[],id:g(/drive=(\d+)/)+'-'+g(/mark=(\d+)/)};MARKS.push(cur)}
  else if(L.startsWith('#NOTE')){const id=(L.match(/drive=(\d+)/)||[])[1]+'-'+(L.match(/mark=(\d+)/)||[])[1];
   (NOTES[id]=NOTES[id]||[]).push({saw:(L.match(/ saw=(\w+)/)||[])[1],text:(L.match(/ text="(.*)"$/)||[])[1]||''})}
  else if(cur&&L.trim())cur.lines.push(L)});
 if(view=='mkRep')repeats();
 $('#mkmsg').textContent=MARKS.length?MARKS.length+' marks · '+Math.round(t.length/1024)+' KB':'No marks saved.';render()}
 catch(e){$('#mkmsg').textContent='Could not load the log.'}}
function copyText(t){const a=el('textarea');a.value=t;a.style.position='fixed';a.style.opacity='0';document.body.append(a);a.select();
 let ok=false;try{ok=document.execCommand('copy')}catch(e){}a.remove();return ok}
function render(){const q=$('#q').value.trim().toLowerCase(),box=$('#marks');box.textContent='';
 const cnt={miss:0,false:0,hit:0,none:0};MARKS.forEach(m=>cnt[kindOf(m)]++);
 const sm=$('#sum');sm.textContent='';Object.keys(cnt).forEach(k=>{if(cnt[k])sm.append(el('span','tag '+KINDS[k][1],KINDS[k][0]+' '+cnt[k]))});
 MARKS.slice().reverse().forEach(m=>{const g=(re)=>(m.h.match(re)||[])[1];const notes=NOTES[m.id]||[];
  const lines=q?m.lines.filter(x=>x.toLowerCase().includes(q)):m.lines;
  const noteHit=q&&notes.some(n=>n.text.toLowerCase().includes(q));
  if(q&&!lines.length&&!noteHit&&!m.h.toLowerCase().includes(q))return;
  const d=el('details'),s=el('summary'),k=kindOf(m);
  s.append(el('span','tag '+KINDS[k][1],KINDS[k][0]),el('b',null,'Drive '+g(/drive=(\d+)/)+' · mark '+g(/mark=(\d+)/)));
  const lead=g(/ lead="([^"]*)"/)||g(/last_lead="([^"]*)"/);const up=+g(/uptime=(\d+)/);
  const nA=m.lines.filter(x=>x.startsWith('ALERT')&&!x.includes('IGNORED_FLEET')).length;
  const when=g(/ time=(\S+)/);s.append(el('span','meta',(when?fmtWhen(when)+' · ':'')+Math.floor(up/60)+' min into drive · '+g(/heard=(\d+)/)+' heard'+(nA?' · '+nA+' alerting':'')+(lead?' · '+lead:'')+(m.h.includes('followup_of')?' · follow-up':'')));
  d.append(s);notes.forEach(n=>{if(n.text){const p=el('div','note');p.append(el('b',null,'Note: '),document.createTextNode(n.text));d.append(p)}});
  // tools: correct the button, add a note, copy
  const t=el('div','tools');const curSaw=(notes.filter(n=>n.saw).pop()||{}).saw||g(/ saw=(\w+)/);
  const bP=el('button','b sm'+(curSaw=='POLICE'?' sel':''),'Police were there'),bN=el('button','b sm'+(curSaw=='NOTHING'?' sel':''),'Nothing was there');
  const inp=el('input');inp.type='text';inp.placeholder='Add a note (where, what you saw)';inp.maxLength=160;
  const bS=el('button','b sm p','Save note'),bC=el('button','b sm','Copy');
  const post=async(saw,text)=>{const b=new URLSearchParams({drive:g(/drive=(\d+)/),mark:g(/mark=(\d+)/),saw:saw||'',text:text||''});
   const r=await fetch('/note',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','X-AirWatch':'1'},body:b});
   if(!r.ok)alert(await r.text());else{const open=m.id;await loadMarks();const e=document.querySelector('[data-id="'+open+'"]');if(e)e.open=true}};
  bP.onclick=()=>post('POLICE','');bN.onclick=()=>post('NOTHING','');bS.onclick=()=>{if(inp.value.trim())post('',inp.value.trim())};inp.onkeydown=e=>{if(e.key==='Enter'){e.preventDefault();bS.click()}};
  bC.onclick=()=>{const txt=[m.h,...m.lines].join('\n')+notes.map(n=>'\n#NOTE '+(n.saw?'saw='+n.saw+' ':'')+(n.text?'"'+n.text+'"':'')).join('');
   bC.textContent=copyText(txt)?'Copied':'Copy failed';setTimeout(()=>bC.textContent='Copy',1500)};
  t.append(bP,bN,inp,bS,bC);const la=g(/ lat=(-?[\d.]+)/),lo=g(/ lon=(-?[\d.]+)/);
  if(la&&lo){const a=el('a','b sm','Map');a.href=mapUrl(la,lo);a.target='_blank';a.rel='noopener';t.append(a)}d.append(t);d.dataset.id=m.id;
  // The device lines are only built when a mark is opened (or a filter is on):
  // a big log would otherwise freeze a phone for seconds.
  const fill=()=>{const al=lines.filter(x=>/^(ALERT|EARLIER)/.test(x)),rest=lines.filter(x=>!/^(ALERT|EARLIER)/.test(x));
  const p=el('pre');const add=(x,c)=>{const sp=el('span',c),mm=x.match(/ at=(-?[\d.]+),(-?[\d.]+)/);
   if(!mm){sp.textContent=x+'\n'}else{const i=mm.index+4,a=el('a',null,mm[1]+','+mm[2]);a.href=mapUrl(mm[1],mm[2]);a.target='_blank';a.rel='noopener';
    sp.append(document.createTextNode(x.slice(0,i)),a,document.createTextNode(x.slice(i+mm[0].length-4)+'\n'))}p.append(sp)};add(m.h,'');
  al.forEach(x=>add(x,x.includes('IGNORED_FLEET')?'ig':x.startsWith('ALERT')?'al':'ea'));rest.forEach(x=>add(x,/ alerted=/.test(x)?'hl':''));
  d.append(p)};
  if(q){fill();d.open=true}else d.addEventListener('toggle',()=>{if(d.open&&!d.dataset.f){d.dataset.f=1;fill()}});box.append(d)})}
$('#q').oninput=render;$('#reload').onclick=loadMarks;
$('#erase').onclick=async()=>{const v=prompt('This deletes every saved mark and note. Download first if you want a copy.\nType ERASE to confirm:');
 if(v!=='ERASE')return;const r=await fetch('/erase',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','X-AirWatch':'1'},body:'confirm=ERASE'});
 alert(await r.text());loadMarks()};
// ---- alert journal ----
let jLoaded=false,JR=[];
const TIERW={HIGH:['POLICE','t-miss'],LOW:['MAYBE','t-false'],ECHO:['MINOR','t-none'],QUIET:['QUIET','t-none']};
async function loadJournal(){jLoaded=true;$('#jmsg').textContent='Loading…';
 try{const t=await (await fetch('/alerts.log')).text();
 JR=t.split('\n').filter(L=>L.startsWith('A ')).map(L=>{const g=r=>(L.match(r)||[])[1];const at=g(/ at=(-?[\d.]+,-?[\d.]+)/);
  return{L,drive:g(/drive=(\d+)/),up:+g(/ up=(\d+)/),time:g(/ time=(\S+)/),tier:g(/ tier=(\S+)/),src:g(/ src=(\S+)/),mac:g(/ mac=(\S+)/),
  best:g(/ best=(\S+)/),ch:g(/ ch=(\d+)/),dur:+g(/ dur=(\d+)/),label:g(/ label="([^"]*)"/)||'',ssid:g(/ ssid="([^"]*)"/)||'',at,muted:/ muted=yes/.test(L)}});
 $('#jmsg').textContent=JR.length?JR.length+' alerts saved · '+Math.round(t.length/1024)+' KB of 256 KB':'No alerts saved yet.';renderJournal()}
 catch(e){$('#jmsg').textContent='Could not load the journal.'}}
function jWhen(a){return a.time?fmtWhen(a.time):'Drive '+a.drive+' · '+Math.floor(a.up/60)+' min in'}
function jWhat(a){const d=el('td');const tw=TIERW[a.tier]||[a.tier,'t-none'];d.append(el('span','tag '+tw[1],tw[0]),' ',el('span',null,a.label+(a.ssid?' · '+a.ssid:'')));
 const sub=el('div','mono meta',a.src+' '+a.mac+(a.muted?' · muted':''));if(a.at){const m=el('a',null,' · map');m.href=mapUrl(...a.at.split(','));m.target='_blank';m.rel='noopener';sub.append(m)}
 d.append(sub);return d}
function renderJournal(){const q=$('#jq').value.trim().toLowerCase();const rows=q?JR.filter(a=>a.L.toLowerCase().includes(q)):JR;
 const cnt={};rows.forEach(a=>cnt[a.tier]=(cnt[a.tier]||0)+1);const sm=$('#jsum');sm.textContent='';
 Object.keys(cnt).forEach(k=>{const tw=TIERW[k]||[k,'t-none'];sm.append(el('span','tag '+tw[1],tw[0]+' '+cnt[k]))});
 const by={};rows.forEach(a=>{const b=by[a.mac]=by[a.mac]||{a,n:0,drives:new Set()};b.n++;b.drives.add(a.drive);b.a=a});
 const top=Object.values(by).sort((x,y)=>y.drives.size-x.drives.size||y.n-x.n).slice(0,15);const tt=$('#jtop');tt.textContent='';
 top.forEach(b=>{const r=el('tr');r.append(jWhat(b.a),el('td',null,String(b.n)),el('td',null,String(b.drives.size)),el('td',null,jWhen(b.a)));tt.append(r)});
 const ta=$('#jall');ta.textContent='';rows.slice().reverse().slice(0,500).forEach(a=>{const r=el('tr');
  r.append(el('td',null,jWhen(a)),jWhat(a),el('td',null,(a.best=='--'?'echo':a.best+' dBm')+(a.ch?' · ch '+a.ch:'')),el('td',null,fmtS(a.dur)));ta.append(r)})}
$('#jq').oninput=renderJournal;$('#jreload').onclick=loadJournal;
// ---- repeat offenders ----
let view='mkList';
document.querySelectorAll('.seg [data-v]').forEach(b=>b.onclick=()=>{view=b.dataset.v;
 document.querySelectorAll('.seg [data-v]').forEach(x=>x.classList.toggle('on',x==b));
 ['mkList','mkJr','mkRep'].forEach(id=>$('#'+id).hidden=id!=view);
 if(view=='mkRep')repeats();if(view=='mkJr'&&!jLoaded)loadJournal()});
const isPolice=k=>k=='hit'||k=='miss';
function repeats(){const A={},P={};
 MARKS.forEach(m=>{const k=kindOf(m),drive=m.id.split('-')[0];
  m.lines.forEach(L=>{const f=L.split(/\s+/),mac=f[1];if(!mac)return;const g=r=>(L.match(r)||[])[1];
   if(/^(ALERT|EARLIER) /.test(L)){const a=A[mac]=A[mac]||{mac,label:'',ssid:'',tiers:new Set(),marks:new Set(),drives:new Set(),kinds:{}};
    a.label=g(/label="([^"]*)"/)||a.label;a.ssid=g(/ssid="([^"]*)"/)||a.ssid;a.tiers.add(g(/tier=(\S+)/));
    if(!a.marks.has(m.id)){a.marks.add(m.id);a.kinds[k]=(a.kinds[k]||0)+1}a.drives.add(drive)}
   else if(/^(WIFI|BLE) /.test(L)){const p=P[mac]=P[mac]||{mac,radio:f[0],name:'',pol:new Set(),oth:new Set(),best:-200,alerted:''};
    p.name=g(/(?:ssid|name)="([^"]*)"/)||p.name;const r=+g(/rssi=(-?\d+)/);if(r>p.best)p.best=r;
    p.alerted=g(/alerted=(\S+)/)||p.alerted;(isPolice(k)?p.pol:p.oth).add(drive)}})});
 const ta=$('#repA');ta.textContent='';
 const rows=Object.values(A).filter(a=>a.marks.size>=2).sort((x,y)=>y.drives.size-x.drives.size||y.marks.size-x.marks.size);
 if(!rows.length){const r=el('tr'),c=el('td',null,'Nothing has alerted in more than one mark yet.');c.colSpan=3;r.append(c);ta.append(r)}
 rows.forEach(a=>{const r=el('tr'),d=el('td');d.append(el('div',null,a.label+(a.ssid?' · '+a.ssid:'')),el('div','mono meta',a.mac+' · '+[...a.tiers].join('/')));
  const yk=el('td');Object.keys(a.kinds).forEach(k=>yk.append(el('span','tag '+KINDS[k][1],KINDS[k][0]+' '+a.kinds[k]),' '));
  const pol=(a.kinds.hit||0)+(a.kinds.miss||0),not=(a.kinds.false||0)+(a.kinds.none||0);
  const hint=a.tiers.has('IGNORED_FLEET')?'Silenced fleet':pol==0?'Never with police: likely fixed. Quiet-list candidate.':not==0?'Only ever with police: strong lead.':'Mixed.';
  d.append(el('div','meta',hint));r.append(d,el('td',null,a.marks.size+' marks · '+a.drives.size+' drives'),yk);ta.append(r)});
 const tp=$('#repP');tp.textContent='';
 const pr=Object.values(P).filter(p=>p.pol.size>=2).sort((x,y)=>y.pol.size-x.pol.size||x.oth.size-y.oth.size).slice(0,60);
 if(!pr.length){const r=el('tr'),c=el('td',null,'Needs police sightings (Missed / Confirmed marks) on at least two drives.');c.colSpan=4;r.append(c);tp.append(r)}
 pr.forEach(p=>{const r=el('tr'),d=el('td');d.append(el('div',null,(p.name||'(no name)')+(p.alerted?' · alerted '+p.alerted:'')),el('div','mono meta',p.radio+' '+p.mac));
  r.append(d,el('td',null,String(p.pol.size)),el('td',null,String(p.oth.size)),el('td',null,p.best+' dBm'));tp.append(r)})}
// ---- devices ----
async function loadDev(){try{const j=await (await fetch('/devices.json')).json();const t=$('#dvt');t.textContent='';
 if(!j.length){const r=el('tr');const c=el('td',null,'Nothing matched in the last 15 minutes.');c.colSpan=7;r.append(c);t.append(r)}
 j.sort((a,b)=>a.ago-b.ago).forEach(d=>{const r=el('tr');[d.tier,d.label,d.ssid,(d.rssi==null?'echo':d.rssi+' dBm')+(d.src=='WIFI'?' · ch '+d.ch:' · BLE'),
  fmtS(d.ago)+' ago · '+d.seen+'×',d.mac,d.why].forEach(v=>r.append(el('td',null,v)));t.append(r)})}catch(e){}}
// ---- console ----
let conNext=null;
async function pollCon(){try{const r=await fetch('/console'+(conNext!=null?'?since='+conNext:''));const t=await r.text();
 conNext=+r.headers.get('X-Next');if(!t)return;const c=$('#con');c.append(document.createTextNode(t));
 while(c.textContent.length>200000)c.removeChild(c.firstChild);if($('#follow').checked)c.scrollTop=c.scrollHeight}catch(e){}}
async function sendCmd(x){await fetch('/cmd',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','X-AirWatch':'1'},body:'c='+encodeURIComponent(x)});setTimeout(pollCon,300)}
document.querySelectorAll('[data-c]').forEach(b=>b.onclick=()=>sendCmd(b.dataset.c));
$('#cform').onsubmit=e=>{e.preventDefault();const v=$('#cin').value.trim();if(!v)return;
 if(v=='ERASE'&&!confirm('Erase every saved mark?'))return;sendCmd(v);$('#cin').value=''};
$('#cclear').onclick=()=>{$('#con').textContent=''};
// ---- firmware ----
$('#up').onclick=()=>{const f=$('#bin').files[0];if(!f)return alert('Pick the .bin file first.');
 if(!/\.bin$/.test(f.name)||/bootloader|partitions|merged/.test(f.name))return alert('Pick airwatch_v2.ino.bin, not the bootloader, partitions or merged file.');
 const fd=new FormData();fd.append('firmware',f,f.name);const x=new XMLHttpRequest();const pr=$('#prog');pr.hidden=false;
 x.upload.onprogress=e=>{if(e.lengthComputable)pr.value=100*e.loaded/e.total};
 x.onload=()=>{$('#fwmsg').textContent=x.status==200?'Installed. Rebooting… reload this page in about a minute.':'Failed: '+x.responseText};
 x.onerror=()=>{$('#fwmsg').textContent='Upload interrupted.'};x.open('POST','/update');x.setRequestHeader('X-AirWatch','1');x.send(fd);$('#fwmsg').textContent='Uploading…'};
show(store.get('tab')||'st');
</script></body></html>
)UIHTML";

static void jsonStr(String& out, const char* s) {
  out += '"';
  for (; s && *s; ++s) {
    const char c = *s;
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (static_cast<unsigned char>(c) < 0x20) out += ' ';
    else out += c;
  }
  out += '"';
}

// Logs page requests: every one for the first few, then a count every 30 s.
static void webLogRequest(bool ok) {
  ++webRequests;
  if (!ok) ++webAuthFails;
  static uint32_t lastSummaryAt = 0, sinceSummary = 0;
  ++sinceSummary;
  const uint32_t now = millis();
  if (webRequests <= 6 || !ok) {
    Console.printf("[WEB] %s %s from %s%s\n", webServer.method() == HTTP_POST ? "POST" : "GET",
                   webServer.uri().c_str(), webServer.client().remoteIP().toString().c_str(),
                   ok ? "" : " - asked for the web login");
  } else if ((now - lastSummaryAt) >= 30000) {
    Console.printf("[WEB] %lu page requests in the last 30 s (total %lu)\n",
                   static_cast<unsigned long>(sinceSummary), static_cast<unsigned long>(webRequests));
    lastSummaryAt = now;
    sinceSummary = 0;
  }
}

// Every POST must carry X-AirWatch: 1. The page's own requests do; a form or
// script on some other website cannot add it without the board's say-so, so
// a cached login cannot be used to erase or reboot from another tab.
static bool webPostAllowed() {
  if (webServer.method() != HTTP_POST || webServer.header("X-AirWatch") == "1") return true;
  webLogRequest(false);
  webServer.send(403, "text/plain", "Use the AirWatch page.");
  return false;
}

static bool webAuth() {
  if (!webPostAllowed()) return false;
  // On the road the board's own WPA2 network already keeps strangers out, and
  // Android's sign-in window does not cope well with a login prompt.
  if (maintRoad || webServer.authenticate(WEB_USER, WEB_PASSWORD)) {
    maintLastRequestAt = millis();     // only a logged-in page keeps it here
    webLogRequest(true);
    return true;
  }
  webLogRequest(false);
  webServer.requestAuthentication(BASIC_AUTH, "AirWatch");
  return false;
}

static const char* tierWord(uint8_t tier) {
  return (tier == TIER_HIGH) ? "POLICE" : (tier == TIER_LOW) ? "MAYBE" : "MINOR";
}

static void webStatus() {
  if (!webAuth()) return;
  const uint32_t now = millis();
  uint16_t wifi = 0, ble = 0;
  countRecentDevices(now, wifi, ble);
  size_t logBytes = 0;
#if ENABLE_MARK_LOG
  if (markLogReady) {
    File f = LittleFS.open(MARK_LOG_PATH, FILE_READ);
    if (f) { logBytes = f.size(); f.close(); }
  }
#endif
  const uint32_t idle = timeReached(now, maintLastRequestAt) ? now - maintLastRequestAt : 0;
  String j;
  j.reserve(720);
  j += "{\"mode\":"; jsonStr(j, maintRoad ? "road" : "home");
#if AUTO_UPLOAD
  {   // the review page on the log server: UPLOAD_URL without "/upload"
    String r = UPLOAD_URL;
    const int cut = r.lastIndexOf("/upload");
    if (cut > 0) r = r.substring(0, cut);
    j += ",\"review\":"; jsonStr(j, (r + "/").c_str());
  }
#endif
  j += ",\"muted\":"; j += audioMuted ? "true" : "false";
  j += ",\"gps\":{\"seen\":"; j += gpsSeen(now) ? "true" : "false";
  j += ",\"fix\":"; j += gpsFixFresh(now) ? "true" : "false";
  j += ",\"sats\":"; j += static_cast<int>(gps.sats);
  if (gpsFixFresh(now)) {
    char ll[48];
    snprintf(ll, sizeof(ll), ",\"lat\":%.5f,\"lon\":%.5f,\"spd\":%.0f", gps.lat, gps.lon, gps.speedMph);
    j += ll;
  }
  j += "},\"lastReset\":"; jsonStr(j, lastResetText);
  j += ",\"lastResetBad\":"; j += lastResetBad ? "true" : "false";
  j += ",\"clock\":";
  if (clockValid()) { char t[24]; isoTime(t, sizeof(t), time(nullptr)); jsonStr(j, t); } else j += "null";
  j += ",\"homeRssi\":"; j += maintRoad ? 0 : static_cast<int>(WiFi.RSSI());
  j += ",\"ip\":"; jsonStr(j, (maintRoad ? WiFi.softAPIP() : WiFi.localIP()).toString().c_str());
  j += ",\"build\":"; jsonStr(j, __DATE__ " " __TIME__);
  j += ",\"drive\":"; j += bootCount;
  j += ",\"marks\":"; j += static_cast<int>(markCount);
  j += ",\"logPct\":"; j += static_cast<int>(markLogPercent);
  j += ",\"logBytes\":"; j += static_cast<unsigned long>(logBytes);
  j += ",\"uptime\":"; j += now / 1000UL;
  j += ",\"idleLeft\":"; j += (idle < MAINT_IDLE_MS) ? (MAINT_IDLE_MS - idle) / 1000UL : 0;
  j += ",\"wifi\":"; j += static_cast<int>(wifi);
  j += ",\"ble\":"; j += static_cast<int>(ble);
  j += ",\"heap\":"; j += static_cast<unsigned long>(esp_get_free_internal_heap_size());
  j += ",\"bleAdverts\":"; j += static_cast<unsigned long>(bleAdvertsSeen);
  j += ",\"obsDrops\":"; j += static_cast<unsigned long>(obsDrops);
  j += ",\"alert\":{\"active\":"; j += alertView.active ? "true" : "false";
  if (alertView.active) {
    j += ",\"tier\":"; jsonStr(j, alertView.quiet ? "QUIET" : tierWord(alertView.tier));
    j += ",\"label\":"; jsonStr(j, alertView.label);
    j += ",\"rssi\":"; if (alertView.hasRssi && !alertView.echoOnly) j += static_cast<int>(alertView.rssi); else j += "null";
  }
  j += "},\"last\":";
  if (lastAlertValid && (now - lastAlertAt) < MARK_HISTORY_MS) {
    j += "{\"tier\":"; jsonStr(j, lastAlert.quiet ? "QUIET" : tierWord(lastAlert.tier));
    j += ",\"label\":"; jsonStr(j, lastAlert.label);
    j += ",\"ago\":"; j += (now - lastAlertAt) / 1000UL; j += "}";
  } else {
    j += "null";
  }
  j += "}";
  webServer.send(200, "application/json", j);
}

static void webDevices() {
  if (!webAuth()) return;
  const uint32_t now = millis();
  String j = "[";
  bool first = true;
  for (size_t k = 0; k < DEVICE_CACHE_SIZE; ++k) {
    const TrackedDevice& d = deviceCache[k];
    if (!d.valid || (now - d.lastSeen) >= MARK_HISTORY_MS) continue;
    char mac[18], why[112];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             d.mac[0], d.mac[1], d.mac[2], d.mac[3], d.mac[4], d.mac[5]);
    formatReasonCodes(d.reasonMask, why, sizeof(why));
    if (!first) j += ',';
    first = false;
    j += "{\"mac\":"; jsonStr(j, mac);
    j += ",\"tier\":"; jsonStr(j, d.suppressed ? "IGNORED_FLEET" : shownTierName(d.tier, d.quietCap));
    j += ",\"src\":"; jsonStr(j, d.src == SRC_BLE ? "BLE" : "WIFI");
    j += ",\"rssi\":"; if (d.hasRssi) j += static_cast<int>(fp8ToRssi(d.smoothedRssiFp8)); else j += "null";
    j += ",\"ch\":"; j += static_cast<int>(d.channel);
    j += ",\"ago\":"; j += (now - d.lastSeen) / 1000UL;
    j += ",\"seen\":"; j += static_cast<int>(d.sightings);
    j += ",\"label\":"; jsonStr(j, d.label ? d.label : "");
    j += ",\"ssid\":"; jsonStr(j, d.ssid);
    j += ",\"why\":"; jsonStr(j, why);
    j += "}";
  }
  j += "]";
  webServer.send(200, "application/json", j);
}

// Uploads (web page and ArduinoOTA) block loop() for their whole length:
// stop the BLE scan meanwhile so flash writes don't share the radio.
// A good upload reboots; a failed one restarts the scan.
static bool uploadPaused = false;
static void uploadPause() {
  audioStop();
#if ENABLE_BLE_SCAN
  if (bleReady && !uploadPaused) NimBLEDevice::getScan()->stop();
#endif
  uploadPaused = true;
}
static void uploadResume() {   // also called for errors before any start (e.g. OTA wrong password)
  if (!uploadPaused) return;
  uploadPaused = false;
#if ENABLE_BLE_SCAN
  if (bleReady) bleStartScan();
#endif
}

static void webMarkLog() {
  if (!webAuth()) return;
  audioStop();
#if ENABLE_MARK_LOG
  if (markLogReady) {
    File f = LittleFS.open(MARK_LOG_PATH, FILE_READ);
    if (f) {
      if (webServer.hasArg("dl")) {
        char cd[80];
        snprintf(cd, sizeof(cd), "attachment; filename=\"airwatch-marks-drive%lu.log\"",
                 static_cast<unsigned long>(bootCount));
        webServer.sendHeader("Content-Disposition", cd);
      }
      webServer.streamFile(f, "text/plain; charset=utf-8");
      f.close();
      return;
    }
  }
#endif
  webServer.send(200, "text/plain", "");
}

// Both journal files, oldest first, as one download.
static void webJournal() {
  if (!webAuth()) return;
  audioStop();
  if (webServer.hasArg("dl")) {
    char cd[80];
    snprintf(cd, sizeof(cd), "attachment; filename=\"airwatch-alerts-drive%lu.log\"",
             static_cast<unsigned long>(bootCount));
    webServer.sendHeader("Content-Disposition", cd);
  }
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "text/plain; charset=utf-8", "");
#if ENABLE_MARK_LOG
  if (markLogReady) {
    static char chunk[1024];
    for (const char* path : {JOURNAL_OLD_PATH, JOURNAL_PATH}) {
      File f = LittleFS.open(path, FILE_READ);
      if (!f) continue;
      while (f.available()) {
        const size_t n = f.read(reinterpret_cast<uint8_t*>(chunk), sizeof(chunk));
        if (n == 0) break;
        webServer.sendContent(chunk, n);
      }
      f.close();
    }
  }
#endif
  webServer.sendContent("");
}

static void webErase() {
  if (!webAuth()) return;
#if ENABLE_MARK_LOG
  if (webServer.arg("confirm") == "ERASE") {
    eraseMarkLog();
    webServer.send(200, "text/plain", "Mark log erased.");
    return;
  }
#endif
  webServer.send(400, "text/plain", "Not erased: confirmation missing.");
}

static bool allDigits(const String& v) {
  if (v.length() == 0 || v.length() > 9) return false;
  for (size_t i = 0; i < v.length(); ++i) if (!isdigit(static_cast<unsigned char>(v[i]))) return false;
  return true;
}

// Adds a note to a mark, or corrects what you saw:
//   #NOTE drive=25 mark=1 saw=POLICE text="Henrico cruiser, traffic stop"
// Appended to the log, so nothing already saved is changed.
static void webNote() {
  if (!webAuth()) return;
#if ENABLE_MARK_LOG
  const String drive = webServer.arg("drive"), mark = webServer.arg("mark");
  const String saw = webServer.arg("saw");
  String text = webServer.arg("text");
  if (!allDigits(drive) || !allDigits(mark)) { webServer.send(400, "text/plain", "Bad mark number."); return; }
  if (saw.length() && saw != "POLICE" && saw != "NOTHING") { webServer.send(400, "text/plain", "Bad choice."); return; }
  if (text.length() > 160) text = text.substring(0, 160);
  char clean[161];
  size_t n = 0;
  for (size_t i = 0; i < text.length() && n < sizeof(clean) - 1; ++i) {
    const char c = text[i];
    clean[n++] = (c == '"') ? '\'' : (c >= 0x20 && c < 0x7F) ? c : ' ';
  }
  clean[n] = '\0';
  if (!saw.length() && n == 0) { webServer.send(400, "text/plain", "Nothing to save."); return; }
  if (!markLogReady || markLogFreeBytes() <= MARK_LOG_RESERVE_BYTES) {
    webServer.send(507, "text/plain", "Mark storage is full.");
    return;
  }
  File f = LittleFS.open(MARK_LOG_PATH, FILE_APPEND);
  if (!f) { webServer.send(500, "text/plain", "Could not open the log."); return; }
  f.printf("#NOTE drive=%s mark=%s", drive.c_str(), mark.c_str());
  if (saw.length()) f.printf(" saw=%s", saw.c_str());
  if (n)            f.printf(" text=\"%s\"", clean);
  f.print("\n");
  f.close();
  markLogRefreshUsage();
  webServer.send(200, "text/plain", "Saved.");
#else
  webServer.send(404, "text/plain", "No mark log in this build.");
#endif
}

// Console tab: GET /console?since=N returns what was printed after byte N
// (X-Next = the N to ask for next). POST /cmd runs one command.
static void webConsole() {
  if (!webAuth()) return;
  if (!consoleRing) { webServer.sendHeader("X-Next", "0"); webServer.send(200, "text/plain", ""); return; }
  const uint32_t total = consoleWritten;
  uint32_t since = webServer.hasArg("since") ? strtoul(webServer.arg("since").c_str(), nullptr, 10) : 0;
  if (since > total) since = total < consoleRingSize ? 0 : total - consoleRingSize;   // device rebooted
  if (total - since > consoleRingSize) since = total - consoleRingSize;                // fell behind
  uint32_t n = total - since;
  if (n > 16384) n = 16384;                                                            // one chunk per poll
  // Straight out of the ring, in at most two slices: no copy, no big heap block.
  const uint32_t start = since % consoleRingSize;
  const uint32_t first = std::min<uint32_t>(n, consoleRingSize - start);
  webServer.sendHeader("X-Next", String(since + n));
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.setContentLength(n);
  webServer.send(200, "text/plain; charset=utf-8", "");
  if (first > 0) webServer.sendContent(consoleRing + start, first);
  if (n > first) webServer.sendContent(consoleRing, n - first);
}
static void webCmd() {
  if (!webAuth()) return;
  String c = webServer.arg("c");
  c.trim();
  if (c.length() == 0 || c.length() > 15) { webServer.send(400, "text/plain", "Bad command"); return; }
  webServer.send(200, "text/plain", "OK");   // reply first: x / w may reboot or switch networks
  runCommand(c.c_str(), true);
}

// Firmware upload: application slot only (the mark log storage is untouched).
static bool uploadAuthed = false;
static void webUpdateChunk() {
  HTTPUpload& up = webServer.upload();
  if (up.status == UPLOAD_FILE_START) {
    uploadAuthed = webServer.header("X-AirWatch") == "1" &&
                   (maintRoad || webServer.authenticate(WEB_USER, WEB_PASSWORD));
    if (!uploadAuthed) return;
    maintLastRequestAt = millis();
    Console.printf("[WEB] firmware upload: %s\n", up.filename.c_str());
    uploadPause();
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) Update.printError(Console);
  } else if (!uploadAuthed) {
    return;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    maintLastRequestAt = millis();
    if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Console);
  } else if (up.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) { Update.printError(Console); uploadResume(); }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    uploadResume();
  }
}
static void webUpdateDone() {
  const bool authed = uploadAuthed;
  uploadAuthed = false;                    // never carried over to the next request
  if (!webPostAllowed()) return;           // 403, not a login prompt, without the page's header
  if (!authed) { webServer.requestAuthentication(BASIC_AUTH, "AirWatch"); return; }
  if (Update.hasError() || !Update.isFinished()) {
    webServer.send(500, "text/plain", String("Update failed: ") + Update.errorString());
    return;
  }
  webServer.send(200, "text/plain", "OK");
  Console.println(F("[WEB] firmware installed, rebooting"));
  webActiveMagic = 0;
  delay(500);
#if ENABLE_MARK_LOG
  journalFlushOngoing();
#endif
  ESP.restart();   // not locked: if still at home it comes back to the page
}

DNSServer dnsServer;

// Wi-Fi events, noted in the Wi-Fi event task and printed from loop().
// Registered before joining / opening the network, so a failed home join
// shows its reason at once instead of only timing out.
static void maintRegisterEvents() {
  static bool registered = false;
  if (registered) return;
  registered = true;
  WiFi.onEvent([](arduino_event_id_t e, arduino_event_info_t info) {
    switch (e) {
      case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
        for (int i = 0; i < 6; ++i) webEvtMac[i] = info.wifi_ap_staconnected.mac[i];
        __atomic_fetch_or(&webEvtFlags, 1U, __ATOMIC_RELAXED); break;
      case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
        webEvtIp = info.wifi_ap_staipassigned.ip.addr;
        __atomic_fetch_or(&webEvtFlags, 2U, __ATOMIC_RELAXED); break;
      case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
        __atomic_fetch_or(&webEvtFlags, 4U, __ATOMIC_RELAXED); break;
      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        webEvtReason = info.wifi_sta_disconnected.reason;
        __atomic_fetch_or(&webEvtFlags, 8U, __ATOMIC_RELAXED); break;
      default: break;
    }
  });
}

static void maintStartServices() {
  if (maintRoad) {
    // Captive portal: every name resolves to the board, and the "is there
    // internet?" checks of Android / iOS / Windows get sent to the page, so
    // the phone offers "Sign in to network" and opens it there.
    dnsServer.start(53, "*", WiFi.softAPIP());
    for (const char* path : {"/generate_204", "/gen_204", "/hotspot-detect.html", "/library/test/success.html",
                             "/connecttest.txt", "/ncsi.txt", "/redirect", "/canonical.html"}) {
      webServer.on(path, []() {
        webServer.sendHeader("Location", "http://192.168.4.1/", true);
        webServer.send(302, "text/plain", "");
      });
    }
  }
  ArduinoOTA.setHostname(DEVICE_HOSTNAME);
  ArduinoOTA.setPassword(WEB_PASSWORD);
  ArduinoOTA.onStart([]() { Console.println(F("[OTA] wireless upload starting")); uploadPause(); });
  ArduinoOTA.onError([](ota_error_t e) { Console.printf("[OTA] error %u\n", static_cast<unsigned>(e)); uploadResume(); });
  ArduinoOTA.begin();                     // also starts mDNS: http://airwatch.local
  MDNS.addService("http", "tcp", 80);

  // Sent straight from flash (no 37 KB copy in RAM).
  webServer.on("/", HTTP_GET, []() {
    if (webAuth()) webServer.send_P(200, "text/html", WEB_UI_HTML, sizeof(WEB_UI_HTML) - 1);
  });
  webServer.on("/status.json", HTTP_GET, webStatus);
  webServer.on("/devices.json", HTTP_GET, webDevices);
  webServer.on("/marks.log", HTTP_GET, webMarkLog);
  webServer.on("/alerts.log", HTTP_GET, webJournal);
  webServer.on("/erase", HTTP_POST, webErase);
  webServer.on("/note", HTTP_POST, webNote);
  webServer.on("/console", HTTP_GET, webConsole);
  webServer.on("/mute", HTTP_POST, []() {
    if (!webAuth()) return;
    toggleMute(millis());
    webServer.send(200, "text/plain", audioMuted ? "muted" : "sound on");
  });
  webServer.on("/cmd", HTTP_POST, webCmd);
  webServer.on("/update", HTTP_POST, webUpdateDone, webUpdateChunk);
  webServer.on("/exit", HTTP_POST, []() {
    if (!webAuth()) return;
    webServer.send(200, "text/plain", "OK");
    delay(300);
    maintExit("web page", true);
  });
  webServer.on("/reboot", HTTP_POST, []() {
    if (!webAuth()) return;
    webServer.send(200, "text/plain", "OK");
    webActiveMagic = 0;
    delay(300);
#if ENABLE_MARK_LOG
    journalFlushOngoing();
#endif
    ESP.restart();
  });
  webServer.onNotFound([]() {
    if (maintRoad) {                       // any other address on the road: the page
      webServer.sendHeader("Location", "http://192.168.4.1/", true);
      webServer.send(302, "text/plain", "");
      return;
    }
    webServer.send(404, "text/plain", "Not found");
  });
  static const char* headerKeys[] = {"X-AirWatch"};
  webServer.collectHeaders(headerKeys, 1);
  webServer.begin();
}

// Leaving is a reboot: the cleanest way back to a sniffer that is exactly
// as it was at power-on. lock = do not come back until away from home.
static void maintExit(const char* why, bool lock) {
  Console.printf("[WEB] leaving web mode (%s), rebooting into listening\n", why);
  webActiveMagic = 0;
  if (lock) maintLockMagic = MAINT_LOCK_MAGIC;
  delay(100);
#if ENABLE_MARK_LOG
  journalFlushOngoing();
#endif
  ESP.restart();
}

// Road access: the board's own password-protected Wi-Fi network.
static void roadEnter(uint32_t now) {
  if (strlen(ROAD_AP_PASSWORD) < 8) {
    Console.println(F("[WEB] road access needs ROAD_AP_PASSWORD (or WEB_PASSWORD) of 8+ characters"));
    showNotice("NO ROAD WEB", "password < 8 chars", now);
    return;
  }
  Console.printf("[WEB] road access: join Wi-Fi \"%s\" (password = ROAD_AP_PASSWORD), then tap \"Sign in\" or open http://192.168.4.1\n", ROAD_AP_SSID);
  webActiveMagic = WEB_ACTIVE_MAGIC;
  esp_wifi_set_promiscuous(false);
  maintRegisterEvents();
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ROAD_AP_SSID, ROAD_AP_PASSWORD, 6, 0, 2);   // channel 6, visible, max 2 phones
  WiFi.setTxPower(WEB_TX_POWER_ROAD);
  Console.printf("[WEB] network \"%s\" is up on %s, channel 6\n", ROAD_AP_SSID, WiFi.softAPIP().toString().c_str());
  maintActive        = true;
  maintRoad          = true;
  maintConnected     = true;
  maintStartedAt     = now;
  maintLastRequestAt = now;
  maintStartServices();
  audioPlay(JINGLE_ROAD, sizeof(JINGLE_ROAD) / sizeof(Note), AUDIO_PRIO_BOOT, now);
  lastRenderedState = DisplaySnapshot{};
}

#if HOME_SYNC
// Stop passive Wi-Fi listening and join the home network. Bluetooth
// detection, alerts and the mark button carry on.
static void maintEnter(uint32_t now, const char* why) {
  Console.printf("[HOME] entering maintenance (%s): joining \"%s\"\n", why, HOME_WIFI_SSID);
  webActiveMagic = WEB_ACTIVE_MAGIC;
  esp_wifi_set_promiscuous(false);
  WiFi.setHostname(DEVICE_HOSTNAME);
  WiFi.setAutoReconnect(true);
  // Mesh (UniFi etc.): look on every channel and join the strongest access
  // point with this name, not the first one heard.
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.setTxPower(WEB_TX_POWER_HOME);
  esp_wifi_set_inactive_time(WIFI_IF_STA, 3);   // notice a vanished router in 3 s, not ~6
  maintRegisterEvents();
  WiFi.begin(HOME_WIFI_SSID, HOME_WIFI_PASSWORD);
  maintActive        = true;
  maintRoad          = false;
  maintConnected     = false;
  maintStartedAt     = now;
  maintLastRequestAt = now;
  lastRenderedState  = DisplaySnapshot{};
}
#endif

static void webPrintEvents(uint32_t now) {
  const uint32_t f = __atomic_exchange_n(&webEvtFlags, 0U, __ATOMIC_RELAXED);   // read + clear in one step
  if (f) {
    if (f & 1) Console.printf("[WEB] phone joined the network (%02X:%02X:%02X:%02X:%02X:%02X)\n",
                              webEvtMac[0], webEvtMac[1], webEvtMac[2], webEvtMac[3], webEvtMac[4], webEvtMac[5]);
    if (f & 2) Console.printf("[WEB] phone got address %s - it can open the page now\n",
                              IPAddress(webEvtIp).toString().c_str());
    if (f & 4) Console.println(F("[WEB] phone left the network"));
    if (f & 8) {
      const uint16_t r = webEvtReason;
      if (maintConnected) {
        Console.printf("[HOME] lost the home Wi-Fi link (reason %u %s)\n", static_cast<unsigned>(r),
                       WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(r)));
      } else {                               // still joining: say each new reason once
        static uint16_t lastJoinReason = 0;
        if (r != lastJoinReason) {
          lastJoinReason = r;
          Console.printf("[HOME] join attempt failed (reason %u %s)%s\n", static_cast<unsigned>(r),
                         WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(r)),
                         (r == 15 || r == 202 || r == 204) ? " - check HOME_WIFI_PASSWORD"
                         : (r == 201) ? " - network name not found: check HOME_WIFI_SSID" : "");
        }
      }
    }
  }
  // Every 60 s while nobody uses the page: say so (and why it may be).
  static uint32_t lastNote = 0;
  now = millis();
  const uint32_t idleMs = timeReached(now, maintLastRequestAt) ? now - maintLastRequestAt : 0;
  if (idleMs >= 60000 && (now - lastNote) >= 60000) {
    lastNote = now;
    if (maintRoad) {
      Console.printf("[WEB] no page requests yet - %u phone(s) on \"%s\". If Chrome hangs, Android is using "
                     "mobile data: tap \"Stay connected\" or turn mobile data off\n",
                     static_cast<unsigned>(WiFi.softAPgetStationNum()), ROAD_AP_SSID);
    } else {
      Console.printf("[WEB] no page requests reached the board for %lu min (home %d dBm, %s). Is the phone on a "
                     "network that can reach it?\n", static_cast<unsigned long>(idleMs / 60000UL),
                     static_cast<int>(WiFi.RSSI()), WiFi.localIP().toString().c_str());
    }
  }
}

#if AUTO_UPLOAD
// ------------------------------------------------------------
// Upload on arrival
// ------------------------------------------------------------
// Each log is first renamed to a ".pending" file, so marks saved while it is
// being sent go to a fresh log. The receiver answers "OK <bytes> <crc32>";
// only an exact match deletes the pending file. Anything not confirmed stays
// and is sent first on the next arrival.

static uint32_t crc32Update(uint32_t crc, const uint8_t* p, size_t n) {   // zlib-compatible
  crc = ~crc;
  while (n--) {
    crc ^= *p++;
    for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1U)));
  }
  return ~crc;
}

static bool fileCrc(const char* path, size_t& size, uint32_t& crc) {
  File f = LittleFS.open(path, FILE_READ);
  if (!f) return false;
  static uint8_t buf[1024];
  size = 0;
  crc  = 0;
  while (f.available()) {
    const size_t n = f.read(buf, sizeof(buf));
    if (n == 0) break;
    crc = crc32Update(crc, buf, n);
    size += n;
  }
  f.close();
  return true;
}

// Sends one file; true only when the receiver confirmed an identical copy.
static bool uploadFile(const char* path, const char* kind) {
  size_t size = 0;
  uint32_t crc = 0;
  if (!fileCrc(path, size, crc)) return false;
  if (size == 0) return true;                       // nothing to keep

  File f = LittleFS.open(path, FILE_READ);
  if (!f) return false;
  char url[200];
  snprintf(url, sizeof(url), "%s?kind=%s&drive=%lu&mac=%s", UPLOAD_URL, kind,
           static_cast<unsigned long>(bootCount), WiFi.macAddress().c_str());
  HTTPClient http;
  http.setTimeout(20000);
  if (!http.begin(url)) { f.close(); return false; }
  http.addHeader("Content-Type", "text/plain; charset=utf-8");
  http.addHeader("X-AirWatch-Token", UPLOAD_TOKEN);
  const int code = http.sendRequest("POST", &f, size);
  const String reply = (code > 0) ? http.getString() : String();
  http.end();
  f.close();

  unsigned long gotBytes = 0, gotCrc = 0;
  const bool ok = (code == 200) &&
                  sscanf(reply.c_str(), "OK %lu %lx", &gotBytes, &gotCrc) == 2 &&
                  gotBytes == size && static_cast<uint32_t>(gotCrc) == crc;
  if (ok) {
    Console.printf("[UPLOAD] %s: %u bytes confirmed\n", kind, static_cast<unsigned>(size));
  } else if (code < 0) {
    // Never reached the receiver: wrong address, receiver not running, or a
    // firewall between networks (e.g. an IoT VLAN that may not reach the LAN).
    Console.printf("[UPLOAD] %s: could not connect to the receiver (%s) - kept on the board\n",
                   kind, HTTPClient::errorToString(code).c_str());
  } else {
    Console.printf("[UPLOAD] %s: receiver did not confirm a matching copy (HTTP %d, reply \"%s\") "
                   "- kept on the board\n", kind, code, reply.substring(0, 60).c_str());
  }
  return ok;
}

// Moves a log aside (unless an earlier pending copy is still waiting), sends
// it, and deletes it once confirmed.
static bool uploadLog(const char* livePath, const char* pendingPath, const char* kind) {
  if (!LittleFS.exists(pendingPath)) {
    if (!LittleFS.exists(livePath)) return true;
    if (!LittleFS.rename(livePath, pendingPath)) return false;
  }
  if (!uploadFile(pendingPath, kind)) return false;
  LittleFS.remove(pendingPath);
  return true;
}

static void uploadOnArrival(uint32_t now) {
  if (!markLogReady) return;
  audioStop();
  Console.printf("[UPLOAD] sending logs to %s\n", UPLOAD_URL);
  showNotice("UPLOADING", "marks, alerts, samples", now);
  refreshDisplay(true);
  bool ok = true;
  // Twice: a copy left from a failed earlier attempt goes first, then
  // whatever was saved since.
  for (int pass = 0; pass < 2 && ok; ++pass) {
    ok = uploadLog(MARK_LOG_PATH, MARKS_PENDING_PATH, "marks") &&
         uploadLog(JOURNAL_OLD_PATH, ALERTS_PENDING_OLD_PATH, "alerts") &&
         uploadLog(JOURNAL_PATH, ALERTS_PENDING_PATH, "alerts") &&
         uploadLog(SAMPLES_PATH, SAMPLES_PENDING_PATH, "samples");
  }
  markLogRefreshUsage();
  showNotice(ok ? "UPLOADED" : "UPLOAD FAIL", ok ? "logs cleared" : "kept, retry next time", millis());
  if (!ok) audioPlay(JINGLE_MARK_NOTHING, sizeof(JINGLE_MARK_NOTHING) / sizeof(Note),
                     AUDIO_PRIO_BOOT, millis());   // two long beeps: not sent, still on the board
}
#endif  // AUTO_UPLOAD

// Runs every loop() while a web mode is up.
static void maintTick(uint32_t now) {
  if (!maintActive) return;
  webPrintEvents(now);
#if HOME_SYNC
  if (!maintRoad) {
    const bool up = (WiFi.status() == WL_CONNECTED);
    static uint32_t lostMs    = 0;     // time without a usable link in this episode
    static uint32_t goodSince = 0;
    static uint32_t lastTick  = now;
    // Capped: after a long blocking job (upload on arrival, a big log
    // download) one weak reading must not count as seconds of being gone.
    const uint32_t step = std::min<uint32_t>(now - lastTick, 1000UL);
    lastTick = now;
    // GPS says we are driving: leave at once, joined or still joining.
    if (homeDriving(now)) {
      Console.println(F("[HOME] GPS shows driving: back to listening"));
      maintExit("driving away", true);
    }
    if (!maintConnected) {
      if (up) {
        maintConnected     = true;
        maintLastRequestAt = now;
        lostMs             = 0;
        goodSince          = now;
        WiFi.setTxPower(WEB_TX_POWER_HOME);       // some drivers reset it on connect
        Console.printf("[HOME] joined %s (%d dBm, channel %d)\n",
                       WiFi.BSSIDstr().c_str(), static_cast<int>(WiFi.RSSI()), static_cast<int>(WiFi.channel()));
        maintStartServices();
        Console.printf("[HOME] joined, web page at http://%s (or http://%s.local)\n",
                      WiFi.localIP().toString().c_str(), DEVICE_HOSTNAME);
        Console.println(F("[HOME] the phone must be on a network that can reach this address "
                          "(same network, no client isolation / IoT firewall)"));
        lastRenderedState = DisplaySnapshot{};
#if AUTO_UPLOAD
        uploadOnArrival(now);
        maintLastRequestAt = millis();
#endif
      } else if ((now - maintStartedAt) >= MAINT_JOIN_TIMEOUT_MS) {
        maintExit("could not join home Wi-Fi in 20 s (out of range, or check secrets.h)", true);
      }
      return;
    }
    // Drove off: router gone, or so weak you are clearly leaving. Time
    // without a usable link adds up across drop-outs (the edge of its range,
    // around a corner); only MAINT_RECOVER_MS of steady link clears it.
    // Leaving locks home mode until you have been away HOME_RELEASE_MS, so
    // it does not grab your Wi-Fi again at the end of the street.
    const bool leaving = !up || (WiFi.RSSI() <= HOME_LEAVE_RSSI);
    if (leaving) {
      goodSince = 0;
      lostMs += step;
      if (lostMs >= MAINT_LOST_MS) {
        Console.printf("[HOME] home Wi-Fi %s: back to listening\n", up ? "very weak" : "gone");
        maintExit("home Wi-Fi out of reach", true);
      }
    } else {
      if (goodSince == 0) goodSince = now;
      if (lostMs != 0 && (now - goodSince) >= MAINT_RECOVER_MS) lostMs = 0;
    }
  }
#endif
  if (maintRoad) dnsServer.processNextRequest();   // captive portal answers
  webServer.handleClient();
  ArduinoOTA.handle();
  // Fresh millis(): handleClient() above may just have moved maintLastRequestAt
  // past `now`, and an unsigned now - later underflows to ~4 billion ms.
  if (timeReached(millis(), maintLastRequestAt + MAINT_IDLE_MS)) maintExit("idle timeout", true);
}

#endif  // WEB_UI

#if HOME_SYNC
// Passive mode: has the home network been in steady view long enough?
static void homeTick(uint32_t now) {
  static uint32_t lastCount   = 0;
  static uint32_t lastSeen    = 0;
  static bool     haveSeen    = false;
  static uint32_t streakStart = 0;
  static bool     streak      = false;
  if (maintActive) return;

  const uint32_t count = homeBeaconCount;
  if (count != lastCount) {
    lastCount = count;
    if (!streak) { streak = true; streakStart = now; }
    haveSeen = true;
    lastSeen = now;
  } else if (streak && (now - lastSeen) >= HOME_GAP_MS) {
    streak = false;
  }

  if (homeLocked) {
    const uint32_t away = haveSeen ? (now - lastSeen) : now;   // since boot if never heard
    if (away >= HOME_RELEASE_MS) {
      homeLocked = false;
      Console.println(F("[HOME] away from home long enough: maintenance mode re-armed"));
    }
    return;
  }
  if (streak && (now - streakStart) >= HOME_SETTLE_MS && !alertView.active && !homeDriving(now)) {
    maintEnter(now, "home Wi-Fi in view");
  }
}
#endif  // HOME_SYNC

// ============================================================
// WHY DID IT LAST RESTART?
// ============================================================
static const char* resetReasonText(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "power on";
    case ESP_RST_SW:        return "its own reboot";
    case ESP_RST_PANIC:     return "CRASH (software error)";
    case ESP_RST_INT_WDT:   return "FROZE (interrupt watchdog)";
    case ESP_RST_TASK_WDT:  return "FROZE (task watchdog)";
    case ESP_RST_WDT:       return "FROZE (watchdog)";
    case ESP_RST_BROWNOUT:  return "POWER DIPPED (brownout: supply too weak)";
    case ESP_RST_EXT:       return "reset pin";
    case ESP_RST_USB:       return "USB reset (flashing / monitor)";
    case ESP_RST_DEEPSLEEP: return "deep sleep wake";
    default:                return "unknown";
  }
}

// Runs early in setup(). Reads why the chip restarted and, after a crash,
// the saved core dump summary (where in the code it happened).
static void resetReportBegin() {
  const esp_reset_reason_t r = esp_reset_reason();
  lastResetBad = (r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT ||
                  r == ESP_RST_WDT || r == ESP_RST_BROWNOUT);
  crashedInWeb = lastResetBad && (webActiveMagic == WEB_ACTIVE_MAGIC);
  webActiveMagic = 0;
  size_t n = snprintf(lastResetText, sizeof(lastResetText), "%s%s", resetReasonText(r),
                      crashedInWeb ? " while the web page was up" : "");
  if (r == ESP_RST_PANIC) {
    esp_core_dump_summary_t* sum = static_cast<esp_core_dump_summary_t*>(malloc(sizeof(esp_core_dump_summary_t)));
    if (sum && esp_core_dump_get_summary(sum) == ESP_OK) {
      n = std::min(n, sizeof(lastResetText) - 1);    // snprintf returns the untruncated length
      n += snprintf(lastResetText + n, sizeof(lastResetText) - n, " | task=%s pc=0x%08lx cause=%lu bt=",
                    sum->exc_task, static_cast<unsigned long>(sum->exc_pc),
                    static_cast<unsigned long>(sum->ex_info.exc_cause));
      n = std::min(n, sizeof(lastResetText) - 1);
      for (uint32_t i = 0; i < sum->exc_bt_info.depth && i < 8 && n < sizeof(lastResetText) - 12; ++i) {
        n += snprintf(lastResetText + n, sizeof(lastResetText) - n, "%s%08lx", i ? "," : "",
                      static_cast<unsigned long>(sum->exc_bt_info.bt[i]));
      }
      esp_core_dump_image_erase();          // report each crash once
    }
    free(sum);
  }
}

// Once the file system is up: keep a short history of bad restarts.
static void resetReportSave() {
  Console.printf("[BOOT] last restart: %s\n", lastResetText);
#if ENABLE_MARK_LOG
  if (!lastResetBad || !markLogReady) return;
  File f = LittleFS.open(RESET_LOG_PATH, FILE_READ);
  const size_t size = f ? f.size() : 0;
  if (f) f.close();
  if (size > RESET_LOG_MAX) LittleFS.remove(RESET_LOG_PATH);
  f = LittleFS.open(RESET_LOG_PATH, FILE_APPEND);
  if (!f) return;
  f.printf("drive=%lu %s\n", static_cast<unsigned long>(bootCount), lastResetText);
  f.close();
#endif
}

static void printResetLog() {
  Console.printf("[BOOT] last restart: %s\n", lastResetText);
#if ENABLE_MARK_LOG
  File f = markLogReady ? LittleFS.open(RESET_LOG_PATH, FILE_READ) : File();
  if (!f || f.size() == 0) { Console.println(F("[BOOT] no crashes / brownouts recorded")); return; }
  Console.println(F("[BOOT] crashes / brownouts (oldest first; drive = boot number):"));
  while (f.available()) Console.write(static_cast<uint8_t>(f.read()));
  f.close();
#endif
}

// ============================================================
// FAIL-FAST INITIALIZATION
// ============================================================

[[noreturn]]
static void fatalInitError(const char* message, esp_err_t err = ESP_OK) {
  buzzerOff();
  strip.setPixelColor(0, 255, 0, 0);
  strip.show();

  Console.println();
  Console.println(F("[FATAL] Passive monitor initialization failed"));
  if (message) Console.printf("[FATAL] %s\n", message);
  if (err != ESP_OK) {
    Console.printf("[FATAL] ESP error: 0x%X (%s)\n",
                  static_cast<unsigned>(err), esp_err_to_name(err));
  }

  if (displayReady) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(F("FATAL INIT ERROR"));
    display.setCursor(0, 12);
    if (message) display.println(message);
    display.display();
  }

  while (true) {
    delay(1000);
  }
}

// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  consoleBegin();
  resetReportBegin();

  // Give a computer up to 1.2 s to open the port so the [INIT] lines are
  // visible when one is attached. On a car charger this simply times out.
  const uint32_t serialWaitStart = millis();
  while (!Serial && (millis() - serialWaitStart) < 1200) {
    delay(10);
  }

#if ARDUINO_USB_CDC_ON_BOOT
  // Native USB serial: never block a write when no computer is attached.
  Serial.setTxTimeoutMs(0);
#endif
  delay(100);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  strip.begin();
  strip.setBrightness(75);
  strip.clear();
  strip.show();

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(OLED_I2C_HZ);
  Wire.setTimeOut(25);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    fatalInitError("SSD1306 allocation failed");
  }
  displayReady = true;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("PASSIVE AIR WATCH"));
  display.setCursor(0, 12);
  display.println(F("Validating DB..."));
  display.display();

  // Also builds the first-octet bitmap the capture callback relies on, so it
  // must run before promiscuous mode is enabled.
  if (!validateSignatureDatabases()) {
    fatalInitError("Database sanity check failed");
  }
#if HOME_SYNC && defined(HOME_WIFI_BSSID)
  parseHomeBssids(HOME_WIFI_BSSID);   // read by the capture callback: fill it before promiscuous mode
#endif

  mgmtIeQueue = xQueueCreateStatic(
      MGMT_IE_QUEUE_SIZE,
      sizeof(MgmtIeEvent),
      mgmtIeQueueStorage,
      &mgmtIeQueueControl);

  headerQueue = xQueueCreateStatic(
      HEADER_QUEUE_SIZE,
      sizeof(HeaderEvent),
      headerQueueStorage,
      &headerQueueControl);

  bleQueue = xQueueCreateStatic(
      BLE_QUEUE_SIZE,
      sizeof(BleEvent),
      bleQueueStorage,
      &bleQueueControl);

  obsQueue = xQueueCreateStatic(
      OBS_QUEUE_SIZE,
      sizeof(Observation),
      obsQueueStorage,
      &obsQueueControl);

  if (!mgmtIeQueue || !headerQueue || !bleQueue || !obsQueue) {
    fatalInitError("Queue allocation failed");
  }

  memset(deviceCache, 0, sizeof(deviceCache));

#if ENABLE_GPS
  Serial1.setRxBufferSize(2048);          // ~2 s of NMEA, covers slow flash writes
  Serial1.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, -1);   // receive only: D6 stays free
#endif

  // Recently-heard table: in PSRAM when the board has it enabled, else heap.
  if (psramFound()) {
    recentTable = static_cast<RecentEntry*>(ps_calloc(RECENT_TABLE_SIZE_PSRAM, sizeof(RecentEntry)));
    if (recentTable) { recentCap = RECENT_TABLE_SIZE_PSRAM; recentInPsram = true; }
  }
  if (!recentTable) {
    recentTable = static_cast<RecentEntry*>(calloc(RECENT_TABLE_SIZE_RAM, sizeof(RecentEntry)));
    recentCap   = RECENT_TABLE_SIZE_RAM;
  }
  recentIndex = static_cast<RecentIndex*>(calloc(recentCap, sizeof(RecentIndex)));
  recentOrder = static_cast<uint16_t*>(calloc(recentCap, sizeof(uint16_t)));
  if (recentInPsram) recentProbes = static_cast<ProbeNames*>(ps_calloc(recentCap, sizeof(ProbeNames)));
  // Signal timelines (optional: without them alerts are logged as before)
  if (psramFound()) deviceTimeline = static_cast<int8_t*>(ps_calloc(DEVICE_CACHE_SIZE, TIMELINE_SECONDS));
  if (!deviceTimeline) deviceTimeline = static_cast<int8_t*>(calloc(DEVICE_CACHE_SIZE, TIMELINE_SECONDS));
  if (!recentTable || !recentIndex || !recentOrder) {
    fatalInitError("Recent table allocation failed");
  }

#if ENABLE_MARK_LOG
  pinMode(MARK_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  display.setCursor(0, 24);
  display.println(F("Preparing log..."));   // first boot formats the flash: can take ~10 s
  display.display();
  markLogBegin();
  if (!markLogReady) {
    Console.println(F("[INIT] Mark log storage failed - marks will not be saved"));
    showNotice("LOG FAILED", "marks will not save", millis());
  }
#endif

  WiFi.persistent(false);   // never save Wi-Fi settings to NVS (home mode joins every trip)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  wifi_promiscuous_filter_t filter{};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;

  esp_err_t err = esp_wifi_set_promiscuous_filter(&filter);
  if (err != ESP_OK) fatalInitError("Promisc filter", err);

  err = esp_wifi_set_promiscuous_rx_cb(&wifi_promiscuous_handler);
  if (err != ESP_OK) fatalInitError("Promisc callback", err);

  const uint8_t initialChannel = SCAN_CHANNELS[0];
  err = esp_wifi_set_channel(initialChannel, WIFI_SECOND_CHAN_NONE);
  if (err != ESP_OK) {
    ++channelErrors;
    fatalInitError("Initial channel", err);
  }
  channelIndex   = 0;
  currentChannel = initialChannel;

  err = esp_wifi_set_promiscuous(true);
  if (err != ESP_OK) fatalInitError("Promisc enable", err);

#if ENABLE_BLE_SCAN
  // BLE is optional at runtime: if it fails, Wi-Fi monitoring carries on.
  bleReady = bleBegin();
  if (!bleReady) Console.println(F("[INIT] BLE scan failed to start - continuing Wi-Fi only"));
#endif

#if WEB_UI
  Console.printf("[INIT] Road access: hold the button 3 s within 15 s of power-up (network \"%s\")\n", ROAD_AP_SSID);
#endif
  resetReportSave();
#if HOME_SYNC
  // Locked = do not open the page by itself until away from home 5 min:
  // after Exit / idle timeout / failed join, and after a crash in web mode
  // (so a crash there cannot turn into a reboot loop in the driveway).
  homeLocked = ((maintLockMagic == MAINT_LOCK_MAGIC) && (esp_reset_reason() == ESP_RST_SW)) || crashedInWeb;
  maintLockMagic = 0;
  Console.printf("[INIT] Home Wi-Fi maintenance: \"%s\"%s\n", HOME_WIFI_SSID,
                homeLocked ? " (locked until away from home 5 min)" : "");
#else
  Console.println(F("[INIT] Home Wi-Fi maintenance: off (no secrets.h)"));
#endif
  Console.println(F("\n[INIT] Passive monitor operational"));
  Console.printf("[INIT] Signatures: %u prefixes, %u SSIDs (%u ignored), %u WPS, %u BLE\n",
                static_cast<unsigned>(NUM_PREFIX_TARGETS),
                static_cast<unsigned>(NUM_SSID_TARGETS),
                static_cast<unsigned>(NUM_IGNORED_SSID_TARGETS),
                static_cast<unsigned>(NUM_WPS_TARGETS),
                static_cast<unsigned>(NUM_BLE_COMPANY_TARGETS + NUM_BLE_SERVICE_TARGETS +
                                      NUM_BLE_NAME_TARGETS));
  Console.printf("[INIT] MgmtIeEvent/HeaderEvent/BleEvent: %u/%u/%u bytes\n",
                static_cast<unsigned>(sizeof(MgmtIeEvent)),
                static_cast<unsigned>(sizeof(HeaderEvent)),
                static_cast<unsigned>(sizeof(BleEvent)));
  Console.printf("[INIT] Queues: mgmt %u, header %u, ble %u entries\n",
                static_cast<unsigned>(MGMT_IE_QUEUE_SIZE),
                static_cast<unsigned>(HEADER_QUEUE_SIZE),
                static_cast<unsigned>(BLE_QUEUE_SIZE));
  Console.printf("[INIT] Target cache: %u slots (%u bytes)\n",
                static_cast<unsigned>(DEVICE_CACHE_SIZE),
                static_cast<unsigned>(sizeof(deviceCache)));
  Console.printf("[INIT] BLE scan: %s (%u ms of every %u ms)\n",
                bleReady ? "on" : "off",
                static_cast<unsigned>(BLE_SCAN_WINDOW_MS),
                static_cast<unsigned>(BLE_SCAN_INTERVAL_MS));
  Console.printf("[INIT] Mark log: %s, drive #%lu, storage %u%% full (l = print, t = telemetry, h = help)\n",
                markLogReady ? "ready" : "off", static_cast<unsigned long>(bootCount),
                static_cast<unsigned>(markLogPercent));
  Console.printf("[INIT] Recent table: %u devices in %s\n",
                static_cast<unsigned>(recentCap), recentInPsram ? "PSRAM" : "RAM (no PSRAM enabled)");
  Console.printf("[INIT] Free internal SRAM: %u bytes\n",
                static_cast<unsigned>(esp_get_free_internal_heap_size()));

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("PASSIVE AIR WATCH"));
  display.setCursor(0, 12);
  display.println(F("Monitor operational"));
  display.display();

  audioMuted = (muteMagic == MUTE_MAGIC) && (esp_reset_reason() == ESP_RST_SW);
  if (!audioMuted) muteMagic = 0;
  if (audioMuted) Console.println(F("[SND] still muted (3 taps = sound on)"));
  audioPlay(JINGLE_BOOT, sizeof(JINGLE_BOOT) / sizeof(Note), AUDIO_PRIO_BOOT, millis());
}

// ============================================================
// MAIN EXECUTION LOOP
// ============================================================

// One pass of everything that must keep running: sound, the capture queues,
// alert decisions and channel hopping. loop() calls it every pass; the mark
// log dump calls it between chunks so the monitor is never deaf.
static void serviceCore() {
  gpsPoll();
  uint32_t now = millis();

  // 1. Buzzer sequencer
  audioTick(now);

  // 2. Per-queue watermark recording
  const uint16_t mgmtDepth = static_cast<uint16_t>(uxQueueMessagesWaiting(mgmtIeQueue));
  const uint16_t hdrDepth  = static_cast<uint16_t>(uxQueueMessagesWaiting(headerQueue));

  if (mgmtDepth > mgmtQueueWatermarkBoot)     mgmtQueueWatermarkBoot = mgmtDepth;
  if (mgmtDepth > mgmtQueueWatermarkInterval) mgmtQueueWatermarkInterval = mgmtDepth;
  if (hdrDepth > headerQueueWatermarkBoot)     headerQueueWatermarkBoot = hdrDepth;
  if (hdrDepth > headerQueueWatermarkInterval) headerQueueWatermarkInterval = hdrDepth;

  // 3. Fairly drain all three queues. The events are static so the 556-byte
  //    management event never sits on the loop task's stack.
  static MgmtIeEvent mgmtEvent;
  static HeaderEvent headerEvent;
  static BleEvent    bleEvent;
  size_t processed = 0;
  while (processed < MAX_EVENTS_PER_LOOP) {
    bool progressed = false;

    if (xQueueReceive(mgmtIeQueue, &mgmtEvent, 0) == pdTRUE) {
      evaluateMgmtIeEvent(mgmtEvent, millis());
      ++processed;
      progressed = true;
    }

    if (processed < MAX_EVENTS_PER_LOOP &&
        xQueueReceive(headerQueue, &headerEvent, 0) == pdTRUE) {
      evaluateHeaderEvent(headerEvent, millis());
      ++processed;
      progressed = true;
    }

    if (processed < MAX_EVENTS_PER_LOOP &&
        xQueueReceive(bleQueue, &bleEvent, 0) == pdTRUE) {
      evaluateBleEvent(bleEvent, millis());
      ++processed;
      progressed = true;
    }

    if (!progressed) break;
  }

  // Mark log: drain all pending observations (RAM table lookup, cheap)
  static Observation obsEvent;
  while (xQueueReceive(obsQueue, &obsEvent, 0) == pdTRUE) {
    recentNote(obsEvent, millis());
  }

  now = millis();

  // 4. Presence tracking, display lead selection, proximity heartbeat
  alertTick(now);
  audioTick(now);

  // 5. Channel hopping state machine (not while joined to home Wi-Fi)
  static uint32_t lastHopTime = 0;
  if (!maintActive && timeReached(now, lastHopTime + channelHopMs)) {
    const size_t nextIndex = (channelIndex + 1) % NUM_SCAN_CHANNELS;
    const uint8_t nextChannel = SCAN_CHANNELS[nextIndex];

    if (esp_wifi_set_channel(nextChannel, WIFI_SECOND_CHAN_NONE) == ESP_OK) {
      channelIndex   = nextIndex;
      currentChannel = nextChannel;
      lastHopTime    = now;
    } else {
      ++channelErrors;
      lastHopTime = now - channelHopMs + CHANNEL_HOP_RETRY_MS;
    }
  }

  if (!maintActive) wifiWatchdog(now);
#if ENABLE_BLE_SCAN
  bleWatchdog(now);
#endif
}

void loop() {
  serviceCore();
  const uint32_t now = millis();

#if ENABLE_MARK_LOG
  markButtonTick(now);
  sampleTick(now);
#endif

  // 6. Display state machine
  static uint32_t lastDisplayTime = 0;
  static bool previousInAlert = false;

  bool noticeChanged = false;
  if (noticeActive && timeReached(now, noticeUntil)) {
    noticeActive  = false;
    noticeChanged = true;
  }
  static bool previousNotice = false;
  if (noticeActive != previousNotice || noticeDirty) {
    previousNotice = noticeActive;
    noticeDirty    = false;
    noticeChanged  = true;
  }

  const bool inAlert = alertView.active;
  const bool alertStateChanged = (inAlert != previousInAlert) || noticeChanged;
  const uint32_t refreshCadence = inAlert ? DISPLAY_ALERT_REFRESH_MS : DISPLAY_IDLE_REFRESH_MS;

  if (alertStateChanged || timeReached(now, lastDisplayTime + refreshCadence)) {
    lastDisplayTime = now;
    refreshDisplay(!inAlert || alertStateChanged);
    previousInAlert = inAlert;
  }

#if WEB_UI
  maintTick(millis());
#endif
#if HOME_SYNC
  homeTick(millis());
#endif

  // 7. Serial telemetry & scheduling
  processSerialCommands();
  ledTestTick(millis());
  outputDiagnostics(millis());   // fresh: 'l' / 'j' can run for seconds above
  delay(1);
}
