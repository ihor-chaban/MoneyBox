////////////////////////////////////
// MoneyBox with coin recognition //
//      Author - Ihor Chaban      //
////////////////////////////////////

#include <EEPROMex.h>
#include <limits.h>
#include <LiquidCrystal_I2C.h>
#include <LowPower.h>
#include <OneButton.h>
#include <StandardCplusplus.h>
#include <set>
#include <vector>

using std::vector;
using std::set;

//  Software settings
#define COINS                 0.05, 0.10, 0.25, 0.50, 1.00, 1.00, 2.00
#define CURRENCY              "UAH"
#define TITLE                 "Beer Money"
#define STANDBY_TIME          30000
#define DETECTION_THRESHOLD   50
#define COIN_SIGNAL_ADJUSTING 1
#define ADJUSTING_COEFFICIENT 3
#define SENSOR_WARNING_VALUE  200
#define SENSOR_ERROR_VALUE    1000

// Hardware settings
#define WAKE_BUTTON_PIN       2
#define CALIBRATE_BUTTON_PIN  3
#define LCD_POWER_PIN         10
#define IR1_LED_POWER_PIN     11
#define IR2_LED_POWER_PIN     12
#define IR_SENSOR_PIN         A3
#define IR_SENSOR_POWER_PIN   A0
#define LCD_ADDRESS           0x3F
#define LCD_WIDTH             16
#define LCD_HEIGHT            2

#define SIGNAL_POSITION(i)    ((i) * sizeof(word))
#define QUANTITY_POSITION(i)  ((i) * sizeof(word) + (coin_amount * sizeof(word)))

LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_WIDTH, LCD_HEIGHT);
OneButton wake_button(WAKE_BUTTON_PIN, true);
OneButton calibrate_button(CALIBRATE_BUTTON_PIN, true);
const float coin_price[] = {COINS};
const byte coin_amount = sizeof(coin_price) / sizeof(float);
word coin_signal[coin_amount], coin_quantity[coin_amount];
word empty_signal, sensor_signal, sensor_max_signal, best_match_delta, smallest_coin_signal;
byte recognized_coin;
unsigned long standby_timer;
bool coin_detected, service_trigger, add_coins, add_coins_trigger, sleeping;
float total_money;
const byte dollar_image[8][8] = {
  {0x01, 0x03, 0x02, 0x06, 0x0C, 0x0C, 0x0C, 0x08},
  {0x01, 0x07, 0x0D, 0x19, 0x11, 0x11, 0x19, 0x0F},
  {0x10, 0x1C, 0x16, 0x13, 0x11, 0x10, 0x10, 0x10},
  {0x10, 0x18, 0x08, 0x0C, 0x04, 0x06, 0x06, 0x02},
  {0x08, 0x0C, 0x0C, 0x04, 0x06, 0x02, 0x03, 0x01},
  {0x01, 0x01, 0x01, 0x11, 0x19, 0x0D, 0x07, 0x01},
  {0x1E, 0x13, 0x11, 0x11, 0x13, 0x16, 0x1C, 0x10},
  {0x02, 0x06, 0x06, 0x04, 0x0C, 0x08, 0x18, 0x10}
};

enum ServiceModes {CALIBRATE, DELETE, DELETE_AND_CALIBRATE, EXIT} service_mode;
ServiceModes operator++(ServiceModes& i, int) {
  if (i == EXIT) {
    return i = CALIBRATE;
  }
  byte temp = i;
  return i = static_cast<ServiceModes> (++temp);
}

