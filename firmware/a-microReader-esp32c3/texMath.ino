/* ======================================================================= */
/* ================ Рендер формул в TeX-синтаксисе (.tex) ================ */
/* Миникалькулятор формул для OLED 128x64 (GyverOLED).                     */
/*                                                                         */
/* ФОРМАТ ФАЙЛА:                                                           */
/*   Одна формула = одна страница.                                         */
/*   Новая формула начинается с команды \title{Название}                   */
/*   (или со строки "## Название"). Если названий нет - каждая             */
/*   непустая строка файла считается отдельной формулой (страницей).       */
/*   Строки, начинающиеся с % или // - комментарии.                       */
/*   Внутри одной формулы можно писать несколько строк - они склеиваются   */
/*   в блок (до 5 строк).                                                  */
/*                                                                         */
/* ПОДДЕРЖИВАЕТСЯ СИНТАКСИС:                                               */
/*   ^{...} _{...}          степени / индексы                              */
/*   \frac{a}{b} \dfrac     дробь                                          */
/*   \binom{n}{k}           биномиальный коэффициент                       */
/*   \sqrt[n]{...}          корень n-й степени (\sqrt{...} - квадратный)   */
/*   \int_a^b{...}          интеграл с пределами                          */
/*   \iint \iiint \oint     двойной/тройной/контурный интегралы           */
/*   \sum_{a}^{b}{...}      сумма с пределами                              */
/*   \prod \bigcup \bigcap  произведение, объединение, пересечение         */
/*   \lim_{x\to a}{...}     предел (подпись снизу)                         */
/*   \left( \right) \bigl \bigr \Bigl \Bigr -> обычные скобки              */
/*   \underbrace{..}_{..}   нижняя огибающая с подписью                    */
/*   \vec{x} \hat{x} \bar{x} \dot{x} \ddot{x} \tilde{x} \overline{x}       */
/*   \overrightarrow{AB}    акцент над выражением                           */
/*   \text{...} \mathrm{...} \operatorname{...} текст внутри формулы       */
/*   \begin{...} \end{...} \section \label - метки игнорируются            */
/*   $ ... $                маркеры формулы (игнорируются)                 */
/*   Греки: alpha beta gamma delta epsilon varepsilon zeta eta theta       */
/*          vartheta iota kappa lambda mu nu xi omicron pi rho sigma       */
/*          tau upsilon phi varphi chi psi omega + заглавные версии        */
/*   Операторы: \cdot \times \div \pm \mp \ast \star \circ \bullet         */
/*     \oplus \ominus \otimes \oslash \odot \setminus                      */
/*   Отношения: \leq \geq \neq \approx \equiv \sim \cong \propto           */
/*     \perp \parallel \ll \gg                                             */
/*   Логика/множества: \in \notin \subset \subseteq \supset \forall        */
/*     \exists \neg \land \lor \emptyset \varnothing \therefore            */
/*   Спецсимволы: \infty \partial \nabla \hbar \ell \Re \Im \aleph         */
/*     \angle \degree \surd \square \checkmark \langle \rangle             */
/*   Стрелки: \to \rightarrow \leftarrow \Rightarrow \Leftarrow             */
/*     \leftrightarrow \Leftrightarrow \mapsto \uparrow \downarrow          */
/*     \nearrow \searrow \iff                                              */
/*   Многоточия: \ldots \cdots \vdots \ddots \dots                         */
/*   Функции: \log \ln \exp \sin \cos \tan \cot \sec \csc \arcsin          */
/*     \sinh \max \min \det \dim \ker \deg \gcd \arg \mod                  */
/*   Неизвестные команды печатаются как текст без обратного слэша.         */
/* ======================================================================= */

#define TEX_MAX_LINES 5      // максимум строк внутри одного блока формулы
#define TEX_LINE_LEN 96      // максимум символов в строке
#define TEX_NAME_LEN 20      // максимум символов названия формулы
#include "texFont.h"         // мини-шрифт 5x7 для рендера формул

// ------------------ прототипы static-функций ------------------
// (Arduino IDE вставляет автопрототипы перед #include, из-за чего
//  struct TexNode ещё не объявлен — объявляем сами)
struct TexNode {
  enum Type { TEXT, SUP, SUB, FRAC, SQRT, BIGOP, LIMIT, ACCENT, UNDER, OVER } type;
  const char* text;         // для TEXT / имя BIGOP / тип ACCENT
  uint8_t len;              // длина текста
  TexNode* a = nullptr;     // тело / числитель / основание
  TexNode* b = nullptr;     // знаменатель / нижний предел / подпись
  TexNode* c = nullptr;     // верхний предел / индекс корня
  TexNode* next = nullptr;  // следующая нода в списке
};
static TexNode* texAlloc();
static TexNode* texMakeText(const char* s, uint8_t l);
static void texAppendChar(char* buf, uint8_t& i, char c);
static TexNode* texParseSeq(const char*& p, char terminator);
static TexNode* texParseGroup(const char*& p);
static TexNode* texParseOneCmd(const char*& p);
static void texPix(int16_t x, int16_t y, bool on);
static void texHLine(int16_t x0, int16_t x1, int16_t y);
static void texVLine(int16_t x, int16_t y0, int16_t y1);
static int16_t texTextW(TexNode* n);
static int16_t texDraw(TexNode* n, int16_t x, int16_t baselineY);
static void texLoadPages(File file);
static void texRenderPage(File file);
// ------------------------------------------------------------

