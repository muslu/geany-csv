# Yapılacaklar — geany-csv

> İş takibi. Devam eden işler **oturum etiketiyle** işaretlenir
> (`<hostname>#<oturum-id> · tarih saat`) ki aynı depoda çalışan başka bir
> oturum aynı işi ikinci kez üstlenmesin.
> Mimari ve kullanım bilgisi [README.md](README.md) içindedir; burada tekrar yok.

---

## ✅ Yapıldı

### 2026-09-18 — ilk sürüm (v1.0.0, commit `0692670`)

**Ortam**
- Geany 1.38 (API 240, ABI 72) + eklenti başlıkları (`geany-common`) doğrulandı.
- `libgtk-3-dev` **kurulamadı**: sury.org'dan gelen `libbrotli1 1.1.0`, Ubuntu'nun
  `libbrotli-dev 1.0.9`'uyla çakışıyor; apt çözüm olarak wine + openjdk-8/11 +
  tüm i386 yığınını silmeyi öneriyor — **kabul edilmedi**.
- Çözüm: `tools/gtk3-sdk-kur.sh` — 87 geliştirme paketini `~/.local/gtk3-sdk`
  altına açar, `.pc` dosyalarının prefix'ini yerel dizine çevirir, dpkg'ye hiç
  dokunmaz. `make sdk` ile çağrılır, Makefile SDK'yı kendiliğinden bulur.
- `xdotool` kuruldu (başsız arayüz testi için, tek paket, çakışmasız).

**Ayrıştırıcı — `src/csv.c`**
- RFC 4180: tırnak içinde ayraç, `""` kaçışı, tırnak içinde satır sonu.
- Her kayıt belgedeki **bayt aralığını** (`bas`/`son`) taşır — düzenlemenin
  dosyanın tamamını yeniden yazmaması bu sayede mümkün.
- Ayraç bulma (`,` `;` sekme `|`): satır başına düşen alan sayısının modu +
  tutarlılık oranı puanlanır; tek sütunlu "tutarlı" sonuç puan almaz.
- Bozuk girdilere hoşgörü: kapanmamış tırnak, düzensiz sütun sayısı, satır
  sonu olmadan biten dosya, boş dosya.
- Geany'den bağımsız (yalnız GLib) → arayüzsüz test edilebilir.

**Arayüz — `src/plugin.c`**
- İleti penceresinde "CSV Tablo" sekmesi (`message_window_notebook`).
- Hücre düzenleme → yalnız ilgili kaydın bayt aralığı `sci_replace_target` ile
  değiştirilir; sonraki kayıtların konumları farkla kaydırılır. Geri alma
  geçmişi, imleç ve dokunulmayan satırların tırnaklama biçimi korunur.
- Satır ekle (tablo genişliğinde boş alanlarla) / satır sil.
- Süzme (satır içi arama, 200 ms gecikme), sayı-duyarlı sıralama.
- Tabloda satır seçince editörde o satıra gitme.
- Canlı eşitleme: `editor-notify` + `SCN_MODIFIED`, 400 ms gecikmeli.
- Panel görünmezken çözümleme yapılmaz (`kirli` bayrağı + `map` sinyali).
- Ayarlar `~/.config/geany/plugins/geany-csv/geany-csv.conf`; Eklenti Yöneticisi
  tercih penceresi; Araçlar menüsü girdisi; iki kısayol eylemi (varsayılan atanmamış).

**Yol boyunca çıkan ve düzeltilen hatalar**
- Sekme etiketi `gtk_widget_show()` edilmediği için sekme görünmez/başlıksız
  kalıyordu (`show_all(panel)` etiketi kapsamaz).
- Eklentiler belgelerden **önce** yüklendiğinden açılışta komut satırından
  verilen dosya yakalanamıyordu → `geany-startup-complete` sinyali eklendi.
- `ellipsize` ayarlı hücre doğal genişliğini en küçük değerde bildiriyor,
  sütunlar içeriğe göre büyümüyordu ("Mehmet" → "Meh…") → `ellipsize` kaldırıldı,
  sütun `AUTOSIZE` + azami genişlik.
- `sci_get_eol_mode()` Geany'de eklentilere kapalı (`GEANY_PRIVATE`) →
  `scintilla_send_message(SCI_GETEOLMODE)`.
