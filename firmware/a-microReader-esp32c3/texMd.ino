/* ======================================================================= */
/* ===== Markdown: таблицы и графики (.md). Вкладка texMd.ino ============ */
/*                                                                         */
/*  ## Название            - начало секции (страницы)                      */
/*  | a | b |              - таблица; 2-я строка |:--|--:| задаёт заголовок */
/*                           и выравнивание (:-- влево, :-: центр, --: вправо) */
/*  plot: y = sin(x)*x/3; y = cos(x); x = -10..10; y = -4..4               */
/*        функции (до 3, разные линии) и диапазоны x=a..b, y=c..d          */
/*        (y можно опустить - автомасштаб; x по умолчанию -10..10)         */
/*  plot: table x=1 y=2,3  - график по таблице секции (x - № столбца,      */
/*        y - до 3 столбцов; x=1 y=2 по умолчанию)                         */
/*  Функции: + - * / ^ ( ) | | sin cos tan asin acos atan sinh cosh tanh   */
/*           exp ln log sqrt abs floor ceil sign; константы pi e; неявное  */
/*           умножение: 2x, 3(x+1), 2sin(x). Диапазоны понимают pi.        */
/* ======================================================================= */

#define MD_BUF 6144          // максимум байт одной секции
#define MD_MAX_VIEWS 40      // максимум страниц в файле
#define MD_MAX_ROWS 80       // максимум строк таблицы
#define MD_MAX_COLS 10       // максимум столбцов
#define PL_MAX_FN 3          // максимум функций на графике
#define PL_MAX_PTS 120       // максимум точек (104 столбца экрана или точки таблицы)

enum : uint8_t { MV_TABLE, MV_PLOT, MV_TEXT };
struct MdView { uint32_t off; uint16_t len; uint8_t type, plotIdx; char name[TEX_NAME_BYTES]; };

static MdView mdViews[MD_MAX_VIEWS];
static uint8_t mdViewCount = 0;
static int8_t mdPage = 0;
static char mdBuf[MD_BUF + 1];

// ---------------- индексация файла ----------------
static void mdCopyName(char* dst, const char* nm, uint8_t total) {
  uint8_t nb = 0, chars = 0;
  while (nb < total && chars < TEX_NAME_LEN) {          // обрезаем по буквам, не разрывая UTF-8
    uint8_t b = (uint8_t)nm[nb];
    uint8_t len = (b >= 0xF0) ? 4 : (b >= 0xE0) ? 3 : (b >= 0xC0) ? 2 : 1;
    if (nb + len > total) break;
    nb += len; chars++;
  }
  memcpy(dst, nm, nb);
  dst[nb] = 0;
}

static void mdIndex(File file) {
  file.seek(0);
  mdViewCount = 0;
  char secName[TEX_NAME_BYTES]; secName[0] = 0;
  uint32_t secStart = 0, pos = 0, lineStart = 0;
  bool hasTable = false, hasText = false;
  uint8_t nPlots = 0;
  String line;

  auto addView = [&](uint32_t end, uint8_t type, uint8_t pi) {
    if (mdViewCount >= MD_MAX_VIEWS) return;
    MdView& v = mdViews[mdViewCount++];
    uint32_t len = (end > secStart) ? end - secStart : 0;
    v.off = secStart;
    v.len = (len > MD_BUF) ? MD_BUF : (uint16_t)len;
    v.type = type; v.plotIdx = pi;
    strcpy(v.name, secName);
  };
  auto closeSec = [&](uint32_t end) {
    if (hasTable) addView(end, MV_TABLE, 0);
    for (uint8_t k = 0; k < nPlots; k++) addView(end, MV_PLOT, k);
    if (!hasTable && !nPlots && hasText) addView(end, MV_TEXT, 0);
    hasTable = hasText = false; nPlots = 0;
  };
  auto handle = [&](uint32_t after) {
    line.trim();
    if (line.startsWith("##")) {
      closeSec(lineStart);
      String nm = line.substring(2); nm.trim();
      mdCopyName(secName, nm.c_str(), min<uint16_t>(nm.length(), 200));
      secStart = after;
    } else if (line.length() && !line.startsWith("%") && !line.startsWith("//")) {
      if (line[0] == '|') hasTable = true;
      else if (line.startsWith("plot:")) nPlots++;
      else hasText = true;
    }
    line = "";
  };

  while (file.available()) {
    int c = file.read(); pos++;
    if (c == '\n') { handle(pos); lineStart = pos; yield(); }
    else if (c != '\r' && line.length() < 400) line += (char)c;
  }
  handle(pos);
  closeSec(pos);
}

