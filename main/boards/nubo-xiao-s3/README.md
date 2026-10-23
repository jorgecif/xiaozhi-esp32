# NUBO "Osito" (Seeed XIAO ESP32S3 Sense)

Robot compañero de bolsillo con cabeza de 2 ejes y cara procedural. Hardware y carcasa:
proyecto NUBO, `docs/02-hardware.md` y `cad/nubo.scad`.

## Hardware

| Bloque | Componente | Pines |
|---|---|---|
| SoC | Seeed XIAO ESP32S3 Sense (8 MB flash, 8 MB PSRAM) | — |
| Pantalla | GC9A01 1.28" 240×240 SPI | SCK GPIO7 (D8), MOSI GPIO9 (D10), DC GPIO3 (D2), BL GPIO4 (D3), CS a GND, RST con red RC |
| Micrófono | PDM de la placa Sense | CLK GPIO42, DATA GPIO41 |
| Altavoz | MAX98357A + altavoz 2011 | BCLK GPIO43 (D6), LRCK GPIO44 (D7), DIN GPIO8 (D9) |
| Servos | 2 micro-servos de 2–2.5 g | Giro GPIO1 (D0), cabeceo GPIO2 (D1) |
| Sensores (opcionales) | LSM6DS3TR-C (0x6A) y MAX17048 (0x36) | SDA GPIO5 (D4), SCL GPIO6 (D5) |
| Cámara | OV2640/OV3660 de la placa Sense | Pines internos de la Sense |
| Botón | BOOT | GPIO0 |

La consola va por USB-Serial-JTAG (el USB-C de la XIAO), porque GPIO43/44 están ocupados por I2S.
Al arrancar, la ROM imprime unos bytes por GPIO43 y puede oírse un "clic" breve en el altavoz.

## Qué hace la placa

- **Cara del Osito** (`osito_face_display.cc`): ojos en forma de píldora con brillo, párpados, rubor y boca, dibujados con LVGL a ~30 fps.
  - Parpadea al azar y mueve la mirada en reposo.
  - Abre más los ojos al escuchar y mueve la boca al hablar.
  - Traduce el campo `emotion` del servidor a 9 expresiones: neutral, feliz, cariño, triste, enojo, sorpresa, pensando, sueño y guiño.
- **Cabeza** (`nubo_motion.cc`):
  - Trayectorias suaves para giro (±75°) y cabeceo (−25° a +12°).
  - Gestos que acompañan cada emoción.
  - Corta el PWM en reposo para que los servos no zumben.
- **Toques** (`nubo_sensors.cc`): un toque en la cabeza lo pone contento; un doble toque empieza o termina la conversación. Si el IMU no está conectado, la placa funciona igual.
- **Batería**: porcentaje real con el MAX17048. Si no está, no se muestra.

## Herramientas MCP

| Herramienta | Uso |
|---|---|
| `self.nubo.look` | Mueve la cabeza a una postura (`pan`, `tilt`, `duration_ms`) |
| `self.nubo.gesture` | Gestos: nod, shake, curious, happy, sad, surprised, think, hello, dance, sleep, center (también en español) |
| `self.nubo.expression` | Muestra una expresión durante unos segundos |
| `self.nubo.calibrate` | Solo usuario: corrige el centro de un servo y lo guarda en NVS |

La cámara se expone con la herramienta común `self.camera.take_photo`.

## Compilar

```bash
python scripts/build.py nubo-xiao-s3 --language es-ES
idf.py -p COMx flash monitor
```

Ajustes rápidos en `config.h`:

- Si la cara sale espejada, cambia `DISPLAY_MIRROR_X` y `DISPLAY_MIRROR_Y`.
- Si un servo gira al revés, usa `SERVO_PAN_INVERT` o `SERVO_TILT_INVERT`.
- Si el servo no llega al rango completo, ajusta `SERVO_MIN_PULSE_US` y `SERVO_MAX_PULSE_US`.
