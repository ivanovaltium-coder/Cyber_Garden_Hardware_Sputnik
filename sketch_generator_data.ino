// ESP32 — генерация случайных данных и отправка на Arduino UNO
// Формат: STEPS,TEMP,SYS,DIA,PULSE,STRESS,ACTIVITY,ECG

void setup() {
  Serial.begin(9600);   // Совпадает со скоростью в Arduino
  randomSeed(analogRead(0)); // Инициализация генератора случайных чисел
}

void loop() {
  // Генерация случайных данных
  int steps = random(0, 20000);           // шаги
  float temp = random(360, 380) / 10.0;   // температура 36.0 – 38.0
  int sys = random(100, 140);             // верхнее давление
  int dia = random(60, 90);               // нижнее давление
  int pulse = random(55, 100);            // пульс
  int stress = random(0, 100);            // стресс (0–100%)
  int activity = random(0, 100);          // активность (0–100%)
  int ecg = random(-20, 20);              // шум/амплитуда ЭКГ

  // Отправка строки в том же формате
  Serial.print(steps);
  Serial.print(",");
  Serial.print(temp, 1);
  Serial.print(",");
  Serial.print(sys);
  Serial.print(",");
  Serial.print(dia);
  Serial.print(",");
  Serial.print(pulse);
  Serial.print(",");
  Serial.print(stress);
  Serial.print(",");
  Serial.print(activity);
  Serial.print(",");
  Serial.println(ecg);

  delay(500); // полсекунды между отправками
}