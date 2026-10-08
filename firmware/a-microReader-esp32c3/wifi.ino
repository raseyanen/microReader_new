// общий цикл меню Wi-Fi: redraw - функция перерисовки меню, ap - режим точки доступа
static void wifiServe(void (*redraw)(void), bool ap) {
  sett.setVersion("FW.V1.2-C3");
  sett.begin(ap);                           // captive portal нужен только в режиме точки доступа
  sett.onBuild(build);
  while (1) {
    ok.tick();
    up.tick();
    down.tick();
    saver.tick();
    if (sett.focused()) uiTimer = millis();     // пока страница открыта в браузере - таймаут не срабатывает

    if (up.click() || up.hold()) {              // яркость +
      uiTimer = millis();
      cfg.dispContrast = constrain(cfg.dispContrast + 10, 10, 100);
      applyContrast();
    }
    if (down.click() || down.hold()) {          // яркость -
      uiTimer = millis();
      cfg.dispContrast = constrain(cfg.dispContrast - 10, 10, 100);
      applyContrast();
    }

    if (ok.click() || (millis() - uiTimer) >= WIFI_TIMEOUT_S * 1000UL) {   // кнопка или таймаут
      uiTimer = millis();
      validateNetSettings();
      checkFileSystem();                        // подхватить файлы, загруженные/удалённые через веб
      drawMainMenu();
      saver.write();                            // сохранить настройки сразу
      if (ap) WiFi.softAPdisconnect();
      WiFi.mode(WIFI_OFF);
      return;
    }

    if (millis() - batTimer >= 5000) {          // перерисовка меню ради индикации заряда
      batTimer = millis();
      redraw();
    }
    sett.tick();
    yield();
  }
}

void enterToWifiMenu(void) {
  validateNetSettings();
  oled.clear();
  oled.home();
  oled.line(0, 10, 127, 10);
  oled.print(F("WI-FI МЕНЮ"));
  checkBatteryCharge();
  drawBatteryCharge();
  oled.update();

  if (cfg.staModeEn) {                          // подключиться к роутеру
    oled.clear();
    oled.home();
    oled.line(0, 10, 127, 10);
    oled.print(F("WI-FI МЕНЮ"));
    oled.setCursor(0, 2);
    oled.print(F("Подключение"));
    checkBatteryCharge();
    drawBatteryCharge();
    oled.update();

    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.staSsid, cfg.staPass);
    oled.setCursor(66, 2);
    for (uint8_t i = 0; i < 10; i++) {          // 10 секунд на подключение
      if (WiFi.status() != WL_CONNECTED) {
        oled.print(".");
        oled.update();
        delay(1000);
      } else {
        drawStaMenu();
        wifiServe(drawStaMenu, false);
        return;
      }
    }
  }

  WiFi.mode(WIFI_AP);                           // STA не получился или выключен - точка доступа
  WiFi.softAP(cfg.apSsid, cfg.apPass);
  drawApMenu();
  wifiServe(drawApMenu, true);
}