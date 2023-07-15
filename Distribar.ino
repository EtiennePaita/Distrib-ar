#include <Arduino.h>
#include <ArduinoJson.h>
#if defined(ESP32)
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#endif
#include <Firebase_ESP_Client.h>

#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// Machine ID
#define UID "295f8f74-18d9-11ee-be56-0242ac120002"

#define WIFI_SSID "iPhone de Elsa"//"XXXX-5ab5"
#define WIFI_PASSWORD "azertyui"//"fnx4dpdsvtwh"

#define API_KEY "AIzaSyCcQYX68LOS7dW939v9RTejZI1I21rr5WU"
#define DATABASE_URL "https://distrib-ar-default-rtdb.europe-west1.firebasedatabase.app/" 

#define GPIO_D0 16
#define GPIO_D1 5
#define GPIO_D2 4
#define GPIO_D3 0
#define GPIO_D4 2
#define GPIO_D5 14
#define GPIO_D6 12
#define GPIO_D7 13
#define GPIO_D8 15

#define CLEAN_DELAY 60000 //in millis
#define COCKTAIL_DOSE_DELAY 20000 //in millis
#define NEW_CONFIG_DELAY 5000 //in millis
#define DEST_SIZE 50

//Define Firebase Data object
FirebaseData configFBDO;
FirebaseData cocktailsFBDO;
FirebaseData cleaningFBDO;
FirebaseData rndFBDO;

FirebaseAuth auth;
FirebaseConfig config;

char fbdoConfigPath[DEST_SIZE] = "/";
char fbdoCocktailsPath[DEST_SIZE] = "/";
char fbdoCleaningPath[DEST_SIZE] = "/";

// Distance sensor
/* Constantes pour le timeout */
const unsigned long MEASURE_TIMEOUT = 25000UL; // 25ms = ~8m à 340m/s
/* Vitesse du son dans l'air en mm/us */
const float SOUND_SPEED = 340.0 / 1000;
const int SCAN_INTERVAL_MS = 1000;
unsigned long previousScanMillis = 0;
float distance_cm = 20.0;

bool signupOK = false;
StaticJsonDocument<200> barConfig;
StaticJsonDocument<256> cocktailsQueue;
bool configurationOK = false;
bool showLed = false;
unsigned long ledOnPreviousMillis = 0;
bool cocktailOnCreation = false;

FirebaseJsonArray cocktail;
bool cocktailAvailable = false;
bool cocktailWaiting = false;
bool cleanFinished = false;

void startAssociatedPump(String data) {
  Serial.print("Turning on ... ");

  if(barConfig["gpio1"] == data) {
    digitalWrite(GPIO_D5, HIGH);
    delay(COCKTAIL_DOSE_DELAY);
    digitalWrite(GPIO_D5, LOW);
  }
  else if(barConfig["gpio2"] == data) {
    digitalWrite(GPIO_D6, HIGH);
    delay(COCKTAIL_DOSE_DELAY);
    digitalWrite(GPIO_D6, LOW);
  }
  else if(barConfig["gpio3"] == data) {
    digitalWrite(GPIO_D3, HIGH);
    delay(COCKTAIL_DOSE_DELAY);
    digitalWrite(GPIO_D3, LOW);
  }
  else if(barConfig["gpio4"] == data) {
    digitalWrite(GPIO_D2, HIGH);
    delay(COCKTAIL_DOSE_DELAY);
    digitalWrite(GPIO_D2, LOW);
  }
  else if(barConfig["gpio5"] == data) {
    digitalWrite(GPIO_D1, HIGH);
    delay(COCKTAIL_DOSE_DELAY);
    digitalWrite(GPIO_D1, LOW);
  } else {
    Serial.println("Liguid not found.");
  }
}

void cocktailDone() {
  Serial.println("Cocktail DONE !!");
  FirebaseJson json;
  json.setJsonData(NULL);
  
  if (Firebase.RTDB.deleteNode(&cocktailsFBDO,fbdoCocktailsPath)){
    Serial.println("Cocktail removed from DB!");
  }
  else {
    Serial.println("Failed to remove cocktail from DB...");
    Serial.println("REASON: " + cocktailsFBDO.errorReason());
  }
}

