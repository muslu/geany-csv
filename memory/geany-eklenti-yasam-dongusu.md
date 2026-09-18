---
name: geany-eklenti-yasam-dongusu
description: Geany eklentisinde ana pencereye eklenen parçalar cleanup'ta yok edilmezse kaldırma sonrası SIGSEGV
metadata:
  type: reference
---

> Not: Projenin mimarisi README'de — burada yalnız API davranışı var.

Geany eklentiyi kaldırırken `g_module_close()` çağırır, ama eklentinin ana
pencereye (Araçlar menüsü, araç çubuğu, defter sayfası) eklediği parçaları
**kendiliğinden temizlemez**. `funcs->cleanup` içinde yok edilmeyen her parça
menüde kalır ve sinyal geri çağrısı kapanmış modüle bakar.

Ölçüldü (2026-09-18, Geany 1.38): menü öğesi yok edilmeden eklenti Eklenti
Yöneticisi'nden kapatıldığında Araçlar menüsünde "CSV Tablo" duruyor; tıklanınca
süreç **SIGSEGV** ile ölüyor (`CIKIS_KODU=139`). Düzeltmeden sonra öğe menüden
tamamen kalkıyor. Ayrıca eklenti yeniden açılırsa her açılışta menüye bir girdi
daha ekleniyordu.

- `ui_add_document_sensitive()` kaydı parça yok edilince kendiliğinden düşer
  (Geany belgesi: "It will be removed when the widget is destroyed") — ayrıca
  bir geri alma çağrısı yok, zaten API'de yok.
- `plugin_set_key_group()` ile kurulan kısayol grubunu Geany kendisi bırakır.
- Defter sayfası `gtk_notebook_remove_page()` ile düşürülünce parça da yok olur.

Sınama yöntemi: [[bassiz-geany-testi]]
