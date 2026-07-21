# GPSDO Tuner

📖 [Project home](https://github.com/jmnlabs/GPSDO_FreeRTOS) · [Manual](README_EN.md) · Languages: **EN** · [PL](gpsdo_tuner_PL.md) · [ES](gpsdo_tuner_ES.md)

A live tuning and phase-visualisation GUI for the GPSDO_FreeRTOS firmware.

Every OCXO has a different EFC gain, every phase detector a different Vsat and
noise floor, every GPS/Rb reference its own character. Rather than chase one set
of compile-time defaults that can never suit every board, this tool puts the
firmware's tuning commands behind direct controls so each builder trims the loop
to their own hardware — reading each parameter back from the device, writing it
live, and committing with `ES` or reverting with `ER`.

## What it does

- **Live plots** — phase (`dph`), detector voltage (`Vphase`, with anchor and
  15–85 % Vsat band guides drawn from the calibration), and frequency error.
- **LTIC panel** — the three-stage ACQ / DPLL / LOCK PID, read from `LL` and
  written live via the `AQ*` / `DP*` / `LK*` verbs.
- **FA panel** — the DPLL and LOCK damping windows (`FAD` / `FAL`), the
  acquisition/steady-state split for chasing a limit cycle.
- **PID panel** — `KP/KI/KD/IL` for the classic algorithms 3–9.
- **Calibration panel** — `LNV/LZO/LRN/LCV/LAT/LIV` and `LPOL`.
- **Raw monitor + command line** — any firmware command, plus quick buttons.

The controls are deliberately direct: this is a bench tuning tool for people who
know their hardware. A live PID change can unsettle a working lock — read the
current values first (the panels do this on connect), change one thing at a time,
and keep `ER` handy to reload the last saved set from EEPROM.

## Install & run

```
pip install pyserial pyqtgraph PySide6
python gpsdo_tuner.py
```

Pick the serial port, Connect, and the panels populate from the device.

## Credits

Inspired by **lucido**'s `GPSDO_log.py` — the live Vphase/Vctl/dPh/qErr logger
with PyQtGraph and a serial TX line. This tool grew from that idea and keeps its
overall shape (serial worker thread, configurable live plots, a command line and
a raw monitor); the tuning panels, parameter read-back and phase-ramp visualiser
are new here. Firmware by J. M. Niewiński (jmnlabs), from André Balsa's v0.06c.