- Makefile'da `export PKG_CONFIG_PATH` yalnız reçetelere uygulandığı için
  Makefile çözümlenirken çalışan `$(shell pkg-config …)` yolu görmüyordu.

**Doğrulama**
- `make test`: 6/6 (basit, tırnak, bayt aralıkları, dizilim gidiş-dönüş,
  ayraç bulma, bozuk girdi).
- `make CFLAGS="-O2 -g -Werror"`: sıfır uyarı.
- Gerçek Geany'de (Xvfb + xdotool) uçtan uca:
  `Ayşe` → `a;b` yazıldı, dosyaya `"a;b";Kaya;Şırnak;düz` olarak indi (ayraç
  içerdiği için kendiliğinden tırnaklandı), öteki satırlar bozulmadı;
  satır ekle → `;;` doğru konuma; satır sil → dosya tam başlangıç haline döndü.
- `~/.config/geany/plugins/geany-csv.so` olarak kuruldu.
- GitHub'a yüklendi: https://github.com/muslu/geany-csv

---

## 🟡 Yapılıyor

Şu an açık iş yok. Bir işe başlarken satırı buraya taşı ve etiketle:

```
🟡 <iş> [muslu-MS-7C08#5d2480a9 · 2026-09-18 12:52]
```

---

## ⬜ Yapılacak

### Özellik
- [ ] **Sütun ekle / sil / başlık yeniden adlandır.** Tek kaydı değil her satırı
      yeniden yazmayı gerektirir; tüm belgeyi tek `sci_replace_target` ile
      değiştirip tek geri alma adımı olarak yazmak gerekir.
- [ ] **Klavyeyle hücre gezinme:** Enter alt hücreye, Tab sağdaki hücreye geçsin
      ve düzenlemeyi açsın (şu an her hücre için iki tık gerekiyor).
- [ ] **Pano desteği:** çok hücreli seçim → panoya TSV kopyalama, panodan yapıştırma.
- [ ] **Sayısal sütunları sağa yasla** (sütun içeriğinin tamamı sayıysa).
- [ ] **Türkçe ondalık sıralaması:** `1.234,56` biçimi şu an harf sırasına düşüyor;
      `gcsv` tarafında yerel ayara duyarlı ayrıştırma gerekli.
- [ ] **Başlık satırı kapalıyken** sütun adları "Sütun 1…" yerine "A, B, C…" olsun.
- [ ] **Sütun genişliklerini belge başına hatırla.**
- [ ] **Satır numarası sütunu** (belgedeki gerçek satır no) — sıralama sonrası
      hangi satırda olduğun görünsün.

### Sağlamlaştırma
- [ ] **Büyük dosya:** şu an gösterim `maks_satir` ile sınırlanıyor. Gerçek çözüm
      `GtkTreeModel` arayüzünü kendimiz uygulayıp (sanal model) depoyu hiç
      doldurmamak. Önce ölçüm yap: 100 bin satırda kurulum süresi ne?
- [ ] **Yapısal değişiklikten sonra seçimi koru** — satır ekle/sil sonrası tam
      yeniden çözümleme yapılıyor, seçim ve kaydırma konumu sıfırlanıyor.
- [ ] **Aynı anda birden çok CSV belgesi:** tek panel var, belge değişince tablo
      yeniden kuruluyor. Belge başına süzgeç/sıralama durumu saklanabilir.
- [ ] **Kodlama:** tablo Geany arabelleğini (UTF-8) okuyor. Geany'de yanlış
      kodlamayla açılmış dosyada davranışı sına.
- [ ] **Bellek:** uzun oturumda yinelenen çözümlemede sızıntı var mı —
      `valgrind --leak-check=full geany` ile bir tur.

### Altyapı
- [ ] **GitHub Actions:** `make test` + `-Werror` derleme (ubuntu-latest,
      `libgtk-3-dev` + `geany-common` apt ile kurulur, SDK betiği gerekmez).
- [ ] **gettext:** metinler `_()` içinde ama katalog yok. `po/tr.po` + `LINGUAS`
      eklenip `PLUGIN_SET_TRANSLATABLE_INFO` kullanılabilir. İngilizce arayüz
      isteniyorsa önce kaynak dizgeleri İngilizceye çevirmek gerekir.
- [ ] **Sürüm etiketi:** `v1.0.0` tag'i + GitHub release.
- [ ] **geany-plugins** projesine gönderme (waf/autotools derleme dosyası ve
      İngilizce arayüz şart).
