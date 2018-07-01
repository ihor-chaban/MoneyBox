#define WAKE_BUTTON_PIN       2
#define CALIBRATE_BUTTON_PIN  3
#define IR1_LED_POWER_PIN     11
#define IR2_LED_POWER_PIN     12
#define IR_SENSOR_PIN         A3
#define IR_SENSOR_POWER_PIN   A0

word max = 0, current = 0;

void setup() {
  Serial.begin(9600);

  pinMode(IR_SENSOR_PIN, INPUT);
  pinMode(CALIBRATE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(IR1_LED_POWER_PIN, OUTPUT);
  pinMode(IR2_LED_POWER_PIN, OUTPUT);
  pinMode(IR_SENSOR_POWER_PIN, OUTPUT);

  digitalWrite(IR1_LED_POWER_PIN, HIGH);
  digitalWrite(IR2_LED_POWER_PIN, HIGH);
  digitalWrite(IR_SENSOR_POWER_PIN, HIGH);
}

word maxx, sign;

void loop() {
  //  current = analogRead(IRsens);
  //  if (current > max) {
  //    max = current;
  //  }
  //  Serial.println(max);
  //  delay(5);
  //
  //  if (!digitalRead(CALIBRATE_BUTTON_PIN)) {
  //    max = 0;
  //  }


  sign = analogRead(IR_SENSOR_PIN);
  maxx = max(maxx, sign);
  Serial.print(sign); Serial.print("\t"); Serial.println(maxx);
  if (!digitalRead(CALIBRATE_BUTTON_PIN)) {
    maxx = 0;
  }
}

