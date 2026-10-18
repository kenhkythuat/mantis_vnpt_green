/*
 * config.h
 *
 *  Created on: AUG 1, 2024
 *      Author: thuanphat7
 */


#ifndef INC_CONFIG_H_
#define INC_CONFIG_H_

#include "stdio.h"
#include <string.h>

// Codename of the farm, where we deploy this node to.

#define VERSION_MANTIS "1.4"

/* OTA release settings. Replace the URL with the deployed HTTPS manifest. */
#define OTA_ENABLE 1
#define OTA_DEVICE_ID "mantis-stm32f103ret6"
#define OTA_MANIFEST_URL "https://raw.githubusercontent.com/kenhkythuat/mantis_vnpt_green/refs/heads/feature/connect_server_VNPT/ota/manifest.json"

//#define FARM "gateway-agriconnect"
#define FARM "demox"

#define a7672s 1  // 4G
#define a7670c 2  // 4G
#define a7670sa 3 // 4G
#define a7080 4  // NB-IOT
#define a7680 5  // 4G

#define SIMCOM_MODEL a7680 // #default is a7670 if you use model other please choose enter your model
#define SAVE_LOAD false
#define INTERVAL_PUPLISH_DATA 15 // the time the device sends data to the server
#define CHECK_TIME_AFTER_MQTT_RESTART 50  //UNITS IN SECONDS
#define NUMBER_LOADS 5
#define RX_BUFFER_UART1 (NUMBER_LOADS*150)


// Serial number. Must be lower case.
#ifndef SERIAL_NUMBER
  #define SERIAL_NUMBER "sw_000007"
#endif

// #define MQTT_USER "node" 		// User - connect to MQTT broker
// #define MQTT_PASS "654321"		// Password - connect to MQTT broker

//#define MQTT_USER "mqttnode"       // User - connect to MQTT broker
//#define MQTT_PASS "congamo"		// Password - connect to MQTT broker

#define MQTT_USER "XR8L5hdbGRjbOW9NxtSo"       // User - connect to MQTT broker
#define MQTT_PASS "65HRrDuBHAHaBJLaFYNv"		// Password - connect to MQTT broker

//#define MQTT_TOPIC_ACTUATOR_STATUS FARM "/sn/" SERIAL_NUMBER "/as/"
//// MQTT topic to subscribe and get command to switch on/off actuator
//#define MQTT_TOPIC_ACTUATOR_CONTROL FARM "/snac/" SERIAL_NUMBER "/"

#define MQTT_TOPIC_RPC_REQUEST "v1/devices/me/rpc/request/+"
#define MQTT_TOPIC_OTA_ATTRIBUTES "v1/devices/me/attributes"
#define MQTT_TOPIC_SENSOR_THINGSBOARD MQTT_TOPIC_RPC_REQUEST
#define MQTT_TOPIC_ACTUATOR_STATUS "v1/devices/me/telemetry"
// MQTT topic to subscribe and get command to switch on/off actuator
#define MQTT_TOPIC_ACTUATOR_CONTROL "v1/devices/me/telemetry"
/** MQTT
 * Global broker: mqtt.agriconnect.vn
 */
// #define MQTT_HOST "tcp://mqtt.agriconnect.vn"
// #define MQTT_CLIENT_ID SERIAL_NUMBER
// #define MQTT_PORT 1883

#define MQTT_HOST "tcp://iot-hub.vnptgreen.vn"      // MQTT broker
#define MQTT_CLIENT_ID  "ORxB1GlAS7a100JhRw2F"
#define MQTT_PORT 30188


#endif /* INC_CONFIG_H_ */
