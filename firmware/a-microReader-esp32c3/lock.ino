/* ===== PIN-код при запуске. Игры доступны без PIN (удержание ВВЕРХ) ===== */
static uint8_t lkDigit[4] = {0, 0, 0, 0};
static uint8_t lkPos = 0, lkFails = 0;
static uint32_t lkUntil = 0, lkRedraw = 0;
static bool lkDirty = true;

void pinLoad(void) {
  pinCode[0] = 0;
  File f = LittleFS.open("/pin.dat", "r");
  if (!f) return;
  char b[5] = {0};
  f.readBytes(b, 4);
  f.close();
  for (uint8_t i = 0; i < 4; i++) if (b[i] < '0' || b[i] > '9') return;   // битый файл - PIN выключен
  memcpy(pinCode, b, 5);
}

// "" - выключить PIN, 4 цифры - установить (вступает в силу при следующем запуске)
bool pinSet(const String& s0) {
  String s = s0; s.trim();
  if (s.length() == 0) { LittleFS.remove("/pin.dat"); pinCode[0] = 0; return true; }
  if (s.length() != 4) return false;
  for (uint8_t i = 0; i < 4; i++) if (!isDigit(s[i])) return false;
  File f = LittleFS.open("/pin.dat", "w");
  if (!f) return false;
  f.write((const uint8_t*)s.c_str(), 4);
  f.close();
  memcpy(pinCode, s.c_str(), 5);
  return true;
}

static void lockDraw(bool wait) {
  oled.autoPrintln(false);
  oled.clear();
  oled.setScale(1);
  oled.setCursorXY(0, 0);
  oled.print("Введите PIN");
  oled.setScale(2);
  for (uint8_t i = 0; i < 4; i++) {
    int16_t x = 8 + i * 30;
    bool cur = (i == lkPos) && !wait;
    oled.rect(x, 14, x + 21, 37, cur ? OLED_FILL : OLED_STROKE);
    oled.invertText(cur);
    oled.setCursorXY(x + 5, 19);
    if (i < lkPos) oled.print("*");
    else if (cur) oled.print((int)lkDigit[i]);
    else oled.print("-");
    oled.invertText(false);
  }
  oled.setScale(1);
  if (wait) {
    oled.setCursorXY(0, 44);
    oled.print("Неверно, ждите ");
    oled.print((int)((lkUntil - millis()) / 1000 + 1));
    oled.print(" с");
  }
  oled.setCursorXY(0, 56);
  oled.print("ВВЕРХ удерж. - игры");
  oled.update();
  oled.autoPrintln(true);
}

void lockTick(void) {
  if (up.hold()) {                                   // игры без PIN
    enterToGameMode();
    lkPos = 0; memset(lkDigit, 0, 4);
    lkDirty = true;
    uiTimer = millis();
    return;
  }
  bool wait = millis() < lkUntil;
  if (lkDirty || (wait && millis() - lkRedraw > 500)) {
    lkRedraw = millis();
    lkDirty = false;
    lockDraw(wait);
  }
  if (wait) return;                                  // во время паузы ввод игнорируется
  if (up.click()) { lkDigit[lkPos] = (lkDigit[lkPos] + 1) % 10; lkDirty = true; }
  else if (down.click()) { lkDigit[lkPos] = (lkDigit[lkPos] + 9) % 10; lkDirty = true; }
  if (ok.click()) {
    if (lkPos < 3) { lkPos++; lkDirty = true; return; }
    bool good = true;
    for (uint8_t i = 0; i < 4; i++) if (lkDigit[i] != (uint8_t)(pinCode[i] - '0')) good = false;
    lkPos = 0; memset(lkDigit, 0, 4);
    if (good) { locked = false; lkFails = 0; uiTimer = millis(); drawMainMenu(); return; }
    if (lkFails < 20) lkFails++;
    lkUntil = millis() + 3000UL * lkFails;
    lkDirty = true;
  }
}
