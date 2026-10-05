/* ======================================================================= */
/* ================ Рендер формул в TeX-синтаксисе (.tex) ================ */
/* Миникалькулятор формул для OLED 128x64 (GyverOLED).                     */
/*                                                                         */
/* ФОРМАТ ФАЙЛА:                                                           */
/*   Одна формула = одна страница. Новая формула начинается с \title{Имя}  */
/*   или со строки "## Имя". Если названий нет - каждая непустая строка    */
/*   файла - отдельная страница ("Имя: формула" тоже работает).            */
/*   Строки с % или // - комментарии. Строки внутри формулы (до 5)         */
/*   рисуются друг под другом. \begin{..}/\end{..} игнорируются.           */
/*                                                                         */
/* ПОДДЕРЖИВАЕТСЯ:                                                         */
/*   ^ _ (индексы/степени, x_1^2), \frac \dfrac \binom, \sqrt[n]{..}       */
/*   \int \iint \iiint \oint \sum \prod \bigcup \bigcap (с пределами)      */
/*   \lim \max \min \sup \inf (пределы под именем), \sin \cos \log ...     */
/*   \vec \hat \bar \dot \ddot \tilde \acute \grave \check \underline      */
/*   \overline \overrightarrow \widehat \widetilde                         */
/*   \underbrace{..}_{..} \overbrace{..}^{..} \stackrel \overset \underset */
/*   \text{..} \mathrm \operatorname (с пробелами), \mathbf \mathcal ...   */
/*   \left \right \big \Big ... (просто пропускаются)                      */
/*   \, \; \: \quad \qquad ~ - пробелы                                     */
/*   греческие буквы, ~90 математических символов (см. texFont.h),         */
/*   неизвестные команды печатаются как текст без обратного слэша.         */
/*   Если строка шире экрана или страница не влезает - мелкий шрифт 3x5.   */
/* ======================================================================= */

#define TEX_MAX_LINES 5      // максимум строк в одной формуле
#define TEX_LINE_LEN 128     // максимум символов в строке
#define TEX_NAME_LEN 20      // максимум символов в названии
#define TEX_NAME_BYTES (TEX_NAME_LEN * 2 + 1) // в UTF-8 буква = до 2 байт
#define TEX_MAX_PAGES 40     // максимум формул (страниц) в файле
#define TEX_POOL 320         // максимум нод дерева на страницу
#include "texFont.h"

// ------------------ типы (ДО автопрототипов Arduino IDE) ------------------
enum : uint8_t { T_TEXT, T_SYM, T_SPACE, T_GROUP, T_FRAC, T_SQRT, T_BIGOP, T_ACCENT, T_BRACE };
enum : uint8_t { F_LIMITS = 1, F_BINOM = 2, F_PAD = 4, F_FUNC = 8, F_OVER = 16, F_REL=32 };

struct TexNode {
  uint8_t type, flags, len;      // len: длина текста / вид акцента или оператора / ширина пробела
  const char* text;              // TEXT: строка; SYM: картинка глифа
  TexNode *a, *b, *c;            // a - тело/числитель, b - знаменатель, c - индекс корня
  TexNode *next;                 // следующая нода
  TexNode *sup, *sub;            // степень и индекс (или пределы сверху/снизу)
};
struct TexBox { int16_t w, up, dn; };   // ширина, строк выше базовой линии, строк ниже

// ------------------ прототипы ------------------
static TexNode* texAlloc();
static TexNode* texMakeText(const char* s, uint8_t l);
static TexNode* texMakeSpace(uint8_t w);
static TexNode* texParseSeq(const char*& p, char term);
static TexNode* texParseGroup(const char*& p);
static TexNode* texParseCmd(const char*& p);
static TexNode* texParseRaw(const char*& p);
static TexBox texLay(TexNode* n, int16_t x, int16_t base, uint8_t lvl, bool draw);
static TexBox texAtom(TexNode* c, int16_t x, int16_t base, uint8_t lvl, bool draw, bool& opnd);
static TexBox texCore(TexNode* c, int16_t x, int16_t base, uint8_t lvl, bool draw, bool& opnd);
static void texLoadPages(File file, int8_t target);
static void texParseAll();
static int16_t texBuildLayout(uint8_t lvl);
static uint8_t texWrap(TexNode* head, int16_t maxW, uint8_t lvl, TexNode** out, uint8_t maxOut);
static void texPrepare(File file);
static void texDrawPage();
// ------------------ данные страницы ------------------
static char texLines[TEX_MAX_LINES][TEX_LINE_LEN];
static uint8_t texLineCount = 0;
static int8_t texPage = 0;
static char texNames[TEX_MAX_PAGES][TEX_NAME_BYTES];
static uint8_t texNameCount = 0;

// ------------------ пул нод ------------------
static TexNode texPool[TEX_POOL];
static uint16_t texPoolUsed = 0;
static bool texOom = false;
static TexNode texSink;

static void texResetPool() { texPoolUsed = 0; texOom = false; }

static TexNode* texAlloc() {
  TexNode* n;
  if (texPoolUsed >= TEX_POOL) { texOom = true; n = &texSink; }   // страница не будет рисоваться
  else n = &texPool[texPoolUsed++];
  memset(n, 0, sizeof(TexNode));
  return n;
}
static TexNode* texMakeText(const char* s, uint8_t l) {
  TexNode* n = texAlloc(); n->type = T_TEXT; n->text = s; n->len = l; return n;
}
static TexNode* texMakeSpace(uint8_t w) {
  TexNode* n = texAlloc(); n->type = T_SPACE; n->len = w; return n;
}

// ------------------ таблицы команд ------------------
static inline bool texIs(const char* s, uint8_t l, const char* n) { return strlen(n) == l && !strncmp(s, n, l); }
static const char* texSkipWs(const char* p) { while (*p == ' ' || *p == '\t') p++; return p; }

