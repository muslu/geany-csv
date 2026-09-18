/*
 * plugin.c — Geany "CSV Tablo" eklentisi.
 *
 * Açık CSV/TSV belgesini alt panelde düzenlenebilir bir tablo olarak gösterir.
 * Bir hücre değiştirildiğinde belgenin TAMAMI yeniden yazılmaz; yalnız o
 * kaydın bayt aralığı Scintilla hedefiyle değiştirilir — böylece geri alma
 * geçmişi, imleç ve dosyanın geri kalanı olduğu gibi kalır.
 *
 * Copyright 2026 Muslu Yüksektepe
 * Lisans: GPL-2.0-or-later
 */
#include <geanyplugin.h>

#include "csv.h"

#include <string.h>

GeanyPlugin *geany_plugin;
GeanyData   *geany_data;

/* Depo sütunları: 0 = kayıt sırası (düzenleme bu numarayı kullanır),
 * 1.. = CSV alanları. Sıralama/süzme sırayı değiştirse de kayıt numarası
 * satırla birlikte taşındığı için düzenleme doğru yere gider. */
#define SUTUN_KAYIT 0
#define SUTUN_ILK   1

#define ESITLEME_GECIKMESI 400   /* ms — yazarken her tuşta yeniden kurmamak için */
#define SUZGEC_GECIKMESI   200

typedef struct
{
	/* Arayüz */
	GtkWidget    *panel;
	GtkWidget    *gorunum;
	GtkWidget    *durum;
	GtkWidget    *ayrac_kutu;
	GtkWidget    *baslik_dugme;
	GtkWidget    *suzgec_giris;
	GtkWidget    *bos_etiket;
	GtkWidget    *yigin;          /* GtkStack: tablo ya da bilgi metni */
	GtkListStore *depo;
	gint          sayfa_no;

	/* Durum */
	GcsvTablo    *tablo;
	guint         belge_id;
	gchar         ayrac;
	guint         sutun_sayisi;
	gboolean      kendi_yazimiz;   /* kendi düzenlememizi yeniden yüklemeyi engeller */
	gboolean      dolduruyoruz;    /* depo doldururken sinyalleri yut */
	gboolean      kirli;           /* panel görünmezken gelen değişiklik */
	gboolean      zorla_acildi;    /* CSV olmayan belge kullanıcı isteğiyle açıldı */
	guint         esitleme_zamani;
	guint         suzgec_zamani;

	/* Ayarlar */
	gchar        *ayar_dosyasi;
	gboolean      ayar_baslik_var;
	gboolean      ayar_canli;
	gboolean      ayar_satira_git;
	gchar        *ayar_ayrac;      /* "oto" ya da tek karakter */
	gint          ayar_maks_satir;
} CsvEklenti;

static CsvEklenti *ek = NULL;

static void tablo_yenile(gboolean zorla);

/* ---------------------------------------------------------------- yardımcı */

static GeanyDocument *gecerli_belge(void)
{
	GeanyDocument *doc = document_get_current();

	return (doc != NULL && doc->is_valid) ? doc : NULL;
}

/** Belge uzantısı/türü CSV ailesinden mi? */
static gboolean csv_belgesi_mi(GeanyDocument *doc)
{
	const gchar *uzantilar[] = { ".csv", ".tsv", ".tab", ".psv", NULL };
	gchar *kucuk;
	gboolean sonuc = FALSE;
	gint i;

	if (doc == NULL || doc->file_name == NULL)
		return FALSE;

	kucuk = g_utf8_strdown(doc->file_name, -1);
	for (i = 0; uzantilar[i] != NULL && ! sonuc; i++)
		sonuc = g_str_has_suffix(kucuk, uzantilar[i]);
	g_free(kucuk);

	return sonuc;
}

static const gchar *eol_metni(ScintillaObject *sci)
{
	/* sci_get_eol_mode() Geany'de eklentilere kapalı (GEANY_PRIVATE);
	 * Scintilla'ya doğrudan soruyoruz. */
	switch (scintilla_send_message(sci, SCI_GETEOLMODE, 0, 0))
	{
		case SC_EOL_CRLF: return "\r\n";
		case SC_EOL_CR:   return "\r";
		default:          return "\n";
	}
}

/** Kayıt aralığını belgede değiştirir; sonraki kayıtların konumlarını kaydırır. */
static void belgeye_yaz(ScintillaObject *sci, gint bas, gint son, const gchar *metin,
		guint ilk_kaydirilacak)
{
	gint yeni_uzunluk;
	gint fark;
	guint i;

	ek->kendi_yazimiz = TRUE;

	sci_start_undo_action(sci);
	sci_set_target_start(sci, bas);
	sci_set_target_end(sci, son);
	yeni_uzunluk = sci_replace_target(sci, metin, FALSE);
	sci_end_undo_action(sci);

	fark = yeni_uzunluk - (son - bas);
	if (fark != 0 && ek->tablo != NULL)
	{
		for (i = ilk_kaydirilacak; i < ek->tablo->satirlar->len; i++)
		{
			GcsvSatir *s = g_ptr_array_index(ek->tablo->satirlar, i);

			s->bas += fark;
			s->son += fark;
		}
	}

	ek->kendi_yazimiz = FALSE;
}

