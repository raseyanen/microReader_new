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
#define DWN_BTN_PIN       1             // GPIO для кнопки ВНИЗ    (ESP32-C3 Super Mini)
#define IIC_SDA_PIN       6             // GPIO SDA дисплея        (ESP32-C3 Super Mini)
#define IIC_SCL_PIN       7             // GPIO SCL дисплея        (ESP32-C3 Super Mini)
#define VBAT_ADC_PIN      1             // ADC1_CH1 для измерения напряжения батареи
#define VBAT_DIVIDER      2             // Коэффициент делителя (1 - без делителя, 2 - делитель 50/50)
#define EE_KEY            'B'           // Ключ EEPROM (1 байт) - измени, чтобы сбросить настройки
#define VBAT_FULL_MV      3600          // Напряжение питания при заряженном аккуме в (мВ)
#define VBAT_EMPTY_MV     2600          // Напряжение питания при севшем аккуме в (мВ)
#define _EB_DEB           25            // Дебаунс кнопок (мс)
#define GAME_SPEED        350           // Скорость  (меньше - быстрее)
#define CALCUL_TYPE       int64_t       // Тип переменной зачений в калькуляторе

#define T_SEGMENT 4            // Сегмент тетриса
#define MAX_WIDTH 64
#define MAX_HEIGHT 128
/* =========================================== */
/* ============ Список библиотек ============= */
#include <Wire.h>           // Либа I2C
//#include <EEPROM.h>       // Либа EEPROM
#include <FileData.h>       // Замена епрома
#include <LittleFS.h>       // Либа файловой системы (в ядре esp32 есть из коробки)
#include <GyverPortal.h>    // Либа веб морды (автоматически выберет WebServer для ESP32)
#include <StringUtils.h>    // Либа работы со строками (su::Text / su::TextParser)
#include <GyverOLED_fix.h>  // Либа олед-дисплея
#include <EncButton.h>      // Либа кнопок
#include <TJpg_Decoder.h>   // Либа jpg'а
#include <GyverTimer.h>     // Либа таймера

