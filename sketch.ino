#include <ESP32Servo.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <Password.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>  // Inclure la bibliothèque ArduinoJson

// Définition des broches et des périphériques
#define servoPin 15
#define LED_PIN 13
#define BUTTON_PIN 14

Servo servo;
LiquidCrystal_I2C lcd(0x27, 16, 2);

const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'},
};
byte rowPins[ROWS] = {23, 19, 18, 5};
byte colPins[COLS] = {17, 16, 4, 2};

bool isDoorLocked = true;
Password password = Password("1234");

const byte maxPasswordLength = 4; // Longueur exacte du mot de passe
byte currentPasswordLength = 0;

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// WiFi credentials
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";  // No password for Wokwi-GUEST network

// MQTT Broker information
const char* MQTT_BROKER = "test.mosquitto.org";
const int MQTT_PORT = 1883;
const char* MQTT_CLIENT_ID = "ESP32_client";
const char* TOPIC_LEDS = "home/led";

bool ledState = HIGH;      // État actuel de la LED (HIGH = éteinte, LOW = allumée)
bool lastButtonState = HIGH; // État précédent du bouton

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Fonction de gestion des messages MQTT reçus
void handleMQTTMessage(char* topic, byte* payload, unsigned int length) {
  String message = String((char*)payload);

  Serial.print("Message reçu sur le topic ");
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(message);

  // Créer un objet JSON pour analyser le message
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, message);

  if (error) {
    Serial.print("Échec de l'analyse du message JSON: ");
    Serial.println(error.f_str());
    return;
  }

  // Vérifier si la clé "on" existe dans le message
  if (doc.containsKey("on")) {
    bool on = doc["on"];
    if (on && ledState != LOW) {
      digitalWrite(LED_PIN, LOW);  // Allumer la LED (LOW = LED allumée)
      ledState = LOW;
    } else if (!on && ledState != HIGH) {
      digitalWrite(LED_PIN, HIGH);  // Éteindre la LED (HIGH = LED éteinte)
      ledState = HIGH;
    }
  }
}

// Fonction de gestion de l'appui sur le bouton
void handleButtonPress() {
  if (ledState == HIGH) {
    digitalWrite(LED_PIN, LOW);  // Allumer la LED
    mqttClient.publish(TOPIC_LEDS, "{\"on\":true}");  // Envoyer l'état "on" via MQTT
    ledState = LOW;
  } else {
    digitalWrite(LED_PIN, HIGH);  // Éteindre la LED
    mqttClient.publish(TOPIC_LEDS, "{\"on\":false}");  // Envoyer l'état "off" via MQTT
    ledState = HIGH;
  }
}

void setup() {
  // Initialisation des broches
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Initialisation du moniteur série
  Serial.begin(115200);

  // Initialisation des périphériques
  servo.attach(servoPin);
  servo.write(10); // Position initiale (porte verrouillée)
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Door LOCK System");
  delay(2000);
  lcd.clear();

  // Connexion au Wi-Fi
  connectToWiFi();

  // Connexion au broker MQTT
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(handleMQTTMessage);

  // LED éteinte par défaut
  digitalWrite(LED_PIN, HIGH);
}

void loop() {
  // Vérifier la connexion au Wi-Fi et au MQTT
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop(); // Gérer la réception des messages MQTT

  // Lecture de l'état actuel du bouton
  bool currentButtonState = digitalRead(BUTTON_PIN);

  // Détecter le changement d'état du bouton (front descendant)
  if (currentButtonState == HIGH && lastButtonState == LOW) {
    handleButtonPress();  // Appel de la fonction pour gérer l'appui du bouton
    delay(300);  // Délai pour éviter les rebonds
  }

  lastButtonState = currentButtonState; // Mise à jour de l'état précédent du bouton
  delay(50);
  // Gestion du système de verrouillage de porte
  lcd.setCursor(3, 0);
  isDoorLocked ? lcd.print("DOOR LOCKED") : lcd.print("DOOR OPEN");

  char key = keypad.getKey();
  if (key != NO_KEY) {
    delay(100);

    if (key == '#') {
      resetPassword(); // Réinitialiser le mot de passe
    } else if (key == '*') {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Enter Password:");
      resetPassword(); // Commencez une nouvelle tentative
    } else {
      processNumberKey(key); // Gérer les touches numériques
    }
  }
}

void processNumberKey(char key) {
  if (currentPasswordLength < maxPasswordLength) {
    lcd.setCursor(currentPasswordLength + 5, 1);
    lcd.print("*");
    password.append(key);
    currentPasswordLength++;

    if (currentPasswordLength == maxPasswordLength) { // Vérifiez si la longueur requise est atteinte
      if (password.evaluate()) { // Mot de passe correct
        isDoorLocked = !isDoorLocked; // Basculer l'état de la porte
        isDoorLocked ? doorLocked() : doorOpen();
      } else { // Mot de passe incorrect
        displayMessage("Wrong Password", "Try Again");
        resetPassword(); // Réinitialisez après chaque tentative
      }
    }
  }
}

void doorLocked() {
  servo.write(10); // Position verrouillée
  displayMessage("Password Correct", "LOCKED");
  resetPassword(); // Réinitialisez après verrouillage
}

void doorOpen() {
  servo.write(180); // Position déverrouillée
  displayMessage("Password Correct", "UNLOCKED");
  resetPassword(); // Réinitialisez après déverrouillage
}

void resetPassword() {
  password.reset();
  currentPasswordLength = 0;
  lcd.clear();
  lcd.setCursor(0, 0);
}

void displayMessage(const char *line1, const char *line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(3, 1);
  lcd.print(line2);
  delay(3000);
  lcd.clear();
}

// Fonction de connexion au Wi-Fi
void connectToWiFi() {
  Serial.print("Connexion au réseau Wi-Fi ");
  Serial.print(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("En attente de la connexion Wi-Fi...");
  }

  Serial.println("Connecté au Wi-Fi");
  Serial.print("Adresse IP : ");
  Serial.println(WiFi.localIP());
}

// Fonction de reconnection au broker MQTT
void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Tentative de connexion au broker MQTT...");
    if (mqttClient.connect(MQTT_CLIENT_ID)) {
      Serial.println("Connecté au broker MQTT");
      // S'abonner au topic des LEDs pour recevoir les commandes MQTT
      mqttClient.subscribe(TOPIC_LEDS);
    } else {
      Serial.print("Échec de connexion, code d'erreur : ");
      Serial.println(mqttClient.state());
      delay(5000);
    }
  }
}