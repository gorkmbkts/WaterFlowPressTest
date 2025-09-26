#pragma once
#include <Arduino.h>

/**
 * Button: debounced edge, click, longPress, doubleClick, hold repeat.
 * - INPUT_PULLUP varsayar (LOW = pressed).
 * - poll() her döngü çağrılmalı (5-10 ms aralık iyidir).
 */
struct ButtonEvent {
  bool pressed = false;     // edge: basıldı (down)
  bool released = false;    // edge: bırakıldı (up)
  bool click = false;       // kısa basış (up ile tetiklenir)
  bool longPress = false;   // uzun basış (basılıyken bir kez)
  bool doubleClick = false; // çift tıklama (kısa + kısa)
  bool repeat = false;      // basılı tutarken tekrar darbesi (auto-repeat)
};

class Button {
public:
  Button(uint8_t pin, bool usePullup=true): _pin(pin), _pullup(usePullup) {}

  void begin() {
    pinMode(_pin, _pullup ? INPUT_PULLUP : INPUT);
    _stable = rawRead();
    _lastChange = millis();
    _lastUpTime = _lastDownTime = 0;
    _clickArmed = false;
  }

  // zaman parametreleri
  void setTimings(uint16_t debounceMs, uint16_t longMs, uint16_t dcGapMs,
                  uint16_t firstRptMs, uint16_t nextRptMs) {
    _debounceMs = debounceMs;
    _longMs = longMs;
    _doubleClickGapMs = dcGapMs;
    _firstRepeatMs = firstRptMs;
    _nextRepeatMs = nextRptMs;
  }

  // tipik kullanım: loop içinde sıkça çağır
  void poll() {
    uint32_t now = millis();
    bool raw = rawRead();

    // debouncing
    if (raw != _reading) {
      _reading = raw;
      _lastBounce = now;
    }
    if ((now - _lastBounce) >= _debounceMs && _reading != _stable) {
      _stable = _reading;
      _lastChange = now;

      if (isDown()) {
        // DOWN edge
        _event.pressed = true;
        _lastDownTime = now;
        _longFired = false;
        _repeatArmed = false;
        // çift tıklama için: previous click silsilesi
        if (_clickArmed && (now - _lastUpTime) <= _doubleClickGapMs) {
          _event.doubleClick = true;
          _clickArmed = false; // tüket
        }
      } else {
        // UP edge
        _event.released = true;
        _lastUpTime = now;

        // kısa basış (long tetiklenmemişse)
        if (!_longFired) {
          // eğer doubleClick mümkün olsun istiyorsak click'i hemen tetiklemek yerine "arm" edelim
          if (!_event.doubleClick) {
            _clickArmed = true;     // potansiyel 1. klik
            _clickArmTime = now;
          }
        }
      }
    }

    // “click” tetikleme (double-click fırsatı verdikten sonra)
    if (_clickArmed && (now - _clickArmTime) > _doubleClickGapMs) {
      _event.click = true;          // tek tık olarak onayla
      _clickArmed = false;
    }

    // longPress
    if (isDown() && !_longFired && (now - _lastDownTime) >= _longMs) {
      _event.longPress = true;
      _longFired = true;
      _repeatArmed = true;
      _nextRepeatTime = now + _firstRepeatMs; // uzun basıştan sonra auto-repeat başlasın
    }

    // auto-repeat (uzun basıştan sonra periyodik darbe)
    if (_repeatArmed && isDown() && now >= _nextRepeatTime) {
      _event.repeat = true;
      _nextRepeatTime = now + _nextRepeatMs;
    }
  }

  // olay bayraklarını tüket ve sıfırla
  ButtonEvent consume() {
    ButtonEvent e = _event;
    _event = ButtonEvent{};
    return e;
  }

  bool isDown() const {
    // pullup ise LOW = pressed
    return _pullup ? (_stable == LOW) : (_stable == HIGH);
  }

private:
  bool rawRead() const { return digitalRead(_pin); }

  uint8_t _pin; bool _pullup;
  bool _reading=false, _stable=false;
  uint32_t _lastBounce=0, _lastChange=0;

  // double click ve click arming
  bool _clickArmed=false;
  uint32_t _clickArmTime=0, _lastUpTime=0, _lastDownTime=0;

  // long & repeat
  bool _longFired=false, _repeatArmed=false;
  uint32_t _nextRepeatTime=0;

  // parametreler
  uint16_t _debounceMs = 10;
  uint16_t _longMs     = 600;
  uint16_t _doubleClickGapMs = 300;
  uint16_t _firstRepeatMs = 350;
  uint16_t _nextRepeatMs  = 120;

  // çıkış olayları
  ButtonEvent _event;
};

/**
 * İki buton için “aynı anda basma (combo)” tespiti.
 * - comboStart: ikisi de basılı duruma ilk kez geçtiğinde
 * - comboLong : ikisi de basılı ve uzun süre geçtiğinde
 * - comboEnd  : herhangi biri bırakıldığında
 */
struct ComboEvent {
  bool comboStart = false;
  bool comboLong  = false;
  bool comboEnd   = false;
};

class ButtonsCombo {
public:
  ButtonsCombo(Button& a, Button& b): _a(a), _b(b) {}

  void setTimings(uint16_t windowMs, uint16_t longMs) {
    _windowMs = windowMs; _longMs = longMs;
  }

  void poll() {
    uint32_t now = millis();
    ComboEvent ev;

    bool A = _a.isDown();
    bool B = _b.isDown();

    if (!_comboActive) {
      // birlikte basılma penceresi: önce birine basıldıysa kısa süre içinde öbürü gelirse
      if ((A && !B) || (!A && B)) {
        if (_firstDownTime == 0) _firstDownTime = now;
        // pencere içinde diğeri basılırsa combo başlar
        if (A && B && (now - _firstDownTime) <= _windowMs) {
          _comboActive = true;
          _comboStartTime = now;
          ev.comboStart = true;
        }
      }
      // doğrudan aynı anda (aynı döngü içinde) basılırsa
      if (A && B && !_comboActive) {
        _comboActive = true;
        _comboStartTime = now;
        ev.comboStart = true;
      }
    } else {
      // combo aktifken
      if (!(A && B)) {
        _comboActive = false;
        _firstDownTime = 0;
        ev.comboEnd = true;
      } else {
        if (!_comboLongFired && (now - _comboStartTime) >= _longMs) {
          _comboLongFired = true;
          ev.comboLong = true;
        }
      }
    }

    // olayları biriktir ve expose et
    _evt.comboStart |= ev.comboStart;
    _evt.comboLong  |= ev.comboLong;
    _evt.comboEnd   |= ev.comboEnd;

    // reset koşulları
    if (!_comboActive) _comboLongFired = false;
    if (!A && !B) _firstDownTime = 0; // her ikisi de kalktıysa pencere sıfırlansın
  }

  ComboEvent consume() {
    ComboEvent e = _evt;
    _evt = ComboEvent{};
    return e;
  }

  bool active() const { return _comboActive; }

private:
  Button& _a; Button& _b;
  bool _comboActive=false, _comboLongFired=false;
  uint32_t _firstDownTime=0, _comboStartTime=0;
  uint16_t _windowMs = 80;  // “aynı anda” sayılacak pencere
  uint16_t _longMs   = 700; // uzun combo
  ComboEvent _evt;
};
