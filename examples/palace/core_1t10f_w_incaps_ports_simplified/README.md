# Core 1T10F with in-caps / ports (simplified GDS)

Palace model of a multi-port RF core with in-package caps. 
Via array merging was already done upstream.  

## Files

| File | Role |
|------|------|
| `palace_core_1t10f_w_incaps_ports_viamerge.py` | Palace model (0–50 GHz, 5 GHz step, order 2) |
| `core_1t10f_w_incaps_ports_simplified.gds` | Layout + ports |
| `SG13G2_200um.xml` | Stackup |

Top cell: `core_1t10f_w_incaps_merged_by_layer`.

Ports: 1–4 via (Metal3→TopMetal2), 5–6 in-plane Metal2 (±X).

## Run in EMStudio

1. Preferences: `PALACE_PYTHON` with `gds2palace`.
2. File → Open Python Model… → `palace_core_1t10f_w_incaps_ports_viamerge.py`.
3. Simulate → Run.
