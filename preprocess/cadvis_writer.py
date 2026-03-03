"""
cadvis_writer.py — Writer for the .cadvis binary intermediate format.

Format (all integers little-endian):

  Header (12 bytes):
    [0:4]   magic         = b"CADV"
    [4:6]   version       = uint16 = 1
    [6:8]   flags         = uint16 = 0
    [8:12]  n_components  = uint32

  String pool:
    [0:4]   n_strings     = uint32
    For each string:
      [0:4]   length      = uint32
      [4:N]   utf-8 bytes

  Component records (n_components entries):
    [0:4]   name_idx      = uint32
    [4:8]   n_rects       = uint32
    [8:12]  n_edges       = uint32

    Rects (n_rects × 48 bytes):
      [0:8]   time_lo       = float64
      [8:16]  time_hi       = float64
      [16:24] out_lo        = float64
      [24:32] out_hi        = float64
      [32:36] label_idx     = uint32
      [36:40] multiplicity  = uint32
      [40:41] flags         = uint8
                bit 0 = time_lo_closed
                bit 1 = time_hi_closed
                bit 2 = out_lo_closed
                bit 3 = out_hi_closed
      [41:48] _padding      = 7 × 0x00

    Edges (n_edges × 8 bytes):
      [0:4]   from_idx      = uint32
      [4:8]   to_idx        = uint32
"""

from __future__ import annotations
import struct
from dataclasses import dataclass, field
from typing import List, Optional


@dataclass
class RectRecord:
    time_lo: float
    time_hi: float
    out_lo: float
    out_hi: float
    out_label: str          # original output string (for axis labels)
    multiplicity: int
    time_lo_closed: bool
    time_hi_closed: bool
    out_lo_closed: bool
    out_hi_closed: bool


@dataclass
class EdgeRecord:
    from_idx: int
    to_idx: int


@dataclass
class ComponentRecord:
    name: str
    rects: List[RectRecord] = field(default_factory=list)
    edges: List[EdgeRecord] = field(default_factory=list)


class CadvisWriter:
    def __init__(self) -> None:
        self._components: List[ComponentRecord] = []
        self._string_pool: List[str] = []
        self._string_index: dict[str, int] = {}

    def intern(self, s: str) -> int:
        """Add string to pool if not already present; return its index."""
        if s not in self._string_index:
            self._string_index[s] = len(self._string_pool)
            self._string_pool.append(s)
        return self._string_index[s]

    def add_component(self, comp: ComponentRecord) -> None:
        self._components.append(comp)

    def write(self, path: str) -> None:
        """Serialise all components to the .cadvis binary format."""
        # Intern all strings first (component names + output labels)
        for comp in self._components:
            self.intern(comp.name)
            for rect in comp.rects:
                self.intern(rect.out_label)

        with open(path, 'wb') as f:
            # Header
            f.write(b'CADV')
            f.write(struct.pack('<HHI', 1, 0, len(self._components)))

            # String pool
            f.write(struct.pack('<I', len(self._string_pool)))
            for s in self._string_pool:
                encoded = s.encode('utf-8')
                f.write(struct.pack('<I', len(encoded)))
                f.write(encoded)

            # Components
            for comp in self._components:
                name_idx = self._string_index[comp.name]
                f.write(struct.pack('<III', name_idx, len(comp.rects), len(comp.edges)))

                for rect in comp.rects:
                    label_idx = self._string_index[rect.out_label]
                    flags = (
                        (1 if rect.time_lo_closed else 0)
                        | (2 if rect.time_hi_closed else 0)
                        | (4 if rect.out_lo_closed  else 0)
                        | (8 if rect.out_hi_closed  else 0)
                    )
                    f.write(struct.pack('<ddddIIB7x',
                        rect.time_lo, rect.time_hi,
                        rect.out_lo,  rect.out_hi,
                        label_idx, rect.multiplicity, flags,
                    ))

                for edge in comp.edges:
                    f.write(struct.pack('<II', edge.from_idx, edge.to_idx))
