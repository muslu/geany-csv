---
name: bassiz-geany-testi
description: geany-csv eklentisini Xvfb + xdotool ile gerçek Geany'de uçtan uca sınama yöntemi ve tuzakları
metadata:
  type: project
---

> Not: Derleme, `make` hedefleri ve SDK kurulumu CLAUDE.md/README'de — burada tekrar yok.

Eklentiyi gerçek Geany'de arayüzsüz sınamanın işleyen düzeni (2026-09-18'de kullanıldı):

1. `Xvfb :77 -screen 0 1280x800x24 &`
2. Yalıtılmış ayar dizini: `geany -c <dizin>`; `<dizin>/geany.conf` içinde
   `[plugins] load_plugins=true` + `active_plugins=<mutlak .so yolu>;`
   — eklenti sistem eklenti dizininde olmak zorunda değil, mutlak yol yeterli.
3. `geany -c <dizin> -i <dosya>` — **`-i` (yeni örnek) şart**, yoksa ikinci
   çağrı çalışan örneğe dosya açtırır ve süreç izlemesi karışır.
4. `xdotool mousemove X Y click 1` + `import -window root <png>` ile adım adım
   doğrula. Menü koordinatları pencere boyutuna göre kayar — **her diyalogu
   tıklamadan önce ekran görüntüsü al**, sabit koordinat varsayma.

Tuzaklar:
- Pencere yöneticisi yok; `xdotool getactivewindow` çalışmaz, `--class geany`
  ile pencere ara ya da doğrudan kök ekranı yakala.
- `xdotool type` içindeki satır sonu Scintilla'ya **Enter olarak gitmez**;
  çok satırlı metin tek satıra yapışır. Satır atlamak için ayrı `key Return`.
- Geany `pref_editor_new_line=true` ile **kaydederken dosya sonuna satır sonu
  ekler**. "Belge gerçekten boş mu" sorusunu kaydettikten sonra dosya
  boyutuna bakarak yanıtlama — editördeki satır sayacına (`satır: 1 / 1`) bak.
- Çöküş kanıtı için Geany'yi sarmalayıcı betikten çalıştır ve `$?` kaydet;
  SIGSEGV = `CIKIS_KODU=139`.

İlgili: [[geany-eklenti-yasam-dongusu]]
