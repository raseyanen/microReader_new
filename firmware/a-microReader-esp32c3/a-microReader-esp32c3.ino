/*
    Скетч проекта "Электронная шпаргалка с Wi-Fi" v1.2 — порт под ESP32-C3 Super Mini
    Внимание! Эта версия хранит данные в FS, при перепрошивке всё сбросится!

    Ссылка на ядро для IDE: https://espressif.github.io/arduino-esp32/package_esp32_index.json
    Рекомендуемая версия ядра esp32 2.0.x / 3.x
    Рекомендуемые настройки Arduino IDE:
      Board: "ESP32C3 Dev Module"
        USB CDC On Boot: Enabled                  // Serial через встроенный USB
        Flash Mode: QIO
        Flash Size: 4MB (32Mb)
        Partition Scheme: "Minimal SPIFFS (1.9MB APP with OTA/1.9MB SPIFFS)"
                          // или "With OTA on SPIFFS", чтобы работал веб-OTA
        CPU Frequency: 160MHz (LPO)
        Erase All Flash Before Sketch Upload: Enabled
        Остальные параметры по умолчанию

    !!! ВАЖНО: ESP32-C3 Super Mini НЕ имеет аналога АЦП VCC у ESP8266,
    поэтому напряжение аккумулятора измеряется на GPIO1 (ADC1_CH1) через
    делитель напряжения 100к/100к (см. VBAT_DIVIDER ниже). Без делителя
    показания будут приблизительными.

    Распиновка для ESP32-C3 Super Mini (настраивается defines'ами ниже):
      Кнопка ВВЕРХ .... GPIO3
      Кнопка ОК ....... GPIO2
      Кнопка ВНИЗ ..... GPIO1   (совмещена с измерением батареи — см. ui.ino)
      SDA дисплея ..... GPIO6
      SCL дисплея ..... GPIO7
      Светодиод ....... LED_BUILTIN (GPIO8 на большинстве плат)
    При необходимости поменяй номера пинов в блоке "Настройки".
*/

/* ================ Настройки ================ */
#define MAX_FILENAME_LEN  12            // Максимальная длина имени файла (не включая .txt/.itxt)
#define AP_DEFAULT_SSID   "Reader AP"   // Стандартное имя точки доступа ESP (До 20-ти символов)
#define AP_DEFAULT_PASS   "00000000"    // Стандартный пароль точки доступа ESP (До 20-ти символов)
#define STA_DEFAULT_SSID  ""            // Стандартное имя точки доступа роутера (До 20-ти символов)
#define STA_DEFAULT_PASS  ""            // Стандартный пароль точки доступа роутера (До 20-ти символов)
#define STA_CONNECT_EN    0             // 1/0 - вкл./выкл. подключение к роутеру
#define OLED_CONTRAST     100           // Яркость дисплея по умолчанию (%)
#define LEFT_MODE         0             // Режим левши
#define WIFI_TIMEOUT_S    300           // Таймаут на отключение Wi-Fi (С)
#define UP_BTN_PIN        3             // GPIO для кнопки ВВЕРХ   (ESP32-C3 Super Mini)
#define OK_BTN_PIN        2             // GPIO для кнопки ОК      (ESP32-C3 Super Mini)
#define DWN_BTN_PIN       0             // GPIO для кнопки ВНИЗ    (ESP32-C3 Super Mini)
#define IIC_SDA_PIN       8             // GPIO SDA дисплея        (ESP32-C3 Super Mini)
#define IIC_SCL_PIN       9             // GPIO SCL дисплея        (ESP32-C3 Super Mini)
#define VBAT_ADC_PIN      1             // ADC1_CH1 для измерения напряжения батареи
#define VBAT_DIVIDER      2             // Коэффициент делителя (1 - без делителя, 2 - делитель 50/50)
#define VBAT_FULL_MV      3600          // Напряжение питания при заряженном аккуме в (мВ)
#define VBAT_EMPTY_MV     2600          // Напряжение питания при севшем аккуме в (мВ)
#define _EB_DEB           25            // Дебаунс кнопок (мс)
#define GAME_SPEED        350           // Скорость  (меньше - быстрее)
//#define ENABLE_BITMAPS         // раскомментируйте для поддержки картинок .itxt / .h

#define SETT_NO_DB        // без GyverDB (привязываем переменные по указателю)
#define SETT_NO_TABLE     // без таблиц и графиков Settings