/* =========================================== */
/* ============ Список объектов ============== */
GyverPortal ui(&LittleFS);          // Портал
GyverOLED<SSD1306_128x64> oled;     // Олед
Button up(UP_BTN_PIN);              // Кнопка вверх
Button ok(OK_BTN_PIN);              // Кнопка ОК
Button down(DWN_BTN_PIN);           // Кнопка вниз
GTimer_ms gameTimer(GAME_SPEED); // Таймер игр

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
} sets;
FileData data(&LittleFS, "/data.dat", EE_KEY, &sets, sizeof(sets));  // замена епрома
#define SEGMENT (sets.tetrisSegment)

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
void enterToReadBmpFile(void);
void enterToReadJpgFile(void);
void enterToReadTexFile(void);                                           // texMath.ino
uint8_t parseItxt(uint8_t* img, File file);
bool oled_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);
uint8_t getBright(uint16_t clr);
void checkBatteryCharge(void);                                           // ui.ino
void drawBatteryCharge(void);
void drawMainMenu(void);
void drawStaMenu(void);
void drawApMenu(void);
void fileReadError(void);
void enterToServiceMode(void);                                           // servmode.ino
void enterToWifiMenu(void);                                              // wifi.ino / portal.ino
void enterToGameMode(void);                                              // gamemode.ino
/* =================================================== */

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);         // Лед на модуле как выход
  pinMode(UP_BTN_PIN, INPUT_PULLUP);
  pinMode(OK_BTN_PIN, INPUT_PULLUP);
  pinMode(DWN_BTN_PIN, INPUT_PULLUP);   // Все пины кнопок в режиме входа с подтяжкой

  batMv = readBatteryMv();

  if (!digitalRead(UP_BTN_PIN)) {       // Запуск с зажатой кнопкой вверх
    enterToServiceMode();               // Сервис мод со своей инициализацией
  }

  ok.setHoldTimeout(1500);              // Длинное удержание кнопки ОК - 1.5 секунды
  up.setHoldTimeout(1500);
  up.setStepTimeout(100);
  down.setStepTimeout(100);

  selectedFile.reserve(MAX_FILENAME_LEN + 6);
  fileNames.reserve(4096);              // Резервируем 2 глобальных строки

  //EEPROM.begin(100);                    // Инициализация EEPROM
  while (!LittleFS.begin()) {           // Инициализация файловой системы
    LittleFS.format();
  }

  /*if (EEPROM.read(0) != EE_KEY) {  // Если ключ еепром не совпадает
    EEPROM.write(0, EE_KEY);       // Пишем ключ
    EEPROM.put(1, sets);           // Пишем дефолтные настройки
    EEPROM.commit();               // Запись
  } else {                         // Если ключ совпадает
    EEPROM.get(1, sets);           // Читаем настройки
  }*/
  data.read();  // это заменяет то, что выше

  while (!oled.init(IIC_SDA_PIN, IIC_SCL_PIN)) {  // Инициализация оледа
    digitalWrite(LED_BUILTIN, LOW);               // Медленно мигаем пока не проинициализируем олед
    delay(500);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(500);
  }

  for (uint8_t i = 0; i < 6; i++) {  // Индикатор УСПЕШНОГО запуска ESP
    digitalWrite(LED_BUILTIN, LOW);
    delay(30);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(30);
  }

  digitalWrite(LED_BUILTIN, HIGH);

  Wire.setClock(600E3);
  oled.flipH(sets.leftmode); // Отзеркалить
  oled.flipV(sets.leftmode); // Отзеркалить
  if (sets.leftmode) {  // меняем кнопки
    up = Button(DWN_BTN_PIN);
    down = Button(UP_BTN_PIN);
  }
  oled.clear();              // Очистка оледа
  oled.update();             // Вывод пустой картинки
  oled.autoPrintln(true);    // Включаем автоперенос строки

  checkFileSystem();
  drawMainMenu();

  // The jpeg image can be scaled by a factor of 1, 2, 4, or 8
  TJpgDec.setJpgScale(1);
  TJpgDec.setCallback(oled_output); // Функция отправки на дисплей
  
  WIDTH = (64/SEGMENT - 16/SEGMENT);          // обновляем ширину 
  HEIGHT = (128/SEGMENT);                     // и высоту
}

void loop() {
  up.tick();
  ok.tick();
  down.tick();
  data.tick();  // тикаем память

  if (up.click()) {                                    // Если нажата или удержана кнопка вверх
    uiTimer = millis();                                // Сбрасываем таймер дисплея
    cursor = constrain(cursor - 1, 0, fileCount - 1);  // Двигаем курсор
    drawMainMenu();                                    // Обновляем главное меню
  } else if (down.click()) {                           // Если нажата или удержана кнопка вниз
    uiTimer = millis();                                // Сбрасываем таймер дисплея
    cursor = constrain(cursor + 1, 0, fileCount - 1);  // Двигаем курсор
    drawMainMenu();                                    // Обновляем главное меню
  }

  if (ok.click()) {                         // Если нажата ОК
    uiTimer = millis();                     // Сбрасываем таймер дисплея
    if (fileCount) {
      if (selectedFile.endsWith(".txt")) {  // Если файл - текстовый
        enterToReadTxtFile();               // Читаем как текст
      } else if (selectedFile.endsWith(".itxt") || selectedFile.endsWith(".h")) {  // Если битмап
        enterToReadBmpFile();               // Читаем как картинку
      } else if (selectedFile.endsWith(".tex")) {  // Если формула в TeX-синтаксисе
        enterToReadTexFile();                      // Рендерим формулы
      } else if (selectedFile.endsWith(".jpg")) {
        enterToReadJpgFile();
      }
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