/* ------------------------------------------------------------------ ayarlar */

static void ayarlari_oku(void)
{
	GKeyFile *kf = g_key_file_new();

	ek->ayar_baslik_var  = TRUE;
	ek->ayar_canli       = TRUE;
	ek->ayar_satira_git  = TRUE;
	ek->ayar_maks_satir  = 50000;
	g_free(ek->ayar_ayrac);
	ek->ayar_ayrac = g_strdup("oto");

	if (g_key_file_load_from_file(kf, ek->ayar_dosyasi, G_KEY_FILE_NONE, NULL))
	{
		if (g_key_file_has_key(kf, "genel", "baslik_var", NULL))
			ek->ayar_baslik_var = g_key_file_get_boolean(kf, "genel", "baslik_var", NULL);
		if (g_key_file_has_key(kf, "genel", "canli_esitleme", NULL))
			ek->ayar_canli = g_key_file_get_boolean(kf, "genel", "canli_esitleme", NULL);
		if (g_key_file_has_key(kf, "genel", "satira_git", NULL))
			ek->ayar_satira_git = g_key_file_get_boolean(kf, "genel", "satira_git", NULL);
		if (g_key_file_has_key(kf, "genel", "maks_satir", NULL))
			ek->ayar_maks_satir = g_key_file_get_integer(kf, "genel", "maks_satir", NULL);
		if (g_key_file_has_key(kf, "genel", "ayrac", NULL))
		{
			g_free(ek->ayar_ayrac);
			ek->ayar_ayrac = g_key_file_get_string(kf, "genel", "ayrac", NULL);
		}
	}

	if (ek->ayar_maks_satir < 100)
		ek->ayar_maks_satir = 100;

	g_key_file_free(kf);
}

static void ayarlari_yaz(void)
{
	GKeyFile *kf = g_key_file_new();
	gchar *dizin = g_path_get_dirname(ek->ayar_dosyasi);
	gchar *veri;

	g_key_file_set_boolean(kf, "genel", "baslik_var", ek->ayar_baslik_var);
	g_key_file_set_boolean(kf, "genel", "canli_esitleme", ek->ayar_canli);
	g_key_file_set_boolean(kf, "genel", "satira_git", ek->ayar_satira_git);
	g_key_file_set_integer(kf, "genel", "maks_satir", ek->ayar_maks_satir);
	g_key_file_set_string(kf, "genel", "ayrac", ek->ayar_ayrac ? ek->ayar_ayrac : "oto");

	utils_mkdir(dizin, TRUE);
	veri = g_key_file_to_data(kf, NULL, NULL);
	utils_write_file(ek->ayar_dosyasi, veri);

	g_free(veri);
	g_free(dizin);
	g_key_file_free(kf);
}

/* ------------------------------------------------------------- tablo kurulumu */

static void bilgi_goster(const gchar *ileti)
{
	gtk_label_set_text(GTK_LABEL(ek->bos_etiket), ileti);
	gtk_stack_set_visible_child_name(GTK_STACK(ek->yigin), "bos");
	gtk_label_set_text(GTK_LABEL(ek->durum), "");
}

/** Sayı gibi görünen değerleri sayısal, ötekileri harf sırasına göre karşılaştırır. */
static gint sutun_karsilastir(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b,
		gpointer veri)
{
	gint sutun = GPOINTER_TO_INT(veri);
	gchar *sa = NULL, *sb = NULL;
	gchar *sona, *sonb;
	gdouble da, db;
	gint sonuc;

	gtk_tree_model_get(model, a, sutun, &sa, -1);
	gtk_tree_model_get(model, b, sutun, &sb, -1);

	if (sa == NULL) sa = g_strdup("");
	if (sb == NULL) sb = g_strdup("");

	da = g_strtod(sa, &sona);
	db = g_strtod(sb, &sonb);

	if (*sa != '\0' && *sona == '\0' && *sb != '\0' && *sonb == '\0')
		sonuc = (da < db) ? -1 : (da > db) ? 1 : 0;
	else
		sonuc = g_utf8_collate(sa, sb);

	g_free(sa);
	g_free(sb);

	return sonuc;
}

