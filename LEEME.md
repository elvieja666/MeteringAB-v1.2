# Unison

*(Antes se llamaba Metering A/B y después Unison A/B. Es el mismo plugin: los proyectos de Logic que lo usaban lo siguen abriendo, y el instalador borra solo las versiones viejas.)*

Plugin de matcheo de loudness Send/Receive para Logic. El volumen se iguala siempre automáticamente. Ponés un Unison antes de tu cadena de efectos y otro después: el segundo iguala automáticamente el volumen, así comparás el sonido con y sin efectos sin que te engañe el "más fuerte suena mejor". Todas las perillas aceptan valores exactos escritos con el teclado.

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
2. Vas a ver "Compilar Unison" con un círculo amarillo girando: está compilando y corriendo las pruebas automáticas. Tarda unos 10 a 15 minutos.
3. Cuando aparece un tilde verde ✅, está listo. Si aparece una cruz roja ❌, tocala, sacá una captura y mandámela.

## 3. Bajar e instalar el plugin

1. Volvé a la página principal del repositorio. A la derecha, en **Releases**, tocá **Unison (última versión)**.
2. Bajá **Unison.zip**. Si Safari no lo descomprime solo, hacé doble clic: queda una carpeta **Unison** en Descargas.
3. Abrí la **Terminal** (Cmd + Espacio, escribí "Terminal"), pegá esto y apretá Enter:

```
bash ~/Downloads/Unison/instalar.command
```

Cuando dice **Listo**, pasá a **Usarlo en Logic**, más abajo. (También podés hacer doble clic en `instalar.command`.)

Se instala en tu usuario (no pide contraseña). Para **desinstalarlo**, con Logic cerrado:

```
bash ~/Downloads/Unison/desinstalar.command
```

## Cuando haya una versión nueva

1. En tu repositorio de GitHub tocá **Add file** → **Upload files**.
2. Arrastrá **todo** el contenido de la carpeta nueva (con Cmd + Shift + . activado, para que entre `.github`). Los archivos con el mismo nombre se reemplazan.
3. Tocá **Commit changes**, esperá el tilde verde en **Actions** y repetí el paso 3.

---

## Usarlo en Logic

1. Abrí Logic. Si te pregunta por plugins nuevos, dejá que los valide.
   Si no aparece: menú **Logic Pro > Configuración (Settings) > Plug-in Manager**, buscá Unison y tocá **Reset & Rescan Selection**.
2. En la pista donde querés comparar:
   - **Primer slot:** Unison → modo **Send (antes de FX)**, Link ID **1**.
   - **En el medio:** tus efectos (compresor, EQ, saturador, lo que sea).
   - **Último slot:** otro Unison → modo **Receive (después de FX)**, Link ID **1**.
3. Dale play. En el Receive tiene que decir en azul **"Connected to the Send on Link ID 1."**

### Los controles (en el Receive)

| Control | Qué hace |
|---|---|
| **Números (Original, Ganancia, Procesado)** | Miden en LUFS mientras suena el tema. Al poner **pausa** en Logic, Original y Procesado bajan de a poco y llegan juntos a -∞ (en menos de un segundo; el más bajo cae más despacio para llegar al mismo tiempo) pero la medición no se borra: al dar play sigue con la ventana de antes y el audio nuevo va reemplazando al viejo, así la ganancia arranca estable (sin saltos al principio); la **Ganancia aplicada** queda a la vista con su último valor. **Clic sobre los números** = borrar la medición y medir de cero; la ganancia se queda quieta hasta tener media ventana de audio y ahí toma el valor nuevo. El volumen sigue igualado en todo momento. |
| **PRE FX / POST FX** | Un solo botón: cada clic alterna. Arranca en **PRE FX**. **POST FX** (azul) = con tus efectos, al mismo volumen que el original. **PRE FX** (gris) = la señal antes de los efectos. Es un parámetro de Logic, así que lo podés asignar a una tecla o control MIDI. Al cambiar hace un crossfade de 10 ms, imperceptible, para que no haya clicks. El volumen se iguala siempre solo, midiendo en **LUFS (filtro K)**, con un máximo de 24 dB para arriba o para abajo. |
| **Ventana (ms / Sync)** | Cuánto tiempo promedia la medición. En **ms** elegís milisegundos. En **Sync** elegís compases (1/4 bar a 8 bars) según el tempo y el compás del proyecto, y el promedio toma compases exactos, así la lectura de un loop queda quieta. Al pasar de Sync a ms se convierte al tiempo equivalente; al volver a Sync se acomoda al valor musical más cercano. Debajo de la perilla ves la equivalencia. |
| **Latencia + Measure** | Compensa efectos que retrasan el audio (EQ de fase lineal, oversampling, limitadores con lookahead). Dale play y tocá **Measure**: en unos 3 segundos el plugin encuentra el retraso exacto, **con decimales** (por ejemplo 64,50 muestras, porque muchos plugins retrasan "medio sample"), y lo muestra en grande con su equivalente en ms. Si el sonido procesado no se parece lo suficiente al original (por ejemplo, una reverb 100% wet), te avisa en vez de poner un valor dudoso. Si cambiás los efectos de la cadena, volvé a medir. |

