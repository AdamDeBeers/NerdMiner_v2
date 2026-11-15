// Adam: Home Assistant MQTT telemetry for NerdMiner.
// Publishes miner stats as JSON to nerdminer/<id>/state and announces the
// sensors via Home Assistant MQTT discovery, so the device appears in HA by itself.
// Identity is the chip MAC (stable and unique per device): <id> = nerdminer_<mac>.
// The worker name after the dot in the BTC address (bc1q....nerd1 -> nerd1) is only
// the display name, so renaming a worker or reusing a name never mixes up devices.

#ifdef ADAM_MQTT

#include <Arduino.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFiManager.h>

#include "adam_mqtt.h"
#include "drivers/storage/storage.h"
#include "version.h"

#ifndef ADAM_BOARD_NAME
#define ADAM_BOARD_NAME "NerdMiner_v2"
#endif
#ifndef ADAM_MQTT_PERIOD_MS
#define ADAM_MQTT_PERIOD_MS 15000
#endif

#define MQTT_FILE "/mqtt.json"

// Mining statistics (defined in mining.cpp)
extern TSettings Settings;
extern uint32_t templates;
extern uint32_t hashes;
extern uint32_t Mhashes;
extern uint64_t upTime;
extern volatile uint32_t shares;
extern volatile uint32_t valids;
extern double best_diff;

static char mqHost[64] = "";
static char mqPort[6] = "1883";
static char mqUser[40] = "";
static char mqPass[64] = "";

static WiFiManagerParameter *pHtml = nullptr;
static WiFiManagerParameter *pHost = nullptr;
static WiFiManagerParameter *pPort = nullptr;
static WiFiManagerParameter *pUser = nullptr;
static WiFiManagerParameter *pPass = nullptr;

static WiFiClient net;
static PubSubClient mq(net);
static String devName;   // display name, e.g. nerd1
static String devId;     // stable id from MAC, e.g. nerdminer_a1b2c3d4e5f6
static String devMac;    // e.g. a1:b2:c3:d4:e5:f6
static String baseTopic; // e.g. nerdminer/nerdminer_a1b2c3d4e5f6

static char jsonBuf[1024];

// ---------- config ----------

static void loadCfg()
{
    if (!SPIFFS.begin(false)) return;
    if (!SPIFFS.exists(MQTT_FILE)) return;
    File f = SPIFFS.open(MQTT_FILE, "r");
    if (!f) return;
    StaticJsonDocument<384> j;
    if (!deserializeJson(j, f))
    {
        strlcpy(mqHost, j["host"] | "", sizeof(mqHost));
        strlcpy(mqPort, j["port"] | "1883", sizeof(mqPort));
        strlcpy(mqUser, j["user"] | "", sizeof(mqUser));
        strlcpy(mqPass, j["pass"] | "", sizeof(mqPass));
    }
    f.close();
}

static void saveCfg()
{
    if (!SPIFFS.begin(false)) return;
    File f = SPIFFS.open(MQTT_FILE, "w");
    if (!f)
    {
        Serial.println("MQTT: failed to save config");
        return;
    }
    StaticJsonDocument<384> j;
    j["host"] = mqHost;
    j["port"] = mqPort;
    j["user"] = mqUser;
    j["pass"] = mqPass;
    serializeJson(j, f);
    f.close();
    Serial.printf("MQTT: config saved (host '%s', port %s)\n", mqHost, mqPort);
}

static void copyTrimmed(char *dst, const char *src, size_t len)
{
    String s(src);
    s.trim();
    strlcpy(dst, s.c_str(), len);
}

