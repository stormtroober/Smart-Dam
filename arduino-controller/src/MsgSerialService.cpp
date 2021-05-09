#include "Arduino.h"
#include <MsgSerialService.h>

String content;
StaticJsonDocument <128>doc;
MsgSerialService msgSerialService;
float distance;
bool test;

void MsgSerialService::init(){
  test = false;
  Serial.begin(9600);
  content.reserve(256);
  content = "";
}

void MsgSerialService::sendMsg(const String msg){
  if(test == false){
    digitalWrite(12, true);
    test = true;
  }
  else{
    digitalWrite(12, false);
    test = false;
  }
  Serial.println(msg);
  Serial.flush();
}

float MsgSerialService::getDistance(){
  return distance;
}

Event* MsgSerialService::eventGenerator(const String msg){
  int size = msg.length();
  char msgChar[size + 1];
  strcpy(msgChar, msg.c_str());
  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, msgChar);
  if (error) { Serial.println(error.f_str()); }

  const int state = doc["state"];
  const int damOpening = doc["damOpening"];
  distance = doc["distance"];
  doc.clear();
  Event* ev;


  switch(state){
    case S_NORMAL:
      ev = new NormalEvent();
      return ev;
      //case break;
    case S_PREALARM:
      ev = new PreAlarmEvent(0, distance);
      return ev;
      //break;
    case S_ALARM:
      ev = new AlarmEvent(damOpening);
      return ev;
      //break;
  }

  return NULL;
}

void serialEvent() {
  /* reading the content */
  while (Serial.available()) {
    noInterrupts();
    char ch = (char) Serial.read();
    interrupts();
    if (ch == '\n'){
      msgSerialService.generateEvent(msgSerialService.eventGenerator(content));
      content = "";
    } else {
      content += ch;      
    }
  }
}
