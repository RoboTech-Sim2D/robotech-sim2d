#!/bin/bash

DEST_DIR="robotech"

# 🧹 Limpiar si ya existía
rm -rf "$DEST_DIR" "$DEST_DIR.tar.gz"
mkdir -p "$DEST_DIR/lib"
mkdir -p "$DEST_DIR/formations-dt"

# 📥 Copiar archivos raíz
cp coach.conf player.conf "$DEST_DIR/" 2>/dev/null
cp sample_player sample_coach "$DEST_DIR/" 2>/dev/null
cp start start.sh kill "$DEST_DIR/" 2>/dev/null

# 📁 Copiar librerías (si hay)
cp -r lib/* "$DEST_DIR/lib/" 2>/dev/null

# 📁 Copiar formaciones (si hay)
cp -r formations-dt/* "$DEST_DIR/formations-dt/" 2>/dev/null

# 🧼 Limpiar basura de Windows
find "$DEST_DIR" -name "*:Zone.Identifier" -delete

# 🔐 Permisos de ejecución
chmod +x "$DEST_DIR"/start "$DEST_DIR"/start.sh "$DEST_DIR"/kill 2>/dev/null
chmod +x "$DEST_DIR"/sample_player "$DEST_DIR"/sample_coach 2>/dev/null

# 📦 Comprimir
tar -czvpf "$DEST_DIR.tar.gz" "$DEST_DIR"

echo "✅ Carpeta '$DEST_DIR' creada con estructura estándar y comprimida como '$DEST_DIR.tar.gz'"

