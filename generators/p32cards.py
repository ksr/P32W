#!/usr/bin/env python3
"""P32W's cards (D49, D50; docs/architecture.md): importing this module
defines every card's netlist in p32net.CARDS, for the schematic, KiCad and
board generators. Each card is a module card_NAME.py here, listed in
CARD_MODULES, whose build() defines it.
"""
import os, sys, importlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

CARD_MODULES = ["card_cpu", "card_mmu", "card_mem", "card_io", "card_disk"]

for _m in CARD_MODULES:
    importlib.import_module(_m).build()