#define T_SEGMENT 4            // Сегмент тетриса
#define MAX_WIDTH 64
#define MAX_HEIGHT 128
/* =========================================== */
/* ============ Список библиотек ============= */
#include <Wire.h>           // Либа I2C
//#include <EEPROM.h>       // Либа EEPROM
#include <SaverFile.h>       // Замена епрома
#include <LittleFS.h>       // Либа файловой системы (в ядре esp32 есть из коробки)
#include <SettingsESP.h>    // Либа веб морды (автоматически выберет WebServer для ESP32)
#include <StringUtils.h>  // GyverLibs StringUtils (su::Text / su::TextParser)
#include <GyverOLED.h>  // Либа олед-дисплея
#include <EncButton.h>      // Либа кнопок
#include <GTimer.h>     // Либа таймера
#include "driver/gpio.h"

/* =========================================== */
/* ============ Список объектов ============== */
SettingsESP sett("Wi-Fi Reader");          // Портал
GyverOLED<SSD1306_128x64> oled;     // Олед
Button up(UP_BTN_PIN);              // Кнопка вверх
Button ok(OK_BTN_PIN);              // Кнопка ОК
Button down(DWN_BTN_PIN);           // Кнопка вниз
GTimer<millis> gameTimer(GAME_SPEED, true); // Таймер игр

/* =========================================== */
/* ========= Глобальные переменные =========== */
struct {                                // Структура со всеми настройками
  char apSsid[21] = AP_DEFAULT_SSID;    // Имя сети для AP режима по умолчанию
  char apPass[21] = AP_DEFAULT_PASS;    // Пароль сети для AP режима по умолчанию
  char staSsid[21] = STA_DEFAULT_SSID;  // Имя сети для STA режима по умолчанию
  char staPass[21] = STA_DEFAULT_PASS;  // Пароль сети для STA режима по умолчанию
  bool staModeEn = STA_CONNECT_EN;      // Подключаться к роутеру по умолчанию?
  int dispContrast = OLED_CONTRAST;     // Яркость оледа
  bool leftmode = LEFT_MODE;            // Режим левши
  uint8_t tetrisSegment = T_SEGMENT;    // Сегмент тетриса
  uint16_t dinoBestScore = 0;           // Счёт динозавра
  uint16_t tetrBestScore = 0;           // Счёт тетриса
  uint16_t snakeBestScore = 0;          // Счёт змейки
} cfg;

SaverFile saver(LittleFS, "/data.dat", cfg);   // CRC, атомарная запись, запись после паузы в изменениях

#define SEGMENT (cfg.tetrisSegment)

String selectedFile = "";  // Имя выбранной строки
String fileNames = "";     // Имена всех читаемых файлов
int16_t fileCount = 0;     // Количество читаемых файлов
int16_t badCount = 0;      // Количество битых файлов
int16_t cursor = 0;        // Указатель (курсор) меню
int16_t batMv = 3000;      // Напряжение питания ESP
uint32_t uiTimer = 0;      // Таймер таймаута дисплея
uint32_t batTimer = 0;     // Таймер опроса АКБ
uint8_t oledbuf[MAX_WIDTH * MAX_HEIGHT]; // замена матрицы для игр
uint8_t buttons;

bool loadingFlag = 0;      // Флаг игр (остался от прошивки Гайвера)
#define X0 16  // сдвиг по ширине для тетриса
uint8_t WIDTH = (64/SEGMENT - 16/SEGMENT);          // -1 (для текста) // ширина для того же тетриса
uint8_t HEIGHT = (128/SEGMENT);                     // и высота

bool locked = false;                 // экран PIN активен
char pinCode[5] = "";                // пусто = PIN выключен
void lockTick(void);                 // lock.ino
void pinLoad(void);
bool pinSet(const String& s);
/* =========================================== */

/* ========= Измерение напряжения батареи (ESP32-C3) ========== */
// ESP32-C3 не умеет измерять VCC как ESP8266 (ESP.getVcc()),
// поэтому читаем ADC1_CH1 и умножаем на коэффициент делителя.
uint16_t readBatteryMv(void) {
  analogSetPinAttenuation(VBAT_ADC_PIN, ADC_11db);   // диапазон до ~3.3 В на пине
  uint32_t mv = analogReadMilliVolts(VBAT_ADC_PIN);  // мВ на пине АЦП
  return (uint16_t)(mv * VBAT_DIVIDER);              // восстанавливаем напряжение батареи
}
/* =========================================== */

