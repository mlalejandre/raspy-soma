# Navegador web de SOMA (parche 025)

Sustituye al visor HTML anterior por un navegador con motor propio, escrito en C freestanding
(sin libc, sin FPU: la aritmética de JavaScript usa coma flotante por software), válido para
`ARCH=x86_64` y `ARCH=aarch64`.

## Instalar / revertir

```sh
python3 parche_navegador.py              # instala (idempotente)
python3 parche_navegador.py --verificar  # instala y compila x86_64 y aarch64
python3 parche_navegador.py --revertir   # restaura lo anterior desde .parche_backup/
make ARCH=x86_64                         # compilación normal
```

El parche añade archivos `src/apps/web_*.c|h`, sustituye `src/apps/browser.c`, y solo toca tres
archivos existentes: `Makefile` (objetos nuevos), `src/apps/browser.h` (una declaración) y
`src/ui/gui.c` (una línea fail-soft para rueda/hover). La API pública de `browser.h` se conserva.

## Qué soporta

- HTML5: analizador tolerante (tablas, formularios, entidades, `<template>`, `<picture>/srcset`, `<base>`, meta refresh).
- CSS3: cascada y especificidad, selectores complejos y pseudo-clases (`:hover`, `:nth-child`, `:not`…),
  `@media`, `@import`, variables `var()`, `calc()`, flexbox, tablas, `position`, `float`, bordes redondeados,
  degradados lineales; sin `box-shadow` ni fuentes web (`@font-face` no se descarga: se usa DejaVu Sans incrustada).
- JavaScript (ES2017 recortado): `let/const`, clases, funciones flecha, destructuración, spread, plantillas,
  `Promise/async/await` (bombeadas de forma síncrona), `Map/Set/Symbol`, expresiones regulares, `JSON`, `Math`,
  `Date` (UTC), tipos tipados (polyfill), `setTimeout/setInterval/requestAnimationFrame`.
- DOM: árbol completo, `querySelector(All)`, eventos con captura/burbuja, `classList`, `dataset`, `style`,
  `getComputedStyle`, `innerHTML`, `fetch`, `XMLHttpRequest`, `FormData`, `URL`, `localStorage/sessionStorage`,
  `history.pushState`, `MutationObserver` básico.
- Imágenes PNG, JPEG, GIF y BMP; cookies; formularios GET/POST (urlencoded y multipart); redirecciones.
- Interfaz: pestañas (hasta 6), barra de URL editable, atrás/adelante/recargar/inicio, barra de desplazamiento
  arrastrable, rueda, lista desplegable de `<select>`, `alert()`, búsqueda en la página (`/`).
- Atajos: `Ctrl+L` URL, `Ctrl+T` pestaña nueva, `Ctrl+W` cerrar pestaña, `Ctrl+R` recargar, `Tab` mover foco,
  flechas/RePág/AvPág/Inicio/Fin desplazan.

## HTTPS

El kernel no implementa TLS. Para `https://` ejecuta en el anfitrión:

```sh
python3 tools/web_gateway.py            # escucha en 127.0.0.1:8096 (el invitado QEMU lo ve como 10.0.2.2:8096)
```

La pasarela rechaza por defecto destinos de redes privadas (`--allow-private` para permitirlos).
Sin ella funcionan `http://`, `file://` y `data:`; para `https://` se muestra un aviso explicativo.

## Parche 028: interfaz nueva, pasarela automática y formatos modernos

- `ejecutar.py` arranca la pasarela en un hilo de fondo (`web_gateway.ensure_gateway()`): ya no hay que lanzarla a mano.
  Si el puerto 8096 ya está ocupado se usa lo que haya en él.
- La pasarela (versión 3) anuncia lo que sabe hacer en la cabecera `X-Soma-Gateway` y el navegador se adapta:
  - **WebP, AVIF, ICO, TIFF y JPEG CMYK** se convierten a PNG/JPEG; las fotos grandes se reducen (necesita Pillow: `pip3 install pillow`).
  - **SVG** (archivos `.svg` y elementos `<svg>` en línea: iconos y logotipos) se rasteriza a PNG. Los `<svg>` en línea se
    envían con `POST /svg?c=<color>`; heredan el color del texto (`currentColor`).
  - Las imágenes WebP/AVIF/SVG servidas por `http://` hacen un segundo intento a través de la pasarela.
