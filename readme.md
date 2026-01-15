# Project Kalkan (WaterFlowPressTest)

ESP32 tabanli, debi (flow) ve tank seviye sensorlerini izleyen, LCD uzerinden gosteren ve SD karta kaydeden microcontroller projesi.

Detayli kullanici kilavuzu icin `manual.md` dosyasina bakabilirsiniz.

## Ozellikler

- Debi sensoru (pulse) okumasi ve istatistikleri
- 4-20 mA seviye sensoru okuma, filtreleme ve istatistik
- 16x2 I2C LCD arayuz (ekranlar arasi gezinme)
- SD kart gunluk log ve olay (event) kaydi
- Kalibrasyon menusu (cihaz uzerinden ayarlanabilir)

## Donanim Ozet

- MCU: ESP32 (PlatformIO `esp32dev`)
- LCD: 16x2 I2C (adres `0x27`)
- Debi sensoru: pulse cikisli
- Seviye sensoru: 4-20 mA (ADC uzerinden okuma)
- Kontrol: 2 buton + analog joystick
- SD kart: SPI

## Pin Haritasi

| Bilesen | Pin |
| --- | --- |
| Debi sensoru | GPIO25 |
| Seviye sensoru (ADC) | GPIO32 |
| Joystick X | GPIO27 |
| Joystick Y | GPIO26 |
| Buton 1 | GPIO13 |
| Buton 2 | GPIO14 |
| LCD SDA / SCL | GPIO21 / GPIO22 |
| SD CS / MOSI / MISO / SCK | GPIO5 / GPIO23 / GPIO19 / GPIO18 |

## Ekran ve Kontroller

### Joystick
- Saga/sola: ekranlar arasi gecis
- Yukari/asagi: deger arttirma/azaltma (zaman, tarih, kalibrasyon)

### Butonlar
- Buton 1 (kisa basma): olay kaydi (event snapshot)
- Buton 1 (3 saniye basili): SD karti guvenli cikarma modu
- Buton 1 + Buton 2 (5 saniye basili): kalibrasyon menusu
- Buton 2: kalibrasyondan cikis

## Ilk Calistirma Akisi

1) Cihaza guc verin, boot ekrani yaklasik 5 saniye gorunur.
2) "Zamani Ayarla" ekrani gelir (joystick ile saat/dakika).
3) "Tarihi Ayarla" ekrani gelir (joystick ile gun/ay/yil).
4) Onaydan sonra ana ekrana gecilir.

## SD Kart Kayitlari

- Gunluk log: `/logs/YYYY-MM-DD.csv`
- Olay kaydi: `/events/event_YYYY-MM-DDTHH-MM-SS.csv`

### Guvenli cikarma

SD karti cikarmadan once:
1) Buton 1'e 3 saniye basili tutun.
2) Ekranda "SD kart kaldirildi" mesaji gorunur.
3) Mesajdan sonra karti fiziksel olarak cikarin.

Kart tekrar takilinca cihaz otomatik algilar ve "SD kart hazir" mesaji gosterir.

## Kalibrasyon (kisa ozet)

Kalibrasyon menusune girmek icin Buton 1 + Buton 2'ye 5 saniye basili tutun.
- Joystick saga/sola: kalem degistir
- Joystick yukari/asagi: deger degistir
- Buton 1: kaydet
- Buton 2: cikis

Kalibrasyon kalemleri: Depth cm, Density, Zero/Full mA, Full mm, Pulse/L, Sensor ms, Log ms, Shunt ohm, Gain.

## Yazilim ve Derleme

Bu proje PlatformIO ile gelistirilmistir.

Gerekli kutuphaneler (platformio.ini):
- LiquidCrystal_I2C
- SdFat
- Bounce2
- ArduinoJson

Komutlar:
```bash
pio run
pio run -t upload
pio device monitor -b 115200
```

## Proje Yapisi

- `src/main.cpp`: uygulama giris noktasi ve gorevler
- `lib/`: ozel kutuphaneler (Buttons, LcdUI, SdLogger, vs.)
- `manual.md`: kodlama bilmeyenler icin ayrintili kullanim kilavuzu

## Sorun Giderme (kisa)

- Debi 0 gorunuyor: debi sensoru baglantisi ve Pulse/L degerini kontrol edin.
- Seviye dalgalaniyor: sensor kablolari ve topraklamayi kontrol edin.
- SD kart algilanmiyor: karti cikartip yeniden takin, "SD kart hazir" mesajini bekleyin.