static void hucre_duzenlendi(GtkCellRendererText *olusturucu, gchar *yol_metni,
		gchar *yeni_deger, gpointer veri)
{
	gint csv_sutun = GPOINTER_TO_INT(veri);
	GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(ek->gorunum));
	GtkTreeIter iter;
	GeanyDocument *doc = gecerli_belge();
	GcsvSatir *satir;
	gint kayit = -1;
	gchar *satir_metni;
	guint i;

	if (doc == NULL || ek->tablo == NULL)
		return;
	if (doc->readonly)
	{
		ui_set_statusbar(TRUE, _("CSV Tablo: belge salt okunur."));
		return;
	}
	if (! gtk_tree_model_get_iter_from_string(model, &iter, yol_metni))
		return;

	gtk_tree_model_get(model, &iter, SUTUN_KAYIT, &kayit, -1);
	if (kayit < 0 || (guint) kayit >= ek->tablo->satirlar->len)
		return;

	satir = g_ptr_array_index(ek->tablo->satirlar, kayit);

	if (g_strcmp0(gcsv_alan(satir, csv_sutun), yeni_deger) == 0)
		return;   /* değişiklik yok — belgeyi kirletme */

	/* Eksik sütunlar boş alanlarla tamamlanır, yoksa değer yanlış sütuna düşer. */
	gcsv_satir_genislet(satir, (guint) csv_sutun + 1);
	g_free(g_ptr_array_index(satir->alanlar, csv_sutun));
	g_ptr_array_index(satir->alanlar, csv_sutun) = g_strdup(yeni_deger);

	satir_metni = gcsv_satir_metni(satir, ek->ayrac);
	belgeye_yaz(doc->editor->sci, satir->bas, satir->son, satir_metni, (guint) kayit + 1);
	satir->son = satir->bas + (gint) strlen(satir_metni);
	g_free(satir_metni);

	/* Görünümü belgeyle aynı hale getir: genişletme boş hücreler doğurmuş olabilir. */
	ek->dolduruyoruz = TRUE;
	for (i = 0; i < ek->sutun_sayisi; i++)
		gtk_list_store_set(ek->depo, &iter, SUTUN_ILK + i, gcsv_alan(satir, i), -1);
	ek->dolduruyoruz = FALSE;
}

/** Görünümün sütunlarını ve deposunu sıfırdan kurar. */
static void sutunlari_kur(GcsvTablo *tablo)
{
	GList *eski = gtk_tree_view_get_columns(GTK_TREE_VIEW(ek->gorunum));
	GList *d;
	GType *turler;
	guint i;
	GcsvSatir *baslik_satiri = NULL;

	for (d = eski; d != NULL; d = d->next)
		gtk_tree_view_remove_column(GTK_TREE_VIEW(ek->gorunum), d->data);
	g_list_free(eski);

	ek->sutun_sayisi = MAX(tablo->sutun_sayisi, 1);

	turler = g_new(GType, ek->sutun_sayisi + 1);
	turler[SUTUN_KAYIT] = G_TYPE_INT;
	for (i = 0; i < ek->sutun_sayisi; i++)
		turler[SUTUN_ILK + i] = G_TYPE_STRING;

	if (ek->depo != NULL)
		g_object_unref(ek->depo);
	ek->depo = gtk_list_store_newv(ek->sutun_sayisi + 1, turler);
	g_free(turler);

	if (ek->ayar_baslik_var && tablo->satirlar->len > 0)
		baslik_satiri = g_ptr_array_index(tablo->satirlar, 0);

	for (i = 0; i < ek->sutun_sayisi; i++)
	{
		GtkCellRenderer *olusturucu = gtk_cell_renderer_text_new();
		GtkTreeViewColumn *sutun;
		gchar *baslik;

		if (baslik_satiri != NULL && *gcsv_alan(baslik_satiri, i) != '\0')
			baslik = g_strdup(gcsv_alan(baslik_satiri, i));
		else
			baslik = g_strdup_printf(_("Sütun %u"), i + 1);

		/* ellipsize KULLANILMIYOR: ayarlandığında hücre doğal genişliğini en küçük
		 * değerde bildirir ve sütunlar içeriğe göre büyümez ("Mehmet" → "Meh…").
		 * Bunun yerine sütun içeriğe göre boyutlanır, azami genişlikle sınırlanır. */
		g_object_set(olusturucu,
			"editable", TRUE,
			"single-paragraph-mode", TRUE,
			NULL);
		g_signal_connect(olusturucu, "edited", G_CALLBACK(hucre_duzenlendi),
			GINT_TO_POINTER(i));

		sutun = gtk_tree_view_column_new_with_attributes(baslik, olusturucu,
			"text", SUTUN_ILK + i, NULL);
		gtk_tree_view_column_set_resizable(sutun, TRUE);
		gtk_tree_view_column_set_sizing(sutun, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
		gtk_tree_view_column_set_min_width(sutun, 50);
		gtk_tree_view_column_set_max_width(sutun, 400);
		gtk_tree_view_column_set_sort_column_id(sutun, SUTUN_ILK + i);
		gtk_tree_view_append_column(GTK_TREE_VIEW(ek->gorunum), sutun);

		gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(ek->depo), SUTUN_ILK + i,
			sutun_karsilastir, GINT_TO_POINTER(SUTUN_ILK + i), NULL);

		g_free(baslik);
	}

	gtk_tree_view_set_model(GTK_TREE_VIEW(ek->gorunum), GTK_TREE_MODEL(ek->depo));
}

/** Satır süzgecine uyuyor mu? (herhangi bir alanda geçiyorsa evet) */
static gboolean satir_uyuyor(GcsvSatir *satir, const gchar *arama_kucuk)
{
	guint i;

	if (arama_kucuk == NULL || *arama_kucuk == '\0')
		return TRUE;

	for (i = 0; i < satir->alanlar->len; i++)
	{
		gchar *kucuk = g_utf8_strdown(g_ptr_array_index(satir->alanlar, i), -1);
		gboolean bulundu = (strstr(kucuk, arama_kucuk) != NULL);

		g_free(kucuk);
		if (bulundu)
			return TRUE;
	}

	return FALSE;
}

