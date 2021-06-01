// Laundry Box Arduino Code
// Part of IF4051 Pengembangan Sistem IoT, Institut Teknologi Bandung
//
// Author : 13516109 Kevin Fernaldy, 13516141 Ilham Wahabi

#define ARDUINOJSON_USE_DOUBLE 1

#include <LiquidCrystal.h>
#include <RotaryEncoder.h>
#include <EspMQTTClient.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <TimeLib.h>
#include <analogWrite.h>
#include <HX711.h>

// *************
// * Wifi Data *
// *************
const char* ssid = "DICKY-ANNA";
const char* password = "dp234234";

// *************************************
// * LCD Display Data & Initialization *
// *************************************
const byte RS = 17;
const byte EN = 5;
const byte D4 = 15;
const byte D5 = 2;
const byte D6 = 4;
const byte D7 = 16;
LiquidCrystal lcd(RS, EN, D4, D5, D6, D7);
const String laundry_type[] = {
  "Reguler 3 Hari  ",
  "Reguler 2 Hari  ",
  "Express 1 Hari  ",
  "Express 12 Jam  ",
  "Express 4 Jam   ",
  "< BACK"
};
byte laundry_type_select = 0;
const String laundry_type_2[] = {
  "Cuci + Kering + Lipat",
  "Cuci + Kering + Setrika + Lipat",
  "Setrika + Lipat",
  "< BACK"
};
byte laundry_type_2_select = 0;
const String laundry_price[5][3] = {
  {"Rp 4000/kg     ", "Rp 4500/kg     ", ""},
  {"Rp 5000/kg     ", "Rp 5500/kg     ", ""},
  {"Rp 6500/kg     ", "Rp 7500/kg     ", "Rp 3000/kg     "},
  {"Rp 8500/kg     ", "Rp 9500/kg     ", "Rp 3500/kg     "},
  {"Rp 11500/kg    ", "Rp 12500/kg    ", "Rp 4500/kg     "}
};
const int laundry_price_int[5][3] = {
  { 4000,  4500,    0},
  { 5000,  5500,    0},
  { 6500,  7500, 3000},
  { 8500,  9500, 3500},
  {11500, 12500, 4500}
};
byte laundry_price_select = 0;

// ****************************************
// * Rotary Encoder Data & Initialization *
// ****************************************
const byte PIN_IN1 = 18;
const byte PIN_IN2 = 19;
const byte PIN_CLK = 21;
RotaryEncoder encoder(PIN_IN1, PIN_IN2, RotaryEncoder::LatchMode::TWO03);
static int current_rotary_position = 0;
unsigned long last_rotary_click = 0;

// *************************************
// * MQTT Client Data & Initialization *
// *************************************
EspMQTTClient client(
  ssid,
  password,
  "202.148.1.57",
  "app-smartlaundrysystem",
  "CcxohtT0HSWn6QzEX6YsV48J9KyES7",
  "ESP32_01"
);

// ********************
// * HX711 Scale Data *
// ********************
const byte LOADCELL_DOUT_PIN = 35;
const byte LOADCELL_SCK_PIN = 32;
HX711 scale;

// ********************
// * Geolocation Data *
// ********************
const String API_KEY = "badc0f4ca2718fc014fa45d81efaab34";
const String HTTP_GEO_ENDPOINT = "http://api.ipstack.com/check?access_key=" + API_KEY + "&fields=latitude,longitude";
String lat = "";
String lng = "";

// ******************
// * Timestamp Data *
// ******************
const String HTTP_TIMESTAMP_ENDPOINT = "https://showcase.api.linx.twenty57.net/UnixTime/tounix?date=now";

// ************
// * LED Data *
// ************
const byte LED_RED = 26;
const byte LED_GREEN = 25;
const byte LED_BLUE = 33;

// *********************
// * Device State Data *
// *********************
byte state = 0;

// State 0 Variables
bool is_weight_reset = false;
unsigned long last_weight_measured = 0;
float weight = 0.0;

// State 2 Variables
byte scrolling_text_position = -1;
byte bottom_text_padding = 0;
bool is_text_rendered = false;
bool is_text_scrolling = false;
bool is_text_start_scrolling = false;
bool is_text_end_scrolling = false;
unsigned long last_text_scrolling = 0;
unsigned long last_full_text_scroll = 0;
unsigned long last_text_end_scroll = 0;

// State 3 Variables
bool confirmation_selection = false;
bool encoder_turned = false;

// State 4 Variables
unsigned long time_start_request = 0;

/* Gets the limit of laundry type */
int getLaundryTypePriceLimit(int select) {
  switch(select) {
    case 0: 
    case 1: {
      return 2;
    }
    case 2:
    case 3:
    case 4: {
      return 3;
    }
  }
}

