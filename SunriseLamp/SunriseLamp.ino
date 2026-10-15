#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <time.h>
#include <Adafruit_NeoPixel.h>

// ====== EDIT THESE TWO LINES ======
const char* WIFI_SSID     = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
// ==================================

#define LED_PIN           4     // ESP32 pin wired to the strip's data wire (through the 330 ohm resistor)
#define LED_COUNT         80    // how many LEDs the lamp uses (80 = about 1.35 m along a full-size headboard)
#define BRIGHTNESS        120   // brightness cap, 0-255 (120 is about 47%)
#define BUTTON_PIN        13    // button wired between this pin and GND
#define AUTO_OFF_MINUTES  30    // how long the lamp stays at full brightness before shutting off

const char* HOSTNAME = "lamp";  // the lamp's page will be at http://lamp.local

// Central time, with daylight saving
const char* TIMEZONE = "CST6CDT,M3.2.0,M11.1.0";

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
WebServer server(80);
Preferences prefs;   // saved settings that survive power loss

// ----- saved settings -----
int alarmHour = 7;
int alarmMinute = 0;
bool alarmEnabled = false;
uint32_t alarmColor = 0xFF8000;   // default color: warm orange
bool sunriseEnabled = false;      // gradual brightening before the alarm time
int rampMinutes = 15;             // how long the sunrise lasts
bool autoOffEnabled = true;       // shut the lamp off automatically

// ----- live state (not saved) -----
bool lampOn = false;              // true while the lamp is lit or brightening
bool ramping = false;             // true during a sunrise
bool atFull = false;              // true once the lamp reaches full brightness
unsigned long rampStartMs = 0;
unsigned long rampDurationMs = 0;
unsigned long fullSinceMs = 0;    // when the lamp reached full brightness
int lastTriggerDay = -1;          // stops the alarm from re-triggering after you turn it off
unsigned long lastClockCheck = 0;
unsigned long lastRampUpdate = 0;
unsigned long lastWifiCheck = 0;
unsigned long lastButtonPress = 0;

