/* ============================================================================
 * SISTEMA DE VALIDACION DE PLAUSIBILIDAD APPS (Accelerator Pedal Position Sensor)
 * UNAM Motorsport - Formula SAE - Regla T.4.2
 * ----------------------------------------------------------------------------
 * Autor: Francisco
 * Descripcion:
 *   Lee dos sensores (potenciometros) redundantes del pedal de aceleracion,
 *   verifica que ambos coincidan dentro de un margen (plausibilidad) y corta
 *   el motor si detecta una falla sostenida por mas de 100 ms.
 *
 * Arquitectura (3 capas independientes, a proposito):
 *   1) ADQUISICION   -> lee voltaje crudo de cada canal (hardware timer, 1 kHz)
 *   2) CALIBRACION   -> traduce voltaje crudo a % de pedal (0-100%). Se puede
 *                       cambiar sin tocar la logica de seguridad.
 *   3) SEGURIDAD     -> compara canales, aplica debounce de 100 ms y decide
 *                       si corta el motor. Nunca depende de los valores de
 *                       calibracion, solo de los % ya normalizados.
 * ==========================================================================*/

// ---------------------------------------------------------------------------
// 1. DEFINICION DE PINES
// ---------------------------------------------------------------------------
const int PIN_SENSOR_1 = 34;   // ADC1_CH6 - Canal A del APPS (post divisor 3.3V)
const int PIN_SENSOR_2 = 35;   // ADC1_CH7 - Canal B del APPS (post divisor 2.5V)
const int PIN_LED_ALERTA = 25; // LED rojo: se enciende en falla confirmada
const int PIN_LED_MOTOR  = 26; // LED verde: encendido = motor habilitado

// ---------------------------------------------------------------------------
// 2. CAPA DE CALIBRACION (independiente de la seguridad)
// ---------------------------------------------------------------------------
// El ADC del ESP32 es de 12 bits (0-4095) y mide de 0V a 3.3V.
// Cada canal tiene un rango electrico distinto a proposito (asimetria),
// definido por el divisor de voltaje que armamos en el circuito:
//   Canal 1 -> divisor 10k/20k -> voltaje max teorico ~3.33V
//   Canal 2 -> divisor 10k/10k -> voltaje max teorico ~2.5V
// Si cambias las resistencias del circuito, SOLO tocas estas 4 constantes.
const int ADC_MIN_CH1 = 0;
const int ADC_MAX_CH1 = 4095;   // corresponde a ~3.3V en canal 1
const int ADC_MIN_CH2 = 0;
const int ADC_MAX_CH2 = 3100;   // corresponde a ~2.5V en canal 2 (rango mas chico)