/* Gets the rotary encoder direction when rotated (clockwise or counter-clockwise) */
int getEncoderDirection() {
  encoder.tick();

  int new_position = encoder.getPosition();

  if ((current_rotary_position != new_position) && (new_position % 2 == 0)) {
    return ((int) encoder.getDirection());
  }

  return 0;
}

/* Checks if the rotary encoder is clicked */
bool getEncoderClicked() {
  byte click_status = digitalRead(PIN_CLK);

  if ((millis() - last_rotary_click > 1000) && (!(click_status))) {
    last_rotary_click = millis();
    Serial.println("YES");
    return true;
  }

  return false;
}

/* Sets the color of LED */
void setLED(float red, float green, float blue) {
  analogWrite(LED_RED, (1 - red) * 255.0);
  analogWrite(LED_GREEN, (1 - green) * 255.0);
  analogWrite(LED_BLUE, (1 - blue) * 255.0);
}

/* Callback function for MQTT Client after the device is connected to Wi-Fi */
void onConnectionEstablished() {
  client.subscribe("ESP32ToSubscribe", [] (const String &payload)  {
    if (state == 4) {
      Serial.println(payload);
      state = 5;
      is_text_rendered = false;
    }
  });
}

/* Sents payload to server */
void publishPayload() {
  String to_sent = "{ \"payload\": { \"laundry_type\": " + String(laundry_type_select) + ", \"laundry_package\": " + String(laundry_type_2_select) + ", \"weight\": " + weight + ", \"geo\": { \"lat\": " + String(lat) + ", \"lng\": " + String(lng) + " }, \"timestamp\": " + String(now()) + " } }";
  client.publish("ESP32ToPublish", to_sent);
}

void setup() {
  Serial.begin(115200);
  client.enableDebuggingMessages(true);

  // Set up LED
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  setLED(0.2, 0.2, 0.0);
  
  // Set up LCD
  lcd.begin(16, 2);
  lcd.setCursor(0, 0);
  lcd.print("   Setting Up   ");
  lcd.setCursor(0, 1);
  lcd.print(" Parameters.... ");

  // Set up load cell
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(2280.f);
  scale.tare();
  
  // Set up Rotary Encoder
  pinMode(PIN_CLK, INPUT_PULLUP);

  // Set up payload data
  WiFi.begin(ssid, password);

  // Set up Wi-Fi connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.printf("Connected to WiFi ");
  Serial.println(ssid);

  // Setting up device's latitude longitude
  HTTPClient http;
  
  http.begin(HTTP_GEO_ENDPOINT);
  byte http_code = http.GET();

  if ((http_code > 0) && (http_code == 200)) {
    String payload = http.getString();
    StaticJsonDocument<65> document;

    DeserializationError error = deserializeJson(document, payload);
    if (error) {
      Serial.println("ERROR in parsing JSON geolocation response");
    } else {
      serializeJson(document["latitude"], lat);
      serializeJson(document["longitude"], lng);
    }
  } else {
    Serial.println("ERROR Getting HTTP geolocation request");
  }
  
  // Setting up device's timestamp
  HTTPClient http2;
  http2.begin(HTTP_TIMESTAMP_ENDPOINT);
  http_code = http2.GET();

  if ((http_code > 0) && (http_code == 200)) {
    String payload = http2.getString();
    payload = payload.substring(1, payload.length()-1);

    setTime(payload.toInt());
    
  } else {
    Serial.println("ERROR Getting HTTP timestamp request");
  }

  http.end();
  http2.end();

  renderSplashScreen();
}

void loop() {
  client.loop();
  
  if (getEncoderClicked()) {
    switch(state) {
      case 0: {
        state = 1;
        is_weight_reset = false;
        renderLCDState1(laundry_type_select);
        break;
      }
      case 1: {
        if (laundry_type_select == 5) {
          state = 0;
          laundry_type_select = 0;
        } else {
          renderLCDState2(laundry_type_2_select);
          state = 2;
        }
        break;
      }
      case 2: {
        resetStateVariables();
        if (laundry_type_2_select == 3) {
          renderLCDState1(laundry_type_select);
          laundry_type_2_select = 0;
          state = 1;
        } else {
          renderLCDState3(confirmation_selection);
          state = 3;
        }
        break;
      }
      case 3: {
        resetStateVariables();
        if (confirmation_selection) {
          publishPayload();
          time_start_request = millis();
          state = 4;
        } else {
          renderLCDState2(laundry_type_2_select);
          state = 2;
        }
        confirmation_selection = false;
      }
    }
  } else {
    switch(state) {
      case 0: {
        state_0();
        break;
      }
      case 1: {
        state_1();
        break;
      }
      case 2: {
        state_2();
        break;
      }
      case 3: {
        state_3();
        break;
      }
      case 4: {
        state_4();
        break;
      }
      case 5: {
        state_5();
        break;
      }
      case 6: {
        state_6();
        break;
      }
    }
  }
}

