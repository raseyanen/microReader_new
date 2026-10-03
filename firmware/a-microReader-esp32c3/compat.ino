/* ======================================================================= */
/* ============ Совместимость: пространства имён su / sutil ============== */
// StringUtils (GyverLibs) объявляет классы в namespace su и алиас sutil = su.
// Если в Arduino IDE подключена ДРУГАЯ версия библиотеки (или её нет),
// эти имена не определены и скетч не компилируется.
// Данный файл предоставляет минимальную совместимую реализацию, которая
// включается ТОЛЬКО если StringUtils с нужными классами не найдена.

// Признак настоящей StringUtils от GyverLibs: файл utils/TextParser.h существует
#if defined(__has_include)
#if __has_include(<utils/TextParser.h>)
#define GYVER_STRINGUTILS_AVAILABLE 1
#endif
#endif

#ifdef GYVER_STRINGUTILS_AVAILABLE
// Настоящая библиотека на месте - ничего не определяем, используем её su:: / sutil::
#include <StringUtils.h>

#else  // StringUtils отсутствует или это другая библиотека - включаем заглушки

#include <Arduino.h>

namespace su {

// Минимальная совместимая замена su::Text (то, что используется в скетче)
class Text : public Printable {
   public:
    Text() {}
    Text(const char* str) : _str(str ? str : ""), _len(strlen(_str)) {}
    Text(const String& str) : _owned(str), _str(_owned.c_str()), _len(_owned.length()) {}

    // длина в юникод-символах (UTF-8): считаем байты, не являющиеся продолжением
    uint16_t lengthUnicode() const {
        uint16_t n = 0;
        for (uint16_t i = 0; i < _len; i++) {
            if (((uint8_t)_str[i] & 0xC0) != 0x80) n++;
        }
        return n;
    }

    uint16_t length() const { return _len; }
    bool valid() const { return _str != nullptr; }

    // заканчивается ли строка на txt
    bool endsWith(const Text& txt) const {
        if (!_str || txt._len > _len || txt._len == 0) return false;
        return !memcmp(_str + _len - txt._len, txt._str, txt._len);
    }
    bool endsWith(const char* s) const { return endsWith(Text(s)); }

    // обрезать пробелы/переносы по краям
    Text trim() const {
        if (!_str) return Text();
        uint16_t a = 0, b = _len;
        while (a < b && (_str[a] == ' ' || _str[a] == '\t' || _str[a] == '\r' || _str[a] == '\n')) a++;
        while (b > a && (_str[b - 1] == ' ' || _str[b - 1] == '\t' || _str[b - 1] == '\r' || _str[b - 1] == '\n')) b--;
        String s(&_str[a], b - a);
        return Text(s);
    }

    // ========================== ЧИСЛА ==========================
    int32_t toInt() const {
        if (!_str) return 0;
        String s(_str, _len);
        return (int32_t)s.toInt();
    }

    // hex-число (0xFF / FFFF / f) до 32 бит
    uint32_t toInt32HEX() const {
        if (!_str) return 0;
        uint32_t val = 0;
        uint16_t i = 0;
        if (_len >= 2 && _str[0] == '0' && (_str[1] == 'x' || _str[1] == 'X')) i = 2;
        for (; i < _len; i++) {
            char c = _str[i];
            uint8_t d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else break;
            val = (val << 4) | d;
        }
        return val;
    }

    String toString() const {
        if (!_str) return String();
        return String(_str, _len);
    }

    // вывод в Print (для oled.print(p) и т.п.)
    virtual size_t printTo(Print& p) const {
        if (!_str) return 0;
        return p.write((const uint8_t*)_str, _len);
    }

   protected:
    String _owned;          // владение строкой (для варианта с String)
    const char* _str = nullptr;
    uint16_t _len = 0;
};

// Минимальная совместимая замена su::TextParser (парсит по одному символу-разделителю)
class TextParser : public Text {
   public:
    TextParser(const Text& txt, char div) : full(txt.toString()), divc(div) {}
    TextParser(const String& txt, char div) : full(txt), divc(div) {}

    // парсить следующий фрагмент. Вернёт true, если есть ещё фрагменты
    bool parse() {
        if (done) return false;
        int idx = full.indexOf(divc, pos);
        if (idx < 0) {
            assign(Text(full.substring(pos)));
            pos = full.length();
            done = true;
        } else {
            assign(Text(full.substring(pos, idx)));
            pos = idx + 1;
        }
        cnt++;
        return true;
    }

    // присвоить Text базовой части (без срезки полей String)
    void assign(const Text& t) {
        _owned = "";
        tmp = t.toString();
        _str = tmp.c_str();
        _len = tmp.length();
    }

    // индекс текущей подстроки (начиная с 1)
    int index() const { return cnt; }

    // получить текущую подстроку
    const Text& get() const { return *this; }

   private:
    String full;
    String tmp;             // хранилище текущей подстроки (_str указывает сюда)
    char divc;
    int pos = 0;
    int cnt = 0;
    bool done = false;
};

}  // namespace su

// Алиас для старых версий кода (StringUtils legacy)
namespace sutil = su;

#endif  // GYVER_STRINGUTILS_AVAILABLE