// Convierte una lectura cruda del ADC a porcentaje de pedal (0.0 - 100.0)
// Esta es la "escala comun y facil de entender" que pide la actividad.
float calibrarCanal(int lecturaCruda, int adcMin, int adcMax) {
  float pct = (float)(lecturaCruda - adcMin) * 100.0 / (float)(adcMax - adcMin);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

// ---------------------------------------------------------------------------
// 3. CAPA DE SEGURIDAD (Regla T.4.2)
// ---------------------------------------------------------------------------
const float UMBRAL_PLAUSIBILIDAD_PCT = 10.0;   // diferencia maxima permitida entre canales
const unsigned long TIEMPO_DEBOUNCE_MS = 100;  // la falla debe persistir mas de esto

// Estados del sistema de seguridad
enum EstadoSeguridad {
  ESTADO_OK,             // ambos canales coinciden
  ESTADO_FALLA_PENDIENTE,// hay discrepancia, pero aun no pasan los 100 ms
  ESTADO_FALLA_CONFIRMADA// discrepancia sostenida > 100 ms -> motor cortado
};

volatile EstadoSeguridad estadoActual = ESTADO_OK;
unsigned long tiempoInicioFalla = 0;
bool fallaEnCurso = false;

// ---------------------------------------------------------------------------
// 4. ADQUISICION POR INTERRUPCION DE HARDWARE (timer a 1 kHz = cada 1 ms)
// ---------------------------------------------------------------------------
// Usamos un timer de hardware en vez de solo leer en el loop() porque un
// sistema de seguridad real no puede depender de que el loop() este libre
// en ese instante. El ISR solo levanta una bandera; el trabajo pesado
// (ADC, matematica, Serial) se hace fuera del ISR, en el loop principal.
hw_timer_t *timerMuestreo = NULL;
volatile bool banderaMuestreo = false;

void IRAM_ATTR onTimerMuestreo() {
  banderaMuestreo = true;
}

// Pin usado para emitir la "senal de interrupcion" hacia el resto del sistema
// (equivalente a la linea que en un auto real corta la ECU/encendido).
const int PIN_SENAL_INTERRUPCION = 27;

void setup() {
  Serial.begin(115200);

  pinMode(PIN_LED_ALERTA, OUTPUT);
  pinMode(PIN_LED_MOTOR, OUTPUT);
  pinMode(PIN_SENAL_INTERRUPCION, OUTPUT);

  digitalWrite(PIN_LED_ALERTA, LOW);
  digitalWrite(PIN_LED_MOTOR, HIGH);   // motor habilitado al arrancar
  digitalWrite(PIN_SENAL_INTERRUPCION, LOW);

  analogReadResolution(12); // 0-4095

  // Configuracion del timer de hardware (API nueva del core ESP32 3.x):
  // timerBegin recibe directo la frecuencia del timer en Hz.
  timerMuestreo = timerBegin(1000000);              // timer corre a 1 MHz (1 tick = 1us)
  timerAttachInterrupt(timerMuestreo, &onTimerMuestreo);
  timerAlarm(timerMuestreo, 1000, true, 0);         // dispara cada 1000us = 1ms, autoreload infinito

  Serial.println("timestamp_ms,ch1_pct,ch2_pct,diferencia_pct,estado");
}

void loop() {
  if (!banderaMuestreo) return;  // no ha llegado el siguiente tick del timer
  banderaMuestreo = false;

  // --- 1) ADQUISICION ---
  int crudo1 = analogRead(PIN_SENSOR_1);
  int crudo2 = analogRead(PIN_SENSOR_2);

  // --- 2) CALIBRACION (escala comun 0-100%) ---
  float pct1 = calibrarCanal(crudo1, ADC_MIN_CH1, ADC_MAX_CH1);
  float pct2 = calibrarCanal(crudo2, ADC_MIN_CH2, ADC_MAX_CH2);
  float diferencia = fabs(pct1 - pct2);

  // --- 3) LOGICA DE SEGURIDAD (plausibilidad + debounce de 100 ms) ---
  bool hayDiscrepancia = (diferencia > UMBRAL_PLAUSIBILIDAD_PCT);
  unsigned long ahora = millis();

  if (hayDiscrepancia) {
    if (!fallaEnCurso) {
      // primera vez que se detecta la discrepancia: arrancamos el reloj
      fallaEnCurso = true;
      tiempoInicioFalla = ahora;
      estadoActual = ESTADO_FALLA_PENDIENTE;
    } else if (ahora - tiempoInicioFalla > TIEMPO_DEBOUNCE_MS) {
      // la discrepancia lleva mas de 100 ms de forma continua -> falla real
      estadoActual = ESTADO_FALLA_CONFIRMADA;
    }
    // si no ha pasado el tiempo de debounce, se queda en FALLA_PENDIENTE
    // (no se corta el motor todavia, podria ser ruido de un solo sample)
  } else {
    // los canales vuelven a coincidir: se resetea todo, no hay memoria de falla
    fallaEnCurso = false;
    estadoActual = ESTADO_OK;
  }

  // --- 4) ACTUACION SOBRE SALIDAS ---
  if (estadoActual == ESTADO_FALLA_CONFIRMADA) {
    digitalWrite(PIN_LED_ALERTA, HIGH);
    digitalWrite(PIN_LED_MOTOR, LOW);          // motor cortado
    digitalWrite(PIN_SENAL_INTERRUPCION, HIGH); // senal de interrupcion activa
  } else {
    digitalWrite(PIN_LED_ALERTA, LOW);
    digitalWrite(PIN_LED_MOTOR, HIGH);         // motor habilitado
    digitalWrite(PIN_SENAL_INTERRUPCION, LOW);
  }

  // --- 5) REGISTRO / MONITOREO (Serial) ---
  Serial.print(ahora);
  Serial.print(",");
  Serial.print(pct1, 2);
  Serial.print(",");
  Serial.print(pct2, 2);
  Serial.print(",");
  Serial.print(diferencia, 2);
  Serial.print(",");
  switch (estadoActual) {
    case ESTADO_OK:                Serial.println("OK"); break;
    case ESTADO_FALLA_PENDIENTE:   Serial.println("PENDIENTE"); break;
    case ESTADO_FALLA_CONFIRMADA:  Serial.println("FALLA_CONFIRMADA"); break;
  }
}