/* State 0 Procedure */
void state_0() {
  if (!(is_weight_reset)) {
    is_weight_reset = true;
    scale.tare();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(" Laundry Weight ");
  }

  if (millis()-last_weight_measured > 500) {
    weight = scale.get_units(20);
    lcd.setCursor(0, 1);
    lcd.print("    " + String(weight, 3) + " KG    ");
    last_weight_measured = millis();
  }
}

/* State 1 Procedure */
void state_1() {
  int rotary_direction = getEncoderDirection();

  switch(rotary_direction) {
    case -1: {  // CW
      if (laundry_type_select < 5) {
        laundry_type_select++;
      }
      renderLCDState1(laundry_type_select);
      break;
    }
    case 1: {   // CCW
      if (laundry_type_select > 0) {
        laundry_type_select--;
      }
      renderLCDState1(laundry_type_select);
      break;
    }
  }
}

/* State 2 Procedure */
void state_2() {
  int rotary_direction = getEncoderDirection();
  switch(rotary_direction) {
    case -1: {  // CW
      if (laundry_type_2_select < 3) {
        resetStateVariables();
        laundry_type_2_select++;
        if ((laundry_type_select < 2) && (laundry_type_2_select == 2)) {
          laundry_type_2_select = 3;
        }
      }
      break;
    }
    case 1: {   // CCW
      if (laundry_type_2_select > 0) {
        resetStateVariables();
        laundry_type_2_select--;
        if ((laundry_type_select < 2) && (laundry_type_2_select == 2)) {
          laundry_type_2_select = 1;
        }
      }
      break;
    }
  }
  renderLCDState2(laundry_type_2_select);
}

/* State 3 Procedure */
void state_3() {
  int rotary_direction = getEncoderDirection();
  switch(rotary_direction) {
    case -1: {  // CW
      confirmation_selection = false;
      encoder_turned = true;
      break;
    }
    case 1: {   // CCW
      confirmation_selection = true;
      encoder_turned = true;
      break;
    }
  }
  renderLCDState3(confirmation_selection);
}

/* State 4 Procedure */
void state_4() {
  renderLCDState4();
  if (millis() - time_start_request > 12000) {
    state = 6;
    is_text_rendered = false;
  }
}

/* State 5 Procedure */
void state_5() {
  renderLCDState5();
  setLED(0.0, 0.2, 0.0);
  delay(4000);
  setLED(0.0, 0.0, 0.0);

  renderSplashScreen();
  is_text_rendered = false;
  state = 0;
}

/* State 6 Procedure */
void state_6() {
  renderLCDState6();
  setLED(0.2, 0.0, 0.0);
  delay(500);
  setLED(0.0, 0.0, 0.0);
  delay(500);
  setLED(0.2, 0.0, 0.0);
  delay(500);
  setLED(0.0, 0.0, 0.0);
  delay(500);
  setLED(0.2, 0.0, 0.0);
  delay(500);
  setLED(0.0, 0.0, 0.0);
  delay(500);
  setLED(0.2, 0.0, 0.0);
  delay(4000);
  setLED(0.0, 0.0, 0.0);

  renderSplashScreen();
  is_text_rendered = false;
  state = 0;
}

/* Renders State 1 output to LCD */
void renderLCDState1(int pos) {
  lcd.clear();
  if (pos < 5) {
    lcd.setCursor(0, 0);
    lcd.print("Paket Laundry"); 
  }
  lcd.setCursor(0, 1);
  lcd.print(laundry_type[pos]);
}

/* Renders State 2 output to LCD */
void renderLCDState2(int pos) {  
  String text = laundry_type_2[pos];
  int text_length = text.length();
  
  if ((text_length < 16) && (!(is_text_rendered))) {
    is_text_rendered = true;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(text);

    if (pos < 3) {
      lcd.setCursor(0, 1);
      lcd.print(laundry_price[laundry_type_select][pos]);
    }
  } else {
    if (!(is_text_start_scrolling)) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(text);
      if (pos < 3) {
        lcd.setCursor(0, 1);
        lcd.print(laundry_price[laundry_type_select][pos]);
      }
      is_text_start_scrolling = true;
      last_full_text_scroll = millis();
    }

    if (millis() - last_full_text_scroll > 2000) {
      if (!(is_text_scrolling)) {
        is_text_scrolling = true;
        scrolling_text_position = 16;
        
        last_text_scrolling = millis();
  
        lcd.autoscroll();
        lcd.setCursor(16, 0);
      }
  
      if (millis() - last_text_scrolling > 500) {
        if (scrolling_text_position < text_length) {
          lcd.print(text[scrolling_text_position]);
          scrolling_text_position++;
          last_text_scrolling = millis();

          if (pos < 3) {
            bottom_text_padding++;
            lcd.noAutoscroll();
            lcd.setCursor(bottom_text_padding, 1);
            lcd.print(laundry_price[laundry_type_select][pos]);
            lcd.autoscroll();
            lcd.setCursor(scrolling_text_position, 0);
          }
        }
    
        if (scrolling_text_position >= text_length) {
          if (!(is_text_end_scrolling)) {
            is_text_end_scrolling = true;
            last_text_end_scroll = millis();
          }

          if (millis() - last_text_end_scroll > 2000) {
            resetStateVariables();
            
          }         
        }
      } 
    }
  }
}

