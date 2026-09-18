/*
 * csv.c — RFC 4180 uyumlu CSV ayrıştırıcı/serileştirici.
 *
 * Copyright 2026 Muslu Yüksektepe
 * Lisans: GPL-2.0-or-later
 */
#include "csv.h"

#include <string.h>

static const gchar AYRAC_ADAYLARI[] = { ',', ';', '\t', '|' };

static void satir_serbest(gpointer veri)
{
	GcsvSatir *satir = veri;

	g_ptr_array_free(satir->alanlar, TRUE);
	g_free(satir);
}

const gchar *gcsv_alan(const GcsvSatir *satir, guint sutun)
{
	if (satir == NULL || sutun >= satir->alanlar->len)
		return "";
	return g_ptr_array_index(satir->alanlar, sutun);
}

void gcsv_satir_genislet(GcsvSatir *satir, guint sutun_sayisi)
{
	while (satir->alanlar->len < sutun_sayisi)
		g_ptr_array_add(satir->alanlar, g_strdup(""));
}

gchar *gcsv_alan_kacisla(const gchar *deger, gchar ayrac)
{
	gboolean tirnak_gerek;
	const gchar *p;
	GString *cikti;

	if (deger == NULL)
		return g_strdup("");

	tirnak_gerek = (strchr(deger, ayrac) != NULL) || (strchr(deger, '"') != NULL) ||
		(strchr(deger, '\n') != NULL) || (strchr(deger, '\r') != NULL);

	if (! tirnak_gerek)
		return g_strdup(deger);

	cikti = g_string_sized_new(strlen(deger) + 8);
	g_string_append_c(cikti, '"');
	for (p = deger; *p != '\0'; p++)
	{
		if (*p == '"')
			g_string_append(cikti, "\"\"");
		else
			g_string_append_c(cikti, *p);
	}
	g_string_append_c(cikti, '"');

	return g_string_free(cikti, FALSE);
}

gchar *gcsv_satir_metni(const GcsvSatir *satir, gchar ayrac)
{
	GString *cikti = g_string_new(NULL);
	guint i;

	for (i = 0; i < satir->alanlar->len; i++)
	{
		gchar *alan = gcsv_alan_kacisla(g_ptr_array_index(satir->alanlar, i), ayrac);

		if (i > 0)
			g_string_append_c(cikti, ayrac);
		g_string_append(cikti, alan);
		g_free(alan);
	}

	return g_string_free(cikti, FALSE);
}

GcsvTablo *gcsv_ayristir(const gchar *metin, gssize uzunluk, gchar ayrac)
{
	GcsvTablo *tablo;
	GString *alan;
	gint i = 0;
	gint len;

	g_return_val_if_fail(metin != NULL, NULL);

	len = (uzunluk < 0) ? (gint) strlen(metin) : (gint) uzunluk;

	tablo = g_new0(GcsvTablo, 1);
	tablo->satirlar = g_ptr_array_new_with_free_func(satir_serbest);
	tablo->ayrac = ayrac;

	alan = g_string_new(NULL);

	while (i < len)
	{
		GcsvSatir *satir = g_new0(GcsvSatir, 1);
		gboolean satir_bitti = FALSE;

		satir->alanlar = g_ptr_array_new_with_free_func(g_free);
		satir->bas = i;

		while (! satir_bitti)
		{
			g_string_truncate(alan, 0);

			if (i < len && metin[i] == '"')
			{
				i++;  /* açılış tırnağı */
				while (i < len)
				{
					if (metin[i] == '"')
					{
						if (i + 1 < len && metin[i + 1] == '"')
						{
							g_string_append_c(alan, '"');
							i += 2;
						}
						else
						{
							i++;  /* kapanış tırnağı */
							break;
						}
					}
					else
					{
						g_string_append_c(alan, metin[i]);
						i++;
					}
				}
				/* Bozuk dosyalara hoşgörü: kapanıştan sonra ayraca kadar olanı da al. */
				while (i < len && metin[i] != ayrac && metin[i] != '\n' && metin[i] != '\r')
				{
					g_string_append_c(alan, metin[i]);
					i++;
				}
			}
			else
			{
				while (i < len && metin[i] != ayrac && metin[i] != '\n' && metin[i] != '\r')
				{
					g_string_append_c(alan, metin[i]);
					i++;
				}
			}

			g_ptr_array_add(satir->alanlar, g_strdup(alan->str));

			if (i < len && metin[i] == ayrac)
				i++;          /* aynı kayıtta sonraki alan */
			else
				satir_bitti = TRUE;
		}

		satir->son = i;

		if (i < len && metin[i] == '\r')
			i++;
		if (i < len && metin[i] == '\n')
			i++;

		if (satir->alanlar->len > tablo->sutun_sayisi)
			tablo->sutun_sayisi = satir->alanlar->len;

		g_ptr_array_add(tablo->satirlar, satir);
	}

	g_string_free(alan, TRUE);

	return tablo;
}