struct TexAlias { const char* from; const char* to; };
static const TexAlias texAliases[] = {
  {"varepsilon","epsilon"}, {"vartheta","theta"}, {"varphi","phi"}, {"varrho","rho"},
  {"varsigma","sigma"}, {"varpi","pi"},
  {"le","leq"}, {"ge","geq"}, {"ne","neq"}, {"simeq","sim"}, {"cong","equiv"},
  {"rightarrow","to"}, {"longrightarrow","to"}, {"hookrightarrow","to"},
  {"gets","leftarrow"}, {"longleftarrow","leftarrow"},
  {"Longrightarrow","Rightarrow"}, {"implies","Rightarrow"},
  {"iff","Leftrightarrow"}, {"Longleftrightarrow","Leftrightarrow"},
  {"owns","ni"}, {"wedge","land"}, {"vee","lor"}, {"lnot","neg"},
  {"varnothing","emptyset"}, {"oslash","emptyset"}, {"bot","perp"},
  {"bullet","cdot"}, {"cdotp","cdot"}, {"centerdot","cdot"}, {"hslash","hbar"},
  {"Box","square"}, {"blacksquare","square"}, {"textdegree","degree"},
};
struct TexAscii { const char* cmd; const char* txt; };
static const TexAscii texAscii[] = {
  {"Upsilon","Y"}, {"omicron","o"}, {"Re","Re"}, {"Im","Im"},
  {"langle","<"}, {"rangle",">"}, {"lbrace","{"}, {"rbrace","}"},
  {"lbrack","["}, {"rbrack","]"}, {"vert","|"}, {"lvert","|"}, {"rvert","|"},
  {"mid","|"}, {"Vert","||"}, {"backslash","\\"}, {"setminus","\\"}, {"prime","'"},
  {"ldots","..."}, {"dots","..."}, {"colon",":"}, {"ast","*"}, {"star","*"},
  {"prec","<"}, {"succ",">"}, {"dagger","+"}, {"imath","i"}, {"jmath","j"},
  {"triangleq","="},
};
static const char* const texLimFns[] = {"lim","limsup","liminf","max","min","sup","inf","det","gcd","Pr"};
static const char* const texFns[] = {
  "sin","cos","tan","cot","sec","csc","arcsin","arccos","arctan","sinh","cosh","tanh",
  "coth","log","ln","lg","exp","deg","dim","ker","arg","mod","hom"
};

static int8_t texBigOpKind(const char* s, uint8_t l) {
  static const char* const nm[] = {"int","iint","iiint","oint","sum","prod","bigcup","bigcap"};
  for (uint8_t i = 0; i < 8; i++) if (texIs(s, l, nm[i])) return i;
  if (texIs(s, l, "coprod")) return 5;
  return -1;
}
// виды акцентов: 0 vec,1 hat,2 bar,3 dot,4 ddot,5 tilde,6 acute,7 grave,8 check,9 underline
static int8_t texAccentKind(const char* s, uint8_t l) {
  static const char* const nm[] = {"vec","hat","bar","dot","ddot","tilde","acute","grave","check","underline"};
  for (uint8_t i = 0; i < 10; i++) if (texIs(s, l, nm[i])) return i;
  if (texIs(s, l, "widehat")) return 1;
  if (texIs(s, l, "overline")) return 2;
  if (texIs(s, l, "widetilde")) return 5;
  if (texIs(s, l, "overrightarrow") || texIs(s, l, "overleftarrow")) return 0;
  return -1;
}

// ------------------ парсер ------------------
// "сырой" текст в {...}: пробелы сохраняются (\text{sum of terms})
static TexNode* texParseRaw(const char*& p) {
  p = texSkipWs(p);
  if (*p == '{') {
    const char* s = ++p;
    uint8_t depth = 1;
    while (*p) {
      if (*p == '{') depth++;
      else if (*p == '}') { if (--depth == 0) break; }
      p++;
    }
    uint16_t len = p - s; if (len > 120) len = 120;
    if (*p == '}') p++;
    return texMakeText(s, len);
  }
  if (*p) { TexNode* t = texMakeText(p, 1); p++; return t; }
  return nullptr;
}

static void texSkipArg(const char*& p) {
  p = texSkipWs(p);
  if (*p == '{') { int d = 0; while (*p) { if (*p == '{') d++; else if (*p == '}' && --d == 0) { p++; break; } p++; } }
}

// аргумент: {группа}, одна команда или один символ
static TexNode* texParseGroup(const char*& p) {
  p = texSkipWs(p);
  if (*p == '{') { p++; TexNode* g = texParseSeq(p, '}'); if (*p == '}') p++; return g; }
  if (*p == '\\') { p++; return texParseCmd(p); }
  if (*p) { TexNode* t = texMakeText(p, 1); p++; return t; }
  return nullptr;
}

static bool texIsRelSym(const char* cmd) {
  static const char* const rel[] = {"leq","geq","neq","approx","equiv","sim","to","leftarrow","leftrightarrow",
    "Rightarrow","Leftarrow","Leftrightarrow","mapsto","in","notin","ni","subset","subseteq","supset","supseteq",
    "perp","parallel","propto","ll","gg"};
  for (auto r : rel) if (!strcmp(cmd, r)) return true;
  return false;
}