void adamMqttAddParams(WiFiManager &wm)
{
    loadCfg();
    if (pHost == nullptr)
    {
        pHtml = new WiFiManagerParameter("<hr><br><label style=\"font-weight: bold;margin-bottom: 25px;display: inline-block;\">Home Assistant MQTT (empty broker = off)</label>");
        pHost = new WiFiManagerParameter("mqttHost", "MQTT broker IP", mqHost, 63);
        pPort = new WiFiManagerParameter("mqttPort", "MQTT port", mqPort, 5);
        pUser = new WiFiManagerParameter("mqttUser", "MQTT user", mqUser, 39);
        pPass = new WiFiManagerParameter("mqttPass", "MQTT password", mqPass, 63, "type=\"password\"");
    }
    wm.addParameter(pHtml);
    wm.addParameter(pHost);
    wm.addParameter(pPort);
    wm.addParameter(pUser);
    wm.addParameter(pPass);
}

void adamMqttSaveParams()
{
    if (pHost == nullptr) return;
    copyTrimmed(mqHost, pHost->getValue(), sizeof(mqHost));
    copyTrimmed(mqPort, pPort->getValue(), sizeof(mqPort));
    copyTrimmed(mqUser, pUser->getValue(), sizeof(mqUser));
    strlcpy(mqPass, pPass->getValue(), sizeof(mqPass));
    if (mqPort[0] == '\0') strlcpy(mqPort, "1883", sizeof(mqPort));
    saveCfg();
}

// ---------- ids ----------

static String cleanId(const String &in)
{
    String out;
    for (size_t i = 0; i < in.length(); i++)
    {
        char c = in[i];
        if (isalnum((unsigned char)c) || c == '_' || c == '-') out += (char)tolower((unsigned char)c);
    }
    return out;
}

