#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
GPSDO Tuner — live parameter tuning and phase visualisation
============================================================

A real-time control panel for the GPSDO_FreeRTOS firmware. It reads the serial
telemetry, plots phase / control voltage / frequency error live, and exposes the
firmware's tuning commands (PID gains for every algorithm, the LTIC three-stage
loop, the FA damping windows, and the detector calibration) as direct controls —
so each builder can trim the loop to their own OCXO and phase detector instead of
chasing a single set of compile-time defaults that can never suit every board.

Every parameter is read back from the firmware before you touch it, written live
with a single click, and can be reverted from EEPROM (`ER`) or committed with
`ES`. The controls are deliberately direct: this is a bench tuning tool for people
who know their hardware, not a guard-railed appliance.

Credits
-------
Inspired by GPSDO_log.py by "lucido" (the live Vphase/Vctl/dPh/qErr logger with
PyQtGraph and a serial TX line) — this tool grew out of that idea and reuses its
overall shape: a serial worker thread, configurable live plots, a command line
and a raw monitor. The tuning panels, the parameter read-back, and the phase-ramp
visualiser are new here.

  Original logger .............. lucido
  GPSDO_FreeRTOS firmware ...... J. M. Niewiński (jmnlabs), from André Balsa's
                                 v0.06c Arduino GPSDO
  This tuning tool ............. built for the jmnlabs GPSDO project

Dependencies:  pip install pyserial pyqtgraph PySide6

Usage:  python gpsdo_tuner.py

