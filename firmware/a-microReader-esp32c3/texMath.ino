/* ======================================================================= */
/* ================ Рендер формул в TeX-синтаксисе (.tex) ================ */
/* Миникалькулятор формул для OLED 128x64 (GyverOLED).                    */
/* Поддерживается:                                                         */
/*   ^{...} _{...}          степени / индексы                              */
/*   \frac{a}{b}            дробь (числитель над чертой, знаменатель под)  */
/*   \sqrt{...}             квадратный корень                               */
/*   \int_a^b{...}          интеграл с пределами                          */
/*   \sum_{a}^{b}{...}      сумма с пределами                              */
/*   \cdot \times \div \pm \leq \geq \neq \approx \infty \pi \alpha \beta  */
/*   \gamma \omega \theta \lambda \mu \sigma \Delta \Omega \to \rightarrow */
/*   \left( \right) \bigl \bigr  -> просто скобки                          */
/*   { ... } $ ... $        группировка (окружение можно писать без $)     */
/* Остальные символы выводятся как текст.                                   */
/* ======================================================================= */

#define TEX_MAX_LINES 5      // максимум строк формулы на странице
#define TEX_LINE_LEN 96      // максимум символов в строке
#include "texFont.h"         // мини-шрифт 5x7 для рендера формул
static char texLines[TEX_MAX_LINES][TEX_LINE_LEN];
static uint8_t texLineCount = 0;
static int8_t texPage = 0;   // текущая страница (блок строк)

// ------------------ разбор дерева формул ------------------
struct TexNode {
  enum Type { TEXT, SUP, SUB, FRAC, SQRT, BIGOP, BINOP_SYM } type;
  const char* text;         // для TEXT / BINOP_SYM
  uint8_t len;              // длина текста
  TexNode* a = nullptr;     // числитель / тело
  TexNode* b = nullptr;     // знаменатель / пределы снизу
  TexNode* c = nullptr;     // пределы сверху
  TexNode* next = nullptr;  // следующая нода в списке
};

static TexNode* texAlloc() {
  static TexNode pool[160];
  static uint8_t used = 0;
  if (used >= 160) used = 0;                 // кольцевой пул (перерисовка страницы)
  TexNode* n = &pool[used++];
  *n = TexNode();
  return n;
}

static const char* texSkipWs(const char* p) { while (*p == ' ' || *p == '\t') p++; return p; }

// читает группу {...} или один символ; возвращает ноду-список или nullptr
static TexNode* texParseGroup(const char*& p);
static TexNode* texParseSeq(const char*& p, char terminator);

static TexNode* texMakeText(const char* s, uint8_t l) {
  TexNode* n = texAlloc();
  n->type = TexNode::TEXT; n->text = s; n->len = l;
  return n;
}

struct TexSym { const char* cmd; const char* repl; };
static const TexSym texSyms[] = {
  {"cdot", "\xB7"}, {"times", "x"}, {"div", "/"}, {"pm", "+-"}, {"mp", "-+"},
  {"leq", "<="}, {"geq", ">="}, {"neq", "!="}, {"approx", "~="},
  {"infty", "INF"}, {"to", "->"}, {"rightarrow", "->"}, {"leftarrow", "<-"},
  {"pi", "p"}, {"alpha", "a"}, {"beta", "b"}, {"gamma", "g"}, {"delta", "d"},
  {"epsilon", "e"}, {"theta", "th"}, {"lambda", "l"}, {"mu", "m"}, {"nu", "n"},
  {"omega", "w"}, {"Sigma", "S"}, {"sigma", "s"}, {"phi", "f"}, {"psi", "ps"},
  {"tau", "t"}, {"rho", "r"}, {"Delta", "D"}, {"Omega", "W"}, {"Gamma", "G"},
  {"int", ""}, {"sum", ""}, {"prod", ""},   // обрабатываются отдельно
};

static void texAppendChar(char* buf, uint8_t& i, char c) { if (i < TEX_LINE_LEN - 1) buf[i++] = c; }

