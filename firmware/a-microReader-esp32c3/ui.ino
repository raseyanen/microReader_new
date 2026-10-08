/* ======================================================================= */
/* ========================= Индикатор заряда ============================ */
void checkBatteryCharge(void) {       // Проверка заряда аккумулятора
  if (millis() - batTimer >= 5000) {  // Таймер батарейки на 5 сек
    batTimer = millis();              // Сброс таймера
    batMv = readBatteryMv();          // Измерение напряжения батареи через АЦП ESP32-C3
  }
}

void drawBatteryCharge(void) {                       // Рисуем батарейку
  byte charge =                                      // Заряд в виде числа
    constrain(                                       // Ограничиваем диапазон
      map(                                           // Преобразуем диапазон
        batMv, VBAT_EMPTY_MV, VBAT_FULL_MV,          // Напряжение в заряд (костыль)
        0, 12                                        // Преобразуем в [0...12]
        ),                                           // Конец map
      0, 12                                          // Ограничиваем от 0 до 12
    );                                               // Конец constrain
  oled.setCursorXY(110, 0);                          // Положение на экране
  oled.drawByte(0b00111100);                         // Пипка
  oled.drawByte(0b00111100);                         // 2 штуки
  oled.drawByte(0b11111111);                         // Передняя стенка
  for (uint8_t i = 0; i < 12; i++) {                 // 12 градаций
    if (i < 12 - charge) oled.drawByte(0b10000001);  // Рисуем пустые
    else oled.drawByte(0b11111111);                  // Рисуем полные
  }
  oled.drawByte(0b11111111);  // Задняя стенка
}
/* ======================================================================= */
/* ============================ Главное меню ============================= */
void drawMainMenu(void) {   // Отрисовка главного меню
  if (fileCount < 0) fileCount = 0;
  if (cursor > fileCount - 1) cursor = fileCount - 1;  // Курсор не должен уезжать за конец списка
  if (cursor < 0) cursor = 0;

  oled.clear();               // Очистка
  oled.home();                // Возврат на 0,0
  oled.line(0, 10, 127, 10);  // Линия
  oled.print(F("ФАЙЛОВ:"));
  oled.print(fileCount);      // Выводим кол-во файлов + битые при наличии
  if (badCount) oled.printf("[%i]", fileCount + badCount);

  const int VISIBLE = 5;            // Видимых строк в списке
  const int firstRow = 2;           // Первая строка списка (пиксельная строка 16)
  int sidx = constrain(cursor - (VISIBLE - 1), 0, fileCount > 0 ? fileCount - 1 : 0);  // Индекс первой видимой записи (0-based)

  selectedFile = "";             // Сбрасываем: если имя не найдём — не откроем мусор
  su::TextParser p(fileNames, "/");  // Парсер (index() нумерует записи С ЕДИНИЦЫ!)
  uint8_t row = 0;                  // Смещение видимой строки (0-based)
  while (p.parse()) {               // Циклически парсим строку имен
    int idx = (int)p.index() - 1;   // Переход к 0-based индексу записи
    if (idx < sidx) continue;       // Пропускаем записи выше окна прокрутки
    if (row >= VISIBLE) break;      // Окно заполнено
    oled.setCursor(6, firstRow + row);  // Ставим курсор на нужную строку
    oled.print(p.get());                // Выводим имя файла
    if (idx == cursor) selectedFile = p.get().toString();  // Запоминаем выбранное имя
    row++;
    yield();                          // Внутренний поллинг ESP
  }

  oled.setCursor(0, firstRow + (cursor - sidx));  // Курсор напротив выбранной записи
  oled.print(">");                                // Выводим галочку-курсор
  checkBatteryCharge();                  // Проверка напряжение аккума
  drawBatteryCharge();                   // Рисуем индикатор
  oled.update();                         // Выводим картинку
}
/* ======================================================================= */
/* ============================= Меню Wi-FI ============================== */
void drawStaMenu(void) {      // Рисуем STA меню
  oled.clear();               // Очистка
  oled.home();                // Возврат на 0,0
  oled.line(0, 10, 127, 10);  // Линия
  oled.print(F("РЕЖИМ STA")); // Выводим режим
  oled.setCursor(0, 2);
  oled.print(F("Сеть: "));
  oled.print(cfg.staSsid);   // Выводим имя сети
  oled.setCursor(0, 4);
  oled.print(F("Локал.IP:"));
  oled.print(WiFi.localIP());  // Выводим IP
  checkBatteryCharge();        // Проверка напряжение аккума
  drawBatteryCharge();         // Рисуем индикатор
  oled.update();               // Выводим картинку
}

void drawApMenu(void) {       // Рисуем AP меню
  oled.clear();               // Очистка
  oled.home();                // Возврат на 0,0
  oled.line(0, 10, 127, 10);  // Линия
  oled.print(F("РЕЖИМ AP"));  // Выводим режим
  oled.setCursor(0, 2);
  oled.print(F("Сеть: "));
  oled.print(cfg.apSsid);    // Выводим имя сети
  oled.setCursor(0, 4);
  oled.print(F("Ключ: "));
  oled.print(cfg.apPass);    // Выводим пароль
  oled.setCursor(0, 6);
  oled.print(F("Локал.IP:"));
  oled.print(F("192.168.4.1"));   // Выводим IP
  checkBatteryCharge();           // Проверка напряжение аккума
  drawBatteryCharge();            // Рисуем индикатор
  oled.update();                  // Выводим картинку
}

void fileReadError(void) {
  oled.clear();
  oled.setScale(2);
  oled.setCursorXY(28, 8);
  oled.print(F("ошибка"));
  oled.setCursorXY(28, 24);
  oled.print(F("чтения"));
  oled.setCursorXY(34, 40);
  oled.print(F("файла"));
  oled.setScale(1);
  oled.update();
  delay(1500);
}

void applyButtonTimeouts(void) {                 // единые таймауты кнопок
  ok.setHoldTimeout(1500);
  up.setHoldTimeout(1500);
  up.setStepTimeout(100);
  down.setStepTimeout(100);
  down.setHoldTimeout(600);                      // стандартные значения EncButton (в динозавре менялись)
  ok.setStepTimeout(200);
}

void setHanded(bool left) {                      // ориентация экрана и кнопок
  oled.flipH(left);
  oled.flipV(left);
  up = Button(left ? DWN_BTN_PIN : UP_BTN_PIN);
  down = Button(left ? UP_BTN_PIN : DWN_BTN_PIN);
  applyButtonTimeouts();
}

void applyHandedness(void) { setHanded(cfg.leftmode); }