Full docs: doc/gpsdo_tuner_EN.md (also PL, ES).
"""

import sys
import re
import time
from collections import deque, defaultdict

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("pyserial not found — run: pip install pyserial")
    sys.exit(1)

try:
    import pyqtgraph as pg
    from PySide6.QtCore import Qt, QThread, Signal, QTimer
    from PySide6.QtWidgets import (
        QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
        QGridLayout, QLabel, QLineEdit, QPushButton, QComboBox, QTextEdit,
        QDoubleSpinBox, QSpinBox, QTabWidget, QGroupBox, QSplitter, QCheckBox,
        QMessageBox,
    )
    from PySide6.QtGui import QFont
except ImportError:
    print("PySide6 / pyqtgraph not found — run: pip install pyqtgraph PySide6")
    sys.exit(1)


# ------------------------------------------------------------------------------
# Firmware command registry
# ------------------------------------------------------------------------------
# Each tuning parameter maps to a firmware CLI verb. The firmware accepts the
# verb with no argument to READ (it prints the value), or with an argument to
# WRITE. The GUI uses exactly the same grammar so the two never drift apart.
#
# PID for algorithms 3-9:  KP/KI/KD/IL <algo> <value>   (read back with LP <algo>)
# LTIC three-stage PID:    ACQ  -> AQP/AQI/AQD/AQL <value>
#                          DPLL -> DPP/DPI/DPD/DPL <value>
#                          LOCK -> LKP/LKI/LKD/LKL <value>
# LTIC calibration:        LNV/LZO/LRN <value>,  LPOL -1|0|1,  LCV <value>
# LTIC thresholds:         LAT (acq ns), LIV (lock interval s)
# FA damping windows:      FAD/FAL/FA 10|100|1000
# ------------------------------------------------------------------------------

# LTIC stage PID verbs: (Kp, Ki, Kd, I_limit)
LTIC_STAGE_VERBS = {
    "ACQ":  ("AQP", "AQI", "AQD", "AQL"),
    "DPLL": ("DPP", "DPI", "DPD", "DPL"),
    "LOCK": ("LKP", "LKI", "LKD", "LKL"),
}

# LTIC calibration verbs and their sensible ranges (lo, hi, decimals)
LTIC_CAL = {
    "LNV": ("ns_per_volt", 0.0, 1e6, 3),
    "LZO": ("zero_offset V", 0.0, 3.3, 4),
    "LRN": ("range_ns (not self-learn)", 0.0, 1e9, 2),
    "LCV": ("centre_v", 0.0, 3.3, 3),
    "LAT": ("acq_thresh ns", 1.0, 5000.0, 2),
    "LIV": ("lock_interval s", 1.0, 3600.0, 0),
}

FA_VALUES = ["10", "100", "1000"]


# ------------------------------------------------------------------------------
# Telemetry parser
# ------------------------------------------------------------------------------
class TelemetryParser:
    """Pulls numeric fields out of the firmware's serial lines.

    Field extraction is deliberately case-insensitive and tolerant of the
    firmware's spacing, matching the same approach lucido's logger used so that
    a rename like dPh->dph never breaks a plot. It also parses the LL and LP
    read-backs so the tuning panels can show the firmware's current values.
    """

    # Live telemetry fields -> label used on plots / spin boxes
    LIVE_FIELDS = ["Vphase", "Vctl", "dph", "qErr", "PWM", "drift", "damp"]

    def extract(self, line, name):
        """Return the first number following `name` on the line, or None."""
        pattern = re.escape(name) + r"\s*[:=]?\s*([-+]?\d+(?:\.\d+)?)"
        m = re.search(pattern, line, re.IGNORECASE)
        return float(m.group(1)) if m else None

    def parse_state(self, line):
        """Loop state from the PWM/Vctl telemetry line, e.g. '... LOCK'."""
        for st in ("LOCK", "DPLL", "ACQ", "SURVEY", "WARMUP", "HOLD"):
            if re.search(r"\b" + st + r"\b", line):
                return st
        return None

    def parse_freq(self, line):
        """Best available frequency estimate from a 'Freq:' line."""
        m = re.search(r"1ks:\s*([\d.]+)", line)
        if m:
            return float(m.group(1))
        m = re.search(r"100s:\s*([\d.]+)", line)
        if m:
            return float(m.group(1))
        m = re.search(r"Freq:\s*([\d.]+)", line)
        return float(m.group(1)) if m else None

    def parse_ll(self, text):
        """Parse an LL read-back block into {stage: {Kp,Ki,Kd,IL}, cal:{...}}.

        The firmware prints LL with one field per line — the ACQ/DPLL/LOCK label
        appears with its Kp on one line, then Ki, Kd, IL each follow on their own
        lines. So this scans line by line, tracking which stage the Kp most
        recently named, and files the following Ki/Kd/IL under it. It also picks
        up the cal fields (LNV/LZO/LRN/...) wherever they land.
        """
        out = {"ACQ": {}, "DPLL": {}, "LOCK": {}, "cal": {}}
        current = None
        for raw in text.splitlines():
            line = raw.strip()
            # a stage line names the stage and carries its Kp
            m = re.match(r"(ACQ|DPLL|LOCK):\s*Kp=([-+]?[\d.]+)", line, re.IGNORECASE)
            if m:
                current = m.group(1).upper()
                out[current]["Kp"] = float(m.group(2))
                continue
            # Ki/Kd/IL on their own lines belong to the current stage
            if current:
                m = re.match(r"(Ki|Kd|IL)=([-+]?[\d.]+)", line, re.IGNORECASE)
                if m:
                    out[current][m.group(1)] = float(m.group(2))
                    if m.group(1).upper() == "IL":
                        current = None   # IL is the last field of a stage
                    continue
            # cal / threshold / polarity fields, wherever they appear
            for key, rx in (("LNV", r"LNV=([-+]?[\d.]+)"),
                            ("LZO", r"LZO=([-+]?[\d.]+)"),
                            ("LRN", r"LRN=([-+]?[\d.]+)"),
                            ("LAT", r"LAT=([-+]?[\d.]+)"),
                            ("LIV", r"LIV=([-+]?\d+)"),
                            ("LPOL", r"LPOL=([-+]?\d+)"),
                            ("LCV", r"LCV=([-+]?[\d.]+)")):
                m = re.search(rx, line, re.IGNORECASE)
                if m:
                    out["cal"][key] = float(m.group(1))
        return out

    def parse_lp(self, text):
        """Parse an 'Algo N Kp=..' block; Ki/Kd/IL may be on following lines."""
        m = re.search(r"Algo\s+(\d+)\s+Kp=([-+]?[\d.]+)", text, re.IGNORECASE)
        if not m:
            return None, None
        algo = int(m.group(1))
        vals = {"Kp": float(m.group(2))}
        for k in ("Ki", "Kd", "IL"):
            mm = re.search(k + r"=([-+]?[\d.]+)", text, re.IGNORECASE)
            if mm:
                vals[k] = float(mm.group(1))
        return algo, vals

    def parse_fa(self, text):
        """Parse 'FA windows: DPLL=100s LOCK=100s'."""
        m = re.search(r"DPLL=(\d+)s?\s+LOCK=(\d+)", text, re.IGNORECASE)
        if m:
            return int(m.group(1)), int(m.group(2))
        return None, None


# ------------------------------------------------------------------------------
# Serial worker thread
# ------------------------------------------------------------------------------
# A background QThread owns the serial port: it reads lines and emits them, and
# accepts outgoing commands via send(). Keeping all port I/O off the GUI thread
# is the same pattern lucido's logger used, and it keeps the plots smooth while
# commands are flying back and forth.
class SerialWorker(QThread):
    line_received = Signal(str)
    connection_changed = Signal(bool, str)

    def __init__(self):
        super().__init__()
        self.ser = None
        self.port = None
        self.baud = 115200
        self._running = False
        self._tx_queue = deque()

    def configure(self, port, baud):
        self.port = port
        self.baud = baud

    def send(self, text):
        """Queue a command for transmission (CR-terminated)."""
        if not text.endswith("\r") and not text.endswith("\n"):
            text += "\r\n"
        self._tx_queue.append(text.encode("ascii", errors="ignore"))

    def stop(self):
        self._running = False

    def run(self):
        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=0.1)
        except serial.SerialException as e:
            self.connection_changed.emit(False, str(e))
            return
        self._running = True
        self.connection_changed.emit(True, self.port)
        buf = b""
        while self._running:
            # drain TX queue
            while self._tx_queue:
                try:
                    self.ser.write(self._tx_queue.popleft())
                except serial.SerialException:
                    pass
            # read available bytes, split into lines
            try:
                data = self.ser.read(256)
            except serial.SerialException:
                break
            if data:
                buf += data
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    text = line.decode("utf-8", errors="replace").rstrip("\r")
                    if text:
                        self.line_received.emit(text)
            else:
                time.sleep(0.01)
        try:
            if self.ser and self.ser.is_open:
                self.ser.close()
        except Exception:
            pass
        self.connection_changed.emit(False, "disconnected")


# ------------------------------------------------------------------------------
# Main window
# ------------------------------------------------------------------------------
class GpsdoTuner(QMainWindow):
    MAXPTS = 3600          # ~1 h at 1 Hz telemetry

    def __init__(self):
        super().__init__()
        self.setWindowTitle("GPSDO Tuner — live tuning & phase visualisation")
        self.resize(1280, 820)

        self.parser = TelemetryParser()
        self.worker = SerialWorker()
        self.worker.line_received.connect(self.on_line)
        self.worker.connection_changed.connect(self.on_conn)

        # rolling data buffers
        self.t0 = time.time()
        self.data = defaultdict(lambda: deque(maxlen=self.MAXPTS))
        self.tbuf = deque(maxlen=self.MAXPTS)
        self.last_state = "?"
        self._monitor_lines = 0
        self._ll_buf = []
        self._ll_active = False
        self._lp_buf = []
        self._lp_active = False

        # widgets that read-back panels populate (verb -> spinbox)
        self.ltic_boxes = {}     # (stage, k) -> spinbox
        self.cal_boxes = {}      # verb -> spinbox
        self.pid_boxes = {}      # k -> spinbox (for the currently shown algo)

        self._build_ui()

        # periodic replot (decoupled from serial rate)
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.refresh_plots)
        self.timer.start(250)

    # ---- UI construction --------------------------------------------------
    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        outer = QVBoxLayout(central)

        outer.addLayout(self._build_connect_row())

        split = QSplitter(Qt.Orientation.Horizontal)
        split.addWidget(self._build_plot_area())
        split.addWidget(self._build_control_tabs())
        split.setStretchFactor(0, 3)
        split.setStretchFactor(1, 2)
        outer.addWidget(split, 1)

        outer.addLayout(self._build_command_row())

    def _build_connect_row(self):
        row = QHBoxLayout()
        self.port_combo = QComboBox()
        self.refresh_ports()
        refresh = QPushButton("Refresh")
        refresh.clicked.connect(self.refresh_ports)
        self.baud_combo = QComboBox()
        self.baud_combo.addItems(["115200", "57600", "38400", "9600"])
        self.connect_btn = QPushButton("Connect")
        self.connect_btn.clicked.connect(self.toggle_connect)
        self.status_lbl = QLabel("disconnected")
        self.state_lbl = QLabel("state: ?")
        self.state_lbl.setStyleSheet("font-weight: bold;")
        for w in (QLabel("Port:"), self.port_combo, refresh,
                  QLabel("Baud:"), self.baud_combo, self.connect_btn,
                  self.status_lbl):
            row.addWidget(w)
        row.addStretch(1)
        row.addWidget(self.state_lbl)
        return row

    def _build_plot_area(self):
        pg.setConfigOptions(antialias=True)
        w = QWidget()
        v = QVBoxLayout(w)

        self.plot_phase = pg.PlotWidget(title="Phase  dph (ns)")
        self.plot_vph = pg.PlotWidget(title="Detector Vphase (V) — ramp position")
        self.plot_freq = pg.PlotWidget(title="Frequency error (Hz, from 1ks avg)")
        for p in (self.plot_phase, self.plot_vph, self.plot_freq):
            p.showGrid(x=True, y=True, alpha=0.3)
            p.setLabel("bottom", "time", units="s")
        self.curve_phase = self.plot_phase.plot(pen=pg.mkPen("#22aa44", width=2))
        self.curve_vph = self.plot_vph.plot(pen=pg.mkPen("#2277cc", width=2))
        self.curve_freq = self.plot_freq.plot(pen=pg.mkPen("#cc7722", width=2))

        # Vphase band guides: anchor and the 15-85% Vsat window get drawn once
        # calibration is known (updated from LL). They make it obvious at a
        # glance when the detector is drifting toward a rail.
        # Guide-line pens — deliberately the most primitive form pyqtgraph
        # accepts: a colour string and an integer width. Three rounds of builder
        # testing showed every fancier form breaking on some version pairing:
        # mkPen(style=enum) crashes one pyqtgraph, a hand-built QPen is treated
        # as a *colour* by another, and a non-cosmetic QPen draws data-unit-wide
        # bands. Solid thin lines in distinct colours carry the same meaning
        # (grey = anchor, red = band edges) and work everywhere.
        self.vph_anchor = pg.InfiniteLine(angle=0, pen=pg.mkPen("#888888", width=1))
        self.vph_lo = pg.InfiniteLine(angle=0, pen=pg.mkPen("#cc3333", width=1))
        self.vph_hi = pg.InfiniteLine(angle=0, pen=pg.mkPen("#cc3333", width=1))
        for ln in (self.vph_anchor, self.vph_lo, self.vph_hi):
            self.plot_vph.addItem(ln)

        for p in (self.plot_phase, self.plot_vph, self.plot_freq):
            v.addWidget(p)
        return w

    def _build_control_tabs(self):
        tabs = QTabWidget()
        tabs.addTab(self._tab_ltic(), "LTIC (algo 10)")
        tabs.addTab(self._tab_fa(), "FA damping")
        tabs.addTab(self._tab_pid(), "PID algo 3-9")
        tabs.addTab(self._tab_cal(), "Calibration")
        tabs.addTab(self._build_monitor(), "Raw monitor")
        return tabs

    def _spin(self, lo, hi, dec, step):
        s = QDoubleSpinBox()
        s.setRange(lo, hi)
        s.setDecimals(dec)
        s.setSingleStep(step)
        s.setMinimumWidth(110)
        return s

    def _tab_ltic(self):
        """Three-stage ACQ/DPLL/LOCK PID, read from LL, written live."""
        w = QWidget()
        g = QGridLayout(w)
        g.addWidget(QLabel("<b>Stage PID — direct write, live</b>"), 0, 0, 1, 6)

        hdr = ["", "Kp", "Ki", "Kd", "I-limit", ""]
        for c, h in enumerate(hdr):
            g.addWidget(QLabel(f"<b>{h}</b>"), 1, c)

        row = 2
        for stage in ("ACQ", "DPLL", "LOCK"):
            g.addWidget(QLabel(stage), row, 0)
            for c, k in enumerate(("Kp", "Ki", "Kd", "IL"), start=1):
                box = self._spin(0.0, 100000.0, 4, 0.1)
                self.ltic_boxes[(stage, k)] = box
                g.addWidget(box, row, c)
            btn = QPushButton("Apply")
            btn.clicked.connect(lambda _, s=stage: self.apply_ltic_stage(s))
            g.addWidget(btn, row, 5)
            row += 1

        read_btn = QPushButton("Read from device (LL)")
        read_btn.clicked.connect(lambda: self.worker.send("LL"))
        g.addWidget(read_btn, row, 0, 1, 3)
        save_btn = QPushButton("Save (ES LTIC)")
        save_btn.clicked.connect(lambda: self.confirm_send("ES LTIC",
                                 "Commit LTIC tuning to EEPROM?"))
        g.addWidget(save_btn, row, 3, 1, 2)
        revert_btn = QPushButton("Revert (ER)")
        revert_btn.clicked.connect(lambda: self.confirm_send("ER",
                                   "Reload all parameters from EEPROM?"))
        g.addWidget(revert_btn, row, 5)
        row += 1

        note = QLabel("Values read back from LL. Apply writes one stage live via "
                      "AQ*/DP*/LK* verbs. Nothing is saved until ES.")
        note.setWordWrap(True)
        note.setStyleSheet("color:#666; font-size:11px;")
        g.addWidget(note, row, 0, 1, 6)
        g.setRowStretch(row + 1, 1)
        return w

    def _tab_fa(self):
        """FA damping windows — the acquisition/steady-state split."""
        w = QWidget()
        g = QGridLayout(w)
        g.addWidget(QLabel("<b>Damping-term averaging window</b>"), 0, 0, 1, 4)
        g.addWidget(QLabel("Shorter windows damp a limit cycle but pass more "
                           "short-tau noise. 100 = firmware default."),
                    1, 0, 1, 4)

        g.addWidget(QLabel("DPLL (acquisition):"), 2, 0)
        self.fa_dpll = QComboBox(); self.fa_dpll.addItems(FA_VALUES)
        self.fa_dpll.setCurrentText("100")
        g.addWidget(self.fa_dpll, 2, 1)
        b1 = QPushButton("Apply FAD")
        b1.clicked.connect(lambda: self.worker.send(f"FAD {self.fa_dpll.currentText()}"))
        g.addWidget(b1, 2, 2)

        g.addWidget(QLabel("LOCK (steady state):"), 3, 0)
        self.fa_lock = QComboBox(); self.fa_lock.addItems(FA_VALUES)
        self.fa_lock.setCurrentText("100")
        g.addWidget(self.fa_lock, 3, 1)
        b2 = QPushButton("Apply FAL")
        b2.clicked.connect(lambda: self.worker.send(f"FAL {self.fa_lock.currentText()}"))
        g.addWidget(b2, 3, 2)

        readb = QPushButton("Read (FA)")
        readb.clicked.connect(lambda: self.worker.send("FA"))
        g.addWidget(readb, 4, 0)
        saveb = QPushButton("Save (ES LTIC)")
        saveb.clicked.connect(lambda: self.confirm_send("ES LTIC",
                              "Commit FA windows to EEPROM?"))
        g.addWidget(saveb, 4, 1, 1, 2)
        g.setRowStretch(5, 1)
        return w

    def _tab_pid(self):
        """KP/KI/KD/IL for a selectable algorithm 3-9."""
        w = QWidget()
        g = QGridLayout(w)
        g.addWidget(QLabel("<b>Classic PID — algorithms 3-9</b>"), 0, 0, 1, 4)
        g.addWidget(QLabel("Algorithm:"), 1, 0)
        self.pid_algo = QComboBox()
        self.pid_algo.addItems([str(n) for n in range(3, 10)])
        g.addWidget(self.pid_algo, 1, 1)
        readb = QPushButton("Read (LP)")
        readb.clicked.connect(lambda: self.worker.send(f"LP {self.pid_algo.currentText()}"))
        g.addWidget(readb, 1, 2)

        row = 2
        for k, dec in (("Kp", 4), ("Ki", 6), ("Kd", 3), ("IL", 1)):
            g.addWidget(QLabel(k), row, 0)
            box = self._spin(0.0, 100000.0, dec, 0.1)
            self.pid_boxes[k] = box
            g.addWidget(box, row, 1)
            verb = {"Kp": "KP", "Ki": "KI", "Kd": "KD", "IL": "IL"}[k]
            btn = QPushButton(f"Apply {verb}")
            btn.clicked.connect(lambda _, vv=verb, kk=k: self.apply_pid(vv, kk))
            g.addWidget(btn, row, 2)
            row += 1

        saveb = QPushButton("Save (ES PID)")
        saveb.clicked.connect(lambda: self.confirm_send("ES PID",
                              "Commit algo 3-9 PID to EEPROM?"))
        g.addWidget(saveb, row, 0, 1, 3)
        row += 1
        note = QLabel("KP/KI/KD apply to algos 3-7; IL (I-limit) to 3-9. "
                      "Read LP first to load the current values.")
        note.setWordWrap(True)
        note.setStyleSheet("color:#666; font-size:11px;")
        g.addWidget(note, row, 0, 1, 3)
        g.setRowStretch(row + 1, 1)
        return w

    def _tab_cal(self):
        """LTIC detector calibration + polarity."""
        w = QWidget()
        g = QGridLayout(w)
        g.addWidget(QLabel("<b>LTIC detector calibration</b>"), 0, 0, 1, 3)
        row = 1
        for verb, (lbl, lo, hi, dec) in LTIC_CAL.items():
            g.addWidget(QLabel(f"{verb} — {lbl}"), row, 0)
            box = self._spin(lo, hi, dec, 0.001 if dec > 2 else 1.0)
            self.cal_boxes[verb] = box
            g.addWidget(box, row, 1)
            btn = QPushButton("Apply")
            btn.clicked.connect(lambda _, vv=verb: self.apply_cal(vv))
            g.addWidget(btn, row, 2)
            row += 1

        g.addWidget(QLabel("LPOL — polarity"), row, 0)
        self.pol_combo = QComboBox()
        self.pol_combo.addItems(["-1", "0", "+1"])
        g.addWidget(self.pol_combo, row, 1)
        polb = QPushButton("Apply LPOL")
        polb.clicked.connect(lambda: self.worker.send(
            f"LPOL {self.pol_combo.currentText().lstrip('+')}"))
        g.addWidget(polb, row, 2)
        row += 1

        readb = QPushButton("Read (LL)")
        readb.clicked.connect(lambda: self.worker.send("LL"))
        g.addWidget(readb, row, 0)
        saveb = QPushButton("Save (ES LTIC)")
        saveb.clicked.connect(lambda: self.confirm_send("ES LTIC",
                              "Commit calibration to EEPROM?"))
        g.addWidget(saveb, row, 1, 1, 2)
        g.setRowStretch(row + 1, 1)
        return w

    def _build_monitor(self):
        w = QWidget()
        v = QVBoxLayout(w)
        self.monitor = QTextEdit()
        self.monitor.setReadOnly(True)
        self.monitor.setFont(QFont("Consolas", 9))
        self.monitor.setLineWrapMode(QTextEdit.LineWrapMode.NoWrap)
        v.addWidget(self.monitor)
        clear = QPushButton("Clear")
        clear.clicked.connect(self.monitor.clear)
        v.addWidget(clear)
        return w

    def _build_command_row(self):
        row = QHBoxLayout()
        self.cmd_edit = QLineEdit()
        self.cmd_edit.setPlaceholderText("Type any firmware command (e.g. LL, FAL 10, ES LTIC) and Enter")
        self.cmd_edit.returnPressed.connect(self.send_manual)
        send = QPushButton("Send")
        send.clicked.connect(self.send_manual)
        for q in ("LL", "FA", "H", "SAW", "WU"):
            b = QPushButton(q)
            b.setMaximumWidth(52)
            b.clicked.connect(lambda _, cc=q: self.worker.send(cc))
            row.addWidget(b)
        row.addWidget(self.cmd_edit, 1)
        row.addWidget(send)
        return row

    # ---- actions ----------------------------------------------------------
    def apply_ltic_stage(self, stage):
        verbs = LTIC_STAGE_VERBS[stage]
        for verb, k in zip(verbs, ("Kp", "Ki", "Kd", "IL")):
            val = self.ltic_boxes[(stage, k)].value()
            self.worker.send(f"{verb} {val:g}")

    def apply_pid(self, verb, k):
        algo = self.pid_algo.currentText()
        val = self.pid_boxes[k].value()
        self.worker.send(f"{verb} {algo} {val:g}")

    def apply_cal(self, verb):
        val = self.cal_boxes[verb].value()
        self.worker.send(f"{verb} {val:g}")

    def send_manual(self):
        txt = self.cmd_edit.text().strip()
        if txt:
            self.worker.send(txt)
            self.cmd_edit.clear()

    def confirm_send(self, cmd, question):
        r = QMessageBox.question(self, "Confirm", question,
                                 QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        if r == QMessageBox.StandardButton.Yes:
            self.worker.send(cmd)

    # ---- serial plumbing --------------------------------------------------
    def refresh_ports(self):
        self.port_combo.clear()
        for p in serial.tools.list_ports.comports():
            self.port_combo.addItem(p.device)

    def toggle_connect(self):
        if self.worker.isRunning():
            self.worker.stop()
            self.worker.wait(1000)
            return
        port = self.port_combo.currentText()
        if not port:
            return
        # A QThread can't be restarted once its run() has returned, so make a
        # fresh worker for each connection rather than reusing the old object.
        self.worker = SerialWorker()
        self.worker.line_received.connect(self.on_line)
        self.worker.connection_changed.connect(self.on_conn)
        self.worker.configure(port, int(self.baud_combo.currentText()))
        self.worker.start()

    def on_conn(self, ok, msg):
        self.status_lbl.setText(msg if ok else f"disconnected ({msg})")
        self.connect_btn.setText("Disconnect" if ok else "Connect")
        if ok:
            # pull current parameters so the panels start populated
            QTimer.singleShot(400, lambda: self.worker.send("LL"))
            QTimer.singleShot(700, lambda: self.worker.send("FA"))

    def on_line(self, line):
        # Data arriving proves the link is live — if the status label somehow
        # missed the connect signal (e.g. a restarted worker), correct it here.
        if self.worker.isRunning() and self.connect_btn.text() == "Connect":
            self.connect_btn.setText("Disconnect")
            self.status_lbl.setText(self.worker.port or "connected")

        # raw monitor — keep it bounded without touching cursor enums (which
        # differ between Qt5/Qt6 bindings). Once it grows past the cap, drop the
        # oldest lines by rewriting from the retained tail.
        self.monitor.append(line)
        self._monitor_lines += 1
        if self._monitor_lines > 2000:
            text = self.monitor.toPlainText()
            kept = text.split("\n")[-1500:]
            self.monitor.setPlainText("\n".join(kept))
            self._monitor_lines = len(kept)
            # scroll to the bottom after the rewrite
            sb = self.monitor.verticalScrollBar()
            sb.setValue(sb.maximum())

        # live numeric fields
        now = time.time() - self.t0
        got_any = False
        for field in ("Vphase", "Vctl", "dph", "PWM", "drift", "damp", "qErr"):
            v = self.parser.extract(line, field)
            if v is not None:
                self.data[field].append(v)
                got_any = True
        f = self.parser.parse_freq(line)
        if f is not None:
            self.data["freq_err"].append((f - 10_000_000.0))
            got_any = True
        if got_any:
            self.tbuf.append(now)

        st = self.parser.parse_state(line)
        if st:
            self.last_state = st
            self.state_lbl.setText(f"state: {st}")

        # Read-back blocks arrive one field per line, so accumulate them into a
        # buffer from the header until the block ends, then parse the whole thing.
        # The LL block starts at "LTIC ... parameters:" and ends at "state=";
        # the LP block is the "Algo N" line plus its Ki/Kd/IL followers.
        if "parameters:" in line and "LTIC" in line:
            self._ll_buf = [line]
            self._ll_active = True
            return
        if getattr(self, "_ll_active", False):
            self._ll_buf.append(line)
            if "state=" in line:               # end of the LL block
                self._ll_active = False
                self._absorb_ll_block("\n".join(self._ll_buf))
            return
        if line.strip().startswith("Algo ") and "Kp=" in line:
            self._lp_buf = [line]
            self._lp_active = True
            return
        if getattr(self, "_lp_active", False):
            # Ki/Kd/IL follow the Algo line; stop at the first non-matching line
            if re.match(r"\s*(Ki|Kd|IL)=", line):
                self._lp_buf.append(line)
                if "IL=" in line:
                    self._lp_active = False
                    self._absorb_lp_block("\n".join(self._lp_buf))
                return
            else:
                self._lp_active = False
                self._absorb_lp_block("\n".join(self._lp_buf))
                # fall through to handle this line normally
        if "DPLL=" in line and "LOCK=" in line:
            d, l = self.parser.parse_fa(line)
            if d:
                self.fa_dpll.setCurrentText(str(d))
            if l:
                self.fa_lock.setCurrentText(str(l))

    def _absorb_ll_block(self, block):
        """Populate LTIC PID + calibration panels from a full LL block."""
        parsed = self.parser.parse_ll(block)
        for stage in ("ACQ", "DPLL", "LOCK"):
            for k in ("Kp", "Ki", "Kd", "IL"):
                if (stage, k) in self.ltic_boxes and k in parsed[stage]:
                    self.ltic_boxes[(stage, k)].setValue(parsed[stage][k])
        cal = parsed["cal"]
        for verb in LTIC_CAL:
            if verb in cal and verb in self.cal_boxes:
                self.cal_boxes[verb].setValue(cal[verb])
        # LPOL combo (stored as -1/0/1)
        if "LPOL" in cal:
            self.pol_combo.setCurrentText(
                {-1: "-1", 0: "0", 1: "+1"}.get(int(cal["LPOL"]), "0"))
        # Vphase band guides from the ramp geometry
        if "LZO" in cal:
            anchor = cal["LZO"]
            self.vph_anchor.setPos(anchor)
            vsat = anchor / 0.63212 if anchor > 0 else 0
            if vsat:
                self.vph_lo.setPos(0.15 * vsat)
                self.vph_hi.setPos(0.85 * vsat)

    def _absorb_lp_block(self, block):
        """Populate the PID panel from a full 'Algo N ...' block."""
        algo, vals = self.parser.parse_lp(block)
        if vals and str(algo) == self.pid_algo.currentText():
            for k in ("Kp", "Ki", "Kd", "IL"):
                if k in self.pid_boxes and k in vals:
                    self.pid_boxes[k].setValue(vals[k])

    def refresh_plots(self):
        if not self.tbuf:
            return
        t = list(self.tbuf)
        def series(name):
            d = list(self.data[name])
            n = min(len(d), len(t))
            return t[-n:], d[-n:]
        tx, ph = series("dph");     self.curve_phase.setData(tx, ph)
        tx, vp = series("Vphase");  self.curve_vph.setData(tx, vp)
        tx, fe = series("freq_err"); self.curve_freq.setData(tx, fe)

    def closeEvent(self, ev):
        if self.worker.isRunning():
            self.worker.stop()
            self.worker.wait(1000)
        ev.accept()


def main():
    app = QApplication(sys.argv)
    win = GpsdoTuner()
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