/* Renders State 3 output to LCD */
void renderLCDState3(bool yes_no) {
  String text = String(weight * laundry_price_int[laundry_type_select][laundry_type_2_select], 2);
  int text_length = text.length();

  if (encoder_turned && is_text_start_scrolling) {
    encoder_turned = false;
    if (is_text_scrolling) {
      lcd.noAutoscroll();
      lcd.setCursor(bottom_text_padding, 0); 
    }
    lcd.setCursor(bottom_text_padding, 1);
    lcd.print("   Ya     Tidak ");
        
    if (yes_no) {
      lcd.setCursor(bottom_text_padding + 1, 1);
      lcd.print(">");
    } else {
      lcd.setCursor(bottom_text_padding + 8, 1);
      lcd.print(">");
    }

    if (is_text_scrolling) {
      lcd.autoscroll();
      lcd.setCursor(scrolling_text_position, 0);
    }
  }
  
  if (!(is_text_start_scrolling)) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Total: ");
    lcd.print(text);
    lcd.setCursor(0, 1);
    lcd.print("   Ya     Tidak ");
  
    if (yes_no) {
      lcd.setCursor(1, 1);
      lcd.print(">");
    } else {
      lcd.setCursor(8, 1);
      lcd.print(">");
    }
    is_text_start_scrolling = true;
    last_full_text_scroll = millis();
  }
   if (millis() - last_full_text_scroll > 2000) {
    if (!(is_text_scrolling)) {
      is_text_scrolling = true;
      scrolling_text_position = 16;
      
      last_text_scrolling = millis();

      lcd.autoscroll();
      lcd.setCursor(16, 0);
    }

    if (millis() - last_text_scrolling > 500) {
      if (scrolling_text_position < text_length) {
        lcd.print(text[scrolling_text_position]);
        scrolling_text_position++;
        last_text_scrolling = millis();

        bottom_text_padding++;
        lcd.noAutoscroll();
        lcd.setCursor(bottom_text_padding, 0);
        lcd.print("Total: ");
        lcd.setCursor(bottom_text_padding, 1);
        lcd.print("   Ya     Tidak ");
        
        if (yes_no) {
          lcd.setCursor(bottom_text_padding + 1, 1);
          lcd.print(">");
        } else {
          lcd.setCursor(bottom_text_padding + 8, 1);
          lcd.print(">");
        }
        lcd.autoscroll();
        lcd.setCursor(scrolling_text_position, 0);
      }
  
      if (scrolling_text_position >= text_length) {
        if (!(is_text_end_scrolling)) {
          is_text_end_scrolling = true;
          last_text_end_scroll = millis();
        }
        if (millis() - last_text_end_scroll > 2000) {
          resetStateVariables();
        }         
      }
    } 
  }
}

/* Renders State 4 output to LCD */
void renderLCDState4() {
  if (!(is_text_rendered)) {
    is_text_rendered = true;

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("    Wait for    ");
    lcd.setCursor(0, 1);
    lcd.print("  Response....  ");
  }
}

/* Renders State 5 output to LCD */
void renderLCDState5() {
  if (!(is_text_rendered)) {
    is_text_rendered = true;

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(" Order Accepted ");
    lcd.setCursor(0, 1);
    lcd.print(" ============== ");
  }
}

/* Renders State 6 output to LCD */
void renderLCDState6() {
  if (!(is_text_rendered)) {
    is_text_rendered = true;

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("    ERR  408    ");
    lcd.setCursor(0, 1);
    lcd.print("    Time Out    ");
  }
}

/* Renders the welcome screen to LCD */
void renderSplashScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("   Welcome...   ");
  
  setLED(0.2, 0.0, 0.0);
  delay(1000);
  setLED(0.0, 0.2, 0.0);
  delay(1000);
  setLED(0.0, 0.0, 0.2);
  delay(1000);
  setLED(0.0, 0.0, 0.0);
}

/* Resets all state variables */
void resetStateVariables() {
  bottom_text_padding = 0;
  scrolling_text_position = 16;
  is_text_scrolling = false;
  is_text_start_scrolling = false;
  is_text_rendered = false;
  is_text_end_scrolling = false;
  lcd.noAutoscroll();
}