/* ==== Прототипы функций из других табов (.ino) ==== */
// Arduino IDE генерирует прототипы автоматически, но вставляет их ПЕРЕД
// #include'ами, из-за чего типы из библиотек (su::Text, TexNode и т.п.)
// остаются неизвестными. Объявляем прототипы вручную:
void checkFileSystem(void);                                              // files.ino
void drawPage(File file);
void enterToReadTxtFile(void);
#ifdef ENABLE_BITMAPS
void enterToReadBmpFile(void);
uint8_t parseItxt(uint8_t* img, File file);
#endif
void enterToReadTexFile(void);                                           // texMath.ino
void enterToReadMdFile(void);                                            // texMd.ino
void checkBatteryCharge(void);                                           // ui.ino
void drawBatteryCharge(void);
void drawMainMenu(void);
void drawStaMenu(void);
void drawApMenu(void);
void fileReadError(void);
void enterToServiceMode(void);                                           // servmode.ino
void enterToWifiMenu(void);                                              // wifi.ino / portal.ino
void enterToGameMode(void);                                              // gamemode.ino
void enterToDeepSleep(void);                                             // gamemode.ino (deep sleep)
const char* basenameOf(const char* path);
void waitOkRelease(void);
void applyHandedness(void);
void build(sets::Builder& b);
void displayBegin(void);
void ledSet(bool on);
void validateNetSettings(void);
void applyContrast(void);
void applySegment(void);
void applyButtonTimeouts(void);
void setHanded(bool left);
void applyHandedness(void);
/* =================================================== */

/* ======= Безопасный запуск дисплея =======
   Светодиод ESP32-C3 Super Mini сидит на GPIO8 - это ТА ЖЕ нога, что SDA дисплея.
   Если дёргать LED_BUILTIN как обычный GPIO, линия I2C залипает: дисплей остаётся
   неинициализированным и показывает "белый шум" (при этом светодиод горит).
   Поэтому светодиод трогаем только если он не совпадает с пинами I2C. */
#define LED_SHARED_WITH_I2C ((LED_BUILTIN) == (IIC_SDA_PIN) || (LED_BUILTIN) == (IIC_SCL_PIN))

void ledSet(bool on) {                                 // on = true -> светодиод горит (плата с активным LOW)
  if (LED_SHARED_WITH_I2C) return;
  digitalWrite(LED_BUILTIN, on ? LOW : HIGH);
}

// Если сброс случился посреди передачи, дисплей может держать SDA низким - тактируем SCL и шлём STOP
static void i2cRecover(uint8_t sda, uint8_t scl) {
  pinMode(sda, INPUT_PULLUP);
  pinMode(scl, OUTPUT);
  digitalWrite(scl, HIGH);
  for (uint8_t i = 0; i < 9 && !digitalRead(sda); i++) {
    digitalWrite(scl, LOW);  delayMicroseconds(10);
    digitalWrite(scl, HIGH); delayMicroseconds(10);
  }
  pinMode(sda, OUTPUT);
  digitalWrite(sda, LOW);  delayMicroseconds(10);       // STOP: SDA низ->верх при высоком SCL
  digitalWrite(scl, HIGH); delayMicroseconds(10);
  digitalWrite(sda, HIGH); delayMicroseconds(10);
  pinMode(sda, INPUT_PULLUP);
  pinMode(scl, INPUT_PULLUP);
}

void displayBegin(void) {
  i2cRecover(IIC_SDA_PIN, IIC_SCL_PIN);
  Wire.begin(IIC_SDA_PIN, IIC_SCL_PIN);
  Wire.setClock(400000);                                // на время инициализации - умеренная скорость
  Wire.beginTransmission(0x3C);
  bool found = (Wire.endTransmission() == 0);
  if (!found) {
    Wire.beginTransmission(0x3D);
    found = (Wire.endTransmission() == 0);
  }
  Serial.println(found ? F("oled: найден") : F("oled: НЕ ОТВЕЧАЕТ - проверь пины SDA/SCL и пайку"));
  oled.init(IIC_SDA_PIN, IIC_SCL_PIN);                  // работает и с версией библиотеки, где init() возвращает bool
  Wire.setClock(600E3);
  oled.clear();
  oled.update();
}