// p указывает на символ после '\'. Возвращает ноду или nullptr (команда пропущена)
static TexNode* texParseCmd(const char*& p) {
  if (!*p) return nullptr;
  if (!isalpha((uint8_t)*p)) {                       // \, \; \{ \% \\ ...
    char ch = *p++;
    switch (ch) {
      case ',': return texMakeSpace(2);
      case ':': return texMakeSpace(3);
      case ';': return texMakeSpace(4);
      case ' ': return texMakeSpace(3);
      case '!': return nullptr;
      case '\\': return texMakeSpace(4);
      case '|': return texMakeText("||", 2);
      default: return texMakeText(p - 1, 1);
    }
  }
  const char* s = p;
  while (isalpha((uint8_t)*p)) p++;
  uint8_t l = (p - s > 40) ? 40 : (uint8_t)(p - s);
  TexNode* n;
#define IS(x) texIs(s, l, x)

  if (IS("frac") || IS("dfrac") || IS("tfrac") || IS("cfrac")) {
    n = texAlloc(); n->type = T_FRAC;
    n->a = texParseGroup(p); n->b = texParseGroup(p); return n;
  }
  if (IS("binom") || IS("dbinom") || IS("tbinom")) {
    n = texAlloc(); n->type = T_FRAC; n->flags = F_BINOM;
    n->a = texParseGroup(p); n->b = texParseGroup(p); return n;
  }
  if (IS("sqrt")) {
    n = texAlloc(); n->type = T_SQRT;
    p = texSkipWs(p);
    if (*p == '[') { p++; n->c = texParseSeq(p, ']'); if (*p == ']') p++; }
    n->a = texParseGroup(p); return n;
  }
  int8_t k = texBigOpKind(s, l);
  if (k >= 0) {
    n = texAlloc(); n->type = T_BIGOP; n->len = k;
    if (k >= 4) n->flags = F_LIMITS;                 // sum prod cup cap - пределы сверху/снизу
    return n;
  }
  k = texAccentKind(s, l);
  if (k >= 0) { n = texAlloc(); n->type = T_ACCENT; n->len = k; n->a = texParseGroup(p); return n; }
  if (IS("underbrace") || IS("overbrace")) {
    n = texAlloc(); n->type = T_BRACE; n->flags = F_LIMITS | (IS("overbrace") ? F_OVER : 0);
    n->a = texParseGroup(p); return n;
  }
  if (IS("stackrel") || IS("overset") || IS("underset")) {
    n = texAlloc(); n->type = T_GROUP; n->flags = F_LIMITS;
    TexNode* lab = texParseGroup(p);
    n->a = texParseGroup(p);
    if (IS("underset")) n->sub = lab; else n->sup = lab;
    return n;
  }
  if (IS("text") || IS("mathrm") || IS("textrm") || IS("mbox") || IS("textit") ||
      IS("textsf") || IS("texttt") || IS("mathsf") || IS("mathtt")) return texParseRaw(p);
  if (IS("operatorname")) { n = texParseRaw(p); if (n) n->flags |= F_FUNC; return n; }
  if (IS("mathbf") || IS("textbf") || IS("boldsymbol") || IS("bm") || IS("mathit") ||
      IS("mathcal") || IS("mathbb") || IS("mathfrak")) {
    n = texAlloc(); n->type = T_GROUP; n->a = texParseGroup(p); return n;
  }
  if (IS("left") || IS("right") || IS("big") || IS("Big") || IS("bigg") || IS("Bigg") ||
      IS("bigl") || IS("bigr") || IS("Bigl") || IS("Bigr") || IS("biggl") || IS("biggr") ||
      IS("Biggl") || IS("Biggr")) {
    if (*p == '.') p++;                              // \left. \right.
    return nullptr;
  }
  if (IS("begin") || IS("end") || IS("label") || IS("section") || IS("subsection") ||
      IS("tag") || IS("title") || IS("hspace") || IS("vspace") || IS("kern")) { texSkipArg(p); return nullptr; }
  if (IS("nonumber") || IS("notag") || IS("displaystyle") || IS("textstyle") ||
      IS("scriptstyle") || IS("limits") || IS("nolimits") || IS("protect")) return nullptr;
  if (IS("quad")) return texMakeSpace(8);
  if (IS("qquad")) return texMakeSpace(16);
  if (IS("enspace")) return texMakeSpace(4);

  for (uint8_t i = 0; i < sizeof(texLimFns) / sizeof(texLimFns[0]); i++)
    if (IS(texLimFns[i])) { n = texMakeText(s, l); n->flags = F_LIMITS | F_FUNC; return n; }
  for (uint8_t i = 0; i < sizeof(texFns) / sizeof(texFns[0]); i++)
    if (IS(texFns[i])) { n = texMakeText(s, l); n->flags = F_FUNC; return n; }
#undef IS

  for (auto& al : texAliases) if (texIs(s, l, al.from)) { s = al.to; l = strlen(al.to); break; }
  for (auto& g : texGlyphs) {
    if (texIs(s, l, g.cmd)) {
      n = texAlloc(); n->type = T_SYM; n->text = g.px;
      n->flags = (g.pad ? F_PAD : 0) | (texIsRelSym(g.cmd) ? F_REL : 0); return n;
    }
  }
  for (auto& as : texAscii) if (texIs(s, l, as.cmd)) return texMakeText(as.txt, strlen(as.txt));
  return texMakeText(s, l);                          // неизвестная команда - как текст
}

static TexNode* texParseSeq(const char*& p, char term) {
  TexNode *head = nullptr, *tail = nullptr;
  while (*p && *p != term && !texOom) {
    p = texSkipWs(p);
    if (!*p || *p == term) break;
    TexNode* node = nullptr;
    char ch = *p;
    if (ch == '{') {
      p++;
      TexNode* g = texParseSeq(p, '}');
      if (*p == '}') p++;
      node = texAlloc(); node->type = T_GROUP; node->a = g;
    } else if (ch == '}') {                          // лишняя скобка
      p++; continue;
    } else if (ch == '\\') {
      p++;
      node = texParseCmd(p);
    } else if (ch == '^' || ch == '_') {
      p++;
      TexNode* arg = texParseGroup(p);
      if (!tail) { tail = head = texAlloc(); tail->type = T_GROUP; }   // пустое основание
      TexNode** slot = (ch == '^') ? &tail->sup : &tail->sub;
      if (!*slot) *slot = arg;
      else if (arg) { TexNode* t = *slot; while (t->next) t = t->next; t->next = arg; }
      continue;                                      // в список не добавляем
    } else if (ch == '$') {
      p++; continue;
    } else if (ch == '%') {
      while (*p) p++; continue;                      // комментарий до конца строки
    } else if (ch == '~') {
      p++; node = texMakeSpace(4);
    } else if (ch == '&') {
      p++; node = texMakeSpace(8);
    } else {
      const char* st = p;
      if (strchr("=<>+-", *p)) p++;                  // оператор - отдельная нода (точка переноса)
      else while (*p && *p != term && !strchr("^_\\{}$%~& \t=<>+-", *p) && (p - st) < 120) p++;
      if (p == st) p++;                              // страховка от зацикливания
      node = texMakeText(st, p - st);
      if (p - st == 1 && strchr("=<>", *st)) node->flags |= F_REL;
    }
    if (node) {
      if (tail) tail->next = node; else head = node;
      tail = node;
    }
  }
  return head;
}

