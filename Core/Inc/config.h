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

#define VERSION_MANTIS "1.0"

//#define FARM "gateway-agriconnect"
#define FARM "demox"

#define a7672s 1  // 4G
#define a7670c 2  // 4G
#define a7670sa 3 // 4G
#define a7080 4  // NB-IOT
#define a7680 5  // 4G

#define SIMCOM_MODEL a7672s // #default is a7670 if you use model other please choose enter your model
#define SAVE_LOAD false
#define INTERVAL_PUPLISH_DATA 10 // the time the device sends data to the server
#define CHECK_TIME_AFTER_MQTT_RESTART 50  //UNITS IN SECONDS
#define NUMBER_LOADS 10

// Serial number. Must be lower case.
#ifndef SERIAL_NUMBER
  #define SERIAL_NUMBER "sw000100"
#endif

#define MQTT_USER "node" 		// User - connect to MQTT broker
#define MQTT_PASS "654321"		// Password - connect to MQTT broker

//#define MQTT_USER "mqttnode"       // User - connect to MQTT broker
//#define MQTT_PASS "congamo"		// Password - connect to MQTT broker

#define MQTT_TOPIC_ACTUATOR_STATUS FARM "/sn/" SERIAL_NUMBER "/as/"
// MQTT topic to subscribe and get command to switch on/off actuator
#define MQTT_TOPIC_ACTUATOR_CONTROL FARM "/snac/" SERIAL_NUMBER "/"
/** MQTT
 * Global broker: mqtt.agriconnect.vn
 */
#define MQTT_HOST "tcp://mqtt.agriconnect.vn"           		// MQTT broker

#define MQTT_CLIENT_ID  SERIAL_NUMBER
#define MQTT_PORT 1883


#endif /* INC_CONFIG_H_ */