### Tamaño de la ventana

Arrastrá la **esquina de abajo a la derecha** del plugin para agrandarlo o achicarlo, con el osciloscopio abierto o cerrado (los dos usan la misma escala): todo crece en proporción, de 40% a 150%. Al 100% el plugin mide 1400 × 650 con el osciloscopio abierto (la columna gris de 350 y el osciloscopio de 1050, tres veces la columna) y 350 × 650 cerrado; por defecto se abre un poco más chico (522 px de alto). **Option + clic en esa esquina** lo vuelve al tamaño por defecto. El tamaño queda guardado con el proyecto. En Send el plugin es la parte de arriba de la columna gris (hasta antes de los números), al mismo tamaño que en el Receive, y no tiene esquina para escalar.

### Osciloscopio

Está a la derecha de la columna gris, a toda la altura del plugin. El botón **Scope** (al pie de la columna gris) lo cierra y lo abre; cerrado queda solo la columna gris. Un plugin nuevo arranca con el osciloscopio cerrado; después se abre como lo dejaste (también al abrir el proyecto). La columna gris tiene 5 partes, de arriba a abajo: título, modo, Link ID y estado (150 px), los tres números (150), Ventana (100), Latencia (100) y Escuchar (150: "Listen" con la nota de LUFS al lado, el botón grande PRE/POST FX y a su derecha Bypass y Scope, uno arriba del otro).

Muestra la onda que estás escuchando: **PRE FX** (gris) o **POST FX** (azul), este último ya nivelado al volumen del original.

- Está **sincronizado al tempo del proyecto**: el ancho es un largo musical que elegís arriba a la derecha (1/4 bar, 1/2 bar, 1 bar, 2 bars o 4 bars). Arriba tiene la grilla de tiempos (0, 1/4, 2/4, 3/4, 4/4) con subdivisiones.
- **Barre de izquierda a derecha** siguiendo la canción (la línea blanca es el cabezal). Al llegar al final vuelve a empezar y va reemplazando lo del compás anterior, así cada compás va cambiando. Está sincronizado con Logic: con Logic en pausa el osciloscopio se queda quieto (y si movés el cursor, el cabezal lo sigue); al dar play se limpia y dibuja desde esa posición.
- **Escala fija en dB**: 0 arriba y abajo, −6, −12, −18 y −∞ en el centro.
- **El osciloscopio sigue a la escucha, siempre**: cada vez que alternás PRE FX / POST FX (botón grande, tecla asignada o automatización), pasa a mostrar la onda que suena, y la nueva va apareciendo a medida que avanza el cabezal, desde donde tocaste el botón; lo de atrás y lo de adelante quedan como estaban hasta que el cabezal pase.
- **Botón Options** (la barra de arriba va Gain, Freeze, Options y el largo): menú con **Channels** (L + R suma, solo L, solo R, L y R separados) y **Grid**, que tiene la resolución de la grilla: *Auto*, que se va achicando con el zoom, o fija en 1/4, 1/8, 1/16, 1/32 o 1/64; con Triplet grid, 1/8T, 1/16T...; si en el ancho que se ve las líneas quedarían a menos de 3 px, muestra la siguiente más grande hasta que hagas zoom; y, abajo, **Show grid** y **Triplet grid** (grilla en tresillos: 3 por tiempo, 6 con zoom, con los números de tresillo arriba).
  - Los menús de Options y del largo **no se cierran al elegir una opción**: se aplica al instante y podés probar varias seguidas. Se cierran con un clic afuera, con Esc o con otro clic sobre la píldora.
  - *Channels* (submenú): **L + R (suma)** (los dos canales sumados y promediados, así lo que está al centro se ve a su nivel real), **Solo L**, **Solo R** o **L y R separados** (L arriba, R abajo, cada uno con su escala).
  - *Show grid* (dentro de Grid): prende o apaga las líneas de fondo (las de cada compás quedan siempre).
  - Todo se guarda con el proyecto.