void createAwesomeCocktail() {
  cocktailOnCreation = true;
  FirebaseJsonData result2;
  for (size_t i = 0; i < cocktail.size(); i++)
  {
    cocktail.get(result2, i);
    if (result2.type == "string" /* result2.typeNum == FirebaseJson::JSON_STRING */){
        startAssociatedPump(result2.to<String>().c_str());
    }
  }
  cocktailAvailable = false;
  cocktailWaiting = true;
}

void updateCleanAttribute() {
  if (Firebase.RTDB.setInt(&cleaningFBDO,fbdoCleaningPath,0)){
    Serial.println("Clean attributes changed");
  } else {
    Serial.println("Failure : clean attributes can not be changed");
  }
  cleanFinished = false;
}

void cleanMachine() {
  Serial.println("Cleaning...");

  digitalWrite(GPIO_D5, HIGH);
  digitalWrite(GPIO_D6, HIGH);  
  digitalWrite(GPIO_D3, HIGH);
  digitalWrite(GPIO_D2, HIGH);
  digitalWrite(GPIO_D1, HIGH);

  delay(CLEAN_DELAY);
  
  digitalWrite(GPIO_D5, LOW);
  digitalWrite(GPIO_D6, LOW);
  digitalWrite(GPIO_D3, LOW);
  digitalWrite(GPIO_D2, LOW);
  digitalWrite(GPIO_D1, LOW);

  cleanFinished = true;
}

void saveConfiguration(String jsonConfig) {
  DeserializationError error = deserializeJson(barConfig, jsonConfig);

  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }
  configurationOK = true;

}

void updateConfig(String dataPath, String newData) {
  if (dataPath == "/gpio1") {
    barConfig["gpio1"] = newData;
    digitalWrite(GPIO_D5, HIGH);
    delay(NEW_CONFIG_DELAY);
    digitalWrite(GPIO_D5, LOW);
  } else if (dataPath == "/gpio2") {
    barConfig["gpio2"] = newData;
    digitalWrite(GPIO_D6, HIGH);
    delay(NEW_CONFIG_DELAY);
    digitalWrite(GPIO_D6, LOW);
  } else if (dataPath == "/gpio3") {
    barConfig["gpio3"] = newData;
    digitalWrite(GPIO_D3, HIGH);
    delay(NEW_CONFIG_DELAY);
    digitalWrite(GPIO_D3, LOW);
  } else if (dataPath == "/gpio4") {
    barConfig["gpio4"] = newData;
    digitalWrite(GPIO_D2, HIGH);
    delay(NEW_CONFIG_DELAY);
    digitalWrite(GPIO_D2, LOW);
  } else if (dataPath == "/gpio5") {
    barConfig["gpio5"] = newData;
    digitalWrite(GPIO_D1, HIGH);
    delay(NEW_CONFIG_DELAY);
    digitalWrite(GPIO_D1, LOW);
  }
}


void configStreamCallback(FirebaseStream data) {
  Serial.println(" --- Config stream Data...");
  Serial.println(data.streamPath());

  if (data.dataTypeEnum() == fb_esp_rtdb_data_type_string) {
    Serial.println(data.to<String>());
    updateConfig(data.dataPath(),data.to<String>());
  }
  else if (data.dataTypeEnum() == fb_esp_rtdb_data_type_json)
  {
    FirebaseJson *json = data.to<FirebaseJson *>();
    Serial.println(json->raw());
    saveConfiguration(data.stringData());
  }
}

void cocktailsStreamCallback(FirebaseStream data) {
  Serial.println(" --- Cocktails stream Data...");
  Serial.print(data.streamPath());

  if (data.dataTypeEnum() == fb_esp_rtdb_data_type_array)
  {
    cocktail = data.to<FirebaseJsonArray>();
    cocktailAvailable = true;
    Serial.println(cocktail.raw());
  }
}

