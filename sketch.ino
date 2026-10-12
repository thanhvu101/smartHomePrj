#include <DHT.h>
#include <WiFi.h>
#include <ESP32Servo.h>
#include "PubSubClient.h"

const char * MQTTServer = "broker.hivemq.com";
const char * MQTT_Topic = "/emohtrams9191/SMART_HOME";
const char * MQTT_ID = "";
int Port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

#define DHT_TYPE DHT22
#define DHT_PIN 15
#define BUTTON_PIN 25
#define TRIG_PIN  27
#define ECHO_PIN  26

DHT dht(DHT_PIN, DHT_TYPE);
Servo servo, servo1, servo2, servo3, servo4;

int ledtv = 23, ldr = 2, pir = 4, leddo = 18, ledvang = 19, ledvang2 = 5, ac = 21, coibao = 22, cuanha = 32, rem = 33, quat = 12, maybom = 13;
bool mqttPumpState = false, sensorPumpState = false;

// MQTT reconnect function.
void MQTT_Reconnect() {
  while (!client.connected()) {
    Serial.println("Đang kết nối MQTT");
    if (client.connect(MQTT_ID)) {
      Serial.print("MQTT Topic kết nối thành công");
      client.subscribe(MQTT_Topic);
      Serial.println("");
    } else {
      Serial.print("Kết nối thất bại, rc=");
      Serial.print(client.state());
      Serial.println(", kết nối lại trong 5s");
      delay(5000);
    }
  }
}


void setup() {
  Serial.begin(115200);
  Serial.print("Connecting to WiFi");
  WiFi.begin("Wokwi-GUEST", "", 6);
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    Serial.print(".");
  }
  Serial.println(" Connected!");
  dht.begin();
  pinMode(ldr, INPUT);
  pinMode(pir, INPUT);
  pinMode(ledvang2, OUTPUT);
  pinMode(leddo, OUTPUT);
  pinMode(ledvang, OUTPUT);
  pinMode(ledtv, OUTPUT);
  pinMode(coibao, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  servo.attach(cuanha, 500, 2400);
  servo1.attach(rem, 500, 2400);
  servo2.attach(ac, 500, 2400);
  servo3.attach(quat, 500, 2400);
  servo4.attach(maybom, 500, 2400);

  client.setServer(MQTTServer, Port);
  client.setCallback(callback);
}

void callback(char* topic, byte* message, unsigned int length) {
  String stMessage;
  for (int i = 0; i < length; i++) {
    stMessage += (char)message[i];
  }
  int mV = stMessage.toInt();
  if (mV == 2) {
    digitalWrite(ledvang, HIGH);
    Serial.println("Bật đèn phòng khách");
  } else if (mV == -2) {
    digitalWrite(ledvang, LOW);
    Serial.println("Tắt đèn phòng khách");
  } else if (mV == 5) {
    digitalWrite(ledvang2, HIGH);
    Serial.println("Bật đèn phòng ngủ");
  } else if (mV == -5) {
    digitalWrite(ledvang2, LOW);
    Serial.println("Tắt đèn phòng ngủ");
  } else if (mV == 3) {
    servo2.write(180);
    Serial.println("Bật điều hòa");
  } else if (mV == -3) {
    servo2.write(0);
    Serial.println("Tắt điều hòa");
  } else if (mV == 4) {
    servo.write(180);
    Serial.println("Mở cửa");
  } else if (mV == -4) {
    servo.write(0);
    Serial.println("Đóng cửa");
  } else if (mV == 1) {
    digitalWrite(ledtv, HIGH);
    Serial.println("Bật TV");
  } else if (mV == -1) {
    digitalWrite(ledtv, LOW);
    Serial.println("Tắt TV");
  } else if (mV == 6) {
    mqttPumpState = true;
  } else if (mV == -6) {
    mqttPumpState = false;
  }
}