void setup() {
  Serial.begin(115200);
  Serial.println(F("boot"));
  if (!LED_SHARED_WITH_I2C) pinMode(LED_BUILTIN, OUTPUT);   // не трогаем светодиод, если он на линии I2C
  pinMode(UP_BTN_PIN, INPUT_PULLUP);
  pinMode(OK_BTN_PIN, INPUT_PULLUP);
  pinMode(DWN_BTN_PIN, INPUT_PULLUP);   // Все пины кнопок в режиме входа с подтяжкой

  batMv = readBatteryMv();

  if (!digitalRead(UP_BTN_PIN)) {       // Запуск с зажатой кнопкой вверх
    enterToServiceMode();               // Сервис мод со своей инициализацией
  }

  selectedFile.reserve(MAX_FILENAME_LEN + 6);
  fileNames.reserve(1024);              // 1 КБ хватает на ~60 файлов, String при необходимости вырастет сам

  displayBegin();                       // дисплей поднимаем ПЕРВЫМ, чтобы было видно, что происходит
  oled.autoPrintln(true);
  oled.home();
  oled.print(F("Запуск..."));
  oled.update();

  if (!LittleFS.begin()) {              // файловая система не смонтировалась - форматируем
    Serial.println(F("fs: формат (до пары минут)"));
    oled.clear();
    oled.home();
    oled.print(F("Формат ФС..."));
    oled.setCursor(0, 2);
    oled.print(F("подождите, это"));
    oled.setCursor(0, 3);
    oled.print(F("может занять минуту"));
    oled.update();
    LittleFS.format();
    if (!LittleFS.begin()) {            // и это не помогло - проблема в таблице разделов
      Serial.println(F("fs: ОШИБКА - проверь Partition Scheme / partitions.csv"));
      oled.clear();
      oled.home();
      oled.print(F("ОШИБКА ФС"));
      oled.setCursor(0, 2);
      oled.print(F("Partition Scheme?"));
      oled.update();
      while (1) delay(1000);
    }
  }
  Serial.println(F("fs ok"));

  saver.begin();                        // настройки и рекорды
  Serial.println(F("settings ok"));

  applyHandedness();                    // экран и кнопки по режиму левши (после init дисплея!)

  oled.clear();
  oled.update();

  checkFileSystem();
  pinLoad();
  locked = (pinCode[0] != 0);
  if (!locked) drawMainMenu();
  Serial.println(F("setup done"));

  WIDTH = (64/SEGMENT - 16/SEGMENT);    // обновляем ширину
  HEIGHT = (128/SEGMENT);               // и высоту
}

void loop() {
  up.tick();
  ok.tick();
  down.tick();
  saver.tick();  // тикаем память
  
  if (locked) { lockTick(); return; }     // экран PIN; игры - удержанием ВВЕРХ

  if (up.click()) {                                    // Если нажата или удержана кнопка вверх
    uiTimer = millis();                                // Сбрасываем таймер дисплея
    cursor = constrain(cursor - 1, 0, fileCount - 1);  // Двигаем курсор
    drawMainMenu();                                    // Обновляем главное меню
  } else if (down.click()) {                           // Если нажата или удержана кнопка вниз
    uiTimer = millis();                                // Сбрасываем таймер дисплея
    cursor = constrain(cursor + 1, 0, fileCount - 1);  // Двигаем курсор
    drawMainMenu();                                    // Обновляем главное меню
  }

  if (ok.click()) {
    uiTimer = millis();
    if (fileCount) {
      if (selectedFile.endsWith(".txt")) enterToReadTxtFile();
      else if (selectedFile.endsWith(".md")) enterToReadMdFile();
      else if (selectedFile.endsWith(".tex")) enterToReadTexFile();
#ifdef ENABLE_BITMAPS
      else if (selectedFile.endsWith(".itxt") || selectedFile.endsWith(".h")) enterToReadBmpFile();
#endif
    }
  }

  if (ok.hold()) {
    uiTimer = millis();  // Сбрасываем таймер дисплея
    enterToWifiMenu();   // Переходим в меню wifi
  }

  if (up.hold()) {
    uiTimer = millis();  // Сбрасываем таймер дисплея
    //dinosaurGame();
    //tetrisGame();
    enterToGameMode();
  }

  if (millis() - uiTimer >= 5000) {  // Каждые 5 сек принудительно обновляем дисп. ради индикации заряда
    uiTimer = millis();
    drawMainMenu();
  }
}