// ------------------ рисование примитивов ------------------
static int16_t texClipTop = 0;
static void texPix(int16_t x, int16_t y) {
  if (x < 0 || x > 127 || y < texClipTop || y > 63) return;
  oled.dot(x, y, 1);
}

static void texHLine(int16_t x0, int16_t x1, int16_t y) { for (int16_t x = x0; x <= x1; x++) texPix(x, y); }
static void texVLine(int16_t x, int16_t y0, int16_t y1) { for (int16_t y = y0; y <= y1; y++) texPix(x, y); }

// 5x7: y - верхняя строка глифа (базовая линия = y+6, "хвосты" уходят на y+7)
static void texChar5(char ch, int16_t x, int16_t y) {
  int16_t idx = (uint8_t)ch - 32;
  if (idx < 0 || idx >= 95) idx = '?' - 32;
  for (uint8_t col = 0; col < 5; col++) {
    uint8_t bits = pgm_read_byte(&texFont5x7[idx][col]);
    for (uint8_t row = 0; row < 8; row++) if (bits & (1 << row)) texPix(x + col, y + row);
  }
}
// 3x5: y - верхняя строка глифа
static void texChar3(char ch, int16_t x, int16_t y) {
  uint16_t g = texGlyph3(ch);
  for (uint8_t r = 0; r < 5; r++)
    for (uint8_t c = 0; c < 3; c++)
      if ((g >> (14 - 3 * r - c)) & 1) texPix(x + c, y + r);
}

// UTF-8 (2 байта) -> код символа; всё нераспознанное -> '?'
static uint16_t texU8Next(const char*& s) {
  uint8_t b = (uint8_t)*s++;
  if (b < 0x80) return b;
  if (b >= 0xC0 && b < 0xE0 && (((uint8_t)*s) & 0xC0) == 0x80) {
    uint16_t cp = ((b & 0x1F) << 6) | (((uint8_t)*s) & 0x3F);
    s++;
    return cp;
  }
  while ((((uint8_t)*s) & 0xC0) == 0x80) s++;       // пропустить хвост чужой последовательности
  return '?';
}
static uint8_t texU8Len(const char* s) { uint8_t n = 0; while (*s) { texU8Next(s); n++; } return n; }

// рисует один символ названия 5x7 (y - верхняя строка)
static void texDrawCp(uint16_t cp, int16_t x, int16_t y) {
  if (cp < 127) { texChar5((char)cp, x, y); return; }
  for (auto& l : texCyrLook) if (l.cp == cp) { texChar5(l.ch, x, y); return; }
  for (auto& g : texCyr) {
    if (g.cp == cp) {
      for (uint8_t i = 0; i < 40; i++) if (g.px[i] == '#') texPix(x + i % 5, y + i / 5);
      return;
    }
  }
  texChar5('?', x, y);
}

// ------------------ раскладка и рисование ------------------
// texLay - список нод слева направо. draw=false - только измерить (тот же код, что и рисует).
static TexBox texLay(TexNode* n, int16_t x, int16_t base, uint8_t lvl, bool draw) {
  TexBox r = {0, 0, 0};
  bool opnd = false;                                  // предыдущая нода - операнд (для унарного минуса)
  for (TexNode* c = n; c; c = c->next) {
    TexBox b = texAtom(c, x + r.w, base, lvl, draw, opnd);
    r.w += b.w;
    if (b.up > r.up) r.up = b.up;
    if (b.dn > r.dn) r.dn = b.dn;
  }
  return r;
}

// нода + её степень/индекс (сбоку или как пределы)
static TexBox texAtom(TexNode* c, int16_t x, int16_t base, uint8_t lvl, bool draw, bool& opnd) {
  if (!c->sup && !c->sub) {
    bool o = opnd;
    TexBox b = texCore(c, x, base, lvl, draw, o);
    opnd = o;
    return b;
  }
  bool o1 = opnd;
  TexBox cb = texCore(c, x, base, lvl, false, o1);
  TexBox S = {0, 0, 0}, U = {0, 0, 0};
  if (c->sup) S = texLay(c->sup, 0, 0, 1, false);
  if (c->sub) U = texLay(c->sub, 0, 0, 1, false);
  TexBox r;

  if (c->flags & F_LIMITS) {                          // пределы по центру сверху/снизу
    int16_t W = max(cb.w, max(S.w, U.w));
    int16_t ox = (W - cb.w) / 2, sx = (W - S.w) / 2, ux = (W - U.w) / 2;
    int16_t supBase = base - cb.up - 2 - S.dn;
    int16_t subBase = base + cb.dn + 2 + U.up;
    if (draw) {
      bool o2 = opnd;
      texCore(c, x + ox, base, lvl, true, o2);
      if (c->sup) texLay(c->sup, x + sx, supBase, 1, true);
      if (c->sub) texLay(c->sub, x + ux, subBase, 1, true);
    }
    r.w = W;
    r.up = c->sup ? base - (supBase - S.up) : cb.up;
    r.dn = c->sub ? (subBase + U.dn - base) : cb.dn;
  } else {                                            // x_1^2: сбоку, друг над другом
    int16_t fs = ((c->flags & F_FUNC) && c->type == T_TEXT) ? 2 : 0;   // без пробела после sin^2
    int16_t supShift = lvl ? max<int16_t>(3, cb.up - 3) : max<int16_t>(4, cb.up - 4);
    int16_t subShift = (lvl ? 2 : 3) + max<int16_t>(0, cb.dn - 1);
    int16_t supBase = base - supShift, subBase = base + subShift;
    if (c->sup && c->sub) {
      int16_t gap = (subBase - U.up) - (supBase + S.dn);
      if (gap < 2) subBase += (2 - gap);
    }
    int16_t sx = x + cb.w - fs;
    if (draw) {
      bool o2 = opnd;
      texCore(c, x, base, lvl, true, o2);
      if (c->sup) texLay(c->sup, sx, supBase, 1, true);
      if (c->sub) texLay(c->sub, sx, subBase, 1, true);
    }
    r.w = cb.w - fs + max(S.w, U.w) + fs;
    r.up = max<int16_t>(cb.up, c->sup ? base - (supBase - S.up) : 0);
    r.dn = max<int16_t>(cb.dn, c->sub ? (subBase + U.dn - base) : 0);
  }
  opnd = o1;
  return r;
}

