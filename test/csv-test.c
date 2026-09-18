/*
 * csv-test.c — ayrıştırıcının birim testleri (GTK/Geany gerektirmez).
 * Çalıştırma: make test
 *
 * Copyright 2026 Muslu Yüksektepe
 * Lisans: GPL-2.0-or-later
 */
#include "../src/csv.h"

#include <string.h>

static void test_basit(void)
{
	const gchar *metin = "ad,soyad,yas\nAli,Veli,30\nAyşe,Kaya,41\n";
	GcsvTablo *t = gcsv_ayristir(metin, -1, ',');

	g_assert_cmpuint(t->satirlar->len, ==, 3);
	g_assert_cmpuint(t->sutun_sayisi, ==, 3);
	g_assert_cmpstr(gcsv_alan(g_ptr_array_index(t->satirlar, 2), 0), ==, "Ayşe");
	g_assert_cmpstr(gcsv_alan(g_ptr_array_index(t->satirlar, 1), 2), ==, "30");
	gcsv_tablo_serbest(t);
}

static void test_tirnak(void)
{
	/* Tırnak içinde ayraç, çift tırnak ve satır sonu. */
	const gchar *metin = "a,\"b,c\",\"d\"\"e\"\n\"çok\nsatırlı\",x,y\n";
	GcsvTablo *t = gcsv_ayristir(metin, -1, ',');
	GcsvSatir *s0 = g_ptr_array_index(t->satirlar, 0);
	GcsvSatir *s1 = g_ptr_array_index(t->satirlar, 1);

	g_assert_cmpuint(t->satirlar->len, ==, 2);
	g_assert_cmpstr(gcsv_alan(s0, 1), ==, "b,c");
	g_assert_cmpstr(gcsv_alan(s0, 2), ==, "d\"e");
	g_assert_cmpstr(gcsv_alan(s1, 0), ==, "çok\nsatırlı");
	g_assert_cmpstr(gcsv_alan(s1, 2), ==, "y");
	gcsv_tablo_serbest(t);
}

static void test_bayt_araliklari(void)
{
	/* Kayıt aralıkları belgeye birebir oturmalı: bunlar bozuksa
	 * düzenleme yanlış yeri değiştirir. */
	const gchar *metin = "ab,cd\r\nef,gh\n";
	GcsvTablo *t = gcsv_ayristir(metin, -1, ',');
	GcsvSatir *s0 = g_ptr_array_index(t->satirlar, 0);
	GcsvSatir *s1 = g_ptr_array_index(t->satirlar, 1);

	g_assert_cmpint(s0->bas, ==, 0);
	g_assert_cmpint(s0->son, ==, 5);          /* "ab,cd" — \r\n hariç */
	g_assert_cmpint(s1->bas, ==, 7);
	g_assert_cmpint(s1->son, ==, 12);
	g_assert_cmpint(strncmp(metin + s1->bas, "ef,gh", 5), ==, 0);
	gcsv_tablo_serbest(t);
}

static void test_dizilim(void)
{
	const gchar *metin = "x\n";
	GcsvTablo *t = gcsv_ayristir(metin, -1, ',');
	GcsvSatir *s = g_ptr_array_index(t->satirlar, 0);
	gchar *cikti;

	g_ptr_array_add(s->alanlar, g_strdup("a,b"));
	g_ptr_array_add(s->alanlar, g_strdup("tırnak \" var"));
	g_ptr_array_add(s->alanlar, g_strdup("düz"));

	cikti = gcsv_satir_metni(s, ',');
	g_assert_cmpstr(cikti, ==, "x,\"a,b\",\"tırnak \"\" var\",düz");
	g_free(cikti);

	/* Gidiş–dönüş: yazılan metin aynı değerlere geri ayrışmalı. */
	cikti = gcsv_satir_metni(s, ',');
	{
		GcsvTablo *t2 = gcsv_ayristir(cikti, -1, ',');
		GcsvSatir *s2 = g_ptr_array_index(t2->satirlar, 0);

		g_assert_cmpstr(gcsv_alan(s2, 1), ==, "a,b");
		g_assert_cmpstr(gcsv_alan(s2, 2), ==, "tırnak \" var");
		gcsv_tablo_serbest(t2);
	}
	g_free(cikti);
	gcsv_tablo_serbest(t);
}

static void test_ayrac_bul(void)
{
	g_assert_cmpint(gcsv_ayrac_bul("ad;soyad;yas\nAli;Veli;30\n", -1), ==, ';');
	g_assert_cmpint(gcsv_ayrac_bul("ad\tsoyad\tyas\nAli\tVeli\t30\n", -1), ==, '\t');
	g_assert_cmpint(gcsv_ayrac_bul("ad,soyad,yas\nAli,Veli,30\n", -1), ==, ',');
	g_assert_cmpint(gcsv_ayrac_bul("a|b|c\n1|2|3\n", -1), ==, '|');
	/* Noktalı virgül ayraç, ondalık ayırıcı virgül — Türkçe Excel çıktısı. */
	g_assert_cmpint(gcsv_ayrac_bul("ürün;fiyat\nmasa;1,50\nsandalye;2,75\n", -1), ==, ';');
}

static void test_bozuk_girdi(void)
{
	GcsvTablo *t;

	/* Kapanmamış tırnak dosyayı yutmamalı, çökmemeli. */
	t = gcsv_ayristir("a,\"bitmeyen\nb,c\n", -1, ',');
	g_assert_cmpuint(t->satirlar->len, >=, 1);
	gcsv_tablo_serbest(t);

	/* Boş girdi. */
	t = gcsv_ayristir("", -1, ',');
	g_assert_cmpuint(t->satirlar->len, ==, 0);
	gcsv_tablo_serbest(t);

	/* Düzensiz sütun sayısı — en geniş satır belirler. */
	t = gcsv_ayristir("a,b\nc\nd,e,f\n", -1, ',');
	g_assert_cmpuint(t->sutun_sayisi, ==, 3);
	g_assert_cmpstr(gcsv_alan(g_ptr_array_index(t->satirlar, 1), 2), ==, "");
	gcsv_tablo_serbest(t);

	/* Son satırda satır sonu yok. */
	t = gcsv_ayristir("a,b\nc,d", -1, ',');
	g_assert_cmpuint(t->satirlar->len, ==, 2);
	g_assert_cmpstr(gcsv_alan(g_ptr_array_index(t->satirlar, 1), 1), ==, "d");
	gcsv_tablo_serbest(t);
}

int main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/csv/basit", test_basit);
	g_test_add_func("/csv/tirnak", test_tirnak);
	g_test_add_func("/csv/bayt-araliklari", test_bayt_araliklari);
	g_test_add_func("/csv/dizilim", test_dizilim);
	g_test_add_func("/csv/ayrac-bul", test_ayrac_bul);
	g_test_add_func("/csv/bozuk-girdi", test_bozuk_girdi);
	return g_test_run();
}