static char texLines[TEX_MAX_LINES][TEX_LINE_LEN];
static uint8_t texLineCount = 0;
static int8_t texPage = 0;          // текущая страница (одна формула)
static char texNames[24][TEX_NAME_LEN + 1];   // названия формул (до 24 страниц)
static uint8_t texNameCount = 0;

// ------------------ разбор дерева формул ------------------
static TexNode* texAlloc() {
  static TexNode pool[220];
  static uint8_t used = 0;
  if (used >= 220) used = 0;                // кольцевой пул (перерисовка страницы)
  TexNode* n = &pool[used++];
  n->type = TexNode::TEXT; n->text = nullptr; n->len = 0;
  n->a = n->b = n->c = n->next = nullptr;
  return n;
}

static const char* texSkipWs(const char* p) { while (*p == ' ' || *p == '\t') p++; return p; }

static TexNode* texMakeText(const char* s, uint8_t l) {
  TexNode* n = texAlloc();
  n->type = TexNode::TEXT; n->text = s; n->len = l;
  return n;
}

struct TexSym { const char* cmd; const char* repl; };
static const TexSym texSyms[] = {
  // --- греки (строчные) ---
  {"alpha", "a"}, {"beta", "b"}, {"gamma", "g"}, {"delta", "d"},
  {"epsilon", "e"}, {"varepsilon", "e"}, {"zeta", "z"}, {"eta", "et"},
  {"theta", "th"}, {"vartheta", "th"}, {"iota", "i"}, {"kappa", "k"},
  {"lambda", "l"}, {"mu", "m"}, {"nu", "n"}, {"xi", "x"},
  {"omicron", "o"}, {"pi", "p"}, {"rho", "r"}, {"sigma", "s"},
  {"tau", "t"}, {"upsilon", "u"}, {"phi", "f"}, {"varphi", "f"},
  {"chi", "ch"}, {"psi", "ps"}, {"omega", "w"},
  // --- греки (заглавные) ---
  {"Gamma", "G"}, {"Delta", "D"}, {"Theta", "Th"}, {"Lambda", "L"},
  {"Xi", "X"}, {"Pi", "P"}, {"Sigma", "S"}, {"Upsilon", "U"},
  {"Phi", "F"}, {"Psi", "Ps"}, {"Omega", "W"},
  // --- бинарные операторы ---
  {"cdot", "."}, {"cdotp", "."}, {"times", "x"}, {"div", "/"},
  {"pm", "+-"}, {"mp", "-+"}, {"ast", "*"}, {"star", "*"}, {"circ", "o"},
  {"bullet", "*"}, {"oplus", "(+)"}, {"ominus", "(-)"}, {"otimes", "(x)"},
  {"oslash", "(/)"}, {"odot", "(.)"}, {"setminus", "\\"},
  // --- отношения ---
  {"leq", "<="}, {"le", "<="}, {"geq", ">="}, {"ge", ">="},
  {"neq", "!="}, {"ne", "!="}, {"approx", "~="}, {"equiv", "=="},
  {"sim", "~"}, {"simeq", "~~"}, {"cong", "=="}, {"propto", "PP"},
  {"perp", "|_"}, {"parallel", "//"}, {"ll", "<<"}, {"gg", ">>"},
  {"prec", "<"}, {"succ", ">"}, {"triangleq", "=^"},
  // --- множества / логика ---
  {"in", "IN"}, {"notin", "!IN"}, {"ni", "NI"}, {"subset", "(="},
  {"subseteq", "(=)"}, {"supset", "=>"}, {"supseteq", "=)"},
  {"cup", "U"}, {"cap", "n"}, {"forall", "FA"}, {"exists", "EX"},
  {"neg", "!"}, {"land", "&"}, {"lor", "V"}, {"emptyset", "EM"},
  {"varnothing", "EM"}, {"therefore", "TF"}, {"because", "BQ"},
  // --- спецсимволы ---
  {"infty", "INF"}, {"partial", "PD"}, {"nabla", "V="}, {"hbar", "HB"},
  {"ell", "E"}, {"Re", "R"}, {"Im", "I"}, {"aleph", "A"},
  {"angle", "<>"}, {"degree", "DG"}, {"surd", "SQ"}, {"square", "[]"},
  {"checkmark", "OK"}, {"backslash", "\\"}, {"vert", "|"}, {"Vert", "||"},
  {"langle", "<"}, {"rangle", ">"}, {"lbrace", "{"}, {"rbrace", "}"},
  {"dots", "..."}, {"ldots", "..."}, {"cdots", "..."}, {"vdots", "::"},
  {"ddots", "D::"}, {"prime", "'"},
  // --- стрелки ---
  {"to", "->"}, {"rightarrow", "->"}, {"longrightarrow", "-->"},
  {"leftarrow", "<-"}, {"gets", "<-"}, {"leftrightarrow", "<->"},
  {"Rightarrow", "=>"}, {"Leftarrow", "<="},
  {"Leftrightarrow", "<=>"}, {"mapsto", "|->"},
  {"uparrow", "UP"}, {"downarrow", "DN"}, {"nearrow", "NE"},
  {"searrow", "SE"}, {"hookrightarrow", "->"}, {"iff", "<=>"},
  // --- крупные операторы (обрабатываются отдельно, заглушки) ---
  {"int", ""}, {"iint", ""}, {"iiint", ""}, {"oint", ""},
  {"sum", ""}, {"prod", ""}, {"bigcup", ""}, {"bigcap", ""},
  {"lim", ""}, {"limsup", ""}, {"liminf", ""},
  // --- spacing (дополнительно) ---
  {"quad", "  "}, {"qquad", "    "},
  {"hspace", "~"}, {"kern", "~"},

};