// ядро ноды без степеней. base - базовая линия (нижняя строка заглавных букв)
static TexBox texCore(TexNode* c, int16_t x, int16_t base, uint8_t lvl, bool draw, bool& opnd) {
  TexBox r = {0, 0, 0};
  switch (c->type) {

    case T_TEXT: {
      const int16_t adv = lvl ? 4 : 6;
      bool o = opnd, xh = true, desc = false;
      int16_t cx = x;
      for (uint8_t i = 0; i < c->len; i++) {
        char ch = c->text[i];
        bool rel = (ch == '=' || ch == '<' || ch == '>');
        bool bin = ((ch == '+' || ch == '-') && o);   // бинарный +/- ; унарный - без пробелов
        int16_t pd = (rel || bin) ? 1 : 0;
        if (draw) { if (lvl) texChar3(ch, cx + pd, base - 4); else texChar5(ch, cx + pd, base - 6); }
        cx += adv + 2 * pd;
        if (!strchr("acemnorsuvwxz .:,'-+=<>", ch)) xh = false;
        if (strchr("gjpqy,;", ch)) desc = true;
        o = isalnum((uint8_t)ch) || ch == ')' || ch == ']' || ch == '!' || ch == '\'';
      }
      r.w = cx - x + ((c->flags & F_FUNC) ? 2 : 0);
      r.up = lvl ? 4 : (xh ? 4 : 6);
      r.dn = (!lvl && desc) ? 1 : 0;
      opnd = (c->flags & F_FUNC) ? false : o;
      break;
    }

    case T_SYM: {
      int16_t pd = (c->flags & F_PAD) ? 1 : 0;
      int8_t first = -1, last = -1;
      for (uint8_t i = 0; i < 40; i++) if (c->text[i] == '#') { int8_t row = i / 5; if (first < 0) first = row; last = row; }
      if (draw) for (uint8_t i = 0; i < 40; i++) if (c->text[i] == '#') texPix(x + pd + i % 5, base - 6 + i / 5);
      r.w = 6 + 2 * pd;
      r.up = (first < 0) ? 0 : max<int16_t>(0, 6 - first);
      r.dn = (last >= 7) ? 1 : 0;
      opnd = (pd == 0);
      break;
    }

    case T_SPACE:
      r.w = c->len;
      break;

    case T_GROUP:
      r = texLay(c->a, x, base, lvl, draw);
      opnd = true;
      break;

    case T_FRAC: {
      TexBox A = texLay(c->a, 0, 0, lvl, false), B = texLay(c->b, 0, 0, lvl, false);
      bool bin = c->flags & F_BINOM;
      int16_t wa = A.w > 0 ? A.w - 1 : 0, wb = B.w > 0 ? B.w - 1 : 0;
      int16_t mv = max(wa, wb);
      int16_t barY = base - (lvl ? 2 : 3);
      int16_t g = bin ? 1 : 2;
      int16_t nb = barY - g - A.dn, db = barY + g + B.up;
      int16_t topY = nb - A.up, botY = db + B.dn;
      int16_t off = bin ? 4 : 1;
      if (draw) {
        texLay(c->a, x + off + (mv - wa) / 2, nb, lvl, true);
        texLay(c->b, x + off + (mv - wb) / 2, db, lvl, true);
        if (bin) {                                    // скобки по высоте блока
          int16_t xr = x + mv + 5;
          texVLine(x + 1, topY, botY); texPix(x + 2, topY); texPix(x + 2, botY);
          texVLine(xr + 1, topY, botY); texPix(xr, topY); texPix(xr, botY);
        } else texHLine(x, x + mv + 1, barY);
      }
      r.w = bin ? mv + 8 : mv + 3;
      r.up = base - topY; r.dn = botY - base;
      opnd = true;
      break;
    }

    case T_SQRT: {
      TexBox Bd = texLay(c->a, 0, 0, lvl, false);
      TexBox I = {0, 0, 0};
      if (c->c) I = texLay(c->c, 0, 0, 1, false);
      int16_t iw = c->c ? I.w : 0;
      int16_t xo = x + iw;
      int16_t yt = base - Bd.up - 2, yb = base + Bd.dn;
      int16_t h = yb - yt; if (h < 4) h = 4;
      if (draw) {
        texPix(xo, yb - 2); texPix(xo + 1, yb - 1);
        for (int16_t y = yb; y >= yt; y--) texPix(xo + 2 + ((yb - y) * 3) / h, y);
        texHLine(xo + 5, xo + 6 + Bd.w, yt);
        texLay(c->a, xo + 7, base, lvl, true);
        if (c->c) texLay(c->c, x, yb - 4 - I.dn, 1, true);
      }
      r.w = iw + 7 + Bd.w;
      r.up = Bd.up + 2;
      if (c->c) r.up = max<int16_t>(r.up, base - (yb - 4 - I.dn - I.up));
      r.dn = Bd.dn;
      opnd = true;
      break;
    }

    case T_BIGOP: {
      uint8_t k = c->len;                             // 0 int,1 iint,2 iiint,3 oint,4 sum,5 prod,6 cup,7 cap
      bool isInt = (k <= 3);
      int16_t gw = isInt ? (k == 1 ? 10 : (k == 2 ? 14 : 6)) : 9;
      r.up = (k >= 6) ? 8 : 9;
      r.dn = (k >= 6) ? 2 : 3;
      r.w = gw + 1;
      if (draw) {
        int16_t y0 = base - r.up;
        if (isInt) {
          uint8_t copies = (k == 1) ? 2 : (k == 2 ? 3 : 1);
          for (uint8_t i = 0; i < copies; i++) {
            int16_t ox = x + 4 * i;
            texVLine(ox + 3, y0 + 1, y0 + 11);
            texPix(ox + 4, y0); texPix(ox + 5, y0 + 1);
            texPix(ox + 2, y0 + 12); texPix(ox + 1, y0 + 11);
          }
          if (k == 3) {                               // кружок на контурном интеграле
            static const int8_t rx[8] = {3, 4, 5, 4, 3, 2, 1, 2};
            static const int8_t ry[8] = {4, 5, 6, 7, 8, 7, 6, 5};
            for (uint8_t i = 0; i < 8; i++) texPix(x + rx[i], y0 + ry[i]);
          }
        } else if (k == 4) {                          // сумма
          texHLine(x, x + 8, y0); texHLine(x, x + 8, y0 + 12);
          texPix(x + 8, y0 + 1); texPix(x + 8, y0 + 11);
          for (int16_t i = 0; i <= 5; i++) texPix(x + i, y0 + 1 + i);
          for (int16_t i = 1; i <= 5; i++) texPix(x + 5 - i, y0 + 6 + i);
        } else if (k == 5) {                          // произведение
          texHLine(x, x + 8, y0);
          texVLine(x + 1, y0 + 1, y0 + 12); texVLine(x + 7, y0 + 1, y0 + 12);
          texPix(x, y0 + 12); texPix(x + 8, y0 + 12);
        } else if (k == 6) {                          // объединение
          texVLine(x, y0, y0 + 8); texVLine(x + 8, y0, y0 + 8);
          texPix(x + 1, y0 + 9); texPix(x + 7, y0 + 9); texHLine(x + 2, x + 6, y0 + 10);
        } else {                                      // пересечение
          texHLine(x + 2, x + 6, y0); texPix(x + 1, y0 + 1); texPix(x + 7, y0 + 1);
          texVLine(x, y0 + 2, y0 + 10); texVLine(x + 8, y0 + 2, y0 + 10);
        }
      }
      opnd = false;
      break;
    }

    case T_ACCENT: {
      uint8_t k = c->len;
      TexBox B = texLay(c->a, x, base, lvl, draw);
      int16_t xe = x + (B.w > 1 ? B.w - 2 : 0), cx = (x + xe) / 2;
      if (k == 9) {                                   // underline
        if (draw) texHLine(x, xe, base + B.dn + 2);
        r.up = B.up; r.dn = B.dn + 2;
      } else {
        int16_t y = base - B.up - 2;
        if (draw) {
          switch (k) {
            case 0: texHLine(x, xe, y); texPix(xe - 1, y - 1); texPix(xe - 1, y + 1); break;
            case 1: texPix(cx, y - 1); texPix(cx - 1, y); texPix(cx + 1, y); break;
            case 2: texHLine(x, xe, y); break;
            case 3: texPix(cx, y); break;
            case 4: texPix(cx - 1, y); texPix(cx + 1, y); break;
            case 5: texPix(cx - 2, y); texPix(cx - 1, y - 1); texPix(cx, y - 1); texPix(cx + 1, y); break;
            case 6: texPix(cx + 1, y - 1); texPix(cx, y); break;
            case 7: texPix(cx - 1, y - 1); texPix(cx, y); break;
            case 8: texPix(cx - 1, y - 1); texPix(cx, y); texPix(cx + 1, y - 1); break;
          }
        }
        r.up = B.up + 3; r.dn = B.dn;
      }
      r.w = B.w;
      opnd = true;
      break;
    }

    case T_BRACE: {                                   // underbrace / overbrace
      TexBox B = texLay(c->a, x, base, lvl, draw);
      int16_t xe = x + (B.w > 1 ? B.w - 2 : 0), cx = (x + xe) / 2;
      if (c->flags & F_OVER) {
        int16_t y = base - B.up - 2;
        if (draw) { texHLine(x, xe, y); texPix(x, y + 1); texPix(xe, y + 1); texPix(cx, y - 1); }
        r.up = B.up + 3; r.dn = B.dn;
      } else {
        int16_t y = base + B.dn + 2;
        if (draw) { texHLine(x, xe, y); texPix(x, y - 1); texPix(xe, y - 1); texPix(cx, y + 1); }
        r.up = B.up; r.dn = B.dn + 3;
      }
      r.w = B.w;
      opnd = true;
      break;
    }
  }
  return r;
}