// ---------------- парсер ----------------
static TexNode* texParseSeq(const char*& p, char terminator) {
  TexNode* head = nullptr, *tail = nullptr;
  while (*p && *p != terminator) {
    p = texSkipWs(p);
    if (!*p || *p == terminator) break;
    TexNode* node = nullptr;

    if (*p == '{') {
      p++;
      TexNode* g = texParseSeq(p, '}');
      if (*p == '}') p++;
      // группа как wrapper: a = содержимое, b = nullptr => рисуется inline
      node = texAlloc();
      node->type = TexNode::FRAC;
      node->a = g;
    } else if (*p == '\\') {
      p++;
      // командное слово
      const char* cmdStart = p;
      while (isalpha((uint8_t)*p)) p++;
      uint8_t cmdLen = p - cmdStart;
      if (cmdLen == 0) {                     // экранированный символ (\, \% ...)
        if (*p) { node = texMakeText(p, 1); p++; }
      } else if (!strncmp(cmdStart, "frac", cmdLen) && cmdLen == 4) {
        node = texAlloc(); node->type = TexNode::FRAC;
        p = texSkipWs(p); node->a = texParseGroup(p);
        p = texSkipWs(p); node->b = texParseGroup(p);
      } else if (!strncmp(cmdStart, "sqrt", cmdLen) && cmdLen == 4) {
        node = texAlloc(); node->type = TexNode::SQRT;
        p = texSkipWs(p); node->a = texParseGroup(p);
      } else if ((!strncmp(cmdStart, "int", cmdLen) && cmdLen == 3) ||
                 (!strncmp(cmdStart, "sum", cmdLen) && cmdLen == 3) ||
                 (!strncmp(cmdStart, "prod", cmdLen) && cmdLen == 4)) {
        node = texAlloc(); node->type = TexNode::BIGOP;
        node->text = cmdStart; node->len = cmdLen;
        p = texSkipWs(p);
        // пределы _{..} ^{..} в любом порядке
        while (*p == '_' || *p == '^') {
          char t = *p; p++;
          TexNode* lim = texParseGroup(p);
          if (t == '_') node->b = lim; else node->c = lim;
          p = texSkipWs(p);
        }
        p = texSkipWs(p);
        node->a = texParseGroup(p);          // тело (опционально)
      } else if (!strncmp(cmdStart, "left", cmdLen) || !strncmp(cmdStart, "right", cmdLen) ||
                 !strncmp(cmdStart, "bigg", cmdLen) || !strncmp(cmdStart, "big", cmdLen) ||
                 !strncmp(cmdStart, "mathrm", cmdLen) || !strncmp(cmdStart, "text", cmdLen)) {
        continue;                             // пропускаем: дальше обычный символ/группа
      } else {
        bool found = false;
        for (auto& s : texSyms) {
          if (strlen(s.cmd) == cmdLen && !strncmp(cmdStart, s.cmd, cmdLen)) {
            node = texMakeText(s.repl, strlen(s.repl)); found = true; break;
          }
        }
        if (!found) node = texMakeText(cmdStart, cmdLen);  // неизвестная команда - как текст
      }
    } else if (*p == '^' || *p == '_') {
      char t = *p; p++;
      TexNode* arg = texParseGroup(p);
      node = texAlloc();
      node->type = (t == '^') ? TexNode::SUP : TexNode::SUB;
      node->a = arg;
    } else if (*p == '$') {
      p++; continue;                          // внутристрочный маркер игнорируем
    } else {
      // обычный текст до следующего спецсимвола
      const char* start = p;
      while (*p && !strchr("^_\\{}$ ", *p)) p++;
      node = texMakeText(start, p - start);
    }

    if (node) {
      if (tail) { tail->next = node; tail = node; }
      else { head = tail = node; }
    }
  }
  return head;
}

static TexNode* texParseGroup(const char*& p) {
  p = texSkipWs(p);
  if (*p == '{') { p++; TexNode* g = texParseSeq(p, '}'); if (*p == '}') p++; return g; }
  if (*p) { const char* s = p; p++; return texMakeText(s, 1); }
  return nullptr;
}

// ---------------- растризация в буфер 128x64 ----------------
static uint8_t texBuf[128 * 64 / 8];
static int16_t texMinY, texMaxY;

static void texPix(int16_t x, int16_t y, bool on) {
  if (x < 0 || x > 127 || y < 0 || y > 63) return;
  if (on) texBuf[y * 16 + (x >> 3)] |= (0x80 >> (x & 7));
  if (y < texMinY) texMinY = y;
  if (y > texMaxY) texMaxY = y;
}

