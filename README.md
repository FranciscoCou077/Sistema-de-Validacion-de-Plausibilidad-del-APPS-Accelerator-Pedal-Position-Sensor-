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

La telemetría se imprime por puerto serie (115200 baudios) en formato CSV:
