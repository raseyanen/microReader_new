/* ===== Картинки .itxt / .h (массив байт 128x64). Включаются #define ENABLE_BITMAPS в главном .ino ===== */
#ifdef ENABLE_BITMAPS

bool INVERT_IMG = 0;

uint8_t parseItxt(uint8_t *img, File file) {
  int imgLen = 0;
  memset(img, 0, 1024);
  while (file.read() != '{') {
    if (!file.available()) return 1;
    yield();
  }
  while (file.available()) {
    String line = file.readStringUntil('\n');
    su::TextParser p(line.c_str(), ',');
    while (p.parse()) {
      uint8_t val = p.trim().toInt32HEX();
      if (INVERT_IMG) val = ~val;
      img[imgLen] = val;
      if (++imgLen >= 1023) return 0;
      yield();
    } yield();
  }
  return 1;
}

void enterToReadBmpFile(void) {
  String fn = ("/" + selectedFile);
  File file = LittleFS.open(fn, "r");
  if (!file) { fileReadError(); checkFileSystem(); drawMainMenu(); return; }
  uint8_t *img = new uint8_t[1024];
  if (parseItxt(img, file)) {
    fileReadError(); delete[] img; uiTimer = millis(); drawMainMenu(); file.close(); return;
  }
  oled.clear();
  oled.drawBmpFromRam(0, 0, img, 128, 64);
  oled.update();
  file.close();
  while (1) {
    ok.tick(); down.tick();
    if (ok.hold()) {                               // выход - удержание ОК
      waitOkRelease();
      uiTimer = millis(); drawMainMenu(); delete[] img; return;
    }
    if (down.click()) {                            // инверсия
      File f2 = LittleFS.open(fn, "r");
      if (!f2) { fileReadError(); checkFileSystem(); delete[] img; uiTimer = millis(); drawMainMenu(); return; }
      INVERT_IMG = !INVERT_IMG;
      if (parseItxt(img, f2)) { fileReadError(); delete[] img; uiTimer = millis(); drawMainMenu(); f2.close(); return; }
      oled.clear();
      oled.drawBmpFromRam(0, 0, img, 128, 64);
      oled.update();
      f2.close();
    }
    yield();
  }
}

#endif  // ENABLE_BITMAPS