// ---------- the web page (HTML + CSS + a little JavaScript) ----------
// words between % signs get swapped for real values in handleRoot()
const char PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Wake-Up Lamp</title>
<link href='https://fonts.googleapis.com/css2?family=Quicksand:wght@500;700&display=swap' rel='stylesheet'>
<style>
  * { box-sizing: border-box; }
  body {
    margin: 0;
    padding: 24px 16px 40px;
    min-height: 100vh;
    font-family: 'Quicksand', 'Nunito', 'Segoe UI', system-ui, sans-serif;
    color: #f1e3ff;
    background: linear-gradient(160deg, #2b1747 0%, #4a2670 60%, #6b2f7a 100%);
  }
  .moon { display: block; margin: 0 auto; }
  h1 { text-align: center; margin: 6px 0 4px; font-size: 1.9em; color: #ff9fd2; }
  .sub { text-align: center; margin: 0 0 20px; color: #c9aee6; font-size: 0.95em; }
  .card {
    max-width: 380px;
    margin: 0 auto 16px;
    padding: 22px;
    background: rgba(255, 255, 255, 0.09);
    border: 2px solid rgba(255, 160, 220, 0.25);
    border-radius: 30px;
    box-shadow: 0 10px 28px rgba(20, 5, 40, 0.35);
  }
  .row { display: flex; justify-content: space-between; align-items: center; margin: 6px 0; }
  .pill { padding: 6px 16px; border-radius: 999px; font-weight: 700; font-size: 0.95em; }
  .pill.on    { background: #ff9fd2; color: #3a1250; }
  .pill.off   { background: rgba(255, 255, 255, 0.15); color: #d9c3ee; }
  .pill.clock { background: #5b2f85; color: #ffd1ea; }
  label.title { display: block; margin: 16px 0 8px; font-weight: 700; color: #ff9fd2; }

  /* the border lives on this wrapper, which clips the time input so it can't overflow the card */
  .timebox {
    display: flex;
    align-items: center;
    justify-content: center;
    width: 100%;
    height: 66px;
    overflow: hidden;
    background: #3a2160;
    border: 3px solid #ff9fd2;
    border-radius: 22px;
  }
  .timebox input[type=time] {
    -webkit-appearance: none;
    appearance: none;
    display: block;
    width: 100%;
    min-width: 0;
    max-width: 100%;
    height: auto;
    margin: 0;
    padding: 0 8px;
    font-family: inherit;
    font-size: 1.5em;
    line-height: 1.2;
    text-align: center;
    color: #ffe3f4;
    color-scheme: dark;
    background: transparent;
    border: 0;
  }
  .timebox input[type=time]::-webkit-date-and-time-value {
    text-align: center;
    min-width: 0;
    margin: 0 auto;
    line-height: 1.2;
  }

  select {
    display: block;
    width: 100%;
    max-width: 100%;
    min-width: 0;
    margin: 0;
    padding: 12px;
    font-size: 1.1em;
    font-family: inherit;
    text-align: center;
    color: #ffe3f4;
    color-scheme: dark;
    background: #3a2160;
    border: 3px solid #ff9fd2;
    border-radius: 22px;
  }
  input[type=color] {
    width: 100%;
    height: 58px;
    padding: 4px;
    background: #3a2160;
    border: 3px solid #ff9fd2;
    border-radius: 22px;
    cursor: pointer;
  }
  .switch { display: flex; justify-content: space-between; align-items: center; margin: 18px 0 6px; font-weight: 700; color: #ff9fd2; }
  .switch input {
    -webkit-appearance: none; appearance: none;
    position: relative;
    flex: none;
    width: 58px; height: 32px;
    border-radius: 32px;
    background: #5a3e82;
    transition: background 0.2s;
    cursor: pointer;
  }
  .switch input::after {
    content: '';
    position: absolute;
    top: 3px; left: 3px;
    width: 26px; height: 26px;
    border-radius: 50%;
    background: #fff;
    box-shadow: 0 2px 5px rgba(0, 0, 0, 0.3);
    transition: left 0.2s;
  }
  .switch input:checked { background: #ff8cc8; }
  .switch input:checked::after { left: 29px; }
  .btn {
    display: block;
    width: 100%;
    margin-top: 14px;
    padding: 15px;
    border: none;
    border-radius: 999px;
    font-family: inherit;
    font-size: 1.1em;
    font-weight: 700;
    color: #fff;
    cursor: pointer;
    box-shadow: 0 6px 0 rgba(15, 0, 30, 0.35);
  }
  .btn:active { transform: translateY(3px); box-shadow: 0 3px 0 rgba(15, 0, 30, 0.35); }
  .pink { background: linear-gradient(#ff9ac9, #ff74b5); }
  .lav  { background: linear-gradient(#b99cff, #9a78f5); }
  .plum { background: linear-gradient(#8a6cc4, #6f52a8); }
  .toast {
    position: fixed;
    top: 14px;
    left: 50%;
    transform: translateX(-50%);
    z-index: 10;
    padding: 12px 26px;
    text-align: center;
    font-weight: 700;
    color: #5a1a4a;
    background: #ffd1ea;
    border-radius: 999px;
    box-shadow: 0 6px 18px rgba(20, 5, 40, 0.4);
  }
  .note { font-size: 0.85em; color: #c9aee6; margin: 4px 0 0; }
</style>
</head>
<body>
  <div class='toast' id='toast' style='display:none'></div>
  <svg class='moon' viewBox='0 0 100 100' width='84' height='84'>
    <path d='M70 15.4 A40 40 0 1 0 70 84.6 A40 40 0 0 1 70 15.4 Z' fill='#ffe9a8'/>
    <path d='M19 47 Q24 52 29 47' stroke='#8a5a3a' stroke-width='3' fill='none' stroke-linecap='round'/>
    <circle cx='19' cy='58' r='4.5' fill='#ffb3c7'/>
    <path d='M24 61 Q27 64 30 61' stroke='#8a5a3a' stroke-width='2.5' fill='none' stroke-linecap='round'/>
    <circle cx='84' cy='22' r='2.5' fill='#ffe9a8'/>
    <circle cx='91' cy='46' r='1.8' fill='#ffe9a8'/>
    <circle cx='80' cy='78' r='2' fill='#ffe9a8'/>
  </svg>
  <h1>Wake-Up Lamp</h1>
  <p class='sub'>soft light for sleepy mornings</p>
  <div class='card'>
    <div class='row'><span>Lamp clock</span><span class='pill clock' id='clock'>%NOW%</span></div>
    <div class='row'><span>Lamp is</span><span class='pill %STATECLASS%' id='state'>%STATE%</span></div>
  </div>
  <div class='card'>
    <form action='/save'>
      <label class='title'>&#9200; Alarm time</label>
      <div class='timebox'><input type='time' name='time' value='%TIME%'></div>
      <label class='title'>&#127912; Wake-up color</label>
      <input type='color' name='color' value='%COLOR%'>
      <div class='switch'><span>Alarm enabled</span><input type='checkbox' name='on' %CHECKED%></div>
      <div class='switch'><span>&#127749; Gradual sunrise</span><input type='checkbox' name='sun' %SUNCHECKED%></div>
      <label class='title'>Sunrise length</label>
      <select name='ramp'>%RAMPOPTS%</select>
      <p class='note'>The lamp starts brightening this long before the alarm time and is fully lit at the alarm time.</p>
      <div class='switch'><span>&#127769; Auto shut-off (30 min)</span><input type='checkbox' name='auto' %AUTOCHECKED%></div>
      <button class='btn pink'>Save</button>
    </form>
  </div>
  <div class='card'>
    <form action='/test'><button class='btn lav'>Test lamp now</button></form>
    <form action='/preview'><button class='btn lav'>Preview sunrise (20 sec)</button></form>
    <form action='/off'><button class='btn plum'>Turn lamp off</button></form>
  </div>
<script>
  // update the clock and "Lamp is" pills without reloading the page
  function setStatus(d) {
    document.getElementById('clock').textContent = d.now;
    var s = document.getElementById('state');
    s.textContent = d.state;
    s.className = 'pill ' + (d.on ? 'on' : 'off');
  }
  function refresh() {
    fetch('/status').then(function (r) { return r.json(); }).then(setStatus).catch(function () {});
  }
  function toast(msg) {
    var t = document.getElementById('toast');
    t.textContent = msg;
    t.style.display = 'block';
    clearTimeout(window._tt);
    window._tt = setTimeout(function () { t.style.display = 'none'; }, 2000);
  }
  // send every button press in the background instead of loading a new page
  document.querySelectorAll('form').forEach(function (f) {
    f.addEventListener('submit', function (e) {
      e.preventDefault();
      var p = new URLSearchParams();
      new FormData(f).forEach(function (v, k) { p.append(k, v); });
      p.append('ajax', '1');
      var action = f.getAttribute('action');
      fetch(action + '?' + p.toString()).then(function () {
        if (action === '/save') toast('Saved!');
        refresh();
      }).catch(function () { toast('Could not reach the lamp'); });
    });
  });
  setInterval(refresh, 3000);
</script>
</body>
</html>
)rawliteral";

// ---------- lamp control ----------
void applyBrightness(int b) {
  strip.setBrightness(b);
  strip.fill(alarmColor);
  strip.show();
}

void lampFull() {                       // lit at full brightness right away
  lampOn = true;
  ramping = false;
  atFull = true;
  fullSinceMs = millis();
  applyBrightness(BRIGHTNESS);
}

void startRamp(unsigned long durationMs) {   // slow brightening up to full
  lampOn = true;
  ramping = true;
  atFull = false;
  rampStartMs = millis();
  rampDurationMs = durationMs;
  applyBrightness(1);
}

void lampOff() {
  lampOn = false;
  ramping = false;
  atFull = false;
  strip.clear();
  strip.show();
}

// ---------- web page handlers ----------
void redirectTo(const char* where) {
  server.sendHeader("Location", where);
  server.send(303);
}

// background requests get an empty reply; a normal page visit gets sent back to the main page
void finish() {
  if (server.hasArg("ajax")) server.send(204);
  else redirectTo("/");
}

String clockText() {
  String nowText = "syncing...";
  struct tm t;
  if (getLocalTime(&t, 10)) {
    char buf[16];
    strftime(buf, sizeof(buf), "%I:%M %p", &t);
    nowText = buf;
  }
  return nowText;
}

String stateText() {
  return !lampOn ? "off" : (ramping ? "brightening" : "ON");
}

void handleStatus() {
  String json = "{\"now\":\"" + clockText() + "\",\"state\":\"" + stateText() + "\",\"on\":" + (lampOn ? "true" : "false") + "}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

void handleRoot() {
  char timeVal[6];
  snprintf(timeVal, sizeof(timeVal), "%02d:%02d", alarmHour, alarmMinute);
  char colorVal[8];
  snprintf(colorVal, sizeof(colorVal), "#%06X", (unsigned int)alarmColor);

  // sunrise length choices
  String rampOpts = "";
  const int choices[] = {5, 10, 15, 20, 30};
  for (int i = 0; i < 5; i++) {
    rampOpts += "<option value='" + String(choices[i]) + "'";
    if (choices[i] == rampMinutes) rampOpts += " selected";
    rampOpts += ">" + String(choices[i]) + " minutes</option>";
  }

  String html = PAGE_HTML;
  html.replace("%NOW%", clockText());
  html.replace("%STATE%", stateText());
  html.replace("%STATECLASS%", lampOn ? "on" : "off");
  html.replace("%TIME%", timeVal);
  html.replace("%COLOR%", colorVal);
  html.replace("%CHECKED%", alarmEnabled ? "checked" : "");
  html.replace("%SUNCHECKED%", sunriseEnabled ? "checked" : "");
  html.replace("%AUTOCHECKED%", autoOffEnabled ? "checked" : "");
  html.replace("%RAMPOPTS%", rampOpts);
  server.sendHeader("Cache-Control", "no-store");   // always show the newest version of the page
  server.send(200, "text/html", html);
}

void handleSave() {
  if (server.hasArg("time")) {
    String tm = server.arg("time");              // looks like "07:30"
    if (tm.length() >= 5) {
      alarmHour = tm.substring(0, 2).toInt();
      alarmMinute = tm.substring(3, 5).toInt();
    }
  }
  if (server.hasArg("color")) {
    String c = server.arg("color");              // looks like "#ff8800"
    if (c.length() == 7) {
      alarmColor = strtoul(c.c_str() + 1, NULL, 16);
    }
  }
  if (server.hasArg("ramp")) {
    rampMinutes = constrain(server.arg("ramp").toInt(), 1, 60);
  }
  alarmEnabled = server.hasArg("on");            // checkboxes are only sent when ticked
  sunriseEnabled = server.hasArg("sun");
  autoOffEnabled = server.hasArg("auto");
  lastTriggerDay = -1;

  prefs.putInt("hour", alarmHour);
  prefs.putInt("min", alarmMinute);
  prefs.putBool("on", alarmEnabled);
  prefs.putUInt("color", alarmColor);
  prefs.putBool("sun", sunriseEnabled);
  prefs.putInt("ramp", rampMinutes);
  prefs.putBool("auto", autoOffEnabled);

  if (lampOn && atFull) applyBrightness(BRIGHTNESS);   // show a new color live if the lamp is lit
  finish();
}

void handleTest() {
  lampFull();
  finish();
}

void handlePreview() {
  startRamp(20000);                               // 20-second sunrise to see how it looks
  finish();
}

void handleOff() {
  lampOff();
  finish();
}

// ---------- setup and loop ----------
void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);   // pin reads LOW while the button is pressed

  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.clear();
  strip.show();

  // load saved settings (defaults are used the very first time)
  prefs.begin("lamp", false);
  alarmHour = prefs.getInt("hour", 7);
  alarmMinute = prefs.getInt("min", 0);
  alarmEnabled = prefs.getBool("on", false);
  alarmColor = prefs.getUInt("color", 0xFF8000);
  sunriseEnabled = prefs.getBool("sun", false);
  rampMinutes = prefs.getInt("ramp", 15);
  autoOffEnabled = prefs.getBool("auto", true);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  configTzTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");   // gets the time from the internet

  Serial.print("Connecting to Wi-Fi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("Connected! Open this on your phone: http://");
    Serial.println(WiFi.localIP());
    if (MDNS.begin(HOSTNAME)) {
      MDNS.addService("http", "tcp", 80);
      Serial.println("Or try: http://lamp.local");
    }
    strip.setPixelColor(0, strip.Color(0, 255, 0));   // first LED flashes green = connected
  } else {
    Serial.println();
    Serial.println("Wi-Fi failed. Check the name and password.");
    strip.setPixelColor(0, strip.Color(255, 0, 0));   // first LED flashes red = failed
  }
  strip.show();
  delay(1500);
  strip.clear();
  strip.show();

  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/save", handleSave);
  server.on("/test", handleTest);
  server.on("/preview", handlePreview);
  server.on("/off", handleOff);
  server.begin();
}

void loop() {
  server.handleClient();

  // physical button: press to toggle the lamp on and off
  if (digitalRead(BUTTON_PIN) == LOW && millis() - lastButtonPress > 300) {
    lastButtonPress = millis();   // the 300 ms gap ignores contact bounce
    if (lampOn) lampOff();
    else lampFull();
  }

  // sunrise: update the brightness a few times a second
  if (ramping && millis() - lastRampUpdate > 100) {
    lastRampUpdate = millis();
    unsigned long elapsed = millis() - rampStartMs;
    if (elapsed >= rampDurationMs) {
      lampFull();
    } else {
      float f = (float)elapsed / (float)rampDurationMs;
      int b = 1 + (int)((BRIGHTNESS - 1) * f * f);   // starts slow, ends brighter
      applyBrightness(b);
    }
  }

  // auto shut-off, counted from the moment the lamp reaches full brightness
  if (lampOn && atFull && autoOffEnabled &&
      millis() - fullSinceMs > (unsigned long)AUTO_OFF_MINUTES * 60000UL) {
    lampOff();
  }

  // once a second, check whether it's time to start
  if (millis() - lastClockCheck > 1000) {
    lastClockCheck = millis();
    struct tm t;
    if (getLocalTime(&t, 10)) {
      int alarmMin = alarmHour * 60 + alarmMinute;
      int leadMin = sunriseEnabled ? rampMinutes : 0;       // sunrise starts this much earlier
      int startMin = (alarmMin - leadMin + 1440) % 1440;
      int nowMin = t.tm_hour * 60 + t.tm_min;
      if (alarmEnabled && !lampOn && nowMin == startMin && t.tm_yday != lastTriggerDay) {
        lastTriggerDay = t.tm_yday;
        if (sunriseEnabled) startRamp((unsigned long)rampMinutes * 60000UL);
        else lampFull();
      }
    }
  }

  // if Wi-Fi drops, try to reconnect every 30 seconds
  if (WiFi.status() != WL_CONNECTED && millis() - lastWifiCheck > 30000) {
    lastWifiCheck = millis();
    WiFi.reconnect();
  }

  delay(2);
}
