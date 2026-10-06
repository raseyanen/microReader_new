#if ENABLE_BITMAPS
bool INVERT_IMG = 0;
#endif

// Имя файла без ведущего пути ("/file.txt" -> "file.txt")
const char* basenameOf(const char* path) {
  const char* slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

/* ========================== Работа с файлами =========================== */
void checkFileSystem(void) {
  fileCount = badCount = 0;
  fileNames = "";
  File root = LittleFS.open("/");
  if (!root) return;
  root.rewindDirectory();
  while (File file = root.openNextFile()) {
    yield();
    if (file) {
      su::Text filename(basenameOf(file.name()));
      bool okExt = filename.endsWith(".txt") || filename.endsWith(".md") || filename.endsWith(".tex");
#if ENABLE_BITMAPS
      okExt = okExt || filename.endsWith(".itxt") || filename.endsWith(".h");
#endif
      if ((filename.lengthUnicode() < MAX_FILENAME_LEN + 5) && okExt) {
        fileCount++;
        fileNames += "/";
        fileNames += basenameOf(file.name());
      } else if (!filename.endsWith(".dat")) badCount++;   // старые .jpg теперь считаются "битыми" - удалите их через веб
    } else badCount++;
    file.close();
  }
}

/* ============================ Чтение файла ============================= */
void drawPage(File file) {
  if (!file.available()) return;
  oled.clear();
  oled.home();
  while (!oled.isEnd() && file.available()) oled.write(file.read());
  oled.update();
}

void enterToReadTxtFile(void) {
  String fn = ("/" + selectedFile);
  File file = LittleFS.open(fn, "r");
  if (!file) {
    fileReadError();
    checkFileSystem();
    drawMainMenu();
    return;
  }
  drawPage(file);
  while (1) {
    up.tick(); ok.tick(); down.tick();
    if (ok.hold()) {                               // выход - удержание ОК
      uiTimer = millis();
      drawMainMenu();
      file.close();
      return;
    }
    if (up.click() or up.step()) {
      uiTimer = millis();
      long pos = file.position() - 500;
      if (pos < 0) pos = 0;
      file.seek(pos);
      drawPage(file);
    } else if (down.click() or down.step()) {
      uiTimer = millis();
      drawPage(file);
    }
    yield();
  }
}

#if ENABLE_BITMAPS
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
#endif