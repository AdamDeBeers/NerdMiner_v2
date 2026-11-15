#ifndef _ADAM_MQTT_H_
#define _ADAM_MQTT_H_

// Adam: Home Assistant MQTT telemetry for NerdMiner (enabled with -D ADAM_MQTT=1).
// Broker settings are entered in the NerdMinerAP portal and stored in /mqtt.json.
// Empty broker host = MQTT off.

#ifdef ADAM_MQTT
#include <WiFiManager.h>

void adamMqttAddParams(WiFiManager &wm); // add portal fields (loads /mqtt.json first)
void adamMqttSaveParams();               // read portal fields and save /mqtt.json
void adamMqttStart();                    // start the MQTT task (after WiFi is up)
#endif

#endif // _ADAM_MQTT_H_