void cleaningStreamCallback(FirebaseStream data) {
  Serial.println(" --- Cleaning stream Data...");
  Serial.println(data.streamPath());
  Serial.println(data.dataPath());
  Serial.println(data.dataType());

  if (data.dataTypeEnum() == fb_esp_rtdb_data_type_integer) {
    Serial.println(data.to<int>());
    if (data.to<int>() == 1) {
      cleanMachine();
    }
  }
}

// Global function that notifies when stream connection lost
// The library will resume the stream connection automatically
void streamTimeoutCallback(bool timeout)
{
  if(timeout){
    // Stream timeout occurred
    Serial.println("Stream timeout, resume streaming...");
  }  
}



void setup(){
  Serial.begin(9600);//115200
  pinMode(GPIO_D5, OUTPUT);
  pinMode(GPIO_D6, OUTPUT);
  pinMode(GPIO_D3, OUTPUT);
  pinMode(GPIO_D2, OUTPUT);
  pinMode(GPIO_D1, OUTPUT);
  pinMode(GPIO_D7, INPUT); // ECHO
  pinMode(GPIO_D8, OUTPUT); // TRIGGER
  digitalWrite(GPIO_D8, LOW);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED){
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  /* Assign the api key (required) */
  config.api_key = API_KEY;

  /* Assign the RTDB URL (required) */
  config.database_url = DATABASE_URL;

  /* Sign up */
  if (Firebase.signUp(&config, &auth, "", "")){
    Serial.println("ok");
    signupOK = true;
  }
  else{
    Serial.printf("%s\n", config.signer.signupError.message.c_str());
  }

  /* Assign the callback function for the long running token generation task */
  config.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h
  
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Firebase.RTDB.setStreamCallback(&configFBDO, configStreamCallback, streamTimeoutCallback);
  Firebase.RTDB.setStreamCallback(&cocktailsFBDO, cocktailsStreamCallback, streamTimeoutCallback);
  Firebase.RTDB.setStreamCallback(&cleaningFBDO, cleaningStreamCallback, streamTimeoutCallback);

  // Set RTDB config paths
  strcat(fbdoConfigPath, UID);
  strcat(fbdoConfigPath, "/config");

  // Set RTDB cocktails paths
  strcat(fbdoCocktailsPath, UID);
  strcat(fbdoCocktailsPath, "/cocktails");

  // Set RTDB cleaning paths
  strcat(fbdoCleaningPath, UID);
  strcat(fbdoCleaningPath, "/nettoyage");

  /* Start streams */
  if (!Firebase.RTDB.beginStream(&configFBDO, fbdoConfigPath)) {
    Serial.println(configFBDO.errorReason());
  }

  if (!Firebase.RTDB.beginStream(&cocktailsFBDO, fbdoCocktailsPath)) {
    Serial.println(cocktailsFBDO.errorReason());
  }

  if (!Firebase.RTDB.beginStream(&cleaningFBDO, fbdoCleaningPath)) {
    Serial.println(cleaningFBDO.errorReason());
  }
}

void loop(){
  if (Firebase.ready() && signupOK) {

    if (cleanFinished) {
      updateCleanAttribute();
    }

    //Create cocktail if
    if (cocktailAvailable && (distance_cm < 4.0)) {
      Serial.println("Start cocktail...");
      delay(700);
      createAwesomeCocktail();
    }

    //Trigger Cocktail done if glass is off
    if (cocktailWaiting && (distance_cm > 18.0)) {
      cocktailWaiting = false;
      cocktailDone();
    }

    //Scan distance every SCAN_INTERVAL_MS
    if (millis() - previousScanMillis > SCAN_INTERVAL_MS) {
      previousScanMillis = millis();
      digitalWrite(GPIO_D8, HIGH);
      delayMicroseconds(10);
      digitalWrite(GPIO_D8, LOW);
      
      long measure = pulseIn(GPIO_D7, HIGH, MEASURE_TIMEOUT);
      distance_cm = (measure / 2.0 * SOUND_SPEED) / 10.0;
      
      /* Affiche les résultats en cm */
      Serial.print(F("Distance: "));
      Serial.println(distance_cm);
    }

  }
}