// известные текстовые команды-функции (печатаются своим именем)
static bool texIsFunc(const char* s, uint8_t l) {
  static const char* fns[] = {
    "sin","cos","tan","cot","sec","csc","arcsin","arccos","arctan",
    "sinh","cosh","tanh","log","ln","exp","max","min","sup","inf",
    "det","dim","ker","deg","gcd","arg","mod","Pr"
  };
  for (auto f : fns) if (strlen(f) == l && !strncmp(s, f, l)) return true;
  return false;
}

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
      node = texAlloc();               // группа-обёртка (рисуется inline)
      node->type = TexNode::OVER;
      node->a = g;
    } else if (*p == '\\') {
      p++;
      const char* cmdStart = p;
      if (!isalpha((uint8_t)*p)) {     // экранированный символ (\% \& \$ ...)
        if (*p) { node = texMakeText(p, 1); p++; }
      } else {
        while (isalpha((uint8_t)*p)) p++;
        uint8_t cmdLen = p - cmdStart;
        if ((cmdLen == 4 && !strncmp(cmdStart, "frac", 4)) ||
            (cmdLen == 5 && !strncmp(cmdStart, "dfrac", 5))) {
          node = texAlloc(); node->type = TexNode::FRAC;
          p = texSkipWs(p); node->a = texParseGroup(p);
          p = texSkipWs(p); node->b = texParseGroup(p);
        } else if (cmdLen == 5 && !strncmp(cmdStart, "binom", 5)) {
          node = texAlloc(); node->type = TexNode::FRAC;
          node->text = "()"; node->len = 2;              // режим бинома: скобки вместо черты
          p = texSkipWs(p); node->a = texParseGroup(p);
          p = texSkipWs(p); node->b = texParseGroup(p);
        } else if (cmdLen == 4 && !strncmp(cmdStart, "sqrt", 4)) {
          node = texAlloc(); node->type = TexNode::SQRT;
          p = texSkipWs(p);
          if (*p == '[') {                                // \sqrt[n]{...}
            p++;
            TexNode* idx = texParseSeq(p, ']');
            if (*p == ']') p++;
            node->c = idx;
          }
          p = texSkipWs(p); node->a = texParseGroup(p);
        } else if ((cmdLen >= 3 && cmdLen <= 6 &&
                    (!strncmp(cmdStart, "int", 3) || !strncmp(cmdStart, "sum", 3) ||
                     !strncmp(cmdStart, "prod", 4) || !strncmp(cmdStart, "oint", 4) ||
                     !strncmp(cmdStart, "iint", 4) || !strncmp(cmdStart, "iiint", 5) ||
                     !strncmp(cmdStart, "coprod", 6))) ||
                   (cmdLen == 6 && (!strncmp(cmdStart, "bigcup", 6) ||
                                    !strncmp(cmdStart, "bigcap", 6)))) {
          node = texAlloc(); node->type = TexNode::BIGOP;
          node->text = cmdStart; node->len = cmdLen;
          p = texSkipWs(p);
          while (*p == '_' || *p == '^') {                // пределы в любом порядке
            char t = *p; p++;
            TexNode* lim = texParseGroup(p);
            if (t == '_') node->b = lim; else node->c = lim;
            p = texSkipWs(p);
          }
          p = texSkipWs(p);
          node->a = texParseGroup(p);                     // тело (опционально)
        } else if ((cmdLen == 3 && !strncmp(cmdStart, "lim", 3)) ||
                   (cmdLen == 6 && (!strncmp(cmdStart, "limsup", 6) ||
                                    !strncmp(cmdStart, "liminf", 6)))) {
          node = texAlloc(); node->type = TexNode::LIMIT;
          node->text = cmdStart; node->len = cmdLen;
          p = texSkipWs(p);
          while (*p == '_' || *p == '^') {
            char t = *p; p++;
            TexNode* lim = texParseGroup(p);
            if (t == '_') node->b = lim; else node->c = lim;
            p = texSkipWs(p);
          }
          p = texSkipWs(p);
          node->a = texParseGroup(p);
        } else if (cmdLen >= 7 && !strncmp(cmdStart, "underbr", 7)) {
          node = texAlloc(); node->type = TexNode::UNDER;
          p = texSkipWs(p); node->a = texParseGroup(p);
          p = texSkipWs(p);
          if (*p == '_') { p++; node->b = texParseGroup(p); }
        } else if (cmdLen >= 3 &&
                   (!strncmp(cmdStart, "vec", 3) || !strncmp(cmdStart, "hat", 3) ||
                    !strncmp(cmdStart, "bar", 3) || !strncmp(cmdStart, "dot", 3) ||
                    !strncmp(cmdStart, "tilde", 5) || !strncmp(cmdStart, "acute", 5) ||
                    !strncmp(cmdStart, "grave", 5) || !strncmp(cmdStart, "check", 5))) {
          node = texAlloc(); node->type = TexNode::ACCENT;
          node->text = cmdStart; node->len = cmdLen;
          p = texSkipWs(p); node->a = texParseGroup(p);
        } else if (!strncmp(cmdStart, "overline", cmdLen) ||
                   !strncmp(cmdStart, "overrightarrow", cmdLen) ||
                   !strncmp(cmdStart, "overbrace", cmdLen) ||
                   !strncmp(cmdStart, "widetilde", cmdLen) ||
                   !strncmp(cmdStart, "widehat", cmdLen)) {
          node = texAlloc(); node->type = TexNode::ACCENT;
          node->text = cmdStart; node->len = cmdLen;
          p = texSkipWs(p); node->a = texParseGroup(p);
        } else if (cmdLen == 8 && !strncmp(cmdStart, "stackrel", 8)) {
          node = texAlloc(); node->type = TexNode::UNDER;   // подпись СВЕРХУ: переиспользуем UNDER
          node->b = nullptr;                                // нижняя подпись отсутствует
          p = texSkipWs(p); node->a = texParseGroup(p);     // основание
          p = texSkipWs(p); node->c = texParseGroup(p);     // метка сверху
        } else if (!strncmp(cmdStart, "textbf", cmdLen) || !strncmp(cmdStart, "mathbf", cmdLen) ||
                   !strncmp(cmdStart, "boldsymbol", cmdLen)) {
          p = texSkipWs(p);
          node = texParseGroup(p);                        // жирность не эмулируем - содержимое
        } else if (!strncmp(cmdStart, "text", cmdLen) || !strncmp(cmdStart, "mathrm", cmdLen) ||
                   !strncmp(cmdStart, "mathit", cmdLen) || !strncmp(cmdStart, "mbox", cmdLen) ||
                   !strncmp(cmdStart, "operatorname", cmdLen)) {
          p = texSkipWs(p);
          node = texParseGroup(p);                        // содержимое как обычный текст
        } else if (!strncmp(cmdStart, "left", cmdLen) || !strncmp(cmdStart, "right", cmdLen) ||
                   !strncmp(cmdStart, "bigg", cmdLen) || !strncmp(cmdStart, "Bigg", cmdLen) ||
                   !strncmp(cmdStart, "big", cmdLen) || !strncmp(cmdStart, "Big", cmdLen)) {
          continue;                                       // пропускаем: дальше обычная скобка
        } else if (!strncmp(cmdStart, "begin", cmdLen) || !strncmp(cmdStart, "end", cmdLen) ||
                   !strncmp(cmdStart, "section", cmdLen) || !strncmp(cmdStart, "label", cmdLen) ||
                   !strncmp(cmdStart, "nonumber", cmdLen)) {
          p = texSkipWs(p);                               // съедаем аргумент {*}[..]
          if (*p == '{') { p++; while (*p && *p != '}') p++; if (*p == '}') p++; }
          if (*p == '[') { p++; while (*p && *p != ']') p++; if (*p == ']') p++; }
          continue;
        } else if (texIsFunc(cmdStart, cmdLen)) {
          node = texMakeText(cmdStart, cmdLen);           // sin cos log ...
        } else {
          bool found = false;
          for (auto& s : texSyms) {
            if (strlen(s.cmd) == cmdLen && !strncmp(cmdStart, s.cmd, cmdLen)) {
              if (s.repl && *s.repl) node = texMakeText(s.repl, strlen(s.repl));
              else node = texMakeText(cmdStart, cmdLen);  // функции крупным планом
              found = true; break;
            }
          }
          if (!found) node = texMakeText(cmdStart, cmdLen);  // неизвестная команда - как текст
        }
      }
    } else if (*p == '^' || *p == '_') {
      char t = *p; p++;
      TexNode* arg = texParseGroup(p);
      node = texAlloc();
      node->type = (t == '^') ? TexNode::SUP : TexNode::SUB;
      node->a = arg;
      if (tail && !tail->a) {            // привязать к предыдущей ноде (x^2 вместо x ^2)
        tail->b = node;
        p = texSkipWs(p);
        while (*p == '^' || *p == '_') { // цепочка x_1^2
          char t2 = *p; p++;
          TexNode* nxt = texAlloc();
          nxt->type = (t2 == '^') ? TexNode::SUP : TexNode::SUB;
          nxt->a = texParseGroup(p);
          node->next = nxt; node = nxt;
          p = texSkipWs(p);
        }
      }
    } else if (*p == '$') {
      p++; continue;                                      // маркер формулы игнорируем
    } else if (*p == '%') {
      while (*p && *p != terminator) p++;                 // комментарий до конца строки
      continue;
    } else if (*p == '~' || *p == '&') {
      p++; continue;                                      // пробел / разделитель колонок
    } else {
      const char* start = p;
      while (*p && !strchr("^_\\{}$%~& ", *p)) p++;
      node = texMakeText(start, p - start);
    }

    if (node) {
      if (tail) { tail->next = node; tail = node; }
      else { head = tail = node; }
    }
  }
  return head;
}

