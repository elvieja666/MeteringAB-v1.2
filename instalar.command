#!/bin/bash
# Instalador de Unison A/B para Mac. Dejalo en la misma carpeta que "Unison AB.component" y
# "Unison AB.vst3" (la carpeta que sale del zip) y hace doble clic. Se instala en tu usuario.
cd "$(dirname "$0")" || exit 1

VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$VST3_DIR" "$AU_DIR"

# Antes se llamaba Metering A/B: se borra la version vieja (es el mismo plugin y no pueden estar los dos).
for viejo in "$VST3_DIR/Metering AB.vst3" "$AU_DIR/Metering AB.component"; do
  if [ -e "$viejo" ]; then
    echo "Borrando la version anterior (Metering A/B): $viejo"
    rm -rf "$viejo"
  fi
done

instalados=0
while IFS= read -r -d '' p; do
  nombre=$(basename "$p")
  case "$nombre" in
    *.vst3) destino="$VST3_DIR" ;;
    *.component) destino="$AU_DIR" ;;
    *) continue ;;
  esac
  echo "Instalando $nombre en $destino ..."
  rm -rf "$destino/$nombre"
  cp -R "$p" "$destino/"
  xattr -cr "$destino/$nombre"                                      # saca el bloqueo de macOS (descargado de internet)
  codesign --force --deep --sign - "$destino/$nombre" 2>/dev/null   # firma local, para que macOS lo deje cargar
  instalados=$((instalados+1))
done < <(find . -maxdepth 3 \( -name "Unison AB.vst3" -o -name "Unison AB.component" \) -print0)

if [ "$instalados" -eq 0 ]; then
  echo "No encontre Unison AB.component ni Unison AB.vst3 al lado de este archivo."
else
  # que Logic vuelva a leer los plugins
  killall -9 AudioComponentRegistrar 2>/dev/null
  echo ""
  echo "Listo, Unison A/B instalado ($instalados archivos)."
  echo "Abri Logic y buscalo en Audio Units > 0600musica > Unison A/B."
  echo "Si no aparece: Logic Pro > Settings > Plug-in Manager > Reset & Rescan All."
  echo "Arquitectura de esta Mac: $(uname -m)"
fi
echo ""
read -n 1 -s -r -p "Toca una tecla para cerrar..."