// ---------------- загрузка: одна формула = одна страница ----------------
// В texLines кладётся ТОЛЬКО формула страницы target; остальные только считаются.
static void texLoadPages(File file, int8_t target) {
  file.seek(0);
  texNameCount = 0;
  texLineCount = 0;
  memset(texNames, 0, sizeof(texNames));
  memset(texLines, 0, sizeof(texLines));

  String formula, pendingName;
  bool haveFormula = false;

  auto flush = [&](const String& name) {
    if (formula.length() == 0) { haveFormula = false; return; }
    if (texNameCount < TEX_MAX_PAGES) {
      if (texNameCount == (uint8_t)target) {
        uint8_t li = 0, ci = 0;
        for (uint16_t i = 0; i < formula.length(); i++) {
          char ch = formula[i];
          if (ch == '\n') { if (li + 1 >= TEX_MAX_LINES) break; li++; ci = 0; continue; }
          if (ci < TEX_LINE_LEN - 1) texLines[li][ci++] = ch;
        }
        texLineCount = li + 1;
      }
            // обрезаем по БУКВАМ, не разрывая двухбайтовый символ
      const char* nm = name.c_str();
      uint8_t total = name.length(), nb = 0, chars = 0;
      while (nb < total && chars < TEX_NAME_LEN) {
        uint8_t b = (uint8_t)nm[nb];
        uint8_t len = (b >= 0xF0) ? 4 : (b >= 0xE0) ? 3 : (b >= 0xC0) ? 2 : 1;
        if (nb + len > total) break;
        nb += len; chars++;
      }
      memcpy(texNames[texNameCount], nm, nb);
      texNames[texNameCount][nb] = 0;
      texNameCount++;
    }
    formula = "";
    haveFormula = false;
  };

  String line;
  auto processLine = [&]() {
    line.trim();
    if (line.startsWith("##")) {
      flush(pendingName);
      pendingName = line.substring(2);
      pendingName.trim();
    } else if (line.startsWith("%") || line.startsWith("//")) {
      // комментарий
    } else if (line.startsWith("\\title")) {
      flush(pendingName);
      pendingName = "";
      int a = line.indexOf('{'), b = line.lastIndexOf('}');
      if (a >= 0 && b > a) pendingName = line.substring(a + 1, b);
      pendingName.trim();
    } else if (line.length()) {
      if (line.startsWith("\\begin") || line.startsWith("\\end")) {   // строка-обёртка - пропускаем
        int b = line.indexOf('}');
        String rest = (b >= 0) ? line.substring(b + 1) : String("");
        rest.trim();
        line = rest;
      }
      if (line == "$$" || line == "\\[" || line == "\\]") line = "";
      if (line.length()) {
        if (pendingName.length() == 0 && !haveFormula) {
          int sep = line.indexOf(": ");                // "Название: формула"
          if (sep > 0 && sep <= TEX_NAME_LEN * 2) {
            pendingName = line.substring(0, sep);
            line = line.substring(sep + 2);
          }
        }
        if (haveFormula) formula += "\n";
        formula += line;
        haveFormula = true;
      }
    }
    line = "";
  };

  while (file.available()) {
    int c = file.read();
    if (c == '\n' || c == '\r') {
      if (c == '\r' && file.peek() == '\n') file.read();
      processLine();
      yield();
    } else if (line.length() < TEX_LINE_LEN * TEX_MAX_LINES) {
      line += (char)c;
    }
  }
  processLine();
  flush(pendingName);
}