/** Depoyu ayrıştırılmış tablodan doldurur (başlık satırı, süzgeç ve üst sınır uygulanır). */
static void depoyu_doldur(void)
{
	const gchar *arama = gtk_entry_get_text(GTK_ENTRY(ek->suzgec_giris));
	gchar *arama_kucuk = g_utf8_strdown(arama, -1);
	guint ilk = (ek->ayar_baslik_var && ek->tablo->satirlar->len > 0) ? 1 : 0;
	guint gosterilen = 0;
	guint eslesen = 0;
	guint i;
	gboolean kirpildi;
	gchar *bilgi;
	gchar ayrac_metni[16];

	ek->dolduruyoruz = TRUE;
	gtk_list_store_clear(ek->depo);

	for (i = ilk; i < ek->tablo->satirlar->len; i++)
	{
		GcsvSatir *satir = g_ptr_array_index(ek->tablo->satirlar, i);
		GtkTreeIter iter;
		guint s;

		if (! satir_uyuyor(satir, arama_kucuk))
			continue;

		eslesen++;
		if (gosterilen >= (guint) ek->ayar_maks_satir)
			continue;

		gtk_list_store_append(ek->depo, &iter);
		gtk_list_store_set(ek->depo, &iter, SUTUN_KAYIT, (gint) i, -1);
		for (s = 0; s < ek->sutun_sayisi; s++)
			gtk_list_store_set(ek->depo, &iter, SUTUN_ILK + s, gcsv_alan(satir, s), -1);
		gosterilen++;
	}

	ek->dolduruyoruz = FALSE;
	kirpildi = (eslesen > gosterilen);

	if (ek->ayrac == '\t')
		g_strlcpy(ayrac_metni, _("sekme"), sizeof ayrac_metni);
	else
	{
		ayrac_metni[0] = ek->ayrac;
		ayrac_metni[1] = '\0';
	}

	if (*arama != '\0')
		bilgi = g_strdup_printf(_("%u / %u satır · %u sütun · ayraç: %s%s"),
			eslesen, ek->tablo->satirlar->len - ilk, ek->sutun_sayisi, ayrac_metni,
			kirpildi ? _(" · liste kırpıldı") : "");
	else
		bilgi = g_strdup_printf(_("%u satır · %u sütun · ayraç: %s%s"),
			ek->tablo->satirlar->len - ilk, ek->sutun_sayisi, ayrac_metni,
			kirpildi ? _(" · liste kırpıldı") : "");

	gtk_label_set_text(GTK_LABEL(ek->durum), bilgi);
	g_free(bilgi);
	g_free(arama_kucuk);
}

static void tablo_yenile(gboolean kullanici_istedi)
{
	GeanyDocument *doc = gecerli_belge();
	const gchar *secili_ayrac;
	gchar *metin;
	gint uzunluk;

	if (ek == NULL || ek->panel == NULL)
		return;

	if (doc == NULL)
	{
		gcsv_tablo_serbest(ek->tablo);
		ek->tablo = NULL;
		ek->belge_id = 0;
		bilgi_goster(_("Açık belge yok."));
		return;
	}

	if (doc->id != ek->belge_id)
	{
		ek->belge_id = doc->id;
		ek->zorla_acildi = FALSE;
	}

	if (! csv_belgesi_mi(doc) && ! kullanici_istedi && ! ek->zorla_acildi)
	{
		gcsv_tablo_serbest(ek->tablo);
		ek->tablo = NULL;
		bilgi_goster(_("Bu belge CSV değil.\n"
			"“Yenile” düğmesiyle yine de tablo olarak açabilirsiniz."));
		return;
	}
	if (kullanici_istedi)
		ek->zorla_acildi = TRUE;

	/* Panel görünmüyorken çözümleme yapma; sekmeye geçilince yakalanır. */
	if (! kullanici_istedi && ! gtk_widget_get_mapped(ek->panel))
	{
		ek->kirli = TRUE;
		return;
	}
	ek->kirli = FALSE;

	uzunluk = sci_get_length(doc->editor->sci);
	metin = sci_get_contents(doc->editor->sci, -1);
	if (metin == NULL)
		return;

	secili_ayrac = ek->ayar_ayrac;
	if (secili_ayrac == NULL || g_strcmp0(secili_ayrac, "oto") == 0)
		ek->ayrac = gcsv_ayrac_bul(metin, uzunluk);
	else
		ek->ayrac = secili_ayrac[0];

	gcsv_tablo_serbest(ek->tablo);
	ek->tablo = gcsv_ayristir(metin, uzunluk, ek->ayrac);
	g_free(metin);

	if (ek->tablo->satirlar->len == 0)
	{
		bilgi_goster(_("Belge boş."));
		return;
	}

	sutunlari_kur(ek->tablo);
	depoyu_doldur();
	gtk_stack_set_visible_child_name(GTK_STACK(ek->yigin), "tablo");
}

/* ------------------------------------------------------------------ eylemler */

