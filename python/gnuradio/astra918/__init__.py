"""Astra918 120 ksps complex I/Q source and shared receiver controls."""
from .astra918_python import source, control
from .cat_serial import CatSerial

__all__ = ["source", "control", "AudioSource", "CatSerial", "ControlPanel"]


def __getattr__(name):
    if name == "AudioSource":
        from .audio_source import AudioSource
        return AudioSource
    if name == "ControlPanel":
        from .control_panel import ControlPanel
        return ControlPanel
    raise AttributeError(name)