// ---------------- разбор таблицы ----------------
static uint16_t mdRowOff[MD_MAX_ROWS];
static uint8_t mdRows = 0, mdCols = 0, mdColChars[MD_MAX_COLS], mdAlign[MD_MAX_COLS];
static bool mdHasHeader = false, mdAscii = true, mdTextMode = false;
static int16_t mdColX[MD_MAX_COLS + 1];
static int16_t mdW = 0, mdH = 0;
static uint8_t mdLvl = 0, mdRowH = 11;

static uint8_t mdU8n(const char* s, uint8_t l) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < l; i++) if (((uint8_t)s[i] & 0xC0) != 0x80) n++;
  return n;
}

// следующая ячейка строки (p - позиция, s/l - текст без пробелов по краям)
static bool mdNextCell(const char*& p, const char*& s, uint8_t& l) {
  if (!*p) return false;
  const char* e = p;
  if (mdTextMode) { while (*e) e++; }
  else while (*e && *e != '|') { if (*e == '\\' && e[1] == '|') e++; e++; }
  const char* a = p; while (*a == ' ' || *a == '\t') a++;
  const char* b = e; while (b > a && (b[-1] == ' ' || b[-1] == '\t')) b--;
  s = a;
  l = (b - a > 60) ? 60 : (uint8_t)(b - a);
  p = (*e == '|') ? e + 1 : e;
  return true;
}

static bool mdIsSep(const char* s) {
  bool dash = false;
  for (; *s; s++) {
    if (*s == '-') dash = true;
    else if (*s != '|' && *s != ':' && *s != ' ' && *s != '\t') return false;
  }
  return dash;
}

static void mdParseTable(bool textMode) {
  mdRows = mdCols = 0; mdHasHeader = false; mdAscii = true; mdTextMode = textMode;
  memset(mdColChars, 0, sizeof(mdColChars));
  memset(mdAlign, 0, sizeof(mdAlign));
  char* p = mdBuf;
  while (*p) {
    char* e = p; while (*e && *e != '\n') e++;
    bool more = (*e == '\n');
    char* t = e; if (t > p && t[-1] == '\r') t--;
    char* s = p; while (*s == ' ' || *s == '\t') s++;
    bool take;
    if (textMode) take = (s < t) && s[0] != '%' && !(s[0] == '/' && s[1] == '/');
    else take = (s < t) && *s == '|';
    *t = 0;                                              // строка становится C-строкой
    if (take && mdRows < MD_MAX_ROWS) mdRowOff[mdRows++] = s - mdBuf;
    p = more ? e + 1 : e;
  }
  if (!textMode && mdRows >= 2 && mdIsSep(mdBuf + mdRowOff[1])) {   // заголовок + выравнивание
    const char* q = mdBuf + mdRowOff[1]; if (*q == '|') q++;
    const char* s; uint8_t l, c = 0;
    while (c < MD_MAX_COLS && mdNextCell(q, s, l)) {
      bool L = l && s[0] == ':', R = l && s[l - 1] == ':';
      mdAlign[c++] = (L && R) ? 1 : (R ? 2 : 0);
    }
    memmove(&mdRowOff[1], &mdRowOff[2], (mdRows - 2) * sizeof(uint16_t));
    mdRows--; mdHasHeader = true;
  }
  for (uint8_t r = 0; r < mdRows; r++) {                 // замер столбцов
    const char* q = mdBuf + mdRowOff[r];
    if (!textMode && *q == '|') q++;
    const char* s; uint8_t l, c = 0;
    while (c < MD_MAX_COLS && mdNextCell(q, s, l)) {
      uint8_t n = mdU8n(s, l);
      if (n > mdColChars[c]) mdColChars[c] = n;
      for (uint8_t i = 0; i < l; i++) if ((uint8_t)s[i] >= 0x80) mdAscii = false;
      c++;
    }
    if (c > mdCols) mdCols = c;
  }
}

// lvl 0: шрифт 5x7 (строка 11 px), lvl 1: 3x5 (строка 8 px, только ASCII)
static void mdLayout(uint8_t lvl) {
  mdLvl = lvl; mdRowH = lvl ? 8 : 11;
  int16_t adv = lvl ? 4 : 6;
  mdColX[0] = 0;
  for (uint8_t c = 0; c < mdCols; c++) mdColX[c + 1] = mdColX[c] + max<int16_t>(1, mdColChars[c]) * adv + 4;
  mdW = mdColX[mdCols] + 1;
  mdH = mdRows * mdRowH + 1;
}

