# Metering A/B

Plugin de matcheo de loudness Send/Receive para Logic. El volumen se iguala siempre automáticamente. Ponés un Metering A/B antes de tu cadena de efectos y otro después: el segundo iguala automáticamente el volumen, así comparás el sonido con y sin efectos sin que te engañe el "más fuerte suena mejor". Todas las perillas aceptan valores exactos escritos con el teclado.

---

El plugin se compila en GitHub, en una Mac en la nube: vos no instalás nada de desarrollo. Funciona en Mac (Apple Silicon e Intel) y se instala como Audio Unit para Logic, más una versión VST3.

---

## 1. Subir el proyecto a GitHub (una sola vez)

1. Creá una cuenta gratis en <https://github.com> (si no tenés).
2. Arriba a la derecha tocá **+** → **New repository**.
   - *Repository name:* `MeteringAB`
   - Puede ser **Private** o **Public**, las dos funcionan.
   - **No** marques ninguna casilla de "Add a README" ni similares.
   - Tocá **Create repository**.
3. En la página que aparece, tocá el link **uploading an existing file**.
4. En el Finder, abrí la carpeta `MeteringAB` y apretá **Cmd + Shift + .** (punto). Así aparecen los archivos ocultos, incluida la carpeta **.github**, que es la que le dice a GitHub cómo compilar.
5. Seleccioná **todo** lo que hay adentro de la carpeta (Cmd + A), incluida `.github`, y arrastralo a la página de GitHub.
6. Esperá a que suban los archivos y tocá el botón verde **Commit changes**.

## 2. Esperar la compilación

1. Arriba en tu repositorio tocá la pestaña **Actions**.
2. Vas a ver "Compilar Metering A/B" con un círculo amarillo girando: está compilando y corriendo las pruebas automáticas. Tarda unos 10 a 15 minutos.
3. Cuando aparece un tilde verde ✅, está listo. Si aparece una cruz roja ❌, tocala, sacá una captura y mandámela.

## 3. Bajar e instalar el plugin

1. Volvé a la página principal del repositorio. A la derecha, en **Releases**, tocá **Metering A/B (última versión)**.
2. Bajá **MeteringAB-mac.zip**. Si Safari no lo descomprime solo, hacé doble clic: queda una carpeta **MeteringAB-mac** en Descargas.
3. Abrí la **Terminal** (Cmd + Espacio, escribí "Terminal"), pegá esto y apretá Enter:

```
bash ~/Downloads/MeteringAB-mac/instalar.command
```

Cuando dice **✅ Listo**, pasá a **Usarlo en Logic**, más abajo.

## Cuando haya una versión nueva

1. En tu repositorio de GitHub tocá **Add file** → **Upload files**.
2. Arrastrá **todo** el contenido de la carpeta nueva (con Cmd + Shift + . activado, para que entre `.github`). Los archivos con el mismo nombre se reemplazan.
3. Tocá **Commit changes**, esperá el tilde verde en **Actions** y repetí el paso 3.

---

## Usarlo en Logic

1. Abrí Logic. Si te pregunta por plugins nuevos, dejá que los valide.
   Si no aparece: menú **Logic Pro > Configuración (Settings) > Plug-in Manager**, buscá Metering A/B y tocá **Reset & Rescan Selection**.
2. En la pista donde querés comparar:
   - **Primer slot:** Metering A/B → modo **Send (antes de FX)**, Link ID **1**.
   - **En el medio:** tus efectos (compresor, EQ, saturador, lo que sea).
   - **Último slot:** otro Metering A/B → modo **Receive (después de FX)**, Link ID **1**.
3. Dale play. En el Receive tiene que decir en verde **Conectado al Send**.

### Los controles (en el Receive)

| Control | Qué hace |
|---|---|
| **PRE FX / POST FX** | Un solo botón: cada clic alterna. Arranca en **PRE FX**. **POST FX** (ámbar) = con tus efectos, al mismo volumen que el original. **PRE FX** (verde agua) = la señal antes de los efectos. Es un parámetro de Logic, así que lo podés asignar a una tecla o control MIDI. Al cambiar hace un crossfade de 10 ms, imperceptible, para que no haya clicks. |
| **Medición** | *LUFS* se parece más a cómo escucha el oído (recomendado). *RMS* es energía pura. |
| **Ventana (ms / Sync)** | Cuánto tiempo promedia la medición. En **ms** elegís milisegundos. En **Sync** elegís compases (1/4 bar a 8 bars) según el tempo y el compás del proyecto, y el promedio toma compases exactos, así la lectura de un loop queda quieta. Al pasar de Sync a ms se convierte al tiempo equivalente; al volver a Sync se acomoda al valor musical más cercano. Debajo de la perilla ves la equivalencia. |
| **Congelar** | Deja fija la corrección de volumen actual (el matcheo es siempre automático, con un máximo de 24 dB para arriba o para abajo). |
| **Latencia + Medir** | Compensa efectos que retrasan el audio (EQ de fase lineal, oversampling, limitadores con lookahead). Dale play y tocá **Medir**: en unos 3 segundos el plugin encuentra el retraso exacto, **con decimales** (por ejemplo 64,50 muestras, porque muchos plugins retrasan "medio sample"), y lo muestra en grande con su equivalente en ms. Si el sonido procesado no se parece lo suficiente al original (por ejemplo, una reverb 100% wet), te avisa en vez de poner un valor dudoso. Si cambiás los efectos de la cadena, volvé a medir. |
| **Ø (polaridad)** | Se enciende solo si **Medir** detecta que algún plugin invierte la polaridad. También lo podés activar a mano. |

