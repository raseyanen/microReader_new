bool INVERT_IMG = 0;

// Вспомогательная функция: имя файла без ведущего пути ("/file.txt" -> "file.txt")
const char* basenameOf(const char* path) {
  const char* slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

/* ======================================================================= */
/* ========================== Работа с файлами =========================== */
void checkFileSystem(void) {           // Проверка и индексация файловой системы
  fileCount = badCount = 0;            // Обнуляем счетчики файлов
  fileNames = "";                      // Обнуляем список имен
  File root = LittleFS.open("/");      // Открываем директорию (корень)
  if (!root) return;                   // На всякий случай
  root.rewindDirectory();              // Гарантируем обход с начала директории
  while (File file = root.openNextFile()) {  // Шагаем по директории (ESP32 API вместо Dir)
    yield();                           // Внутренний поллинг
    if (file) {                        // Если файл существует
      su::Text filename(basenameOf(file.name()));  // Имя файла без ведущего "/"
      if ((filename.lengthUnicode() < MAX_FILENAME_LEN + 5) && (filename.endsWith(".txt") || filename.endsWith(".itxt") || filename.endsWith(".h") || filename.endsWith(".jpg") || filename.endsWith(".tex") || filename.endsWith(".md"))) {
        fileCount++;                   // Нормальный файл (Имя короткое, тип .txt / .itxt / .h / .jpg / .tex)
        fileNames += "/";              // + /
        fileNames += basenameOf(file.name());  // + Имя файла без ведущего "/"
      } else if (!filename.endsWith(".dat")) badCount++;     // Битый
    } else badCount++;                                       // Битый
    file.close();                                            // Закрываем файл
  }
}

/* ======================================================================= */
/* ============================ Чтение файла ============================= */
void drawPage(File file) {        // Отрисовка страницы на олед
  if (!file.available()) return;  // Если файл кончился - не выводим
  oled.clear();
  oled.home();                                 // Очистка и установка в начало дисплея
  while (!oled.isEnd() && file.available()) {  // Выводим символы пока не кончился дисплей или файл
    oled.write(file.read());                   // Транслируем символы в дисплей
  }
  oled.update();  // Выводим картинку
}

void enterToReadTxtFile(void) {        // Режим чтения файла
  String fn = ("/" + selectedFile);    // Собираем путь до файла
  File file = LittleFS.open(fn, "r");  // Открываем файл
  if (!file) {                         // Если сам файл не порядке
    fileReadError();
    checkFileSystem();  // Чекаем файловую систему
    drawMainMenu();     // Рисуем главное меню
    file.close();       // Закрываем файл
    return;             // Выходим
  }

  drawPage(file);          // Если с файлом все ок - рисуем первую страницу
  while (1) {              // Бесконечный цикл
    up.tick();
    ok.tick();
    down.tick();           // Опрос кнопок
    if (ok.click()) {      // Если ок нажат
      uiTimer = millis();  // Сбрасываем таймер дисплея
      drawMainMenu();      // Рисуем главное меню
      file.close();        // Закрываем файл
      return;              // Выходим
    }

    if (up.click() or up.step()) {             // Если нажата или удержана вверх
      uiTimer = millis();                      // Сбрасываем таймер дисплея
      long pos = file.position() - 500;        // Смещаем положение файла вверх
      if (pos < 0) pos = 0;                    // Если достигли нуля - не идем дальше
      file.seek(pos);                          // Устанавливаем указатель файла
      drawPage(file);                          // Рисуем страницу
    } else if (down.click() or down.step()) {  // Если нажата или удержана вниз
      uiTimer = millis();                      // Сбрасываем таймер дисплея
      drawPage(file);                          // Рисуем страницу
    }
    yield();  // Внутренний поллинг ESP
  }
}

void enterToReadBmpFile(void) {
  String fn = ("/" + selectedFile);    // Собираем путь до файла
  File file = LittleFS.open(fn, "r");  // Открываем файл
  if (!file) {                         // Если сам файл не порядке
    fileReadError();
    checkFileSystem();  // Чекаем файловую систему
    drawMainMenu();     // Рисуем главное меню
    file.close();       // Закрываем файл
    return;             // Выходим
  }

  uint8_t *img = new uint8_t[128 * 64];
  if (parseItxt(img, file)) {
    fileReadError();     // Выводим ошибку чтения
    delete[] img;        // Выгружаем буфер
    uiTimer = millis();  // Сбрасываем таймер дисплея
    drawMainMenu();      // Рисуем главное меню
    file.close();        // Закрываем файл
    return;              // Выходим
  }

  oled.clear();                             // Чистим олед
  oled.drawBitmap(0, 0, img, 128, 64);  // Выводим картинку
  oled.update();                            // Обновляем олед
  file.close();                             // Закрываем файл

  while (1) {              // Бесконечный цикл
    ok.tick();             // Опрос кнопки
    down.tick();           // Опрос кнопки
    if (ok.click()) {      // Если ок нажат
      uiTimer = millis();  // Сбрасываем таймер дисплея
      drawMainMenu();      // Рисуем главное меню
      delete[] img;        // Выгружаем буфер
      return;              // Выходим
    }
    if (down.click()) {    // Если нажали вниз
      File file = LittleFS.open(fn, "r");  // Открываем файл
      if (!file) {                         // Если сам файл не порядке
        fileReadError();
        checkFileSystem();   // Чекаем файловую систему
        file.close();        // Закрываем файл
        delete[] img;        // Выгружаем буфер
        uiTimer = millis();  // Сбрасываем таймер дисплея
        drawMainMenu();      // Рисуем главное меню
        return;              // Выходим
      }
      oled.clear();             // Залить чёрным
      INVERT_IMG = !INVERT_IMG; // Инвертировать
      if (parseItxt(img, file)) {
        fileReadError();     // Выводим ошибку чтения
        delete[] img;        // Выгружаем буфер
        uiTimer = millis();  // Сбрасываем таймер дисплея
        drawMainMenu();      // Рисуем главное меню
        file.close();        // Закрываем файл
        return;              // Выходим
      }
      oled.drawBitmap(0, 0, img, 128, 64);  // Выводим картинку
      oled.update();       // Обновить
      file.close();        // Закрываем файл
    }
    yield();  // Внутренний поллинг ESP
  }
}
// ---------------- JPEG через JPEGDEC ----------------
File jpgFileH;
uint16_t kW = 1, kH = 1;      // прореживание после аппаратного масштаба

void* jpgOpen(const char* name, int32_t* size) {
  jpgFileH = LittleFS.open(name, "r");
  if (!jpgFileH) return nullptr;
  *size = jpgFileH.size();
  return &jpgFileH;
}
void jpgClose(void* h) { if (jpgFileH) jpgFileH.close(); }
int32_t jpgRead(JPEGFILE* h, uint8_t* buf, int32_t len) { return jpgFileH ? jpgFileH.read(buf, len) : 0; }
int32_t jpgSeek(JPEGFILE* h, int32_t pos) { return jpgFileH ? jpgFileH.seek(pos) : 0; }

// Колбэк: блок 8-бит серых пикселей -> порог 127 -> точка на олед (с прореживанием kW/kH, как раньше)
int jpgDraw(JPEGDRAW* d) {
  const uint8_t* px = (const uint8_t*)d->pPixels;
  for (int j = 0; j < d->iHeight; j++) {
    int y = d->y + j;
    if (y % kH) continue;
    int oy = y / kH;
    if (oy >= 64) break;
    for (int i = 0; i < d->iWidth; i++) {
      int x = d->x + i;
      if (x % kW) continue;
      int ox = x / kW;
      if (ox >= 128) break;
      oled.dot(ox, oy, px[j * d->iWidth + i] > 127 ? !INVERT_IMG : INVERT_IMG);
    }
  }
  return 1;
}

bool drawJpg(const String& fn) {
  oled.clear();
  if (!jpeg.open(fn.c_str(), jpgOpen, jpgClose, jpgRead, jpgSeek, jpgDraw)) return false;
  int w = jpeg.getWidth(), h = jpeg.getHeight();
  int s = 1;                                              // аппаратный масштаб, пока картинка не меньше экрана
  while (s < 8 && (w / (s * 2)) >= 128 && (h / (s * 2)) >= 64) s *= 2;
  int opt = (s == 2) ? JPEG_SCALE_HALF : (s == 4) ? JPEG_SCALE_QUARTER : (s == 8) ? JPEG_SCALE_EIGHTH : 0;
  int sw = w / s, sh = h / s;
  kW = max(1, (int)ceil(sw / 128.0));
  kH = max(1, (int)ceil(sh / 64.0));
  jpeg.setPixelType(EIGHT_BIT_GRAYSCALE);
  int ok = jpeg.decode(0, 0, opt);
  jpeg.close();
  oled.update();
  return ok;
}

void enterToReadJpgFile(void) {
  String fn = ("/" + selectedFile);
  if (!LittleFS.exists(fn) || !drawJpg(fn)) {             // нет файла или не декодируется
    fileReadError();
    checkFileSystem();
    uiTimer = millis();
    drawMainMenu();
    return;
  }
  while (1) {
    down.tick();
    ok.tick();
    if (ok.click()) {
      uiTimer = millis();
      drawMainMenu();
      return;
    }
    if (down.click()) {                                   // инверсия
      INVERT_IMG = !INVERT_IMG;
      drawJpg(fn);
    }
    yield();
  }
}

uint8_t parseItxt(uint8_t *img, File file) {
  int imgLen = 0;                               // Длина+индекс для массива
  memset(img, 0, 1024);                         // Чистим буфер

  while (file.read() != '{') {                  // Ищем начало массива '{'
    if (!file.available()) return 1;            // Если не нашли - ошибка
    yield();                                    // Внутренний поллинг ESP
  }

  while (file.available()) {                    // Пока файл не кончился
    String line = file.readStringUntil('\n');   // Читаем по строке
    su::TextParser p(line.c_str(), ',');           // Готовим парсер (реальный API StringUtils)
    while (p.parse()) {                         // Парсим по ','
      uint8_t val = p.trim().toInt32HEX();      // Вытаскиваем байт
      if (INVERT_IMG) val = ~val;               // Если надо - инвертировать
      img[imgLen] = val;                        // Записать
      if (++imgLen >= 1023) return 0;           // Как только насобирали 1кб - выходим праздновать
      yield();                                  // Внутренний поллинг ESP
    } yield();                                  // На всякий случай еще и для внешнего цикла
  }

  return 1;                                     // не насобирали - ошибка
}
/* ======================================================================= */