- **Bypass** (Match Gain Bypass; a la derecha del botón grande, arriba de Scope): prendido (naranja), POST FX suena con su nivel real, sin la ganancia de matcheo. La ganancia se sigue midiendo y se ve en gris; al lado de "Listen" dice "Match gain bypassed (real level)". Se puede automatizar y se guarda con el proyecto.
- **Regla de nivel**: al pasar el mouse por el osciloscopio aparece una línea horizontal con su valor en dB. **Clic** = la línea queda fija (gris claro, arriba y abajo de -∞); clic sobre ella (o sobre su cartelito) la saca; con el mouse sobre su cartelito de dB no aparece la línea del mouse; **doble clic** (también sobre el cartelito de dB) = escribir el valor exacto (por ejemplo -6 o -12.5) y Enter; un clic en cualquier otra parte cierra la caja (Escape cancela).
- **Gain** (el primero de la barra): dibuja una línea blanca punteada (sin cartel) al nivel de la **ganancia aplicada**, en dB. Sirve para ver hasta dónde llega lo procesado después de nivelarlo. Otro clic la saca; se guarda con el proyecto.
- **Freeze** (botón arriba del osciloscopio): lo congela; otro clic lo descongela.
- **Zoom**: rueda del mouse o pellizco en el trackpad, centrado donde está el mouse (hasta ver unas 48 muestras a lo ancho). **Arrastrá** para moverte (o deslizá de costado en el trackpad) y tocá **Show all** (abajo a la izquierda; con el mouse encima se subraya) para volver a ver todo. Con zoom se ve la onda real, muestra por muestra; funciona congelado o en vivo.
- Sirve para ver, por ejemplo, cuánto achica un compresor o un saturador los golpes: los picos grises que sobresalen por encima de los azules son lo que el efecto recortó.

### Valores exactos

- **Clic en el número** debajo de cada perilla, escribí el valor (por ejemplo `-3.25`) y Enter. También acepta coma: `-3,25`.
- **Shift + arrastrar** = ajuste fino.
- **Doble clic** en la perilla = vuelve al valor por defecto.

### Tips

- **Cada pista con su propio Link ID** (1 a 16). Si dos Send usan el mismo número, Unison te avisa en rojo.
- **Latencia:** usá el botón **Measure** con el tema sonando. Si cambiás de efectos, volvé a medir.
- El Send no cambia el audio para nada; solo lo "mira".
- **Ventana recomendada:** Sync con 1 o 2 bars para comparar sobre un loop; en ms, unos 3000 ms. Evitá ventanas muy cortas (menos de 1 segundo): la corrección se movería con cada golpe y alteraría la comparación.

---

## Para curiosos: cómo está armado

- `Manual/` · el manual en PDF que va en el zip (`Manual Unison.pdf`) y su fuente (`manual.html` + capturas). Para regenerarlo: `node Manual/topdf.js "$PWD/Manual/manual.html" "$PWD/Manual/Manual Unison.pdf"` (con Playwright).
- `Source/PluginProcessor.cpp` · el procesamiento de audio (Send y Receive).
- `Source/LoudnessMeter.h` · el medidor LUFS (filtro K de la norma ITU-R BS.1770).
- `Source/SharedBus.h` · el "cable invisible" que conecta el Send con el Receive.
- `Source/LatencyMeasurer.h` · la medición automática de latencia (botón Measure).
- `Source/PluginEditor.cpp` · la interfaz (perillas, botones, colores).
- `Source/Scope.h` · el osciloscopio.
- `Source/Theme.h` · colores y letra. La letra es **Inter** (licencia libre OFL, ver `Resources/Fonts/OFL.txt`), incluida dentro del plugin.
- `Tests/` · pruebas automáticas que verifican que el matcheo funcione.
- `.github/workflows/compilar.yml` · las instrucciones para que GitHub compile el plugin.
- `instalar.command` · instala en tu Mac (en tu usuario) el plugin que compiló GitHub.
- `desinstalar.command` · lo desinstala de tu usuario.

Si cambiás algo del código, subí el cambio a GitHub, esperá el tilde verde, instalá la versión nueva y reabrí el plugin en Logic.