- Motor: `box-shadow`, esquinas redondeadas con antialias, `overflow:hidden` + `border-radius` recorta el contenido,
  degradados recortados al radio (botones «pastilla»), `radial-gradient` aproximado, `repeat(auto-fit, minmax())` según el
  ancho real del contenedor, imágenes con `width:100%` que ya no desbordan la rejilla y `/>` respetado dentro de `<svg>`.
- Interfaz: pestañas redondeadas, botones con iconos, campo de dirección con candado en HTTPS, barra de progreso y
  línea de estado discreta (los avisos de JavaScript de las páginas ya no se muestran en rojo).

### Rendimiento (revisión 2 del parche 028)

- El cálculo de estilos tenía un coste cuadrático: cada declaración CSS buscaba `var(` recorriendo **toda** la hoja de
  estilos. En una portada con 300 KB de CSS era la mayor parte del tiempo de carga. Ahora la búsqueda se limita al valor.
- El hilo del navegador cede la CPU por tiempo (cada ~4 ms) también mientras descarga, descomprime y decodifica JPEG:
  el planificador del kernel es cooperativo y antes la GUI se quedaba congelada durante esos tramos.
- Imágenes: primero las visibles; las que están lejos de la pantalla esperan a que se acerque el desplazamiento.
  Cada foto se guarda al tamaño al que se muestra y, si falta memoria, se liberan las más lejanas (antes, al agotarse el
  tope de 14 MB, el resto fallaba con «JPEG dañado o demasiado grande»).
- `img{width:100%;height:auto}` con atributos `width`/`height` mantiene la proporción (antes salían estiradas).
- La pasarela (v4) precarga en paralelo las imágenes, hojas de estilo y scripts de cada página y recodifica a JPEG
  normal los progresivos y CMYK, que SOMA decodifica más rápido.

## Arquitectura

- Hilo de la GUI: solo dibuja (`wp_paint`) y encola eventos.
- Hilo `web_worker` (pila propia de 1 MiB): red, análisis, scripts, maquetación y temporizadores. Se crea al abrir
  la ventana, no al arrancar; con la ventana oculta duerme 250 ms.
- Una sola página viva; las pestañas en segundo plano se descartan (URL, historial y scroll) y se recargan al volver.
- Intérprete JS en pila de 768 KiB, heap con tope de 24 MiB por página.

## Límites conocidos

- Sin generadores, módulos ES, `Proxy`, Workers, WebSocket, WebAssembly, canvas/WebGL (solo stubs), SVG ni WebP.
  (Si en el PC anfitrión hay Pillow —`pip install pillow`—, `tools/web_gateway.py` convierte WebP/AVIF a PNG/JPEG y reduce las
  fotos de más de 1600 px antes de entregarlas, así que en páginas https:// esas imágenes sí se ven.)

## Imágenes y fluidez
- Imágenes diferidas (`data-src`, `data-lazy-src`, `data-original`, `data-srcset`…) se promueven a `src`; en `<picture>` se
  descartan las fuentes WebP/AVIF/SVG y se pide al servidor solo PNG/JPEG/GIF (sin comodines) para que no responda en WebP.
- Cada imagen que falla escribe una línea `[web] img KO: <url> h=<estado> n=<bytes> <motivo>` por el puerto serie (máx. 24 por página).
- Los bucles largos (HTML, CSS, maquetación, JPEG/PNG) ceden la CPU periódicamente desde el hilo trabajador; mientras se maqueta,
  la ventana repinta la última instantánea de la página en vez de tocar el árbol de cajas.
- Las cadenas JS son bytes UTF-8 (índices por byte); las fechas son siempre UTC.
- Sin recolector de basura: una página con JS muy intensivo agota su tope de 24 MiB y se detiene el script.
- Sitios modernos pesados (frameworks SPA grandes) pueden no funcionar o ir lentos: el motor es un subconjunto.
- `localStorage` es de memoria (no persiste entre arranques).
- El planificador del kernel es cooperativo: una maquetación enorme puede congelar la GUI unos instantes.
- SRCFS: al crecer el código, el empaquetador de `ejecutar.py` omite por diseño las tablas de datos grandes
  (`fonts/vfont_data.h`) del visor de fuentes dentro de SOMA. No afecta a la compilación.
