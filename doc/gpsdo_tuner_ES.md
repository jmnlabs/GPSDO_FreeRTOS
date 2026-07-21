# GPSDO Tuner

📖 [Inicio del proyecto](https://github.com/jmnlabs/GPSDO_FreeRTOS) · [Manual](README_ES.md) · Idiomas: [EN](gpsdo_tuner_EN.md) · [PL](gpsdo_tuner_PL.md) · **ES**

Una GUI de ajuste en vivo y visualización de fase para el firmware GPSDO_FreeRTOS.

Cada OCXO tiene una ganancia EFC distinta, cada detector de fase un Vsat y un
suelo de ruido distintos, cada referencia GPS/Rb su propio carácter. En lugar de
perseguir un único conjunto de valores por defecto de compilación que nunca podrá
servir a todas las placas, esta herramienta pone los comandos de ajuste del
firmware tras controles directos — para que cada constructor ajuste el lazo a su
propio hardware: leyendo cada parámetro del dispositivo, escribiéndolo en vivo y
confirmando con `ES` o revirtiendo con `ER`.

## Qué hace

- **Gráficas en vivo** — fase (`dph`), tensión del detector (`Vphase`, con guías
  de ancla y de la banda 15–85 % Vsat dibujadas a partir de la calibración) y
  error de frecuencia.
- **Panel LTIC** — el PID de tres etapas ACQ / DPLL / LOCK, leído de `LL` y
  escrito en vivo mediante los comandos `AQ*` / `DP*` / `LK*`.
- **Panel FA** — las ventanas de amortiguación DPLL y LOCK (`FAD` / `FAL`), la
  separación entre adquisición y estado estacionario para rastrear un ciclo
  límite.
- **Panel PID** — `KP/KI/KD/IL` para los algoritmos clásicos 3–9.
- **Panel de calibración** — `LNV/LZO/LRN/LCV/LAT/LIV` y `LPOL`.
- **Monitor en bruto + línea de comandos** — cualquier comando del firmware, más
  botones rápidos.

Los controles son deliberadamente directos: es una herramienta de banco para
quien conoce su hardware. Un cambio de PID en vivo puede desestabilizar un lock
en funcionamiento — lee primero los valores actuales (los paneles lo hacen al
conectar), cambia una cosa a la vez y ten `ER` a mano para recargar el último
conjunto guardado desde EEPROM.

## Instalación y ejecución

```
pip install pyserial pyqtgraph PySide6
python gpsdo_tuner.py
```

Elige el puerto serie, pulsa Connect y los paneles se rellenan desde el
dispositivo.

## Créditos

Inspirado en `GPSDO_log.py` de **lucido** — el registrador en vivo de
Vphase/Vctl/dPh/qErr con PyQtGraph y una línea TX serie. Esta herramienta creció
a partir de esa idea y conserva su forma general (hilo de trabajo serie, gráficas
en vivo configurables, una línea de comandos y un monitor en bruto); los paneles
de ajuste, la lectura de parámetros y el visualizador de la rampa de fase son
nuevos aquí. Firmware de J. M. Niewiński (jmnlabs), a partir de la v0.06c de
André Balsa.