void gcsv_tablo_serbest(GcsvTablo *tablo)
{
	if (tablo == NULL)
		return;

	g_ptr_array_free(tablo->satirlar, TRUE);
	g_free(tablo);
}

/*
 * Ayraç seçimi: her aday için ilk satırlar ayrıştırılır; en sık görülen alan
 * sayısı (mod) ve o sayıyı tutturan satır oranı puanlanır. Tutarlılık tek
 * başına yetmez — her dosya ",": 1 sütunla "tutarlıdır" — bu yüzden puan
 * alan sayısıyla çarpılır ve tek sütunlu sonuç hiç puan almaz.
 */
gchar gcsv_ayrac_bul(const gchar *metin, gssize uzunluk)
{
	const gint ORNEK_SATIR = 50;
	const gssize ORNEK_BAYT = 64 * 1024;
	gchar en_iyi_ayrac = ',';
	gdouble en_iyi_puan = -1.0;
	gsize a;
	gint len;

	g_return_val_if_fail(metin != NULL, ',');

	len = (uzunluk < 0) ? (gint) strlen(metin) : (gint) uzunluk;
	if (len > ORNEK_BAYT)
		len = ORNEK_BAYT;

	for (a = 0; a < G_N_ELEMENTS(AYRAC_ADAYLARI); a++)
	{
		gchar aday = AYRAC_ADAYLARI[a];
		GcsvTablo *tablo = gcsv_ayristir(metin, len, aday);
		GHashTable *sayac = g_hash_table_new(g_direct_hash, g_direct_equal);
		guint bakilan = MIN(tablo->satirlar->len, (guint) ORNEK_SATIR);
		guint mod_sayi = 0, mod_adet = 0, i;
		gdouble puan;

		for (i = 0; i < bakilan; i++)
		{
			GcsvSatir *satir = g_ptr_array_index(tablo->satirlar, i);
			guint n = satir->alanlar->len;
			guint adet = GPOINTER_TO_UINT(g_hash_table_lookup(sayac, GUINT_TO_POINTER(n))) + 1;

			g_hash_table_insert(sayac, GUINT_TO_POINTER(n), GUINT_TO_POINTER(adet));
			if (adet > mod_adet || (adet == mod_adet && n > mod_sayi))
			{
				mod_adet = adet;
				mod_sayi = n;
			}
		}

		if (bakilan == 0 || mod_sayi < 2)
			puan = 0.0;
		else
			puan = ((gdouble) mod_adet / bakilan) * (gdouble) mod_sayi;

		if (puan > en_iyi_puan)
		{
			en_iyi_puan = puan;
			en_iyi_ayrac = aday;
		}

		g_hash_table_destroy(sayac);
		gcsv_tablo_serbest(tablo);
	}

	return en_iyi_ayrac;
}