void nhietdo() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  static float prevTemp = 0.0, prevHum = 0.0;
  if (t != prevTemp || h != prevHum) {
    Serial.println("Nhiệt độ: " + String(t) + "°C");
    Serial.println("Độ ẩm: " + String(h) + "%");
    if (t > 50) {
      digitalWrite(coibao, HIGH);
      tone(coibao, 262, 250);
      Serial.println("Cảnh báo cháy!");
    }
    else digitalWrite(coibao, LOW);
    if (t > 28) {
      servo3.write(180);
      Serial.println("Quạt bật");
    }
    else {
      servo3.write(0);
      Serial.println("Quạt tắt");
    }
    char temperatureString[8], humidityString[8];
    dtostrf(t, 1, 2, temperatureString);
    dtostrf(h, 1, 2, humidityString);
    client.publish("/emohtrams9191/temp", temperatureString);
    client.publish("/emohtrams9191/humidity", humidityString);
    prevTemp = t;
    prevHum = h;
  }
}

void chuyendong() {
  static int prevVal = LOW;
  int val = digitalRead(pir);
  if (val != prevVal) {
    if (val == HIGH) {
      digitalWrite(leddo, HIGH);
      Serial.println("Đèn nhà vệ sinh bật");
    } else {
      digitalWrite(leddo, LOW);
      Serial.println("Đèn nhà vệ sinh tắt");
    }
    prevVal = val;
  }
}

void anhsang() {
  static bool prevAnhsang = LOW;
  int anhsang = digitalRead(ldr);
  if (anhsang != prevAnhsang) {
    if (anhsang == HIGH) {
      servo1.write(0);
      client.publish(MQTT_Topic, "-7");
      Serial.println("Rèm đóng");
    } else {
      servo1.write(180);
      client.publish(MQTT_Topic, "7");
      Serial.println("Rèm mở");
    }
    prevAnhsang = anhsang;
  }
}

void handleButton() {
  int cambienvantay = digitalRead(BUTTON_PIN);
  if (cambienvantay == LOW) { // Nếu nhận được vân tay
    servo.write(180); // Mở cửa
    Serial.println("Nhận diện thành công");
    Serial.println("Mở cửa");
    client.publish(MQTT_Topic, "4");
    delay(5000); // giữ trạng thái 5s
    servo.write(0); // Đóng cửa
    Serial.println("Đóng cửa");
    client.publish(MQTT_Topic, "-4");
  }
  delay(1000);
}


void controlPump() {
  static bool prevPumpState = false;
  bool currentPumpState = (mqttPumpState && sensorPumpState);
  if (currentPumpState != prevPumpState) {
    if (currentPumpState) {
      servo4.write(180);
      client.publish(MQTT_Topic, "6");
      Serial.println("Bơm nước bật");
    } else {
      servo4.write(0);
      client.publish(MQTT_Topic, "-6");
      Serial.println("Bơm nước tắt");
    }
    prevPumpState = currentPumpState;
  }
  if (mqttPumpState && !sensorPumpState) {
    client.publish(MQTT_Topic, "-6");
  }
}

void kiemtrakhoangcach() {
  int duration, distance;
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  duration = pulseIn(ECHO_PIN, HIGH);
  distance = duration * 0.0344 / 2;
  static bool prevSensorPumpState = false;
  bool currentSensorPumpState = (distance > 150);
  if (currentSensorPumpState != prevSensorPumpState) {
    sensorPumpState = currentSensorPumpState;
    prevSensorPumpState = currentSensorPumpState;
    if (sensorPumpState) {
      Serial.println("Cạn nước");
    } else {
      Serial.println("Đủ nước");
    }
  }
}

void loop() {
  if (!client.connected()) {
    MQTT_Reconnect();
  }
  client.loop();
  nhietdo();
  anhsang();
  chuyendong();
  kiemtrakhoangcach();
  handleButton();
  controlPump();
  delay(10);
}