// одна команда целиком (для аргументов вида \frac dx)
static TexNode* texParseOneCmd(const char*& p) {
  const char* save = p;               // на случай отката
  TexNode* node = nullptr;
  p++;                                // пропускаем '\'
  if (!isalpha((uint8_t)*p)) {        // экранированный символ (\, \; \! \{ ...)
    if (*p) { node = texMakeText(p, 1); p++; }
    return node;
  }
  const char* cmdStart = p;
  while (isalpha((uint8_t)*p)) p++;
  uint8_t cmdLen = p - cmdStart;
  if ((cmdLen == 4 && !strncmp(cmdStart, "frac", 4)) ||
      (cmdLen == 5 && !strncmp(cmdStart, "dfrac", 5))) {
    node = texAlloc(); node->type = TexNode::FRAC;
    p = texSkipWs(p); node->a = texParseGroup(p);
    p = texSkipWs(p); node->b = texParseGroup(p);
  } else if (cmdLen == 5 && !strncmp(cmdStart, "binom", 5)) {
    node = texAlloc(); node->type = TexNode::FRAC;
    node->text = "()"; node->len = 2;
    p = texSkipWs(p); node->a = texParseGroup(p);
    p = texSkipWs(p); node->b = texParseGroup(p);
  } else if (cmdLen == 4 && !strncmp(cmdStart, "sqrt", 4)) {
    node = texAlloc(); node->type = TexNode::SQRT;
    p = texSkipWs(p);
    if (*p == '[') { p++; TexNode* idx = texParseSeq(p, ']'); if (*p == ']') p++; node->c = idx; }
    p = texSkipWs(p); node->a = texParseGroup(p);
  } else if ((cmdLen >= 3 && cmdLen <= 6 &&
              (!strncmp(cmdStart, "int", 3) || !strncmp(cmdStart, "sum", 3) ||
               !strncmp(cmdStart, "prod", 4) || !strncmp(cmdStart, "oint", 4) ||
               !strncmp(cmdStart, "iint", 4) || !strncmp(cmdStart, "iiint", 5) ||
               !strncmp(cmdStart, "coprod", 6))) ||
             (cmdLen == 6 && (!strncmp(cmdStart, "bigcup", 6) ||
                              !strncmp(cmdStart, "bigcap", 6)))) {
    node = texAlloc(); node->type = TexNode::BIGOP;
    node->text = cmdStart; node->len = cmdLen;
    p = texSkipWs(p);
    while (*p == '_' || *p == '^') {
      char t = *p; p++;
      TexNode* lim = texParseGroup(p);
      if (t == '_') node->b = lim; else node->c = lim;
      p = texSkipWs(p);
    }
  } else if ((cmdLen == 3 && !strncmp(cmdStart, "lim", 3)) ||
             (cmdLen == 6 && (!strncmp(cmdStart, "limsup", 6) ||
                              !strncmp(cmdStart, "liminf", 6)))) {
    node = texAlloc(); node->type = TexNode::LIMIT;
    node->text = cmdStart; node->len = cmdLen;
    p = texSkipWs(p);
    while (*p == '_' || *p == '^') {
      char t = *p; p++;
      TexNode* lim = texParseGroup(p);
      if (t == '_') node->b = lim; else node->c = lim;
      p = texSkipWs(p);
    }
  } else if (cmdLen >= 7 && !strncmp(cmdStart, "underbr", 7)) {
    node = texAlloc(); node->type = TexNode::UNDER;
    p = texSkipWs(p); node->a = texParseGroup(p);
    p = texSkipWs(p);
    if (*p == '_') { p++; node->b = texParseGroup(p); }
  } else {
    p = save;                         // простые команды - как обычный токен seq-парсера
    return texParseSeq(p, '\0');
  }
  return node;
}