static void mdPutText(const char* s, uint8_t l, int16_t x, int16_t y, bool bold) {
  const char* end = s + l;
  uint8_t i = 0;
  while (s < end) {
    uint16_t cp = texU8Next(s);
    if (mdLvl) texChar3(cp < 127 ? (char)cp : '?', x + i * 4, y);
    else { texDrawCp(cp, x + i * 6, y); if (bold) texDrawCp(cp, x + i * 6 + 1, y); }
    i++;
  }
}

// ---------------- вычислитель выражений ----------------
static const char* evP; static float evX; static bool evErr;
static float evExpr();
static void evWs() { while (*evP == ' ' || *evP == '\t') evP++; }

static float evAtom() {
  evWs();
  char c = *evP;
  if ((c >= '0' && c <= '9') || c == '.') {
    char* end; float v = strtof(evP, &end);
    if (end == evP) { evErr = true; evP++; return NAN; }
    evP = end; return v;
  }
  if (c == '(') { evP++; float v = evExpr(); evWs(); if (*evP == ')') evP++; else evErr = true; return v; }
  if (c == '|') { evP++; float v = evExpr(); evWs(); if (*evP == '|') evP++; else evErr = true; return fabsf(v); }
  if (isalpha((uint8_t)c)) {
    char id[8]; uint8_t n = 0;
    while (isalpha((uint8_t)*evP)) { if (n < 7) id[n++] = *evP; evP++; }
    id[n] = 0;
    if (!strcmp(id, "x")) return evX;
    if (!strcmp(id, "pi")) return 3.14159265f;
    if (!strcmp(id, "e")) return 2.71828183f;
    evWs();
    if (*evP == '(') {
      evP++; float a = evExpr(); evWs();
      if (*evP == ')') evP++; else evErr = true;
      if (!strcmp(id, "sin")) return sinf(a);
      if (!strcmp(id, "cos")) return cosf(a);
      if (!strcmp(id, "tan")) return tanf(a);
      if (!strcmp(id, "asin")) return asinf(a);
      if (!strcmp(id, "acos")) return acosf(a);
      if (!strcmp(id, "atan")) return atanf(a);
      if (!strcmp(id, "sinh")) return sinhf(a);
      if (!strcmp(id, "cosh")) return coshf(a);
      if (!strcmp(id, "tanh")) return tanhf(a);
      if (!strcmp(id, "exp")) return expf(a);
      if (!strcmp(id, "ln")) return logf(a);
      if (!strcmp(id, "log")) return log10f(a);
      if (!strcmp(id, "sqrt")) return sqrtf(a);
      if (!strcmp(id, "abs")) return fabsf(a);
      if (!strcmp(id, "floor")) return floorf(a);
      if (!strcmp(id, "ceil")) return ceilf(a);
      if (!strcmp(id, "sign")) return a > 0 ? 1.0f : (a < 0 ? -1.0f : 0.0f);
    }
  }
  evErr = true;
  if (*evP) evP++;                                       // всегда двигаемся вперёд
  return NAN;
}
static float evUnary();
static float evPow() {
  float b = evAtom(); evWs();
  if (*evP == '^') { evP++; float ex = evUnary(); return powf(b, ex); }
  return b;
}
static float evUnary() {
  evWs();
  if (*evP == '-') { evP++; return -evUnary(); }
  if (*evP == '+') { evP++; return evUnary(); }
  return evPow();
}
static float evTerm() {
  float v = evUnary();
  for (;;) {
    evWs(); char c = *evP;
    if (c == '*') { evP++; v *= evUnary(); }
    else if (c == '/') { evP++; v /= evUnary(); }
    else if (isalpha((uint8_t)c) || isdigit((uint8_t)c) || c == '(' || c == '.') v *= evPow();   // неявное умножение
    else break;
  }
  return v;
}
static float evExpr() {
  float v = evTerm();
  for (;;) {
    evWs();
    if (*evP == '+') { evP++; v += evTerm(); }
    else if (*evP == '-') { evP++; v -= evTerm(); }
    else break;
  }
  return v;
}
// вычислить строку при x; err=true при синтаксической ошибке
static float evEval(const char* s, float x, bool& err) {
  evP = s; evX = x; evErr = false;
  float v = evExpr(); evWs();
  if (*evP) evErr = true;
  err = evErr;
  return v;
}

