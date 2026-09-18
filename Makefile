# geany-csv — Geany için CSV tablo eklentisi
#
# make            → eklentiyi derler (geany-csv.so)
# make test       → ayrıştırıcı birim testlerini çalıştırır
# make install    → ~/.config/geany/plugins altına kurar
# make uninstall  → kurulumu kaldırır
# make sdk        → GTK3 başlıklarını sisteme dokunmadan yerel SDK'ya açar

EKLENTI    := geany-csv.so
KAYNAKLAR  := src/csv.c src/plugin.c
NESNELER   := $(KAYNAKLAR:.c=.o)
KURULUM    := $(HOME)/.config/geany/plugins

# Bu makinede libgtk-3-dev sistem geneline kurulamıyor (sury.org libbrotli1
# çakışması). Varsa yerel SDK'daki .pc dosyaları önce gelir; sistemde düzgün
# bir libgtk-3-dev varsa bu blok sessizce atlanır.
SDK        ?= $(HOME)/.local/gtk3-sdk
SDK_PC     := $(wildcard $(SDK)/usr/lib/*/pkgconfig)
ifneq ($(SDK_PC),)
PC_PATH    := $(SDK_PC):$(SDK)/usr/share/pkgconfig:$(PKG_CONFIG_PATH)
else
PC_PATH    := $(PKG_CONFIG_PATH)
endif
export PKG_CONFIG_PATH := $(PC_PATH)

# $(shell ...) çağrılarına yolu açıkça geçiriyoruz: `export` yalnız reçetelere
# uygulanır, Makefile çözümlenirken çalışan $(shell) ona güvenemez.
PC           := PKG_CONFIG_PATH="$(PC_PATH)" pkg-config
GEANY_CFLAGS := $(shell $(PC) --cflags geany 2>/dev/null)
GLIB_CFLAGS  := $(shell $(PC) --cflags glib-2.0)
GLIB_LIBS    := $(shell $(PC) --libs glib-2.0)

UYARILAR := -Wall -Wextra -Wno-unused-parameter -Wmissing-prototypes -Wshadow
CFLAGS   ?= -O2 -g
ALL_CFLAGS := $(CFLAGS) $(UYARILAR) -fPIC $(GEANY_CFLAGS)

# Eklenti, Geany süreci içinde dlopen edilir; GTK/Geany simgeleri çalışma
# anında ana süreçten çözülür, bu yüzden hiçbir kitaplığa bağlanmaz.
LDFLAGS_SO := -shared

.PHONY: all test install uninstall clean sdk kontrol

all: kontrol $(EKLENTI)

kontrol:
	@$(PC) --exists geany || { \
	  echo "HATA: geany.pc bulunamadı."; \
	  echo "  Geany başlıkları: sudo nala install geany-common"; \
	  echo "  GTK3 başlıkları  : make sdk"; exit 1; }

$(EKLENTI): $(NESNELER)
	$(CC) $(LDFLAGS_SO) -o $@ $^ $(LDFLAGS)

%.o: %.c src/csv.h
	$(CC) $(ALL_CFLAGS) -c $< -o $@

test: test/csv-test
	./test/csv-test

test/csv-test: test/csv-test.c src/csv.c src/csv.h
	$(CC) $(CFLAGS) $(UYARILAR) $(GLIB_CFLAGS) -o $@ test/csv-test.c src/csv.c $(GLIB_LIBS)

install: all
	install -d $(KURULUM)
	install -m 644 $(EKLENTI) $(KURULUM)/$(EKLENTI)
	@echo "Kuruldu: $(KURULUM)/$(EKLENTI)"
	@echo "Geany → Araçlar → Eklenti Yöneticisi'nden etkinleştirin."

uninstall:
	rm -f $(KURULUM)/$(EKLENTI)

sdk:
	bash tools/gtk3-sdk-kur.sh

clean:
	rm -f $(NESNELER) $(EKLENTI) test/csv-test