static uint8_t texTotalPages() { return max<uint8_t>(texNameCount, 1); }

// ---------------- переносы, раскладка, прокрутка ----------------
#define TEX_MAX_SEGS 16        // максимум строк на экране после переносов
#define TEX_INDENT 8           // отступ строки-продолжения
static TexNode* texTree[TEX_MAX_LINES];
static uint8_t texRows = 0;
static TexNode* texSeg[TEX_MAX_SEGS];
static TexBox texSegBox[TEX_MAX_SEGS];
static bool texSegCont[TEX_MAX_SEGS], texSegWrap[TEX_MAX_SEGS];
static uint8_t texSegN = 0, texLvl = 0;
static int16_t texContentW = 0, texContentH = 0, texTopY = 0;
static int16_t texMaxX = 0, texMaxY = 0, texScrollX = 0, texScrollY = 0;
enum : uint8_t { TM_PAGE, TM_VERT, TM_HORZ };
static uint8_t texMode = TM_PAGE;

static void texParseAll() {
  texResetPool();
  texRows = 0;
  for (uint8_t i = 0; i < texLineCount; i++) {
    if (!texLines[i][0]) continue;
    const char* src = texLines[i];
    texTree[texRows++] = texParseSeq(src, '\0');
  }
}

// Режет верхний уровень списка на строки. Переносим ПОСЛЕ отношения, иначе ПОСЛЕ бинарного оператора,
// только вне скобок. Возвращает число строк, out[] - их первые ноды (список физически обрезается).
static uint8_t texWrap(TexNode* head, int16_t maxW, uint8_t lvl, TexNode** out, uint8_t maxOut) {
  uint8_t n = 0;
  TexNode* start = head;
  TexNode *bestRel = nullptr, *bestBin = nullptr;
  int16_t w = 0;
  int8_t depth = 0;
  bool opnd = false;
  TexNode* c = head;
  while (c) {
    bool before = opnd;
    TexBox b = texAtom(c, 0, 0, lvl, false, opnd);
    int16_t room = maxW - (n ? TEX_INDENT : 0);
    if (w > 0 && w + b.w > room && (bestRel || bestBin) && n + 1 < maxOut) {
      TexNode* cut = bestRel ? bestRel : bestBin;
      TexNode* nxt = cut->next;
      cut->next = nullptr;
      out[n++] = start;
      start = c = nxt;                               // новая строка - заново меряем с узла после разреза
      w = 0; depth = 0; opnd = false; bestRel = bestBin = nullptr;
      continue;
    }
    w += b.w;
    if (c->type == T_TEXT) {
      for (uint8_t i = 0; i < c->len; i++) {
        char ch = c->text[i];
        if (ch == '(' || ch == '[') depth++;
        else if ((ch == ')' || ch == ']') && depth > 0) depth--;
      }
    }
    if (depth == 0) {
      if (c->flags & F_REL) bestRel = c;
      else if (before && c->type == T_TEXT && c->len == 1 && (c->text[0] == '+' || c->text[0] == '-')) bestBin = c;
      else if (before && c->type == T_SYM && (c->flags & F_PAD)) bestBin = c;
    }
    c = c->next;
  }
  out[n++] = start;
  return n;
}

// Парсинг + переносы + замеры при размере lvl. Возвращает суммарную высоту.
static int16_t texBuildLayout(uint8_t lvl) {
  texParseAll();
  texSegN = 0; texLvl = lvl; texContentW = 0;
  if (texOom) return 0;
  int16_t tot = 0;
  for (uint8_t r = 0; r < texRows; r++) {
    TexNode* parts[TEX_MAX_SEGS];
    uint8_t maxOut = TEX_MAX_SEGS - texSegN - (texRows - r - 1);
    uint8_t cnt = texWrap(texTree[r], 126, lvl, parts, maxOut);
    for (uint8_t k = 0; k < cnt; k++) {
      TexBox b = texLay(parts[k], 0, 0, lvl, false);
      texSeg[texSegN] = parts[k];
      texSegBox[texSegN] = b;
      texSegCont[texSegN] = (k > 0);
      texSegWrap[texSegN] = (cnt > 1);
      int16_t w = b.w + (k > 0 ? TEX_INDENT : 0);
      if (w > texContentW) texContentW = w;
      tot += b.up + b.dn + 1;
      texSegN++;
    }
  }
  if (texSegN > 1) tot += (texSegN - 1) * 2;
  return tot;
}