/** Seçili satırın belgedeki kayıt numarasını döndürür; seçim yoksa -1. */
static gint secili_kayit(void)
{
	GtkTreeSelection *secim = gtk_tree_view_get_selection(GTK_TREE_VIEW(ek->gorunum));
	GtkTreeModel *model;
	GtkTreeIter iter;
	gint kayit = -1;

	if (gtk_tree_selection_get_selected(secim, &model, &iter))
		gtk_tree_model_get(model, &iter, SUTUN_KAYIT, &kayit, -1);

	return kayit;
}

static void satir_ekle_cb(GtkButton *dugme, gpointer veri)
{
	GeanyDocument *doc = gecerli_belge();
	ScintillaObject *sci;
	GcsvSatir *satir;
	GString *metin;
	gint kayit;
	gint konum;
	guint i;

	if (doc == NULL || ek->tablo == NULL || ek->tablo->satirlar->len == 0)
		return;
	if (doc->readonly)
	{
		ui_set_statusbar(TRUE, _("CSV Tablo: belge salt okunur."));
		return;
	}

	sci = doc->editor->sci;
	kayit = secili_kayit();
	if (kayit < 0)
		kayit = (gint) ek->tablo->satirlar->len - 1;

	satir = g_ptr_array_index(ek->tablo->satirlar, kayit);
	konum = satir->son;

	/* Yeni kayıt, tablo genişliğinde boş alanlarla açılır ki sütunlar kaymasın. */
	metin = g_string_new(eol_metni(sci));
	for (i = 1; i < ek->sutun_sayisi; i++)
		g_string_append_c(metin, ek->ayrac);

	belgeye_yaz(sci, konum, konum, metin->str, (guint) kayit + 1);
	g_string_free(metin, TRUE);

	tablo_yenile(TRUE);
}

static void satir_sil_cb(GtkButton *dugme, gpointer veri)
{
	GeanyDocument *doc = gecerli_belge();
	ScintillaObject *sci;
	GcsvSatir *satir;
	gint kayit;
	gint bas, son;

	if (doc == NULL || ek->tablo == NULL)
		return;
	if (doc->readonly)
	{
		ui_set_statusbar(TRUE, _("CSV Tablo: belge salt okunur."));
		return;
	}

	kayit = secili_kayit();
	if (kayit < 0)
	{
		ui_set_statusbar(TRUE, _("CSV Tablo: önce silinecek satırı seçin."));
		return;
	}

	sci = doc->editor->sci;
	satir = g_ptr_array_index(ek->tablo->satirlar, kayit);
	bas = satir->bas;
	son = satir->son;

	/* Satır sonunu da al, yoksa geride boş satır kalır. */
	if ((guint) kayit + 1 < ek->tablo->satirlar->len)
	{
		GcsvSatir *sonraki = g_ptr_array_index(ek->tablo->satirlar, kayit + 1);

		son = sonraki->bas;
	}
	else if (kayit > 0)
	{
		GcsvSatir *onceki = g_ptr_array_index(ek->tablo->satirlar, kayit - 1);

		bas = onceki->son;
	}

	belgeye_yaz(sci, bas, son, "", (guint) kayit + 1);
	tablo_yenile(TRUE);
}

static void yenile_cb(GtkButton *dugme, gpointer veri)
{
	tablo_yenile(TRUE);
}

static void ayrac_degisti_cb(GtkComboBox *kutu, gpointer veri)
{
	const gchar *kimlik = gtk_combo_box_get_active_id(kutu);

	if (kimlik == NULL)
		return;

	g_free(ek->ayar_ayrac);
	ek->ayar_ayrac = g_strdup(kimlik);
	ayarlari_yaz();
	tablo_yenile(ek->zorla_acildi);
}

static void baslik_degisti_cb(GtkToggleButton *dugme, gpointer veri)
{
	ek->ayar_baslik_var = gtk_toggle_button_get_active(dugme);
	ayarlari_yaz();
	tablo_yenile(ek->zorla_acildi);
}

static gboolean suzgec_zamanlayici(gpointer veri)
{
	ek->suzgec_zamani = 0;
	if (ek->tablo != NULL)
		depoyu_doldur();
	return G_SOURCE_REMOVE;
}

static void suzgec_degisti_cb(GtkEditable *giris, gpointer veri)
{
	if (ek->suzgec_zamani != 0)
		g_source_remove(ek->suzgec_zamani);
	ek->suzgec_zamani = g_timeout_add(SUZGEC_GECIKMESI, suzgec_zamanlayici, NULL);
}

/** Tabloda satır seçilince editörde o kayda git. */
static void imlec_degisti_cb(GtkTreeView *gorunum, gpointer veri)
{
	GeanyDocument *doc = gecerli_belge();
	GcsvSatir *satir;
	gint kayit;

	if (! ek->ayar_satira_git || ek->dolduruyoruz || doc == NULL || ek->tablo == NULL)
		return;

	kayit = secili_kayit();
	if (kayit < 0 || (guint) kayit >= ek->tablo->satirlar->len)
		return;

	satir = g_ptr_array_index(ek->tablo->satirlar, kayit);
	ek->kendi_yazimiz = TRUE;
	sci_goto_line(doc->editor->sci,
		sci_get_line_from_position(doc->editor->sci, satir->bas), TRUE);
	sci_set_current_position(doc->editor->sci, satir->bas, TRUE);
	ek->kendi_yazimiz = FALSE;
}

