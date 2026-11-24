#!/usr/bin/env python3
"""Ensure runtime weather uses the exporter's exact Jaguar CRY reference table."""
import ast,json
from pathlib import Path
root=Path(__file__).resolve().parents[1]
source=json.loads((root/'tools/cry_palette.json').read_text())
header=(root/'include/jaguar_cry.hpp').read_text()
body=header.split('jaguarCryChannels[3][16][16]=',1)[1].split(';',1)[0]
actual=ast.literal_eval(body.replace('{','[').replace('}',']'))
assert actual==[source[channel] for channel in ('red','green','blue')]
print('Runtime Jaguar CRY table matches the cartridge-art exporter reference')