static TexNode* texParseGroup(const char*& p) {
  p = texSkipWs(p);
  if (*p == '{') { p++; TexNode* g = texParseSeq(p, '}'); if (*p == '}') p++; return g; }
  if (*p == '\\') return texParseOneCmd(p);   // одиночная команда как аргумент (\frac dx)
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
static void texVLine(int16_t x, int16_t y0, int16_t y1) { for (int16_t y = y0; y <= y1; y++) texPix(x, y, 1); }

// печать строки текста шрифтом 5x7 (baselineY - нижняя линия глифа)
static void texPrint(const char* s, uint8_t l, int16_t x, int16_t baselineY) {
  for (uint8_t i = 0; i < l; i++) {
    int16_t idx = (uint8_t)s[i] - 32;
    if (idx < 0 || idx >= 95) idx = '?' - 32;
    for (uint8_t col = 0; col < 5; col++) {
      uint8_t bits = pgm_read_byte(&texFont5x7[idx][col]);
      for (uint8_t row = 0; row < 7; row++)
        if (bits & (1 << row)) texPix(x + col, baselineY - 6 + row, 1);
    }
    x += 6;
  }
}

// ширина списка нод в пикселях (глиф 5px + 1 межсимвольный)
static int16_t texTextW(TexNode* n) {
  int16_t w = 0;
  for (TexNode* c = n; c; c = c->next) {
    switch (c->type) {
      case TexNode::OVER: w += texTextW(c->a) + texTextW(c->b); break;
      case TexNode::TEXT: w += c->len * 6 + texTextW(c->b); break;
      case TexNode::SUP: case TexNode::SUB: w += 6 + texTextW(c->a) + texTextW(c->b); break;
      case TexNode::FRAC: {
        int16_t m = max(texTextW(c->a), texTextW(c->b));
        w += (c->text ? m + 8 : m + 2);                      // бином шире (скобки)
        break;
      }
      case TexNode::SQRT: {
        int16_t iw = c->c ? texTextW(c->c) : 0;
        w += texTextW(c->a) + 6 + (iw ? iw + 2 : 0);
        break;
      }
      case TexNode::BIGOP: {
        int16_t body = texTextW(c->a);
        int16_t lo = texTextW(c->b), hi = texTextW(c->c);
        w += max(body, max(lo, hi)) + 10;
        break;
      }
      case TexNode::LIMIT: {
        int16_t name = c->len * 6;
        w += max(name, max(texTextW(c->b), texTextW(c->a))) + 4;
        break;
      }
      case TexNode::ACCENT: w += texTextW(c->a) + 2; break;
      case TexNode::UNDER:
        w += max(max(texTextW(c->a), texTextW(c->b)), texTextW(c->c)) + 2;
        break;
    }
  }
  return w;
}

// рисует список нод; baselineY - линия базового текста; возвращает итоговую ширину
static int16_t texDraw(TexNode* n, int16_t x, int16_t baselineY) {
  int16_t startX = x;
  for (TexNode* c = n; c; c = c->next) {
    if (c->type == TexNode::SUP || c->type == TexNode::SUB) {
      int16_t yy = (c->type == TexNode::SUP) ? baselineY - 4 : baselineY + 3;
      x += texDraw(c->a, x, yy);
      x += texDraw(c->b, x, yy);               // привязанная цепочка x_1^2
      continue;
    }
    if (c->type == TexNode::OVER) {            // группа {..} - inline
      x += texDraw(c->a, x, baselineY);
      x += texDraw(c->b, x, baselineY);        // привязанные индекс/степень
      continue;
    }
    switch (c->type) {
      case TexNode::TEXT: {
        texPrint(c->text, c->len, x, baselineY);
        x += c->len * 6;
        x += texDraw(c->b, x, baselineY);      // привязанный sup/sub (e^{i\pi})
        break;
      }
      case TexNode::FRAC: {
        int16_t wa = texTextW(c->a), wb = texTextW(c->b);
        int16_t w = max(wa, wb);
        if (c->text) {                                 // \binom: скобки, без черты
          texVLine(x + 1, baselineY - 6, baselineY + 6);
          texPix(x, baselineY - 6, 1); texPix(x, baselineY + 6, 1);
          texDraw(c->a, x + 4, baselineY - 4);
          texDraw(c->b, x + 4, baselineY + 4);
          int16_t sx = x + 4 + w + 1;
          texVLine(sx - 1, baselineY - 6, baselineY + 6);
          texPix(sx, baselineY - 6, 1); texPix(sx, baselineY + 6, 1);
          x += w + 8;
        } else {
          texDraw(c->a, x + (w - wa) / 2, baselineY - 4);   // числитель
          texHLine(x, x + w + 1, baselineY + 1);            // черта дроби
          texDraw(c->b, x + (w - wb) / 2, baselineY + 8);   // знаменатель
          x += w + 2;
        }
        break;
      }
      case TexNode::SQRT: {
        int16_t w = texTextW(c->a);
        int16_t xo = x;
        if (c->c) {                                    // индекс корня (n-я степень)
          texDraw(c->c, x, baselineY - 5);
          xo += texTextW(c->c) + 1;
        }
        texPix(xo, baselineY - 3, 1);
        texPix(xo + 1, baselineY - 1, 1);
        texPix(xo + 2, baselineY + 1, 1);
        texPix(xo + 3, baselineY - 1, 1);
        texPix(xo + 4, baselineY - 3, 1);
        texHLine(xo + 4, xo + 4 + w, baselineY - 7);   // перекладина
        texDraw(c->a, xo + 5, baselineY);
        x = xo + w + 6;
        break;
      }
      case TexNode::BIGOP: {
        const char* sym = "SUM";
        if (c->len == 3 && !strncmp(c->text, "int", 3)) sym = "INT";
        else if (c->len == 4 && !strncmp(c->text, "iint", 4)) sym = "IIN";
        else if (c->len == 5 && !strncmp(c->text, "iiint", 5)) sym = "III";
        else if (c->len == 4 && !strncmp(c->text, "oint", 4)) sym = "CNT";
        else if (c->len == 4 && !strncmp(c->text, "prod", 4)) sym = "PRD";
        else if (c->len == 6 && !strncmp(c->text, "bigcup", 6)) sym = "UUN";
        else if (c->len == 6 && !strncmp(c->text, "bigcap", 6)) sym = "NNA";
        int16_t bw = strlen(sym) * 6;
        int16_t bodyW = texTextW(c->a);
        int16_t loW = texTextW(c->b), hiW = texTextW(c->c);
        int16_t w = max(bodyW, max(max(bw, loW), hiW));
        bool isInt = !strcmp(sym, "INT") || !strcmp(sym, "IIN") || !strcmp(sym, "III") || !strcmp(sym, "CNT");
        if (isInt) {
          // стилизованный знак интеграла: S-образная вертикальная черта
          int16_t ix = x + 2;
          int8_t rep = 1;
          if (!strcmp(sym, "IIN")) rep = 2;
          if (!strcmp(sym, "III")) rep = 3;
          for (int8_t r = 0; r < rep; r++) {
            for (int16_t k = 0; k < 14; k++) {
              int16_t xx = ix + r * 6 + (k < 3 ? (2 - k) : (k > 11 ? (k - 11) : 0));
              texPix(xx, baselineY + 7 - k, 1);
            }
            texPix(ix + r * 6 + 2, baselineY + 7, 1);
            texPix(ix + r * 6 + 1, baselineY - 7, 1);
          }
          if (!strcmp(sym, "CNT")) {                   // контурный интеграл: окружность
            static const int8_t cx[] = {2, 1, 0, -1, -2, -1, 0, 1};
            static const int8_t cy[] = {0, -1, -2, -1, 0, 1, 2, 1};
            for (int8_t a = 0; a < 8; a++) texPix(x + 3 + cx[a], baselineY + cy[a], 1);
          }
          bw = 6 + rep * 6;
        } else {
          TexNode* op = texMakeText(sym, strlen(sym));
          texDraw(op, x, baselineY);
        }
        if (c->c) texDraw(c->c, x, baselineY - 9);              // верхний предел
        if (c->b) texDraw(c->b, x, baselineY + 9);              // нижний предел
        if (c->a) texDraw(c->a, x + max(bw, max(loW, hiW)) + 3, baselineY);
        x += w + 3;
        break;
      }
      case TexNode::LIMIT: {
        int16_t name = c->len * 6;
        int16_t loW = texTextW(c->b);
        int16_t w = max(name, loW);
        texPrint(c->text, c->len, x, baselineY);
        if (c->b) texDraw(c->b, x + (w - loW) / 2, baselineY + 8);   // предел снизу
        if (c->c) texDraw(c->c, x + w + 2, baselineY - 4);           // сверху
        if (c->a) texDraw(c->a, x + w + 4, baselineY);               // тело справа
        x += w + 4;
        break;
      }
      case TexNode::ACCENT: {
        int16_t w = texTextW(c->a);
        texDraw(c->a, x, baselineY);
        const char* t = c->text; uint8_t l = c->len;
        if (l >= 3 && (!strncmp(t, "vec", 3) || !strncmp(t, "acute", l))) {
          texPix(x + w - 2, baselineY - 8, 1);
          texPix(x + w - 1, baselineY - 9, 1);
        } else if (l >= 3 && !strncmp(t, "hat", 3)) {
          texPix(x + w / 2, baselineY - 10, 1);
          texPix(x + w / 2 - 1, baselineY - 9, 1);
          texPix(x + w / 2 + 1, baselineY - 9, 1);
        } else if (l >= 5 && !strncmp(t, "tilde", 5)) {
          texPix(x + w / 2 - 2, baselineY - 8, 1);
          texPix(x + w / 2 - 1, baselineY - 9, 1);
          texPix(x + w / 2, baselineY - 8, 1);
          texPix(x + w / 2 + 1, baselineY - 9, 1);
        } else if (l >= 3 && !strncmp(t, "dot", 3)) {
          texPix(x + w / 2, baselineY - 9, 1);
          if (l >= 4 && !strncmp(t, "ddot", 4)) texPix(x + w / 2 + 3, baselineY - 9, 1);
        } else {                                     // bar / overline / overrightarrow...
          texHLine(x, x + w - 1, baselineY - 8);
          if (l >= 14) {                             // overrightarrow: наконечник стрелки
            texPix(x + w, baselineY - 8, 1);
            texPix(x + w - 1, baselineY - 9, 1);
            texPix(x + w - 1, baselineY - 7, 1);
          }
        }
        x += w + 2;
        break;
      }
      case TexNode::UNDER: {
        int16_t w = texTextW(c->a);
        texDraw(c->a, x, baselineY);
        if (c->b) {                                  // подпись СНИЗУ (underbrace)
          int16_t uy = baselineY + 3;
          for (int16_t i = 0; i < w; i += 2) {       // огибающая
            int16_t dy = (i < 3 || i > w - 4) ? 1 : 0;
            texPix(x + i, uy + dy, 1);
          }
          texPix(x + w / 2, uy + 2, 1);
          int16_t uw = texTextW(c->b);
          texDraw(c->b, x + (w - uw) / 2, baselineY + 10);
        } else if (c->c) {                           // метка СВЕРХУ (stackrel)
          int16_t uw = texTextW(c->c);
          texDraw(c->c, x + (w - uw) / 2, baselineY - 8);
        }
        x += w + 2;
        break;
      }
    }
  }
  return x - startX;
}

// ---------------- загрузка: одна формула = одна страница ----------------
static void texLoadPages(File file) {
  file.seek(0);
  texNameCount = 0;
  memset(texNames, 0, sizeof(texNames));

  String formula;                 // текущая формула (склеенные строки)
  String pendingName;             // название текущей формулы
  bool haveFormula = false;

  auto flush = [&](const String& name) {
    if (formula.length() == 0) return;
    if (texNameCount >= 24) return;               // защита от переполнения
    texLineCount = 0;
    memset(texLines, 0, sizeof(texLines));
    uint8_t li = 0, ci = 0;
    for (uint16_t i = 0; i < formula.length(); i++) {
      char ch = formula[i];
      if (ch == '\n') { li++; ci = 0; if (li >= TEX_MAX_LINES) li = TEX_MAX_LINES - 1; continue; }
      if (ci < TEX_LINE_LEN - 1) texLines[li][ci++] = ch;
    }
    texLines[li][ci] = 0;
    texLineCount = li + 1;
    uint8_t nl = min<uint16_t>(name.length(), TEX_NAME_LEN);
    memcpy(texNames[texNameCount], name.c_str(), nl);
    texNames[texNameCount][nl] = 0;
    texNameCount++;
    formula = "";
    haveFormula = false;
  };

  String line;
  while (file.available()) {
    int c = file.read();
    if (c == '\n' || c == '\r') {
      if (c == '\r' && file.peek() == '\n') file.read();
      line.trim();
      if (line.startsWith("##")) {                 // ## Название - новая страница
        flush(pendingName);
        pendingName = line.substring(2);
        pendingName.trim();
      } else if (line.startsWith("%") || line.startsWith("//")) {
        // комментарий - игнор
      } else if (line.startsWith("\\title")) {     // \title{Название} - новая страница
        flush(pendingName);
        pendingName = "";
        int a = line.indexOf('{'), b = line.lastIndexOf('}');
        if (a >= 0 && b > a) pendingName = line.substring(a + 1, b);
        pendingName.trim();
      } else if (line.length()) {
        if (pendingName.length() == 0 && !haveFormula) {
          int sep = line.indexOf(": ");            // "Название: формула"
          if (sep > 0 && sep <= TEX_NAME_LEN) {
            pendingName = line.substring(0, sep);
            line = line.substring(sep + 2);
          }
        }
        if (haveFormula) formula += "\n";
        formula += line;
        haveFormula = true;
      }
      line = "";
      yield();
    } else {
      if (line.length() < TEX_LINE_LEN * TEX_MAX_LINES) line += (char)c;
    }
  }
  line.trim();
  if (line.length() && !line.startsWith("%") && !line.startsWith("//")) {
    if (haveFormula) formula += "\n";
    formula += line;
    haveFormula = true;
  }
  flush(pendingName);
}

static uint8_t texTotalPages() {
  return max<uint8_t>(texNameCount, 1);
}

static void texRenderPage(File file) {
  // ВАЖНО: setup() включает oled.autoPrintln(true), из-за чего любая печать
  // короче ширины экрана переводит курсор на новую строку и screen.dirty()
  // каждый такой вызов выводит ОДНУ строку. Пока идем построчно - это выглядит
  // как "все формулы = последняя". Отключаем автоперенос на время рендера.
  bool ap = oled.isAutoPrintln();
  oled.autoPrintln(false);

  file.seek(0);
  texLoadPages(file);

  uint8_t totalPages = texTotalPages();
  if (texPage >= totalPages) texPage = totalPages - 1;

  memset(texBuf, 0, sizeof(texBuf));
  texMinY = 63; texMaxY = 0;

  // название формулы - сверху по центру (если задано)
  const char* name = texNames[texPage];
  int16_t topY = 8;
  if (name && *name) {
    int16_t nw = strlen(name) * 6;
    int16_t nx = (128 - nw) / 2;
    if (nx < 0) nx = 0;
    texPrint(name, strlen(name), nx, topY);
    texHLine(nx, nx + nw - 1, topY + 2);
    topY += 6;
  }

  // сама формула: блоки строк разделены переводом строки
  uint8_t rows = texLineCount;
  int16_t blockH = rows * 18;
  int16_t avail = 60 - topY;
  int16_t y0 = topY + max<int16_t>((avail - blockH) / 2 + 12, 12);

  for (uint8_t i = 0; i < rows; i++) {
    const char* src = texLines[i];
    if (!*src) continue;
    TexNode* tree = texParseSeq(src, '\0');
    int16_t w = texTextW(tree);
    int16_t x = (128 - w) / 2;
    if (x < 0) x = 0;                         // широкие формумы печатаем с левого края
    texDraw(tree, x, y0 + i * 18);
  }

  // нижний статус: номер страницы
  char status[16];
  snprintf(status, sizeof(status), "%d/%d", texPage + 1, totalPages);
  texPrint(status, strlen(status), 128 - strlen(status) * 6 - 1, 62);

  // вывод буфера на OLED: заполняем весь экран за один update()
  oled.clear();
  for (int16_t y = 0; y < 64; y++)
    for (int16_t x = 0; x < 128; x++)
      if (texBuf[y * 16 + (x >> 3)] & (0x80 >> (x & 7))) oled.dot(x, y, 1);
  oled.update();

  oled.autoPrintln(ap);                       // возвращаем настройку читалки
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
  { // посчитать страницы (формул)
    texLoadPages(file);
    pages = texTotalPages();
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