static void panel_gorunur_cb(GtkWidget *parca, gpointer veri)
{
	if (ek->kirli)
		tablo_yenile(FALSE);
}

/* -------------------------------------------------------------------- panel */

static GtkWidget *arac_cubugu_kur(void)
{
	GtkWidget *kutu = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget *etiket;
	GtkWidget *dugme;

	gtk_container_set_border_width(GTK_CONTAINER(kutu), 4);

	etiket = gtk_label_new(_("Ayraç:"));
	gtk_box_pack_start(GTK_BOX(kutu), etiket, FALSE, FALSE, 0);

	ek->ayrac_kutu = gtk_combo_box_text_new();
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ek->ayrac_kutu), "oto", _("Otomatik"));
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ek->ayrac_kutu), ",", _("Virgül  ,"));
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ek->ayrac_kutu), ";", _("Noktalı virgül  ;"));
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ek->ayrac_kutu), "\t", _("Sekme"));
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ek->ayrac_kutu), "|", _("Dikey çizgi  |"));
	gtk_combo_box_set_active_id(GTK_COMBO_BOX(ek->ayrac_kutu),
		ek->ayar_ayrac ? ek->ayar_ayrac : "oto");
	g_signal_connect(ek->ayrac_kutu, "changed", G_CALLBACK(ayrac_degisti_cb), NULL);
	gtk_box_pack_start(GTK_BOX(kutu), ek->ayrac_kutu, FALSE, FALSE, 0);

	ek->baslik_dugme = gtk_check_button_new_with_mnemonic(_("İlk satır _başlık"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ek->baslik_dugme), ek->ayar_baslik_var);
	g_signal_connect(ek->baslik_dugme, "toggled", G_CALLBACK(baslik_degisti_cb), NULL);
	gtk_box_pack_start(GTK_BOX(kutu), ek->baslik_dugme, FALSE, FALSE, 0);

	ek->suzgec_giris = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(ek->suzgec_giris), _("Satırlarda ara…"));
	gtk_entry_set_icon_from_icon_name(GTK_ENTRY(ek->suzgec_giris),
		GTK_ENTRY_ICON_PRIMARY, "edit-find-symbolic");
	gtk_entry_set_width_chars(GTK_ENTRY(ek->suzgec_giris), 18);
	g_signal_connect(ek->suzgec_giris, "changed", G_CALLBACK(suzgec_degisti_cb), NULL);
	gtk_box_pack_start(GTK_BOX(kutu), ek->suzgec_giris, FALSE, FALSE, 0);

	dugme = gtk_button_new_with_mnemonic(_("_Yenile"));
	gtk_widget_set_tooltip_text(dugme, _("Belgeyi yeniden okuyup tabloyu kurar"));
	g_signal_connect(dugme, "clicked", G_CALLBACK(yenile_cb), NULL);
	gtk_box_pack_start(GTK_BOX(kutu), dugme, FALSE, FALSE, 0);

	dugme = gtk_button_new_with_mnemonic(_("Satır _ekle"));
	gtk_widget_set_tooltip_text(dugme, _("Seçili satırın altına boş satır ekler"));
	g_signal_connect(dugme, "clicked", G_CALLBACK(satir_ekle_cb), NULL);
	gtk_box_pack_start(GTK_BOX(kutu), dugme, FALSE, FALSE, 0);

	dugme = gtk_button_new_with_mnemonic(_("Satır _sil"));
	gtk_widget_set_tooltip_text(dugme, _("Seçili satırı belgeden siler (Ctrl+Z ile geri alınır)"));
	g_signal_connect(dugme, "clicked", G_CALLBACK(satir_sil_cb), NULL);
	gtk_box_pack_start(GTK_BOX(kutu), dugme, FALSE, FALSE, 0);

	ek->durum = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(ek->durum), 1.0);
	gtk_label_set_ellipsize(GTK_LABEL(ek->durum), PANGO_ELLIPSIZE_END);
	gtk_box_pack_end(GTK_BOX(kutu), ek->durum, TRUE, TRUE, 0);

	return kutu;
}

static void paneli_kur(void)
{
	GtkWidget *kaydirma;
	GtkWidget *sekme_etiketi;
	GtkNotebook *defter = GTK_NOTEBOOK(geany->main_widgets->message_window_notebook);

	ek->panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	ek->gorunum = gtk_tree_view_new();
	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(ek->gorunum), FALSE);
	gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(ek->gorunum), GTK_TREE_VIEW_GRID_LINES_BOTH);
	g_signal_connect(ek->gorunum, "cursor-changed", G_CALLBACK(imlec_degisti_cb), NULL);

	kaydirma = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(kaydirma),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(kaydirma), ek->gorunum);

	ek->bos_etiket = gtk_label_new(_("Bir CSV belgesi açın."));
	gtk_label_set_justify(GTK_LABEL(ek->bos_etiket), GTK_JUSTIFY_CENTER);
	gtk_label_set_line_wrap(GTK_LABEL(ek->bos_etiket), TRUE);

	ek->yigin = gtk_stack_new();
	gtk_stack_add_named(GTK_STACK(ek->yigin), kaydirma, "tablo");
	gtk_stack_add_named(GTK_STACK(ek->yigin), ek->bos_etiket, "bos");
	gtk_stack_set_visible_child_name(GTK_STACK(ek->yigin), "bos");

	gtk_box_pack_start(GTK_BOX(ek->panel), arac_cubugu_kur(), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(ek->panel), ek->yigin, TRUE, TRUE, 0);

	gtk_widget_show_all(ek->panel);
	g_signal_connect(ek->panel, "map", G_CALLBACK(panel_gorunur_cb), NULL);

	/* Sekme etiketi ayrı bir parçadır: show_all(panel) onu kapsamaz, gösterilmezse
	 * sekme başlıksız/görünmez kalır. */
	sekme_etiketi = gtk_label_new(_("CSV Tablo"));
	gtk_widget_show(sekme_etiketi);
	ek->sayfa_no = gtk_notebook_append_page(defter, ek->panel, sekme_etiketi);
}

