#define GAME_MENU_LINES 6
uint8_t gCursor = 0;

// ------------------ глубокий сон ------------------
// Выключает ESP32-C3 в deep sleep. Разбудить можно кнопкой ВВЕРХ (GPIO3):
// она подключена к GPIO с возможностью разбудить (RTCIO: GPIO0-5), поэтому
// используется EXT0-пробуждение по низкому уровню (кнопка подтянута вверх,
// нажатие тянет пин в GND). После пробуждения чип перезагружается заново
// (rtc_wakeup не сохраняет RAM). Остальные кнопки тоже переводятся в INPUT_PULLUP,
// чтобы ОК/ВНИЗ могли разбудить через OR-сумму низких уровней (EXT1 по маске пинов).
void enterToDeepSleep(void) {
  // экран "спокойной ночи"
  oled.clear();
  oled.home();
  oled.setScale(2);
  oled.setCursorXY(24, 16);
  oled.print(F("СОН..."));
  oled.setScale(1);
  oled.setCursorXY(8, 44);
  oled.print(F("ВВЕРХ - разбудить"));
  oled.update();
  delay(800);

  oled.setPower(false);           // гасим дисплей до сна
  data.tick();                    // дать FileData досписать настройки, если есть
  delay(50);

  // готовим будильники-пины: все три кнопки как входы с подтяжкой
  pinMode(UP_BTN_PIN, INPUT_PULLUP);
  pinMode(OK_BTN_PIN, INPUT_PULLUP);
  pinMode(DWN_BTN_PIN, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // лед погашен (активен LOW на многих модах)

  // маска пинов для EXT1 (пробуждение по ЛЮБОЙ из кнопок)
  uint64_t mask = (1ULL << UP_BTN_PIN) | (1ULL << OK_BTN_PIN) | (1ULL << DWN_BTN_PIN);
  esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);

  delay(100);                     // дать I2C/питанию успокоиться
  esp_deep_sleep_start();         // не возвращает управление
}

void enterToGameMode(void) {
  gCursor = 0;
  drawGameMenu();

  while (1) {
    up.tick();
    ok.tick();
    down.tick();
    data.tick();

    if (up.click()) {
      gCursor = constrain(gCursor - 1, 0, GAME_MENU_LINES - 1);
      drawGameMenu();
    } else if (down.click()) {
      gCursor = constrain(gCursor + 1, 0, GAME_MENU_LINES - 1);
      drawGameMenu();
    }

    if (ok.click()) {
      uiTimer = millis();
      switch (gCursor) {
        case 0:           // Динозаврик
          dinosaurGame();
          break;

        case 1:            // Тетрис
          tetrisGame();
          break;

        case 2:            // Змейка
          snakeGame();
          break;

        case 3:            // Калькулятор
          calcul();
          break;

        case 4:            // Глубокий сон (выключение)
          enterToDeepSleep();
          break;          // не возвращается

        case 5:            // Выход
          drawMainMenu();
          return;
      }
    }
    yield();
  }
}

void drawGameMenu(void) {
  oled.clear();               // Чистим дисплей
  oled.line(0, 10, 127, 10);  // Линия
  oled.print(F("GAME MODE"));   // Версия прошивки
  oled.setCursor(0, 2);       // Выводим с 2й строки
  
  oled.print(F(               // Выводим пункты
    "  ДИНОЗАВРИК\r\n"                                                    // ВНИМАНИЕ!   Тут какой-то глюк, с одним пробелом не работает
    "  ТЕТРИС\r\n"
    "  ЗМЕЙКА\r\n"
    "  КАЛЬКУЛЯТОР\r\n"
    "  ГЛУБОКИЙ СОН\r\n"
    "  ВЫХОД\r\n"));

  oled.setCursor(0, gCursor + 2);  // Указывем строку для курсора
  oled.print(">");                    // Выводим курсор
  checkBatteryCharge();               // Проверка напряжение аккума
  drawBatteryCharge();                // Рисуем индикатор
  oled.update();                      // Апдейтим дисплей
}
