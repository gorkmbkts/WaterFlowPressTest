# Project Kalkan Kullanim Kilavuzu

Bu kilavuz, Project Kalkan cihazini kod yazmadan kullanmak isteyenler icindir.
Cihaz su akisini (debi) ve tank seviyesini olcer, LCD ekranda gosterir ve SD karta kaydeder.

## Donanim ve baglantilar (ozet)

Bu bolum, cihazin fiziksel baglantilarini kontrol etmek isteyenler icindir.

| Bilesen | Pin |
| --- | --- |
| Debi sensuru (flow) | GPIO25 |
| Seviye sensuru (4-20 mA) | GPIO32 (ADC1) |
| Joystick X | GPIO27 |
| Joystick Y | GPIO26 |
| Buton 1 | GPIO13 |
| Buton 2 | GPIO14 |
| LCD (I2C SDA/SCL) | GPIO21 / GPIO22 |
| SD kart (CS/MOSI/MISO/SCK) | GPIO5 / GPIO23 / GPIO19 / GPIO18 |

Notlar:
- LCD 16x2 I2C ekrandir (adres 0x27).
- Butonlar dahili pull-up ile kullanilir; butonun bir ucu GND'ye gider.
- SD kart olmadan cihaz calisir, ancak kayit yapamaz.

## Ilk calistirma ve zaman ayari

1) Cihaza guc verin. Ekranda yaklasik 5 saniye "Project Kalkan" ve "Hazirlaniyor..." gorunur.
2) Ardindan "Zamani Ayarla" ekrani gelir.
   - Joystick yukari/asagi: saat/dakika degerini arttirir/azaltir.
   - Joystick saga/sola: imleci bir sonraki haneye tasir.
   - Son haneden saga gecince tarih ekranina otomatik gider.
3) "Tarihi Ayarla" ekrani gelir.
   - Joystick yukari/asagi: gun/ay/yil degerini degistirir.
   - Joystick saga/sola: alan degistirir.
   - Son alandan saga gecince tarih/saat kaydedilir ve ana ekrana gecilir.

Not:
- Cihaz her acilista zaman/tarih ekranlarini gosterir.
- Zaman/tarih, log dosyalarinin adlandirilmasi icin kullanilir.

## Kontroller (buton ve joystick)

### Joystick
- Saga/sola: ekranlar arasinda gezinme.
- Yukari/asagi: deger arttirma/azaltma (zaman, tarih, kalibrasyon).
- Yonu ters hissederseniz, ekrandaki hareketi takip ederek dogru yonu bulun.

### Butonlar
- Buton 1 (kisa basma): Olay kaydi (event snapshot) baslatir.
- Buton 1 (3 saniye basili): SD karti guvenli cikarma moduna alir.
- Buton 1 + Buton 2 (5 saniye basili): Kalibrasyon menusu.
- Buton 2: Kalibrasyondan cikis.

## Ekranlar ve gosterimler

### Ana ekran (Main)
Ekran iki satirdir ve otomatik kayar (yaklasik 2 saniyede bir).

FLOW satiri (debi):
- Q: Anlik debi (L/s)
- Med: Medyan debi
- P10: %10 persentil (minimum saglikli debi)
- P90: %90 persentil (baz debi)
- d%: Anlik debinin baza gore farki
- CV: Darbe periyodu degiskenligi (kararlilik)

TANK satiri (seviye):
- h: Anlik seviye (cm)
- Med: Medyan seviye
- Empty: Tahmini bos seviye
- Full: Tahmini dolu seviye
- d%: Anlik seviye ile dolu seviye arasindaki fark
- Noise: Olcum gurultusu yuzdesi
- Sig: Sinyal kalitesi (good/fair/poor)

Not:
- Ekranda "--" gorurseniz sensor verisi yok veya hesaplanamadi demektir.

### Seviye istatistik ekrani (LevelStats)
- Satir 1: MED (medyan seviye), N (gurultu yuzdesi)
- Satir 2: E (tahmini bos), F (tahmini dolu), d (fark yuzdesi)

### Debi istatistik ekrani (FlowStats)
- Satir 1: MED (medyan debi), CV (degiskenlik)
- Satir 2: P10 (min saglikli), P90 (baz)

### Ekranlar arasi gecis
Ana ekran <-> Seviye istatistik <-> Debi istatistik:
Joysticku saga/sola iterek gecis yapilir.

## SD kart kullanimi ve kayitlar

### Guvenli cikarma
1) SD karti cikarmadan once Buton 1'e 3 saniye basili tutun.
2) Ekranda "SD kart kaldirildi" mesaji gorunur (yaklasik 5 saniye).
3) Bu mesajdan sonra karti fiziksel olarak cikarin.

Kart tekrar takildiginda ekranda "SD kart hazir" mesaji gorunur ve kayitlar devam eder.

### Log dosyalari
- Gunluk kayit: `/logs/YYYY-MM-DD.csv`
- Olay kaydi: `/events/event_YYYY-MM-DDTHH-MM-SS.csv`

CSV dosyalari Excel veya benzeri programlarda acilabilir.

### Olay kaydi (Event snapshot)
Buton 1'e kisa basinca olay kaydi baslar:
- Yaklasik son 20 dakikalik veri ve
- Sonraki 60 dakikalik veri
tek bir dosyada toplanir.

Not:
- Olay kaydi ekranda ayrica bir mesaj gostermeyebilir.

## Kalibrasyon ve ayarlar

### Kalibrasyon menusune giris
Buton 1 + Buton 2'ye birlikte 5 saniye basili tutun.
Ekranda "CAL ..." basligi gorunur.

### Kalibrasyonda kontrol
- Joystick saga/sola: kalem (ayar) degistirir.
- Joystick yukari/asagi: deger degistirir.
- Buton 1: OK (degeri kaydet).
- Buton 2: EX (cikis).

### Kalibrasyon kalemleri ve anlamlari
- Depth cm: Gercek olculen su derinligi (cm).
  - Tankta cetvelle olcerek degeri girin.
  - Buton 1 ile onaylayinca cihaz olcumu bu degere hizalar.
- Density: Sivi yogunluk katsayisi. Su icin 1.00.
- Zero mA: Seviye sensoru minimum akimi (genelde 4 mA).
- Full mA: Seviye sensoru maksimum akimi (genelde 20 mA).
- Full mm: Sensorun full skala yuksekligi (mm).
- Pulse/L: Debi sensoru icin litre basina darbe sayisi.
- Sensor ms: Sensor okuma araligi (ms, minimum 200).
- Log ms: SD karta yazma araligi (ms, minimum 500).
- Shunt ohm: Akim olcum direnci (ohm).
- Gain: Akim olcum kazanci.

Not:
- Ayarlar kalicidir; cihaz kapanip acilsa da saklanir.
- Yanlis kalibrasyon olcumleri ciddi etkiler. Degerleri bilmiyorsaniz degistirmeyin.

## Sorun giderme (hizli)

- Ekranda "--" goruyorum:
  Sensor baglantisini, gucu ve kalibrasyonu kontrol edin.
- Debi hep 0:
  Debi sensoru kablosu ve Pulse/L degeri dogru mu kontrol edin.
- Gurultu (Noise) yuksek:
  Seviye sensoru kablolari, topraklama ve kablo guzergahini kontrol edin.
- SD kart algilanmiyor:
  Karti cikarip tekrar takin. "SD kart hazir" mesaji gelene kadar bekleyin.
- Zaman/tarih surekli yanlis:
  Her acilista zaman/tarihi tekrar ayarlayin.
