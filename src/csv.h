/*
 * csv.h — RFC 4180 uyumlu CSV ayrıştırıcı/serileştirici.
 *
 * Geany'den bağımsızdır; yalnız GLib kullanır, böylece testleri UI olmadan
 * çalışır. Her kayıt, belgedeki bayt aralığını (bas..son) taşır: düzenleme
 * yapıldığında tüm dosyayı değil yalnız o aralığı değiştirebilmek için.
 *
 * Copyright 2026 Muslu Yüksektepe
 * Lisans: GPL-2.0-or-later
 */
#ifndef GCSV_CSV_H
#define GCSV_CSV_H

#include <glib.h>

G_BEGIN_DECLS

/** Tek bir CSV kaydı (mantıksal satır — tırnak içinde satır sonu içerebilir). */
typedef struct
{
	GPtrArray *alanlar;   /* gchar* — kaçışları çözülmüş UTF-8 değerler */
	gint       bas;       /* belgedeki başlangıç baytı */
	gint       son;       /* kayıt sonu; satır sonu karakteri HARİÇ */
} GcsvSatir;

/** Ayrıştırılmış tablo. */
typedef struct
{
	GPtrArray *satirlar;      /* GcsvSatir* */
	gchar      ayrac;
	guint      sutun_sayisi;  /* en geniş kaydın alan sayısı */
} GcsvTablo;

/** Metne bakarak en olası ayracı seçer (`,` `;` tab `|`). */
gchar gcsv_ayrac_bul(const gchar *metin, gssize uzunluk);

/** Metni ayrıştırır. Dönen tablo gcsv_tablo_serbest() ile bırakılır. */
GcsvTablo *gcsv_ayristir(const gchar *metin, gssize uzunluk, gchar ayrac);

void gcsv_tablo_serbest(GcsvTablo *tablo);

/** Alan değeri; sütun yoksa "" döner (asla NULL değil). */
const gchar *gcsv_alan(const GcsvSatir *satir, guint sutun);

/** Satırı eksik sütunları boş alanla tamamlayarak istenen genişliğe getirir. */
void gcsv_satir_genislet(GcsvSatir *satir, guint sutun_sayisi);

/** Alanı gerekiyorsa tırnaklar/kaçışlar. Dönen dizge g_free() ile bırakılır. */
gchar *gcsv_alan_kacisla(const gchar *deger, gchar ayrac);

/** Kaydın belgeye yazılacak metnini üretir. g_free() ile bırakılır. */
gchar *gcsv_satir_metni(const GcsvSatir *satir, gchar ayrac);

G_END_DECLS

#endif /* GCSV_CSV_H */
