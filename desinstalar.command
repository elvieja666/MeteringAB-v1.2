#!/bin/bash
# ---------------------------------------------------------------------------
#  Desinstala Metering A/B de tu usuario (lo que puso instalar.command).
#  Uso: doble clic, o en la Terminal:  bash ~/Downloads/MeteringAB-mac/desinstalar.command
#  Tus proyectos de Logic no se tocan: solo se borran los plugins.
# ---------------------------------------------------------------------------

AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
borrados=0

for p in "$AU_DIR/Metering AB.component" "$VST3_DIR/Metering AB.vst3"; do
    if [ -e "$p" ]; then
        rm -rf "$p" && echo "🗑  Borrado: $p" && borrados=$((borrados + 1))
    fi
done

if [ "$borrados" -eq 0 ]; then
    echo "ℹ️  Metering A/B no estaba instalado en tu usuario."
else
    # Avisarle a macOS que el Audio Unit ya no está.
    killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
    echo "✅ Listo: Metering A/B quedó desinstalado. Si Logic estaba abierto, cerralo y volvé a abrirlo."
fi
