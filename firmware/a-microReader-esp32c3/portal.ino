/* ================= Веб-морда на Settings (GyverLibs) ================= */

// то, что раньше проверялось при сабмите формы
void validateNetSettings(void) {
  if (strlen(cfg.apSsid) < 1) strcpy(cfg.apSsid, AP_DEFAULT_SSID);   // пустое имя сети AP
  if (strlen(cfg.apPass) < 8) strcpy(cfg.apPass, AP_DEFAULT_PASS);   // короткий пароль AP
  if (cfg.staModeEn && (strlen(cfg.staSsid) < 1 || strlen(cfg.staPass) < 8)) {
    cfg.staModeEn = false;                                           // битые имя/пароль - выключаем коннект
  }
}

void applyContrast(void) {
  oled.setContrast(map(cfg.dispContrast, 10, 100, 1, 255));
}

void applySegment(void) {
  cfg.tetrisSegment = constrain(cfg.tetrisSegment, 1, 8);            // 0 раньше приводило к делению на ноль
  WIDTH = 64 / cfg.tetrisSegment - 16 / cfg.tetrisSegment;
  HEIGHT = 128 / cfg.tetrisSegment;
}

void build(sets::Builder& b) {
  {
    sets::Group g(b, "Точка доступа");
    b.Input("Имя сети", cfg.apSsid);
    b.Pass("Пароль (от 8 символов)", cfg.apPass);
  }
  {
    sets::Group g(b, "Подключение к сети");
    b.Input("Имя сети", cfg.staSsid);
    b.Pass("Пароль", cfg.staPass);
    b.Switch("Автоподключение", &cfg.staModeEn);
  }
  {
    sets::Group g(b, "Другое");
    if (b.Switch("Режим левши", &cfg.leftmode)) applyHandedness();
    if (b.Number("Размер сегмента игр", &cfg.tetrisSegment, 1, 8)) applySegment();
    if (b.Slider("Яркость", 10, 100, 10, "%", &cfg.dispContrast)) applyContrast();
    if (b.Input("PIN (4 цифры, пусто = выкл.)", pinCode, R"(^(\d{4})?$)", "Только 4 цифры")) {
      if (!pinSet(String(pinCode))) pinLoad();     // неверный формат - вернуть сохранённый PIN
    }
  }
}