// Загрузка страницы + выбор размера: обычный с переносами -> мелкий с переносами -> обычный со скроллом
static void texPrepare(File file) {
  texLoadPages(file, texPage);
  uint8_t total = texTotalPages();
  if (texPage >= total) { texPage = total - 1; texLoadPages(file, texPage); }
  texTopY = *texNames[texPage] ? 12 : 0;
  int16_t availH = 64 - texTopY;

  int16_t h = texBuildLayout(0);
  if (!texOom && (h > availH || texContentW > 128)) {
    int16_t h1 = texBuildLayout(1);
    if (texOom || h1 > availH || texContentW > 128) h = texBuildLayout(0);   // и так не влезло - скролл
    else h = h1;
  }
  texContentH = h;
  texMaxX = (texContentW > 128) ? texContentW - 128 + 2 : 0;
  texMaxY = (texContentH > availH) ? texContentH - availH : 0;
  texScrollX = texScrollY = 0;
  if ((texMode == TM_VERT && !texMaxY) || (texMode == TM_HORZ && !texMaxX)) texMode = TM_PAGE;
}

static void texClr(int16_t x, int16_t y) { if (x >= 0 && x <= 127 && y >= 0 && y <= 63) oled.dot(x, y, 0); }

// стрелка-индикатор 3x5 в центре (x,y). dir: 0 вверх, 1 вниз, 2 влево, 3 вправо
static void texArrow(uint8_t dir, int16_t x, int16_t y) {
  for (int8_t i = -3; i <= 3; i++) for (int8_t j = -3; j <= 3; j++) texClr(x + i, y + j);
  for (int8_t r = 0; r < 3; r++)
    for (int8_t m = -r; m <= r; m++) {
      switch (dir) {
        case 0: texPix(x + m, y - 1 + r); break;
        case 1: texPix(x + m, y + 1 - r); break;
        case 2: texPix(x - 1 + r, y + m); break;
        default: texPix(x + 1 - r, y + m); break;
      }
    }
}

static void texDrawPage() {
  oled.autoPrintln(false);
  oled.clear();
  texClipTop = 0;
  uint8_t total = texTotalPages();
  const char* name = texNames[texPage];
  if (*name) {                                        // название сверху по центру
    uint8_t nl = texU8Len(name);
    int16_t nw = nl * 6 - 1;
    int16_t nx = max<int16_t>(0, (128 - nw) / 2);
    const char* sp = name;
    for (uint8_t i = 0; *sp; i++) texDrawCp(texU8Next(sp), nx + i * 6, 0);
    texHLine(nx, nx + nw - 1, 9);
  }
  int16_t availH = 64 - texTopY;
  texClipTop = texTopY;                               // содержимое не залезает на заголовок

  if (texOom || texSegN == 0) {
    const char* msg = texOom ? "FORMULA TOO LONG" : "(EMPTY)";
    uint8_t ml = strlen(msg);
    for (uint8_t i = 0; i < ml; i++) texChar5(msg[i], (128 - ml * 6) / 2 + i * 6, 30);
  } else {
    int16_t extra = 0, y0;
    if (texMaxY == 0) {                               // влезает по высоте - центрируем
      extra = (texSegN > 1) ? min<int16_t>(4, (availH - texContentH) / (texSegN + 1)) : 0;
      y0 = texTopY + (availH - texContentH - extra * (texSegN - 1)) / 2;
    } else y0 = texTopY - texScrollY;
    int16_t y = y0;
    for (uint8_t i = 0; i < texSegN; i++) {
      TexBox& bx = texSegBox[i];
      int16_t ind = texSegCont[i] ? TEX_INDENT : 0;
      int16_t x;
      if (texMaxX) x = 1 - texScrollX + ind;                    // шире экрана - слева + прокрутка
      else if (texSegWrap[i]) x = 1 + ind;                      // перенесённые строки - по левому краю
      else x = max<int16_t>(0, (128 - bx.w) / 2);               // остальные по центру
      texLay(texSeg[i], x, y + bx.up, texLvl, true);
      y += bx.up + bx.dn + 1 + 2 + extra;
    }
  }

  texClipTop = 0;
  // метки "есть скрытое содержимое"
  if (texScrollX < texMaxX) texArrow(3, 126, texTopY + availH / 2);
  if (texScrollX > 0)       texArrow(2, 1,   texTopY + availH / 2);
  if (texScrollY < texMaxY) texArrow(1, 64,  62);
  if (texScrollY > 0)       texArrow(0, 64,  texTopY + 2);

  // номер страницы (и режим V/H) в правом нижнем углу
  char st[16];
  snprintf(st, sizeof(st), "%s%d/%d", texMode == TM_VERT ? "V " : (texMode == TM_HORZ ? "H " : ""), texPage + 1, total);
  int16_t sw = strlen(st) * 4;
  for (int16_t x = 128 - sw - 1; x < 128; x++) for (int16_t yy = 58; yy < 64; yy++) texClr(x, yy);
  for (uint8_t i = 0; st[i]; i++) texChar3(st[i], 128 - sw + i * 4, 59);

  oled.update();
  oled.autoPrintln(true);
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
  texMode = TM_PAGE;
  texLoadPages(file, 0);                  // только посчитать страницы
  uint8_t pages = texTotalPages();
  texPrepare(file);
  texDrawPage();

  while (1) {
    up.tick(); ok.tick(); down.tick();
    bool overflow = texMaxX || texMaxY;

    // выход: удержание ОК, а если всё влезло - как раньше, по клику
    if (ok.hold() || (ok.click() && !overflow)) {
      uiTimer = millis();
      drawMainMenu();
      file.close();
      return;
    }
    if (ok.click()) {                     // переключение режима: страницы -> верт. -> гориз.
      uiTimer = millis();
      if (texMode == TM_PAGE) texMode = texMaxY ? TM_VERT : TM_HORZ;
      else if (texMode == TM_VERT) texMode = texMaxX ? TM_HORZ : TM_PAGE;
      else texMode = TM_PAGE;
      texDrawPage();
    }

    int8_t dir = 0;
    if (down.click() || down.step()) dir = 1;
    if (up.click() || up.step()) dir = -1;
    if (dir) {
      uiTimer = millis();
      if (texMode == TM_PAGE) {
        int8_t np = texPage + dir;
        if (np >= 0 && np < pages) { texPage = np; texPrepare(file); texDrawPage(); }
      } else if (texMode == TM_VERT) {
        texScrollY = constrain(texScrollY + dir * 8, 0, texMaxY);
        texDrawPage();
      } else {
        texScrollX = constrain(texScrollX + dir * 8, 0, texMaxX);
        texDrawPage();
      }
    }
    yield();
  }
}