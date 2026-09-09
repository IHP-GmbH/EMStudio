from pathlib import Path
import math
import json
import numpy as np

base_dir = Path(__file__).resolve().parent / "palace_model"
A = base_dir / "palace_cmim_data/output/palace_cmim"
B = base_dir / "palace_cmim_tm1fix_data/output/palace_cmim_tm1fix"


def load_s2p(path):
    rows = []
    for line in open(path):
        line = line.strip()
        if not line or line.startswith("!") or line.startswith("#"):
            continue
        rows.append([float(x) for x in line.split()])
    return np.array(rows)


def toc(m, a):
    mag = 10 ** (m / 20)
    ar = math.radians(a)
    return mag * complex(math.cos(ar), math.sin(ar))


def metrics(path, f_targets=(1, 10, 50, 100)):
    d = load_s2p(path)
    out = []
    for ft in f_targets:
        i = int(np.argmin(np.abs(d[:, 0] - ft)))
        r = d[i]
        f = r[0]
        s11, s21, s12, s22 = toc(r[1], r[2]), toc(r[3], r[4]), toc(r[5], r[6]), toc(r[7], r[8])
        S = np.array([[s11, s12], [s21, s22]])
        Y = (1 / 50) * (np.eye(2) - S) @ np.linalg.inv(np.eye(2) + S)
        w = 2 * math.pi * f * 1e9
        C = -Y[1, 0].imag / w * 1e15
        out.append(
            dict(
                f=f,
                C=C,
                s21db=20 * math.log10(abs(s21)),
                s21ang=math.degrees(math.atan2(s21.imag, s21.real)),
            )
        )
    return out


def elapsed(pj):
    d = json.loads(Path(pj).read_text())
    et = d.get("ElapsedTime", {})
    dur = et.get("Durations", {}) if isinstance(et, dict) else {}
    return dur.get("Total", et)


print("Elapsed baseline:", elapsed(A / "palace.json"))
print("Elapsed tm1fix :", elapsed(B / "palace.json"))
print()

pairs = [
    ("raw", A / "palace_cmim.s2p", B / "palace_cmim_tm1fix.s2p"),
    ("deembed", A / "palace_cmim_deembedded.s2p", B / "palace_cmim_tm1fix_deembedded.s2p"),
]

for label, pa, pb in pairs:
    ma, mb = metrics(pa), metrics(pb)
    print(f"=== {label} ===")
    print(
        f'{"f GHz":>8} {"C base":>10} {"C fix":>10} {"dC%":>8} '
        f'{"|S21| base":>12} {"|S21| fix":>12} {"dS21 dB":>9}'
    )
    for a, b in zip(ma, mb):
        dC = (b["C"] - a["C"]) / a["C"] * 100
        dS = b["s21db"] - a["s21db"]
        print(
            f"{a['f']:8g} {a['C']:10.3f} {b['C']:10.3f} {dC:8.2f} "
            f"{a['s21db']:12.2f} {b['s21db']:12.2f} {dS:9.3f}"
        )
    print()