static void makeIds()
{
    uint8_t m[6];
    WiFi.macAddress(m);
    char id[32];
    snprintf(id, sizeof(id), "nerdminer_%02x%02x%02x%02x%02x%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
    char mac[18];
    snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
    devId = id;
    devMac = mac;
    baseTopic = "nerdminer/" + devId;

    String w(Settings.BtcWallet);
    int dot = w.indexOf('.');
    if (dot > 0 && dot < (int)w.length() - 1) devName = cleanId(w.substring(dot + 1));
    if (devName.length() == 0)
    {
        char b[16];
        snprintf(b, sizeof(b), "%02x%02x%02x", m[3], m[4], m[5]);
        devName = b;
    }
}

// ---------- Home Assistant discovery ----------

struct SensorDef
{
    const char *key;
    const char *name;
    const char *unit;
    const char *dclass;
    const char *sclass;
    const char *icon;
};

static const SensorDef sensors[] = {
    {"hashrate",  "Hashrate",         "kH/s", nullptr,           "measurement",      "mdi:pickaxe"},
    {"temp",      "Chip temperature", "°C",   "temperature",     "measurement",      nullptr},
    {"best_diff", "Best difficulty",  nullptr, nullptr,          "measurement",      "mdi:trophy"},
    {"shares",    "32-bit shares",    nullptr, nullptr,          "total_increasing", "mdi:counter"},
    {"valids",    "Valid blocks",     nullptr, nullptr,          "total_increasing", "mdi:bitcoin"},
    {"templates", "Block templates",  nullptr, nullptr,          "total_increasing", "mdi:file-document-outline"},
    {"total_mh",  "Total hashes",     "MH",   nullptr,           "total_increasing", "mdi:sigma"},
    {"uptime",    "Uptime",           "s",    "duration",        "total_increasing", nullptr},
    {"rssi",      "WiFi signal",      "dBm",  "signal_strength", "measurement",      nullptr},
};

static void publishDiscovery()
{
    String stateTopic = baseTopic + "/state";
    String availTopic = baseTopic + "/status";
    for (const SensorDef &s : sensors)
    {
        DynamicJsonDocument d(1024);
        d["name"] = s.name;
        d["uniq_id"] = devId + "_" + s.key;
        d["obj_id"] = "nerdminer_" + devName + "_" + s.key; // entity_id hint only, uniq_id is the identity
        d["stat_t"] = stateTopic;
        d["val_tpl"] = String("{{ value_json.") + s.key + " }}";
        d["avty_t"] = availTopic;
        if (s.unit) d["unit_of_meas"] = s.unit;
        if (s.dclass) d["dev_cla"] = s.dclass;
        if (s.sclass) d["stat_cla"] = s.sclass;
        if (s.icon) d["ic"] = s.icon;
        JsonObject dev = d.createNestedObject("dev");
        dev["ids"][0] = devId;
        JsonArray conn = dev.createNestedArray("cns");
        JsonArray macPair = conn.createNestedArray();
        macPair.add("mac");
        macPair.add(devMac);
        dev["name"] = "NerdMiner " + devName;
        dev["mdl"] = ADAM_BOARD_NAME;
        dev["mf"] = "BitMaker NerdMiner_v2 (Adam fork)";
        dev["sw"] = CURRENT_VERSION;
        size_t n = serializeJson(d, jsonBuf, sizeof(jsonBuf));
        String topic = String("homeassistant/sensor/") + devId + "/" + s.key + "/config";
        if (!mq.publish(topic.c_str(), (const uint8_t *)jsonBuf, n, true))
            Serial.printf("MQTT: discovery publish failed for %s\n", s.key);
    }
}

static bool connectMqtt()
{
    String availTopic = baseTopic + "/status";
    mq.setServer(mqHost, atoi(mqPort));
    mq.setBufferSize(sizeof(jsonBuf));
    mq.setKeepAlive(30);
    bool ok;
    if (mqUser[0] != '\0')
        ok = mq.connect(devId.c_str(), mqUser, mqPass, availTopic.c_str(), 0, true, "offline");
    else
        ok = mq.connect(devId.c_str(), availTopic.c_str(), 0, true, "offline");
    if (ok)
    {
        mq.publish(availTopic.c_str(), "online", true);
        publishDiscovery();
    }
    return ok;
}

static void publishState(double rateKHs)
{
    StaticJsonDocument<384> d;
    d["hashrate"] = round(rateKHs * 10.0) / 10.0;
    d["temp"] = round(temperatureRead() * 10.0) / 10.0;
    d["best_diff"] = best_diff;
    d["shares"] = (uint32_t)shares;
    d["valids"] = (uint32_t)valids;
    d["templates"] = templates;
    d["total_mh"] = Mhashes;
    d["uptime"] = (uint32_t)upTime;
    d["rssi"] = WiFi.RSSI();
    size_t n = serializeJson(d, jsonBuf, sizeof(jsonBuf));
    String topic = baseTopic + "/state";
    mq.publish(topic.c_str(), (const uint8_t *)jsonBuf, n, false);
}

static void mqttTask(void *)
{
    makeIds();
    Serial.printf("MQTT: device %s (%s) -> broker %s:%s\n", devId.c_str(), devName.c_str(), mqHost, mqPort);

    uint32_t lastKH = Mhashes * 1000 + hashes / 1000;
    unsigned long lastMs = millis();
    unsigned long lastTry = 0;
    bool first = true;

    for (;;)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            if (!mq.connected())
            {
                if (first || millis() - lastTry > 10000)
                {
                    first = false;
                    lastTry = millis();
                    if (connectMqtt())
                        Serial.println("MQTT: connected, discovery sent");
                    else
                        Serial.printf("MQTT: connect failed, rc=%d (retry in 10 s)\n", mq.state());
                }
            }
            else
            {
                mq.loop();
                unsigned long now = millis();
                if (now - lastMs >= ADAM_MQTT_PERIOD_MS)
                {
                    uint32_t kh = Mhashes * 1000 + hashes / 1000;
                    double rate = (double)(kh - lastKH) * 1000.0 / (double)(now - lastMs);
                    lastKH = kh;
                    lastMs = now;
                    publishState(rate);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void adamMqttStart()
{
    loadCfg();
    if (mqHost[0] == '\0')
    {
        Serial.println("MQTT: no broker set, MQTT off");
        return;
    }
    xTaskCreatePinnedToCore(mqttTask, "MQTT", 6144, NULL, 1, NULL, 1);
}

#endif // ADAM_MQTT