static void texHLine(int16_t x0, int16_t x1, int16_t y) { for (int16_t x = x0; x <= x1; x++) texPix(x, y, 1); }

// ширина текста в пикселях (шрифт 6x8, 5px + 1 пробел)
static int16_t texTextW(TexNode* n) {
  int16_t w = 0;
  for (TexNode* c = n; c; c = c->next) {
    switch (c->type) {
      case TexNode::TEXT: case TexNode::BINOP_SYM: w += c->len * 6; break;
      case TexNode::SUP: case TexNode::SUB: w += 6; break;               // привязка к предыдущему
      case TexNode::FRAC: {
        int16_t wa = texTextW(c->a), wb = texTextW(c->b);
        w += max(wa, wb) + 2; break;
      }
      case TexNode::SQRT: w += texTextW(c->a) + 3; break;
      case TexNode::BIGOP: {
        int16_t body = texTextW(c->a);
        int16_t lo = texTextW(c->b), hi = texTextW(c->c);
        w += max(body, max(lo, hi)) + 8; break;
      }
    }
  }
  return w;
}

// рисует список нод; baselineY - линия базового текста; возвращает итоговую ширину
static int16_t texDraw(TexNode* n, int16_t x, int16_t baselineY) {
  int16_t startX = x;
  for (TexNode* c = n; c; c = c->next) {
    if (c->type == TexNode::SUP || c->type == TexNode::SUB) {
      // рисуем уменьшенным: смещение вверх/вниз на 4 px, текст smaller через пропуск нижних строк
      // (без масштабирования шрифта эмулируем сдвигом)
      int16_t yy = (c->type == TexNode::SUP) ? baselineY - 4 : baselineY + 3;
      // печать символов вручную через oled-подобный bitmap? Используем тот же TEXT-путь,
      // но со смещением - упрощение: рисуем обычным размером со сдвигом
      x += texDraw(c->a, x, yy);
      continue;
    }
    if (c->type == TexNode::FRAC && !c->b) {   // группа {..} - inline
      x += texDraw(c->a, x, baselineY);
      continue;
    }
    switch (c->type) {
      case TexNode::TEXT: case TexNode::BINOP_SYM: {
        for (uint8_t i = 0; i < c->len; i++) {
          char ch = c->text[i];
          int16_t idx = ch - 32;                       // собственный мини-шрифт 5x7 (texFont.h)
          if (idx < 0 || idx >= 95) idx = '?' - 32;
          for (uint8_t col = 0; col < 5; col++) {
            uint8_t bits = pgm_read_byte(&texFont5x7[idx][col]);
            for (uint8_t row = 0; row < 7; row++)
              if (bits & (1 << row)) texPix(x + col, baselineY - 6 + row, 1);
          }
          x += 6;
        }
        break;
      }
      case TexNode::FRAC: {
        int16_t wa = texTextW(c->a), wb = texTextW(c->b);
        int16_t w = max(wa, wb);
        int16_t cx = x + (w - wa) / 2;
        texDraw(c->a, cx, baselineY - 4);       // числитель выше
        int16_t cy = baselineY + 1;
        texHLine(x, x + w + 1, cy);             // черта дроби
        texDraw(c->b, x + (w - wb) / 2, baselineY + 8); // знаменатель ниже
        x += w + 2;
        break;
      }
      case TexNode::SQRT: {
        int16_t w = texTextW(c->a);
        // знак корня: "v"-образная ножка + верхняя перекладина над телом
        texPix(x, baselineY - 3, 1);
        texPix(x + 1, baselineY - 1, 1);
        texPix(x + 2, baselineY + 1, 1);
        texPix(x + 3, baselineY - 1, 1);
        texPix(x + 4, baselineY - 3, 1);
        texHLine(x + 4, x + 4 + w, baselineY - 7);   // верхняя перекладина
        texDraw(c->a, x + 5, baselineY);
        x += w + 6;
        break;
      }
      case TexNode::BIGOP: {
        const char* sym = "SUM";
        if (c->len == 3 && !strncmp(c->text, "int", 3)) sym = "INT";
        if (c->len == 4) sym = "PRD";
        int16_t bw = strlen(sym) * 6;
        int16_t bodyW = texTextW(c->a);
        int16_t loW = texTextW(c->b), hiW = texTextW(c->c);
        int16_t w = max(bodyW, max(max(bw, loW), hiW));
        // сам символ оператора
        TexNode* op = texMakeText(sym, strlen(sym));
        texDraw(op, x, baselineY);
        if (c->c) texDraw(c->c, x, baselineY - 8);       // верхний предел
        if (c->b) texDraw(c->b, x, baselineY + 8);       // нижний предел
        if (c->a) texDraw(c->a, x + w + 2, baselineY);   // тело справа
        x += w + 3;
        break;
      }
    }
  }
  return x - startX;
}

