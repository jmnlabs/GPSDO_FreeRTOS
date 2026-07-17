# Buffer circular en Flash — procedimiento de puesta en marcha

[English](FLASH_RING_BRINGUP_EN.md) | [Polski](FLASH_RING_BRINGUP_PL.md) | **Español**

📖 [Inicio del proyecto](../README.md) · [Manual](README_ES.md)

El buffer circular con nivelación de desgaste guarda los datos «vivos»
(deriva/amortiguación aprendida, calibración LC, último PWM) en el **sector 6**
del Flash (0x08040000, 128 KB), separado del firmware y de la EEPROM de ajustes
(sector 7). Se activa **en tiempo de ejecución** con `FR 0|1` (guardado con
`ES`, activo por defecto) — sin flag de compilación, así que sin líos de caché
de compilación.

La lógica del núcleo está probada (28 aserciones en PC). La primera vez,
ponlo en marcha con cuidado y con una copia de seguridad.

## 0. Primero la copia de seguridad (recomendado)

Con el J-Link (o ST-Link), vuelca todo el Flash para poder restaurar si hace falta:

```
JLinkExe -device STM32F411CE -if SWD -speed 4000
> savebin backup_full.bin 0x08000000 0x80000
> exit
```

Para restaurar después: `loadbin backup_full.bin 0x08000000`.

## 1. Confirma que el firmware cabe por debajo del sector 6

Revisa la línea de tamaño: `Sketch uses NNNNNN bytes ...`

`NNNNNN` debe quedar **por debajo de 262144** (0x08040000 − 0x08000000): ahí
empiezan el sector 6 y el ring. v0.95 mide 216976 B (212 KB), con ~44 KB de
margen; v0.90 eran ~170 KB, así que crece.

Ignora el porcentaje que imprime el IDE: lo calcula sobre los 512 KB completos,
así que v0.95 aparece como «41%» — pero los sectores 6 y 7 están ocupados (ring
y EEPROM), y frente a los 256 KB que el firmware puede usar realmente, la cifra
verdadera es 83%.

Si un build futuro se acerca a 256 KB,
mueve el anillo o reduce el firmware **antes** de programar.

## 2. Activa el anillo, observa `EW`

El anillo está **activo** por defecto. Para ser explícito, por el puerto serie:

```
FR 1
ES
```

Luego `EW`. En un sector virgen espera:

```
Flash ring: erase cycles=1  slots used=0/4095  (sector 6, 0x08040000)
```

`erase cycles=1` es normal: el primer `begin` no encuentra una cabecera válida,
borra una vez y coloca una nueva. `slots used=0` porque aún no se ha
auto-guardado nada.

## 3. Fuerza un guardado, confirma la persistencia

Deja que LRN acumule deriva por encima del umbral de histéresis (> 8 LSB), o
ejecuta una calibración `LC` exitosa (guarda de inmediato). Entonces `EW`
debería mostrar `slots used=1`. Apaga y enciende la unidad. Al arrancar deberías ver:

```
Flash ring: live data recalled
Live store: LRN + LC applied from flash ring
```

Ejecuta `EW` otra vez — sigue en `slots used=1`, y los valores
aprendidos/calibración están restaurados. Esto prueba que la escritura
sobrevivió a un reinicio — de eso se trata.

## 4. Seguridad ante basura/borrado (opcional, a fondo)

Borra con el J-Link solo el sector 6 y reinicia:

```
> erase 0x08040000 0x0805FFFF
```

Al arrancar, el firmware debe detectar el sector en blanco, reinicializar el
anillo (el contador de borrados aumenta) y arrancar con los valores por
defecto — sin colgarse, sin basura. Verifica la ruta de robustez.

## 5. Desactivación

`FR 0` + `ES` detiene toda la actividad del anillo: sin lecturas, escrituras ni
borrados. Los valores aprendidos/calibración viven entonces solo en RAM y se
pierden al reiniciar. Reactiva en cualquier momento con `FR 1` + `ES`.

## 6. Reprogramar el firmware más tarde (importante)

- **Bootloader / DFU / Arduino IDE** solo toca los sectores del firmware (0–5);
  el anillo (6) y la EEPROM de ajustes (7) sobreviven.
- **Borrado total del chip con J-Link/ST-Link** borra todo. Para conservar la
  calibración y el aprendizaje, borra solo los sectores 0–5:
  ```
  > erase 0x08000000 0x0803FFFF
  > loadbin firmware.bin 0x08000000
  ```
- Si el anillo se borra, el firmware reaprende/recalibra desde los valores por
  defecto — nada se rompe, solo se pierde el ajuste acumulado.

## Reversión

Si algo va mal, `FR 0` + `ES` desactiva el anillo de inmediato (sin
reprogramar). Restaura `backup_full.bin` si el contenido del Flash se vio afectado.