static void panele_gec(void)
{
	GtkNotebook *defter = GTK_NOTEBOOK(geany->main_widgets->message_window_notebook);
	gint no = gtk_notebook_page_num(defter, ek->panel);

	if (no >= 0)
	{
		/* Ileti penceresi kapalıysa aç, sonra sekmeye geç. */
		if (! gtk_widget_get_visible(GTK_WIDGET(defter)))
			gtk_widget_show(GTK_WIDGET(defter));
		gtk_notebook_set_current_page(defter, no);
	}
	gtk_widget_grab_focus(ek->gorunum);
}

/* ----------------------------------------------------------------- sinyaller */

static gboolean esitleme_zamanlayici(gpointer veri)
{
	ek->esitleme_zamani = 0;
	tablo_yenile(FALSE);
	return G_SOURCE_REMOVE;
}

static void esitlemeyi_zamanla(void)
{
	if (ek->esitleme_zamani != 0)
		g_source_remove(ek->esitleme_zamani);
	ek->esitleme_zamani = g_timeout_add(ESITLEME_GECIKMESI, esitleme_zamanlayici, NULL);
}

static void belge_etkin_cb(GObject *nesne, GeanyDocument *doc, gpointer veri)
{
	tablo_yenile(FALSE);
}

static void belge_kapandi_cb(GObject *nesne, GeanyDocument *doc, gpointer veri)
{
	if (doc != NULL && doc->id == ek->belge_id)
	{
		gcsv_tablo_serbest(ek->tablo);
		ek->tablo = NULL;
		ek->belge_id = 0;
		ek->zorla_acildi = FALSE;
		bilgi_goster(_("Belge kapatıldı."));
	}
}

static gboolean editor_bildirim_cb(GObject *nesne, GeanyEditor *editor,
		SCNotification *nt, gpointer veri)
{
	if (! ek->ayar_canli || ek->kendi_yazimiz)
		return FALSE;
	if (nt->nmhdr.code != SCN_MODIFIED)
		return FALSE;
	if (! (nt->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)))
		return FALSE;
	if (editor == NULL || editor->document == NULL || editor->document->id != ek->belge_id)
		return FALSE;

	esitlemeyi_zamanla();

	return FALSE;
}

/* Eklentiler belgelerden ÖNCE yüklenir; açılıştaki ilk çözümleme bu sinyalle
 * yapılır, yoksa Geany komut satırından açılan dosyayı panel görmez. */
static void baslangic_tamam_cb(GObject *nesne, gpointer veri)
{
	tablo_yenile(FALSE);
}

static PluginCallback eklenti_sinyalleri[] =
{
	{ "geany-startup-complete", (GCallback) &baslangic_tamam_cb, FALSE, NULL },
	{ "document-activate",  (GCallback) &belge_etkin_cb,     FALSE, NULL },
	{ "document-open",      (GCallback) &belge_etkin_cb,     FALSE, NULL },
	{ "document-reload",    (GCallback) &belge_etkin_cb,     FALSE, NULL },
	{ "document-save",      (GCallback) &belge_etkin_cb,     FALSE, NULL },
	{ "document-filetype-set", (GCallback) &belge_etkin_cb,  FALSE, NULL },
	{ "document-close",     (GCallback) &belge_kapandi_cb,   FALSE, NULL },
	{ "editor-notify",      (GCallback) &editor_bildirim_cb, FALSE, NULL },
	{ NULL, NULL, FALSE, NULL }
};

/* --------------------------------------------------------------- kısayollar */

enum
{
	KB_GOSTER,
	KB_YENILE,
	KB_SAYISI
};

static void kb_goster_cb(guint kimlik)
{
	tablo_yenile(TRUE);
	panele_gec();
}

static void kb_yenile_cb(guint kimlik)
{
	tablo_yenile(TRUE);
}

static void menu_cb(GtkMenuItem *oge, gpointer veri)
{
	tablo_yenile(TRUE);
	panele_gec();
}

/* ------------------------------------------------------- eklenti yaşam döngüsü */

