# 50 GHz MPA core (no BJT)

Palace model of a 50 GHz MPA core layout without BJT devices. Sweep 1–350 GHz
with via merge (`merge_polygon_size = 1.2`).

## Files

| File | Role |
|------|------|
| `palace_core_dev.py` | Palace model |
| `50_ghz_mpa_core_no_BJT.gds` | Layout + ports (unmerged; zip had no `*_merged.gds`) |
| `SG13G2_200um.xml` | Stackup (script originally pointed at `SG13G2_100um.xml`) |

Top cell: `50_ghz_mpa_core`.

Ports: 1–2 via (Metal3→TopMetal2), 3–4 in-plane Metal2 (±X).

## Run in EMStudio

1. Preferences: `PALACE_PYTHON` with `gds2palace` (not a local `gds2palace_dev` copy).
2. File → Open Python Model… → `palace_core_dev.py`.
3. Simulate → Run.