// ---------------- загрузка и постраничная разбивка ----------------
static void texLoadPages(File file) {
  texLineCount = 0;
  // читаем построчно
  String line;
  uint8_t curLine = 0;
  memset(texLines, 0, sizeof(texLines));
  while (file.available()) {
    int c = file.read();
    if (c == '\n' || c == '\r') {
      if (line.length()) {
        if (curLine >= TEX_MAX_LINES) { texPage++; curLine = 0; }
        strncpy(texLines[curLine], line.c_str(), TEX_LINE_LEN - 1);
        curLine++; texLineCount = max<uint8_t>(texLineCount, curLine);
        line = "";
      }
      if (c == '\r' && file.peek() == '\n') file.read();
    } else {
      if (line.length() < TEX_LINE_LEN - 1) line += (char)c;
    }
    yield();
  }
  if (line.length()) {
    if (curLine >= TEX_MAX_LINES) { texPage++; curLine = 0; }
    strncpy(texLines[curLine], line.c_str(), TEX_LINE_LEN - 1);
    curLine++; texLineCount = max<uint8_t>(texLineCount, curLine);
  }
}

static void texRenderPage(File file) {
  file.seek(0);
  texLoadPages(file);

  // рассчитываем общее число страниц
  uint8_t totalLines = texLineCount;
  uint8_t totalPages = (totalLines + TEX_MAX_LINES - 1) / TEX_MAX_LINES;
  if (totalPages == 0) totalPages = 1;
  if (texPage >= totalPages) texPage = totalPages - 1;

  memset(texBuf, 0, sizeof(texBuf));
  texMinY = 63; texMaxY = 0;

  uint8_t base = texPage * TEX_MAX_LINES;
  uint8_t rows = 0;
  for (uint8_t i = 0; i < TEX_MAX_LINES; i++) {
    if (base + i >= totalLines) break;
    rows++;
  }
  int16_t blockH = rows * 16;
  int16_t y0 = (64 - blockH) / 2 + 10;

  for (uint8_t i = 0; i < rows; i++) {
    const char* src = texLines[base + i];
    if (!*src) continue;
    TexNode* tree = texParseSeq(src, '\0');
    int16_t w = texTextW(tree);
    int16_t x = (128 - w) / 2;
    if (x < 0) x = 0;
    texDraw(tree, x, y0 + i * 16);
  }

  // вывод буфера на OLED
  oled.clear();
  for (int16_t y = 0; y < 64; y++)
    for (int16_t x = 0; x < 128; x++)
      if (texBuf[y * 16 + (x >> 3)] & (0x80 >> (x & 7))) oled.dot(x, y, 1);
  oled.update();
}

void enterToReadTexFile(void) {
  String fn = ("/" + selectedFile);
  File file = LittleFS.open(fn, "r");
  if (!file) {
    fileReadError();
    checkFileSystem();
    drawMainMenu();
    return;
  }

  texPage = 0;
  uint8_t pages = 0;
  { // посчитать страницы
    file.seek(0);
    texLoadPages(file);
    pages = (texLineCount + TEX_MAX_LINES - 1) / TEX_MAX_LINES;
    if (pages == 0) pages = 1;
  }
  texRenderPage(file);

  while (1) {
    up.tick(); ok.tick(); down.tick();
    if (ok.click()) {
      uiTimer = millis();
      drawMainMenu();
      file.close();
      return;
    }
    if (down.click() || down.step()) {
      uiTimer = millis();
      if (texPage < pages - 1) { texPage++; texRenderPage(file); }
    }
    if (up.click() || up.step()) {
      uiTimer = millis();
      if (texPage > 0) { texPage--; texRenderPage(file); }
    }
    yield();
  }
}
