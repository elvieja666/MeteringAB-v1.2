#!/bin/bash
# Desinstalador de Metering A/B para Mac: hace doble clic. Borra el plugin de las carpetas de
# plugins de tu usuario. Tus proyectos de Logic no se tocan.

VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"

borrados=0
for p in "$VST3_DIR/Metering AB.vst3" "$AU_DIR/Metering AB.component"; do
  if [ -e "$p" ]; then
    echo "Borrando $p ..."
    rm -rf "$p"
    borrados=$((borrados+1))
  fi
done

if [ "$borrados" -eq 0 ]; then
  echo "No encontre Metering A/B instalado en las carpetas de tu usuario."
else
  # que Logic vuelva a leer los plugins
  killall -9 AudioComponentRegistrar 2>/dev/null
  echo ""
  echo "Listo, Metering A/B desinstalado ($borrados archivos). Si Logic todavia lo muestra:"
  echo "Logic Pro > Settings > Plug-in Manager > Reset & Rescan All."
fi
echo ""
read -n 1 -s -r -p "Toca una tecla para cerrar..."