### Osciloscopio

Está abajo de todo, separado por una línea, y arranca cerrado: tocá **▸ Osciloscopio** para abrirlo. Tocá **▾ Osciloscopio** para ocultarlo (la ventana del plugin se achica); queda guardado con el proyecto.

Muestra el **PRE FX** (verde agua) y, encima, el **POST FX** (ámbar), ya nivelado al volumen del original, así ves solo lo que cambió.

- Está **sincronizado al tempo del proyecto**: el ancho es un largo musical que elegís arriba a la derecha (1/4 bar, 1/2 bar, 1 bar, 2 bars o 4 bars). Arriba tiene la grilla de tiempos (0, 1/4, 2/4, 3/4, 4/4) con subdivisiones.
- **Barre de izquierda a derecha** siguiendo la canción (la línea blanca es el cabezal). Al llegar al final vuelve a empezar y va reemplazando lo del compás anterior, así cada compás va cambiando. Está sincronizado con Logic: con Logic en pausa el osciloscopio se queda quieto (y si movés el cursor, el cabezal lo sigue); al dar play se limpia y dibuja desde esa posición.
- **Escala fija en dB**: 0 arriba y abajo, −6, −12, −18 y −∞ en el centro.
- **Botones PRE FX y POST FX** (arriba, al lado del largo): muestran u ocultan cada onda. Encendido tiene el color de su onda; apagado queda gris. Se guardan con el proyecto.
- **Botón LINK**: prendido, el osciloscopio sigue a la escucha: cada vez que alternás PRE FX / POST FX (botón grande, tecla asignada o automatización), la nueva onda va apareciendo a medida que avanza el cabezal, desde donde tocaste el botón; lo de atrás y lo de adelante quedan como estaban hasta que el cabezal pase. Así cada parte del compás muestra lo que sonaba en ese momento. El botón toma el color de lo que escuchás. Apagado, el osciloscopio no cambia solo y muestra lo que dejes prendido con PRE FX y POST FX (una, las dos o ninguna). Se guarda con el proyecto.
- **Botón Estilo**: elegí cómo se ve.
  - *Forma PRE FX* y *Forma POST FX* (cada una por separado, por ejemplo PRE en Contorno y POST en Solo líneas): **Contorno** (relleno de color plano, sin borde), **Detallado** (los picos reales en un solo color; muestra cada ciclo de los graves) o **Solo líneas** (solo el borde, ideal para comparar PRE y POST superpuestos).
  - *Canales*: **L + R (suma)** (los dos canales sumados y promediados, así lo que está al centro se ve a su nivel real), **Solo L**, **Solo R** o **L y R separados** (L arriba, R abajo, cada uno con su escala).
  - *Colores*: paletas listas (Verde agua / Ámbar, Grises, Azul / Rosa, Blanco / Rojo, Violeta / Lima) o **Personalizado**, eligiendo el color de PRE y de POST con un selector. Los botones PRE FX / POST FX toman los mismos colores.
  - *Mostrar grilla*: prende o apaga las líneas de fondo (las de cada compás quedan siempre).
  - Todo se guarda con el proyecto.
- **Clic sobre el osciloscopio** para congelarlo; otro clic lo descongela.
- Sirve para ver, por ejemplo, cuánto achica un compresor o un saturador los golpes: los picos verdes que sobresalen por encima de los ámbar son lo que el efecto recortó.

### Valores exactos

- **Clic en el número** debajo de cada perilla, escribí el valor (por ejemplo `-3.25`) y Enter. También acepta coma: `-3,25`.
- **Shift + arrastrar** = ajuste fino.
- **Doble clic** en la perilla = vuelve al valor por defecto.

### Tips

- **Cada pista con su propio Link ID** (1 a 16). Si dos Send usan el mismo número, Metering A/B te avisa en rojo.
- **Latencia:** usá el botón **Medir** con el tema sonando. Si cambiás de efectos, volvé a medir.
- El Send no cambia el audio para nada; solo lo "mira".
- **Ventana recomendada:** Sync con 1 o 2 bars para comparar sobre un loop; en ms, unos 3000 ms. Evitá ventanas muy cortas (menos de 1 segundo): la corrección se movería con cada golpe y alteraría la comparación.

---

## Para curiosos: cómo está armado

- `Source/PluginProcessor.cpp` · el procesamiento de audio (Send y Receive).
- `Source/LoudnessMeter.h` · el medidor RMS / LUFS (filtro K de la norma ITU-R BS.1770).
- `Source/SharedBus.h` · el "cable invisible" que conecta el Send con el Receive.
- `Source/LatencyMeasurer.h` · la medición automática de latencia (botón Medir).
- `Source/PluginEditor.cpp` · la interfaz (perillas, botones, colores).
- `Source/Scope.h` · el osciloscopio.
- `Tests/` · pruebas automáticas que verifican que el matcheo funcione.
- `.github/workflows/compilar.yml` · las instrucciones para que GitHub compile el plugin.
- `instalar.command` · instala en tu Mac el plugin que compiló GitHub.

Si cambiás algo del código, subí el cambio a GitHub, esperá el tilde verde, instalá la versión nueva y reabrí el plugin en Logic.
