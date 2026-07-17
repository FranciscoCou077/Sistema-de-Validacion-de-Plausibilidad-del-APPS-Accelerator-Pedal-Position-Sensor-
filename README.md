# Sistema-de-Validacion-de-Plausibilidad-del-APPS-Accelerator-Pedal-Position-Sensor-

Actividad 1 del proceso de reclutamiento de **UNAM Motorsport** (División de Electrónica y Sistemas Embebidos).

Sistema embebido sobre **ESP32** que implementa la validación de plausibilidad de un **APPS (Accelerator Pedal Position Sensor)** conforme a la regla **T.4.2 de Formula SAE**: lee dos sensores redundantes del pedal de aceleración, verifica que sus lecturas coincidan dentro de un margen tolerable y corta el motor si detecta una falla sostenida por más de 100 ms.

## Descripción del proyecto

En un vehículo *drive-by-wire* de Formula SAE, el pedal del acelerador no está conectado mecánicamente al motor: la señal viaja electrónicamente hasta la ECU. Si el sensor que reporta la posición del pedal falla, el vehículo podría interpretar una aceleración no solicitada por el piloto. El reglamento exige por ello **dos sensores independientes** más lógica de supervisión que compare ambas lecturas en todo momento y corte la potencia ante una discrepancia sostenida.

Este proyecto reproduce ese esquema a escala de prototipo:

- Dos potenciómetros simulan el pedal físico (mecánicamente acoplados: se giran juntos para simular operación normal).
- Dos divisores de voltaje resistivos, **con rangos eléctricos distintos entre sí a propósito** (10 kΩ/20 kΩ y 10 kΩ/10 kΩ), acondicionan la señal hacia el ADC del ESP32.
- El ESP32 ejecuta la lógica de plausibilidad y controla dos LEDs (verde = motor habilitado, rojo = falla confirmada) más una señal de interrupción que en un sistema real cortaría encendido/inyección.

## Arquitectura del firmware

El código está diseñado en **tres capas independientes**, de forma que cada una pueda modificarse sin afectar a las demás:

1. **Adquisición** — lectura del voltaje crudo de cada canal, disparada por un temporizador de hardware a 1 kHz (interrupción cada 1 ms). El ISR solo levanta una bandera; el trabajo pesado se hace en el `loop()`.
2. **Calibración** — traduce la lectura cruda del ADC (0–4095) a un porcentaje normalizado (0–100 %) mediante interpolación lineal con recorte en los extremos. Cambiar las resistencias del circuito solo requiere ajustar cuatro constantes, sin tocar la lógica de seguridad.
3. **Seguridad** — compara los dos porcentajes ya normalizados, aplica un umbral de plausibilidad (10 %) y un filtro de *debounce* de 100 ms, y gestiona una máquina de estados de tres niveles: `OK` → `FALLA_PENDIENTE` → `FALLA_CONFIRMADA`.

La telemetría se imprime por puerto serie (115200 baudios) en formato CSV: timestamp_ms,ch1_pct,ch2_pct,diferencia_pct,estado

## Hardware utilizado

- ESP32 DevKit V1 (30 pines)
- 2× potenciómetro lineal B10K
- 4× resistencias (divisores 10kΩ+20kΩ y 10kΩ+10kΩ)
- 2× LED (verde y rojo) + resistencias limitadoras de 220 Ω
- Protoboard y jumpers

| Señal | Pin ESP32 |
|---|---|
| Canal 1 (post divisor 10k/20k, ~3.3V) | GPIO35 |
| Canal 2 (post divisor 10k/10k, ~2.5V) | GPIO34 |
| LED alerta (rojo) | GPIO25 |
| LED motor habilitado (verde) | GPIO26 |
| Señal de interrupción (corte simulado) | GPIO27 |

## Requisitos y entorno

- Arduino IDE 2.3.10 o superior
- Board package **ESP32 by Espressif Systems** (core `arduino-esp32` 3.x — usa la API nueva de timers: `timerBegin(freq)` / `timerAlarm(...)`)
- Placa objetivo: `ESP32 Dev Module`
- Monitor Serie a 115200 baudios

## Cómo correrlo

1. Abrir `apps_plausibilidad.ino` en Arduino IDE.
2. Seleccionar la placa `ESP32 Dev Module` y el puerto COM correspondiente.
3. Compilar y subir.
4. Abrir el Monitor Serie a 115200 baudios.
5. Mover ambos potenciómetros juntos → LED verde, estado `OK`.
6. Desincronizar un potenciómetro más de 10 % por más de 100 ms → LED rojo, estado `FALLA_CONFIRMADA`.

## Pruebas de validación realizadas

| Prueba | Resultado |
|---|---|
| Movimiento sincronizado (caso normal) | LED verde constante, estado `OK` |
| Falla por desincronización sostenida | LED rojo se enciende y permanece, `FALLA_CONFIRMADA` |
| Recuperación tras falla | Regreso automático a `OK` al resincronizar, sin reinicio manual |
| Filtro de *debounce* (<100 ms) | Queda en `PENDIENTE` y se autoresuelve, no dispara falla |
| Rango completo (0–100 %) | Ambos canales llegan limpio a 0.00 % y 100.00 % tras recalibración |
| Formato de salida CSV | Consistente durante operación continua |

## Estructura del repositorio
├── apps_plausibilidad.ino     # Firmware principal
├── wokwi/                     # Proyecto de simulación (diagram.json, sketch.ino)
├── docs/
│   └── Reporte Tecnico: Docs_act1
└── README.md
## Notas

Este proyecto fue mi primera experiencia programando un microcontrolador y trabajando con sistemas embebidos. El desarrollo se apoyó en documentación oficial de Espressif y Arduino, en el reglamento de Formula SAE, y en asistencia de IA (Claude, Anthropic) para investigación, depuración de hardware y revisión de código. El detalle completo del diseño, la depuración y las pruebas está documentado en `docs/Reporte_Tecnico_APPS_Plausibilidad.md`.

## Autor

Francisco José Coutiño Morales — Facultad de Ingeniería, UNAM