// ---------------- график ----------------
static char plFn[PL_MAX_FN][80];
static uint8_t plNFn = 0, plTx = 1, plNTy = 0, plTy[3];
static bool plTable = false, plHasX = false, plHasY = false, plBad = false;
static float plX0, plX1, plY0, plY1;
static float plPx[PL_MAX_PTS], plPy[3][PL_MAX_PTS];
static uint16_t plNPts = 0;
static const char* plMsg = nullptr;
static int16_t plAx0 = 23, plAx1 = 126, plAy0 = 1, plAy1 = 54;

static char* plTrim(char* s) {
  while (*s == ' ' || *s == '\t') s++;
  char* e = s + strlen(s);
  while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
  return s;
}

static bool mdFindPlotLine(uint8_t idx, char* out, uint16_t cap) {
  const char* p = mdBuf;
  uint8_t k = 0;
  while (*p) {
    const char* e = p; while (*e && *e != '\n') e++;
    const char* s = p; while (s < e && (*s == ' ' || *s == '\t')) s++;
    if (e - s >= 5 && !strncmp(s, "plot:", 5)) {
      if (k == idx) {
        uint16_t n = min<uint16_t>(e - s - 5, cap - 1);
        memcpy(out, s + 5, n); out[n] = 0;
        return true;
      }
      k++;
    }
    p = *e ? e + 1 : e;
  }
  return false;
}