static gboolean eklenti_baslat(GeanyPlugin *plugin, gpointer pdata)
{
	GeanyKeyGroup *grup;
	GtkWidget *menu_oge;

	geany_plugin = plugin;
	geany_data = plugin->geany_data;

	ek = g_new0(CsvEklenti, 1);
	ek->ayar_dosyasi = g_build_filename(geany->app->configdir,
		"plugins", "geany-csv", "geany-csv.conf", NULL);
	ayarlari_oku();

	paneli_kur();

	menu_oge = gtk_menu_item_new_with_mnemonic(_("_CSV Tablo"));
	gtk_widget_show(menu_oge);
	gtk_container_add(GTK_CONTAINER(geany->main_widgets->tools_menu), menu_oge);
	g_signal_connect(menu_oge, "activate", G_CALLBACK(menu_cb), NULL);
	ui_add_document_sensitive(menu_oge);

	grup = plugin_set_key_group(plugin, "geany_csv", KB_SAYISI, NULL);
	keybindings_set_item(grup, KB_GOSTER, kb_goster_cb, 0, 0,
		"csv_goster", _("CSV tablosunu göster"), menu_oge);
	keybindings_set_item(grup, KB_YENILE, kb_yenile_cb, 0, 0,
		"csv_yenile", _("CSV tablosunu yenile"), NULL);

	tablo_yenile(FALSE);

	return TRUE;
}

static void eklenti_bitir(GeanyPlugin *plugin, gpointer pdata)
{
	GtkNotebook *defter = GTK_NOTEBOOK(geany->main_widgets->message_window_notebook);
	gint no;

	if (ek == NULL)
		return;

	if (ek->esitleme_zamani != 0)
		g_source_remove(ek->esitleme_zamani);
	if (ek->suzgec_zamani != 0)
		g_source_remove(ek->suzgec_zamani);

	no = gtk_notebook_page_num(defter, ek->panel);
	if (no >= 0)
		gtk_notebook_remove_page(defter, no);

	if (ek->depo != NULL)
		g_object_unref(ek->depo);
	gcsv_tablo_serbest(ek->tablo);
	g_free(ek->ayar_ayrac);
	g_free(ek->ayar_dosyasi);
	g_free(ek);
	ek = NULL;
}

static void ayar_yaniti_cb(GtkDialog *dialog, gint yanit, gpointer veri)
{
	GtkWidget *kap = veri;

	if (yanit != GTK_RESPONSE_OK && yanit != GTK_RESPONSE_APPLY)
		return;

	ek->ayar_canli = gtk_toggle_button_get_active(
		g_object_get_data(G_OBJECT(kap), "canli"));
	ek->ayar_satira_git = gtk_toggle_button_get_active(
		g_object_get_data(G_OBJECT(kap), "satira_git"));
	ek->ayar_maks_satir = gtk_spin_button_get_value_as_int(
		g_object_get_data(G_OBJECT(kap), "maks_satir"));

	ayarlari_yaz();
	tablo_yenile(ek->zorla_acildi);
}

static GtkWidget *eklenti_ayarla(GeanyPlugin *plugin, GtkDialog *dialog, gpointer pdata)
{
	GtkWidget *kap = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	GtkWidget *canli, *satira_git, *satir_kutu, *etiket, *maks;

	canli = gtk_check_button_new_with_label(
		_("Yazarken tabloyu kendiliğinden güncelle"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(canli), ek->ayar_canli);
	gtk_box_pack_start(GTK_BOX(kap), canli, FALSE, FALSE, 0);

	satira_git = gtk_check_button_new_with_label(
		_("Tabloda satır seçilince editörde o satıra git"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(satira_git), ek->ayar_satira_git);
	gtk_box_pack_start(GTK_BOX(kap), satira_git, FALSE, FALSE, 0);

	satir_kutu = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	etiket = gtk_label_new(_("En çok gösterilecek satır:"));
	maks = gtk_spin_button_new_with_range(100, 1000000, 1000);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(maks), ek->ayar_maks_satir);
	gtk_widget_set_tooltip_text(maks,
		_("Büyük dosyalarda arayüzü yavaşlatmamak için gösterim sınırı. "
		  "Belgenin tamamı yine de düzenlenebilir."));
	gtk_box_pack_start(GTK_BOX(satir_kutu), etiket, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(satir_kutu), maks, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(kap), satir_kutu, FALSE, FALSE, 0);

	g_object_set_data(G_OBJECT(kap), "canli", canli);
	g_object_set_data(G_OBJECT(kap), "satira_git", satira_git);
	g_object_set_data(G_OBJECT(kap), "maks_satir", maks);

	g_signal_connect(dialog, "response", G_CALLBACK(ayar_yaniti_cb), kap);

	return kap;
}

G_MODULE_EXPORT void geany_load_module(GeanyPlugin *plugin)
{
	plugin->info->name = _("CSV Tablo");
	plugin->info->description = _("CSV/TSV belgelerini alt panelde düzenlenebilir "
		"tablo olarak gösterir.");
	plugin->info->version = "1.0.0";
	plugin->info->author = "Muslu Yüksektepe <muslu.yuksektepe@makdos.com>";

	plugin->funcs->init = eklenti_baslat;
	plugin->funcs->cleanup = eklenti_bitir;
	plugin->funcs->configure = eklenti_ayarla;
	plugin->funcs->callbacks = eklenti_sinyalleri;

	GEANY_PLUGIN_REGISTER(plugin, 225);
}