void setup() {
  // Debug mode
  // Serial.begin(9600);

  pinMode(IR_SENSOR_PIN, INPUT);
  pinMode(CALIBRATE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(LCD_POWER_PIN, OUTPUT);
  pinMode(IR1_LED_POWER_PIN, OUTPUT);
  pinMode(IR2_LED_POWER_PIN, OUTPUT);
  pinMode(IR_SENSOR_POWER_PIN, OUTPUT);
  attachInterrupt(digitalPinToInterrupt(WAKE_BUTTON_PIN), WakeUp, RISING);
  digitalWrite(LCD_POWER_PIN, HIGH);
  digitalWrite(IR1_LED_POWER_PIN, HIGH);
  digitalWrite(IR2_LED_POWER_PIN, HIGH);
  digitalWrite(IR_SENSOR_POWER_PIN, HIGH);
  wake_button.attachDuringLongPress(ShowCoins);
  wake_button.attachLongPressStop(ShowMainScreen);
  calibrate_button.attachClick(NextServiceMode);
  calibrate_button.attachDoubleClick(ExecuteServiceMode);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("MoneyBox");
  lcd.setCursor(0, 1);
  lcd.print("by Gorus");
  for (byte i = 0; i < sizeof(dollar_image[0]); i++) {
    lcd.createChar(i, dollar_image[i]);
  }
  for (byte i = 0; i < (sizeof(dollar_image[0]) / 2); i++) {
    lcd.setCursor(LCD_WIDTH - (sizeof(dollar_image[0]) / 2) + i, 0);
    lcd.write(i);
  }
  for (byte i = 0; i < (sizeof(dollar_image[0]) / 2); i++) {
    lcd.setCursor(LCD_WIDTH - (sizeof(dollar_image[0]) / 2) + i, 1);
    lcd.write((sizeof(dollar_image[0]) / 2) + i);
  }
  empty_signal = 0;
  for (byte i = 0; i < 5; i++) {
    empty_signal += analogRead(IR_SENSOR_PIN);
    delay(1000);
  }
  empty_signal = round(empty_signal / 5.0);
  if (!digitalRead(CALIBRATE_BUTTON_PIN)) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("  Service Mode  ");
    service_trigger = false;
    service_mode = EXIT;
    while (!digitalRead(CALIBRATE_BUTTON_PIN)) {
    }
    NextServiceMode();
    while (!service_trigger) {
      calibrate_button.tick();
    }
  } else if (empty_signal > SENSOR_WARNING_VALUE) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("    Warning!    ");
    lcd.setCursor(0, 1);
    lcd.print("  Check sensor  ");
    delay(5000);
  } else if (empty_signal > SENSOR_ERROR_VALUE) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("     Error!     ");
    lcd.setCursor(0, 1);
    lcd.print(" Sensor is down ");
    while (true) {
    }
  }
  total_money = 0;
  for (byte i = 0; i < coin_amount; i++) {
    coin_signal[i] = EEPROM.readInt(SIGNAL_POSITION(i));
    coin_quantity[i] = EEPROM.readInt(QUANTITY_POSITION(i));
    total_money += coin_quantity[i] * coin_price[i];
  }
  smallest_coin_signal = GetMinSignal();
  standby_timer = millis();

  // Debug mode
  //  for (byte i = 0; i < coin_amount; i++) {
  //    Serial.println("Coin signal [" + String(coin_price[i]) + "] - " + String(coin_signal[i]));
  //  }
  //  Serial.println();
}

void loop() {
  if (sleeping) {
    sleeping = false;
    lcd.init();
    empty_signal = 0;
    for (byte i = 0; i < 5; i++) {
      empty_signal += analogRead(IR_SENSOR_PIN);
      delay(5);
    }
    empty_signal = round(empty_signal / 5.0);
    if (empty_signal > SENSOR_WARNING_VALUE) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("    Warning     ");
      lcd.setCursor(0, 1);
      lcd.print("  Check sensor  ");
      delay(5000);
    } else if (empty_signal > SENSOR_ERROR_VALUE) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("     Error      ");
      lcd.setCursor(0, 1);
      lcd.print(" Sensor is down ");
      while (true) {
      }
    }
  }
  ShowMainScreen();
  sensor_max_signal = empty_signal;
  while (true) {
    sensor_signal = analogRead(IR_SENSOR_PIN);
    if (sensor_signal > sensor_max_signal) {
      sensor_max_signal = sensor_signal;
    }
    if ((sensor_max_signal - sensor_signal) > DETECTION_THRESHOLD) {
      coin_detected = true;
    }
    if (coin_detected && (abs((int)(sensor_signal - empty_signal)) < round(DETECTION_THRESHOLD / 2.0))) {
      coin_detected = false;
      if (sensor_max_signal > (smallest_coin_signal - DETECTION_THRESHOLD)) {
        best_match_delta = UINT_MAX;
        for (byte i = 0; i < coin_amount; i++) {
          word delta = abs((int)(sensor_max_signal - coin_signal[i]));
          if (delta < best_match_delta) {
            best_match_delta = delta;
            recognized_coin = i;
          }

          // Debug mode
          // Serial.println("Delta [" + String(coin_price[i]) + "] - " + String(delta));
        }
        // Debug mode
        // Serial.println("Recognized as " + String(coin_price[recognized_coin]) + " with delta " + String(best_match_delta));

        if (COIN_SIGNAL_ADJUSTING) {
          // Debug mode
          // Serial.print("Old coin signal is " + String(coin_signal[recognized_coin]) + ", New coin signal is " + String(sensor_max_signal) + ", Adjusted coin signal is ");

          coin_signal[recognized_coin] = round((float)((coin_signal[recognized_coin] * ADJUSTING_COEFFICIENT) + sensor_max_signal) / (ADJUSTING_COEFFICIENT + 1));

          // Debug mode
          // Serial.println(String(coin_signal[recognized_coin])  + "\n");
        }
        coin_quantity[recognized_coin]++;
        total_money += coin_price[recognized_coin];
        lcd.setCursor(0, 1);
        lcd.print(total_money);
        standby_timer = millis();
      }
      // Debug mode
      //      else {
      //        Serial.println("Recognized as random noise peak with delta " + String(smallest_coin_signal - sensor_max_signal) + "\n");
      //      }
      break;
    }
    if ((millis() - standby_timer) > STANDBY_TIME) {
      GoodNight();
      break;
    }
    wake_button.tick();
  }
}

void ShowMainScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(TITLE);
  lcd.setCursor(0, 1);
  lcd.print(total_money);
  lcd.setCursor(LCD_WIDTH - 3, 1);
  lcd.print(CURRENCY);
}

void ShowCoins() {
  vector<float> unique_coin_price = {COINS};
  set<float> temp_set(unique_coin_price.begin(), unique_coin_price.end());
  unique_coin_price.assign(temp_set.begin(), temp_set.end());
  vector<word> unique_coin_quantity(unique_coin_price.size(), 0);
  for (byte i = 0; i < unique_coin_price.size(); i++) {
    for (byte j = 0; j < coin_amount; j++) {
      if (unique_coin_price[i] == coin_price[j])
        unique_coin_quantity[i] += coin_quantity[j];
    }
  }
  String temp_string;
  byte number_of_screens = ((unique_coin_price.size() + 1) % 4) ? ((unique_coin_price.size() + 1) / 4 + 1) : ((unique_coin_price.size() + 1) / 4);
  word coins_in_total = 0;
  for (byte i = 0; i < coin_amount; i++) {
    coins_in_total += coin_quantity[i];
  }
  for (byte i = 0; i < number_of_screens; i++) {
    lcd.clear();
    for (byte j = i * 4; j < std::min((int)(unique_coin_price.size() + 1), i * 4 + 4); j++) {
      lcd.setCursor((j - i * 4) * 4, 0);
      temp_string = String(unique_coin_price[j]);
      if (unique_coin_price[j] < 1) {
        temp_string.remove(0, 1);
      } else {
        temp_string.remove(temp_string.length() - 1);
      }
      lcd.print((j < unique_coin_price.size()) ? temp_string : "All");
      lcd.setCursor((j - i * 4) * 4, 1);
      lcd.print((j < unique_coin_price.size()) ? unique_coin_quantity[j] : coins_in_total);
    }
    delay(5000);
  }
}

void GoodNight() {
  for (byte i = 0; i < coin_amount; i++) {
    EEPROM.updateInt(QUANTITY_POSITION(i), coin_quantity[i]);
  }
  if (COIN_SIGNAL_ADJUSTING) {
    for (byte i = 0; i < coin_amount; i++) {
      EEPROM.updateInt(SIGNAL_POSITION(i), coin_signal[i]);
    }
  }
  sleeping = true;
  digitalWrite(LCD_POWER_PIN, LOW);
  digitalWrite(IR1_LED_POWER_PIN, LOW);
  digitalWrite(IR2_LED_POWER_PIN, LOW);
  digitalWrite(IR_SENSOR_POWER_PIN, LOW);
  LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);
}

void WakeUp() {
  if (sleeping) {
    digitalWrite(LCD_POWER_PIN, HIGH);
    digitalWrite(IR1_LED_POWER_PIN, HIGH);
    digitalWrite(IR2_LED_POWER_PIN, HIGH);
    digitalWrite(IR_SENSOR_POWER_PIN, HIGH);
  }
  standby_timer = millis();
}

void NextServiceMode() {
  service_mode++;
  lcd.setCursor(0, 1);
  switch (service_mode) {
    case CALIBRATE: {
        lcd.print("   Calibrate    ");
        break;
      }
    case DELETE: {
        lcd.print("     Delete     ");
        break;
      }
    case DELETE_AND_CALIBRATE: {
        lcd.print("Delete&Calibrate");
        break;
      }
    case EXIT: {
        lcd.print("      Exit      ");
        break;
      }
  }
}