static void plParse(const char* spec) {
  plNFn = 0; plTable = false; plHasX = plHasY = false; plTx = 1; plNTy = 0; plBad = false;
  char buf[200];
  strncpy(buf, spec, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
  char* save = nullptr;
  for (char* f = strtok_r(buf, ";", &save); f; f = strtok_r(nullptr, ";", &save)) {
    f = plTrim(f);
    if (!*f) continue;
    if (!strncmp(f, "table", 5)) {                       // table [x=N] [y=N[,N,N]]
      plTable = true;
      const char* o = f + 5;
      while (*o) {
        while (*o == ' ') o++;
        if (o[0] == 'x' && o[1] == '=') { plTx = atoi(o + 2); while (*o && *o != ' ') o++; }
        else if (o[0] == 'y' && o[1] == '=') {
          plNTy = 0; const char* q = o + 2;
          while (*q && *q != ' ') {
            if (isdigit((uint8_t)*q)) { if (plNTy < 3) plTy[plNTy++] = atoi(q); while (isdigit((uint8_t)*q)) q++; }
            else q++;
          }
          o = q;
        } else if (*o) o++;
      }
      continue;
    }
    char* eq = strchr(f, '=');
    if (!eq) { plBad = true; continue; }
    *eq = 0;
    char* name = plTrim(f);
    char* rhs = plTrim(eq + 1);
    char* dd = strstr(rhs, "..");
    if (dd) {                                            // диапазон
      *dd = 0;
      bool e1, e2;
      float a = evEval(plTrim(rhs), 0, e1), b = evEval(plTrim(dd + 2), 0, e2);
      if (e1 || e2) { plBad = true; continue; }
      if (name[0] == 'x') { plX0 = a; plX1 = b; plHasX = true; }
      else if (name[0] == 'y') { plY0 = a; plY1 = b; plHasY = true; }
      else plBad = true;
    } else if (name[0] != 'x' || name[1]) {              // функция y = ...
      if (plNFn < PL_MAX_FN) { strncpy(plFn[plNFn], rhs, 79); plFn[plNFn][79] = 0; plNFn++; }
    } else plBad = true;
  }
}

static bool mdCellAt(const char* line, uint8_t idx, const char*& s, uint8_t& l) {
  const char* p = line;
  if (*p == '|') p++;
  for (uint8_t i = 0;; i++) {
    if (!mdNextCell(p, s, l)) return false;
    if (i == idx) return true;
  }
}
static bool mdNum(const char* line, uint8_t idx, float& out) {
  const char* s; uint8_t l;
  if (!mdCellAt(line, idx, s, l) || !l || l > 15) return false;
  char tmp[16];
  for (uint8_t i = 0; i < l; i++) tmp[i] = (s[i] == ',') ? '.' : s[i];
  tmp[l] = 0;
  char* end; out = strtof(tmp, &end);
  return end != tmp && !*end;
}

static void mdPlotPrepare(uint8_t idx) {
  plAx0 = 23; plAx1 = 126;
  plBad = false; plMsg = nullptr; plNPts = 0;
  char spec[200];
  if (!mdFindPlotLine(idx, spec, sizeof(spec))) { plBad = true; plMsg = "NO PLOT"; return; }
  plParse(spec);
  if (plBad) { plMsg = "BAD SYNTAX"; return; }

  float mn = 1e30f, mx = -1e30f;
  if (plTable) {
    mdParseTable(false);
    if (!plNTy) { plNTy = 1; plTy[0] = 2; }
    uint8_t nx = plTx ? plTx - 1 : 0;
    for (uint8_t r = 0; r < mdRows && plNPts < PL_MAX_PTS; r++) {
      const char* line = mdBuf + mdRowOff[r];
      float xv, yv[3]; bool ok = mdNum(line, nx, xv);
      for (uint8_t k = 0; ok && k < plNTy; k++) ok = mdNum(line, plTy[k] - 1, yv[k]);
      if (!ok) continue;                                 // заголовок и нечисловые строки пропускаем
      plPx[plNPts] = xv;
      for (uint8_t k = 0; k < plNTy; k++) plPy[k][plNPts] = yv[k];
      plNPts++;
    }
    if (plNPts < 2) { plBad = true; plMsg = "NO DATA"; return; }
    if (!plHasX) {
      plX0 = plX1 = plPx[0];
      for (uint16_t i = 1; i < plNPts; i++) { if (plPx[i] < plX0) plX0 = plPx[i]; if (plPx[i] > plX1) plX1 = plPx[i]; }
    }
    for (uint8_t k = 0; k < plNTy; k++)
      for (uint16_t i = 0; i < plNPts; i++) { if (plPy[k][i] < mn) mn = plPy[k][i]; if (plPy[k][i] > mx) mx = plPy[k][i]; }
  } else {
    if (!plNFn) { plBad = true; plMsg = "NO FUNCTION"; return; }
    if (!plHasX) { plX0 = -10; plX1 = 10; }
    if (plX1 <= plX0) plX1 = plX0 + 1;
    plNPts = plAx1 - plAx0 + 1;
    for (uint8_t f = 0; f < plNFn; f++) {
      for (uint16_t i = 0; i < plNPts; i++) {
        bool err;
        float x = plX0 + (plX1 - plX0) * i / (plNPts - 1);
        float y = evEval(plFn[f], x, err);
        if (err) { plBad = true; plMsg = "BAD EXPR"; return; }
        if (!isfinite(y) || fabsf(y) > 1e6f) y = NAN;
        plPy[f][i] = y;
        if (isfinite(y)) { if (y < mn) mn = y; if (y > mx) mx = y; }
      }
    }
  }
  if (plX1 <= plX0) plX1 = plX0 + 1;
  if (!plHasY) {                                          // автомасштаб по Y
    if (mn > mx) { mn = 0; mx = 1; }
    if (mx - mn < 1e-9f) { mn -= 1; mx += 1; }
    else { float pad = (mx - mn) * 0.06f; mn -= pad; mx += pad; }
    plY0 = mn; plY1 = mx;
  }
  if (plY1 <= plY0) plY1 = plY0 + 1;
}

static void plPix(int16_t x, int16_t y) {
  if (x < plAx0 || x > plAx1 || y < plAy0 || y > plAy1) return;
  oled.dot(x, y, 1);
}
static int16_t plClampI(float v) { return (int16_t)constrain(v, -3000.0f, 3000.0f); }

// стиль: 0 сплошная, 1 пунктир, 2 точки
static void plLine(float fx0, float fy0, float fx1, float fy1, uint8_t style, uint8_t& ph) {
  int16_t x0 = plClampI(fx0), y0 = plClampI(fy0), x1 = plClampI(fx1), y1 = plClampI(fy1);
  if ((y0 < plAy0 && y1 < plAy0) || (y0 > plAy1 && y1 > plAy1) ||
      (x0 < plAx0 && x1 < plAx0) || (x0 > plAx1 && x1 > plAx1)) return;
  int16_t dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int16_t dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int16_t err = dx + dy;
  for (;;) {
    bool on = (style == 0) || (style == 1 && (ph % 5) < 3) || (style == 2 && (ph % 3) == 0);
    if (on) plPix(x0, y0);
    ph++;
    if (x0 == x1 && y0 == y1) break;
    int16_t e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

static float plNiceStep(float range, int maxTicks) {
  float raw = range / max(1, maxTicks);
  float mag = powf(10.0f, floorf(log10f(raw)));
  float f = raw / mag;
  float nf = f <= 1 ? 1 : f <= 2 ? 2 : f <= 5 ? 5 : 10;
  return nf * mag;
}
static void plFmt(char* b, float v) {
  if (fabsf(v) < 1e-9f) v = 0;
  snprintf(b, 10, "%.4g", v);
  if (strlen(b) > 6) snprintf(b, 10, "%.2g", v);
}
static void plLabel(const char* s, int16_t x, int16_t y) {
  for (uint8_t i = 0; s[i]; i++) texChar3(s[i], x + i * 4, y);
}

static void mdTitleCentered(const char* name) {
  uint8_t nl = texU8Len(name);
  int16_t nw = nl * 6 - 1;
  int16_t nx = max<int16_t>(0, (128 - nw) / 2);
  const char* sp = name;
  for (uint8_t i = 0; *sp; i++) texDrawCp(texU8Next(sp), nx + i * 6, 0);
  texHLine(nx, nx + nw - 1, 9);
}

static void mdDrawPlot(uint8_t vi) {
  const char* name = mdViews[vi].name;
  texClipTop = 0;
  plAy0 = *name ? 10 : 1; plAy1 = 54;
  if (*name) {                                            // название слева сверху (не более 17 букв)
    const char* sp = name;
    for (uint8_t i = 0; *sp && i < 17; i++) texDrawCp(texU8Next(sp), i * 6, 0);
  }
  if (mdViewCount > 1) {                                  // счётчик страниц сверху справа
    char st[10]; snprintf(st, sizeof(st), "%d/%d", mdPage + 1, mdViewCount);
    int16_t sw = strlen(st) * 4;
    for (int16_t x = 127 - sw; x < 128; x++) for (int16_t y = 0; y < 7; y++) texClr(x, y);
    plLabel(st, 128 - sw, 1);
  }
  if (plBad) {
    const char* msg = plMsg ? plMsg : "ERROR";
    uint8_t ml = strlen(msg);
    for (uint8_t i = 0; i < ml; i++) texChar5(msg[i], (128 - ml * 6) / 2 + i * 6, 28);
    return;
  }
  float xr = plX1 - plX0, yr = plY1 - plY0;
  float xs = plNiceStep(xr, max(2, (plAx1 - plAx0 + 1) / 26));
  float ys = plNiceStep(yr, max(2, (plAy1 - plAy0 + 1) / 12));
  auto mapX = [&](float x) -> float { return plAx0 + (x - plX0) / xr * (plAx1 - plAx0); };
  auto mapY = [&](float y) -> float { return plAy1 - (y - plY0) / yr * (plAy1 - plAy0); };

  for (int16_t y = plAy0; y <= plAy1 + 1; y++) oled.dot(plAx0 - 1, y, 1);      // левая ось
  for (int16_t x = plAx0 - 1; x <= plAx1; x++) oled.dot(x, plAy1 + 1, 1);      // нижняя ось

  if (plX0 < 0 && plX1 > 0) { int16_t px = (int16_t)lroundf(mapX(0)); for (int16_t y = plAy0; y <= plAy1; y += 2) plPix(px, y); }
  if (plY0 < 0 && plY1 > 0) { int16_t py = (int16_t)lroundf(mapY(0)); for (int16_t x = plAx0; x <= plAx1; x += 2) plPix(x, py); }

  int32_t ix0 = (int32_t)ceilf(plX0 / xs - 1e-4f), ix1 = (int32_t)floorf(plX1 / xs + 1e-4f);
  int32_t iy0 = (int32_t)ceilf(plY0 / ys - 1e-4f), iy1 = (int32_t)floorf(plY1 / ys + 1e-4f);
  if (ix1 - ix0 > 20) ix1 = ix0 + 20;
  if (iy1 - iy0 > 20) iy1 = iy0 + 20;
  char lb[10];
  for (int32_t i = ix0; i <= ix1; i++) {                  // деления и подписи по X
    int16_t px = (int16_t)lroundf(mapX(i * xs));
    if (px < plAx0 || px > plAx1) continue;
    oled.dot(px, plAy1 + 2, 1); oled.dot(px, plAy1 + 3, 1);
    plFmt(lb, i * xs);
    int16_t w = strlen(lb) * 4 - 1;
    plLabel(lb, constrain(px - w / 2, 0, 128 - w), 59);
    for (int32_t j = iy0; j <= iy1; j++) {                // точки сетки на пересечениях
      int16_t py = (int16_t)lroundf(mapY(j * ys));
      plPix(px, py);
    }
  }
  for (int32_t j = iy0; j <= iy1; j++) {                  // деления и подписи по Y
    int16_t py = (int16_t)lroundf(mapY(j * ys));
    if (py < plAy0 || py > plAy1) continue;
    oled.dot(plAx0 - 3, py, 1); oled.dot(plAx0 - 2, py, 1);
    plFmt(lb, j * ys);
    int16_t w = strlen(lb) * 4 - 1;
    plLabel(lb, max<int16_t>(0, plAx0 - 4 - w), py - 2);
  }

  if (!plTable) {                                         // функции
    for (uint8_t f = 0; f < plNFn; f++) {
      bool pv = false; float ppy = 0; uint8_t ph = 0;
      for (uint16_t i = 0; i < plNPts; i++) {
        float y = plPy[f][i];
        bool ok = isfinite(y);
        float py = ok ? mapY(y) : 0;
        int16_t px = plAx0 + i;
        if (ok) {
          if (pv && fabsf(py - ppy) < 3.0f * (plAy1 - plAy0 + 1)) plLine(px - 1, ppy, px, py, f, ph);
          else plPix(px, plClampI(py));
        }
        pv = ok; ppy = py;
      }
    }
  } else {                                                // таблица: линия + маркеры
    for (uint8_t k = 0; k < plNTy; k++) {
      uint8_t ph = 0;
      for (uint16_t i = 0; i < plNPts; i++) {
        float px = mapX(plPx[i]), py = mapY(plPy[k][i]);
        if (i) plLine(mapX(plPx[i - 1]), mapY(plPy[k][i - 1]), px, py, k, ph);
        if (plNPts <= 40) {
          int16_t cx = plClampI(px), cy = plClampI(py);
          plPix(cx, cy); plPix(cx - 1, cy); plPix(cx + 1, cy); plPix(cx, cy - 1); plPix(cx, cy + 1);
        }
      }
    }
  }
}

// ---------------- отрисовка таблицы ----------------
static void mdDrawTable(uint8_t vi) {
  const char* name = mdViews[vi].name;
  texClipTop = 0;
  if (*name) mdTitleCentered(name);
  int16_t availH = 64 - texTopY;
  texClipTop = texTopY;
  if (!mdRows) {
    const char* msg = "(EMPTY)";
    for (uint8_t i = 0; msg[i]; i++) texChar5(msg[i], 43 + i * 6, 30);
  } else {
    int16_t ox = texMaxX ? -texScrollX : (128 - mdW) / 2;
    int16_t oy = texMaxY ? texTopY - texScrollY : texTopY + (availH - mdH) / 2;
    int16_t adv = mdLvl ? 4 : 6;
    for (uint8_t r = 0; r < mdRows; r++) {
      int16_t y = oy + r * mdRowH;
      if (y + mdRowH < texClipTop || y > 63) continue;
      if (!mdTextMode) texHLine(ox, ox + mdW - 1, y);
      const char* p = mdBuf + mdRowOff[r];
      if (!mdTextMode && *p == '|') p++;
      const char* s; uint8_t l, c = 0;
      while (c < mdCols && mdNextCell(p, s, l)) {
        uint8_t n = mdU8n(s, l);
        int16_t inner = mdColChars[c] * adv - 1, tw = n ? n * adv - 1 : 0;
        int16_t x0 = ox + (mdTextMode ? 2 : mdColX[c] + 3), x = x0;
        if (!mdTextMode) {
          if (mdAlign[c] == 2) x = x0 + inner - tw;
          else if (mdAlign[c] == 1) x = x0 + (inner - tw) / 2;
        }
        mdPutText(s, l, x, y + 2, mdHasHeader && r == 0);       // заголовок - "жирным"
        c++;
      }
    }
    if (!mdTextMode) {
      texHLine(ox, ox + mdW - 1, oy + mdRows * mdRowH);
      if (mdHasHeader) texHLine(ox, ox + mdW - 1, oy + mdRowH + 1);   // двойная линия под заголовком
      for (uint8_t c = 0; c <= mdCols; c++) texVLine(ox + mdColX[c], oy, oy + mdH - 1);
    }
  }
  texClipTop = 0;
  if (texScrollX < texMaxX) texArrow(3, 126, texTopY + availH / 2);
  if (texScrollX > 0)       texArrow(2, 1,   texTopY + availH / 2);
  if (texScrollY < texMaxY) texArrow(1, 64,  62);
  if (texScrollY > 0)       texArrow(0, 64,  texTopY + 2);
  char st[16];
  snprintf(st, sizeof(st), "%s%d/%d", texMode == TM_VERT ? "V " : (texMode == TM_HORZ ? "H " : ""),
           mdPage + 1, max<uint8_t>(mdViewCount, 1));
  int16_t sw = strlen(st) * 4;
  for (int16_t x = 128 - sw - 1; x < 128; x++) for (int16_t yy = 58; yy < 64; yy++) texClr(x, yy);
  plLabel(st, 128 - sw, 59);
}

static void mdDraw() {
  oled.autoPrintln(false);
  oled.clear();
  texClipTop = 0;
  if (!mdViewCount) {
    const char* msg = "(EMPTY)";
    for (uint8_t i = 0; msg[i]; i++) texChar5(msg[i], 43 + i * 6, 30);
  } else if (mdViews[mdPage].type == MV_PLOT) mdDrawPlot(mdPage);
  else mdDrawTable(mdPage);
  oled.update();
  oled.autoPrintln(true);
}

// загрузка секции страницы + раскладка
static void mdPrepare(File f) {
  texScrollX = texScrollY = 0; texMaxX = texMaxY = 0; texTopY = 0;
  if (!mdViewCount) return;
  if (mdPage >= mdViewCount) mdPage = mdViewCount - 1;
  const MdView& v = mdViews[mdPage];
  f.seek(v.off);
  uint16_t n = f.read((uint8_t*)mdBuf, v.len);
  mdBuf[n] = 0;
  if (v.type == MV_PLOT) { texMode = TM_PAGE; mdPlotPrepare(v.plotIdx); return; }
  texTopY = v.name[0] ? 12 : 0;
  mdParseTable(v.type == MV_TEXT);
  int16_t availH = 64 - texTopY;
  mdLayout(0);
  if ((mdW > 128 || mdH > availH) && mdAscii) {            // не влезло - пробуем мелкий шрифт
    mdLayout(1);
    if (mdW > 128 || mdH > availH) mdLayout(0);            // и так не влезло - обычный + скролл
  }
  texMaxX = (mdW > 128) ? mdW - 128 + 1 : 0;
  texMaxY = (mdH > availH) ? mdH - availH + 1 : 0;
  if ((texMode == TM_VERT && !texMaxY) || (texMode == TM_HORZ && !texMaxX)) texMode = TM_PAGE;
}

void enterToReadMdFile(void) {
  String fn = ("/" + selectedFile);
  File file = LittleFS.open(fn, "r");
  if (!file) {
    fileReadError();
    checkFileSystem();
    drawMainMenu();
    return;
  }
  mdIndex(file);
  mdPage = 0;
  texMode = TM_PAGE;
  mdPrepare(file);
  mdDraw();

  while (1) {
    up.tick(); ok.tick(); down.tick();
    bool overflow = texMaxX || texMaxY;
    if (ok.hold() || (ok.click() && !overflow)) {          // выход
      uiTimer = millis();
      drawMainMenu();
      file.close();
      return;
    }
    if (ok.click()) {                                      // страницы -> верт. -> гориз.
      uiTimer = millis();
      if (texMode == TM_PAGE) texMode = texMaxY ? TM_VERT : TM_HORZ;
      else if (texMode == TM_VERT) texMode = texMaxX ? TM_HORZ : TM_PAGE;
      else texMode = TM_PAGE;
      mdDraw();
    }
    int8_t dir = 0;
    if (down.click() || down.step()) dir = 1;
    if (up.click() || up.step()) dir = -1;
    if (dir) {
      uiTimer = millis();
      if (texMode == TM_PAGE) {
        int8_t np = mdPage + dir;
        if (np >= 0 && np < (int8_t)mdViewCount) { mdPage = np; mdPrepare(file); mdDraw(); }
      } else if (texMode == TM_VERT) {
        texScrollY = constrain(texScrollY + dir * 8, 0, texMaxY); mdDraw();
      } else {
        texScrollX = constrain(texScrollX + dir * 8, 0, texMaxX); mdDraw();
      }
    }
    yield();
  }
}
