// Имя файла без ведущего пути ("/file.txt" -> "file.txt")
const char* basenameOf(const char* path) {
  const char* slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

// После выхода по удержанию ОК ждём отпускания, иначе главный цикл увидит удержание и откроет Wi-Fi
void waitOkRelease(void) {
  uint32_t t = millis();
  while (!digitalRead(OK_BTN_PIN) && millis() - t < 5000) { delay(5); yield(); }   // нажата = LOW
  ok.tick();
  ok.tick();
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
#ifdef ENABLE_BITMAPS
      okExt = okExt || filename.endsWith(".itxt") || filename.endsWith(".h");
#endif
      if ((filename.lengthUnicode() < MAX_FILENAME_LEN + 5) && okExt) {
        fileCount++;
        fileNames += "/";
        fileNames += basenameOf(file.name());
      } else if (!filename.endsWith(".dat")) badCount++;
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
      waitOkRelease();
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