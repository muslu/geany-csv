#!/usr/bin/env bash
# GTK3 gelistirme basliklarini SISTEME DOKUNMADAN yerel bir SDK dizinine acar.
# Sebep: sury.org libbrotli1 1.1.0 <-> jammy libbrotli-dev 1.0.9 catismasi
# yuzunden `apt install libgtk-3-dev` wine/openjdk/i386 yiginini silmek istiyor.
set -euo pipefail

SDK="${1:-$HOME/.local/gtk3-sdk}"
INDIR="$(mktemp -d)"
trap 'rm -rf "$INDIR"' EXIT

echo "==> Bagimliliklar cozuluyor"
mapfile -t PKGS < <(
  apt-cache depends --recurse --no-recommends --no-suggests \
      --no-conflicts --no-breaks --no-replaces --no-enhances \
      libgtk-3-dev 2>/dev/null \
    | grep -E '^[a-z0-9]' | sed 's/:i386$//' | sort -u \
    | grep -E -- '(-dev|-dev-bin|wayland-protocols|xorgproto|x11proto)$'
)
echo "    ${#PKGS[@]} gelistirme paketi"

echo "==> Indiriliyor"
cd "$INDIR"
ok=0; atlanan=()
for p in "${PKGS[@]}"; do
  if apt-get download -q "$p" >/dev/null 2>&1; then ok=$((ok+1)); else atlanan+=("$p"); fi
done
echo "    $ok indirildi, ${#atlanan[@]} atlandi: ${atlanan[*]:-yok}"

echo "==> $SDK icine aciliyor"
rm -rf "$SDK"; mkdir -p "$SDK"
for d in "$INDIR"/*.deb; do dpkg-deb -x "$d" "$SDK"; done

echo "==> .pc dosyalari yerel prefix'e cevriliyor"
find "$SDK" -name '*.pc' -print0 | xargs -0 sed -i \
  -e "s|^prefix=/usr\$|prefix=$SDK/usr|" \
  -e "s|^exec_prefix=/usr\$|exec_prefix=$SDK/usr|" \
  -e "s|^libdir=/usr/|libdir=$SDK/usr/|" \
  -e "s|^includedir=/usr/|includedir=$SDK/usr/|" \
  -e "s|^datarootdir=/usr/|datarootdir=$SDK/usr/|" \
  -e "s|^datadir=/usr/|datadir=$SDK/usr/|" \
  -e "s|-I/usr/include|-I$SDK/usr/include|g"

echo "==> Dogrulama"
export PKG_CONFIG_PATH="$SDK/usr/lib/x86_64-linux-gnu/pkgconfig:$SDK/usr/share/pkgconfig:/usr/lib/x86_64-linux-gnu/pkgconfig:/usr/share/pkgconfig"
pkg-config --modversion gtk+-3.0 && pkg-config --cflags gtk+-3.0 >/dev/null && echo "    gtk+-3.0 OK"
pkg-config --modversion geany     && echo "    geany OK"
du -sh "$SDK"