void ExecuteServiceMode() {
  service_trigger = true;
  word temp_signal;
  switch (service_mode) {
    case CALIBRATE: {
        for (byte i = 0; i < coin_amount; i++) {
          coin_quantity[i] = EEPROM.readInt(QUANTITY_POSITION(i));
        }
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Calibrating    ");
        for (byte i = 0; i < coin_amount; i++) {
          lcd.setCursor(0, 1);
          lcd.print(coin_price[i]);
          lcd.setCursor(LCD_WIDTH - 3, 1);
          lcd.print(CURRENCY);
          sensor_max_signal = empty_signal;
          for (byte j = 0; j < 3; j++) {
            lcd.setCursor(LCD_WIDTH - 1, 0);
            lcd.print(j + 1);
            temp_signal = empty_signal;
            while (true) {
              sensor_signal = analogRead(IR_SENSOR_PIN);
              if (sensor_signal > temp_signal) {
                temp_signal = sensor_signal;
              }
              if ((temp_signal - sensor_signal) > DETECTION_THRESHOLD) {
                coin_detected = true;
              }
              if (coin_detected && (abs((int)(sensor_signal - empty_signal)) < round(DETECTION_THRESHOLD / 2.0))) {
                coin_detected = false;
                coin_signal[i] += temp_signal;
                break;
              }
            }

            // Debug mode
            // Serial.println("Calibrate " + String(j + 1) + " [" + String(coin_price[i]) + "] - " + String(temp_signal));
          }
          coin_signal[i] = round(coin_signal[i] / 3.0);
          EEPROM.updateInt(SIGNAL_POSITION(i), coin_signal[i]);

          // Debug mode
          // Serial.println("Average [" + String(coin_price[i]) + "] - " + String(coin_signal[i]) + "\n");
        }
        lcd.clear();
        lcd.print("Add coins to sum");
        lcd.setCursor(0, 1);
        ToggleAddCoins();
        calibrate_button.attachClick(ToggleAddCoins);
        calibrate_button.attachDoubleClick(ExecuteAddCoins);
        calibrate_button.reset();
        while (!add_coins_trigger) {
          calibrate_button.tick();
        }
        break;
      }
    case DELETE: {
        for (byte i = 0; i < coin_amount; i++) {
          EEPROM.updateInt(QUANTITY_POSITION(i), 0);
        }
        break;
      }
    case DELETE_AND_CALIBRATE: {
        for (byte i = 0; i < coin_amount; i++) {
          coin_quantity[i] = 0;
        }
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Calibrating    ");
        for (byte i = 0; i < coin_amount; i++) {
          coin_quantity[i] = 0;
        }
        for (byte i = 0; i < coin_amount; i++) {
          lcd.setCursor(0, 1);
          lcd.print(coin_price[i]);
          lcd.setCursor(LCD_WIDTH - 3, 1);
          lcd.print(CURRENCY);
          sensor_max_signal = empty_signal;
          for (byte j = 0; j < 3; j++) {
            lcd.setCursor(LCD_WIDTH - 1, 0);
            lcd.print(j + 1);
            temp_signal = empty_signal;
            while (true) {
              sensor_signal = analogRead(IR_SENSOR_PIN);
              if (sensor_signal > temp_signal) {
                temp_signal = sensor_signal;
              }
              if ((temp_signal - sensor_signal) > DETECTION_THRESHOLD) {
                coin_detected = true;
              }
              if (coin_detected && (abs((int)(sensor_signal - empty_signal)) < round(DETECTION_THRESHOLD / 2.0))) {
                coin_detected = false;
                coin_signal[i] += temp_signal;
                break;
              }
            }

            // Debug mode
            // Serial.println("Calibrate " + String(j + 1) + " [" + String(coin_price[i]) + "] - " + String(temp_signal));
          }
          coin_signal[i] = round(coin_signal[i] / 3.0);
          EEPROM.updateInt(SIGNAL_POSITION(i), coin_signal[i]);

          // Debug mode
          // Serial.println("Average [" + String(coin_price[i]) + "] - " + String(coin_signal[i]) + "\n");
        }
        lcd.clear();
        lcd.print("Add coins to sum");
        lcd.setCursor(0, 1);
        ToggleAddCoins();
        calibrate_button.attachClick(ToggleAddCoins);
        calibrate_button.attachDoubleClick(ExecuteAddCoins);
        calibrate_button.reset();
        while (!add_coins_trigger) {
          calibrate_button.tick();
        }
        break;
      }
    case EXIT: {
        break;
      }
  }
  lcd.clear();
  if (service_mode != EXIT) {
    lcd.setCursor(0, 0);
    lcd.print("      Done      ");
    delay(3000);
  }
  ShowMainScreen();
}

void ToggleAddCoins() {
  add_coins = !add_coins;
  lcd.setCursor(0, 1);
  lcd.print((add_coins) ? " Yes <       No " : " Yes       > No ");
}

void ExecuteAddCoins() {
  add_coins_trigger = true;
  if (add_coins) {
    for (byte i = 0; i < coin_amount; i++) {
      EEPROM.updateInt(QUANTITY_POSITION(i), coin_quantity[i] + 3);
    }
  } else {
    for (byte i = 0; i < coin_amount; i++) {
      EEPROM.updateInt(QUANTITY_POSITION(i), coin_quantity[i]);
    }
  }
}

word GetMinSignal() {
  word min_signal;
  min_signal = UINT_MAX;
  for (byte i = 0; i < coin_amount; i++) {
    if (coin_signal[i] < min_signal) {
      min_signal = coin_signal[i];
    }
  }
  return min_signal;
}

