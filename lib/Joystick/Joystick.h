#pragma once
#include <Arduino.h>

/**
 * 2D analog joystick okuyucu.
 * - 12-bit ADC okur, IIR filtre uygular.
 * - normalize çıkış: [-1.0 .. +1.0]
 * - deadzone ve merkez (calibration) desteği
 * - step/repeat: menü veya tarih-saat arttır/azalt için
 */
class Joystick {
public:
  Joystick(uint8_t pinX, uint8_t pinY)
  : _pinX(pinX), _pinY(pinY) {}

  void begin() {
    analogReadResolution(12); // 0..4095
    // ESP32 için (isteğe bağlı) uygun atten ayarı:
    // analogSetPinAttenuation(_pinX, ADC_11db);
    // analogSetPinAttenuation(_pinY, ADC_11db);
    // ilk merkez kabulü (uyg. açılışta stick'i serbest bırakılmış kabul ediyoruz)
    _cx = analogRead(_pinX);
    _cy = analogRead(_pinY);
    _fx = _cx; _fy = _cy;
  }

  // Kullanıcı merkez kalibrasyonu (menüde uzun basış vb.)
  void calibrateCenter() {
    _cx = analogRead(_pinX);
    _cy = analogRead(_pinY);
    _fx = _cx; _fy = _cy;
  }

  // Her döngüde çağır (ör. 50–200 Hz)
  void sample() {
    const float a = _alpha;
    int rx = analogRead(_pinX);
    int ry = analogRead(_pinY);
    _fx = a*rx + (1.0f-a)*_fx;
    _fy = a*ry + (1.0f-a)*_fy;
    _nx = norm(_fx, _cx);
    _ny = norm(_fy, _cy);
    updateStepRepeat();
  }

  // normalize edilmiş -1..+1
  float nx() const { return _nx; }
  float ny() const { return _ny; }

  // anlık yön (threshold üstünde ise)
  bool left()  const { return _nx < -_th; }
  bool right() const { return _nx >  _th; }
  bool up()    const { return _ny >  _th; }   // genelde Y yukarı artı kabul
  bool down()  const { return _ny < -_th; }

  // step olayları (menü/tarih-saat için “tik” üretir)
  bool stepLeft()  { return consume(_stepL); }
  bool stepRight() { return consume(_stepR); }
  bool stepUp()    { return consume(_stepU); }
  bool stepDown()  { return consume(_stepD); }

  // ayarlar
  void setDeadzone(float dz)     { _dead = constrain(dz, 0.0f, 0.5f); }
  void setThreshold(float th)    { _th = constrain(th, 0.02f, 0.9f); }
  void setAlpha(float alpha)     { _alpha = constrain(alpha, 0.01f, 1.0f); }
  void setRepeat(uint16_t firstMs, uint16_t contMs) {
    _firstRepeatMs = max<uint16_t>(firstMs, 50);
    _contRepeatMs  = max<uint16_t>(contMs,  30);
  }

private:
  // [-1..+1] normalizasyon + deadzone
  float norm(float v, int c) const {
    // tipik joystick tam sapmada ~ 0..4095; orta ~2048 civarı
    float t = (v - c) / 2048.0f;
    if (fabsf(t) < _dead) t = 0.0f;
    return constrain(t, -1.0f, 1.0f);
  }

  // step/repeat üretimi: edge + repeat (klavye tuşu gibi)
  void updateStepRepeat() {
    uint32_t now = millis();

    // X ekseni
    int dirX = (_nx > _th) ? +1 : (_nx < -_th ? -1 : 0);
    stepCore(dirX, _lastDirX, _pressT_X, _nextT_X, _stepR, _stepL, now);

    // Y ekseni
    int dirY = (_ny > _th) ? +1 : (_ny < -_th ? -1 : 0);
    stepCore(dirY, _lastDirY, _pressT_Y, _nextT_Y, _stepU, _stepD, now);
  }

  // dir=+1 pozitif yön (X: right, Y: up), dir=-1 negatif
  void stepCore(int dir, int &lastDir, uint32_t &pressT, uint32_t &nextT,
                bool &stepPos, bool &stepNeg, uint32_t now)
  {
    // merkezde
    if (dir == 0) { lastDir = 0; pressT = nextT = 0; return; }

    // yeni yön -> ilk adım
    if (dir != lastDir) {
      lastDir = dir;
      pressT = now;
      nextT  = now + _firstRepeatMs;
      if (dir > 0) stepPos = true; else stepNeg = true;
      return;
    }

    // basılı tutma -> repeat
    if (now >= nextT) {
      nextT = now + _contRepeatMs;
      if (dir > 0) stepPos = true; else stepNeg = true;
    }
  }

  bool consume(bool &flag){ bool t=flag; flag=false; return t; }

  // pins
  uint8_t _pinX, _pinY;

  // filters
  float _alpha = 0.25f;  // IIR
  float _fx=2048, _fy=2048;
  int   _cx=2048, _cy=2048;

  // outputs
  float _nx=0.0f, _ny=0.0f;
  float _dead = 0.06f;   // deadzone
  float _th   = 0.20f;   // “yön” eşiği (deadzone’dan büyük olmalı)

  // step/repeat
  uint16_t _firstRepeatMs = 400;  // ilk tekrar gecikmesi
  uint16_t _contRepeatMs  = 120;  // devam tekrarı
  int _lastDirX=0, _lastDirY=0;
  uint32_t _pressT_X=0, _pressT_Y=0, _nextT_X=0, _nextT_Y=0;
  bool _stepL=false, _stepR=false, _stepU=false, _stepD=false;
};
