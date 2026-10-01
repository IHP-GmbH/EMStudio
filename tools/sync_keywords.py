#!/usr/bin/env python3
"""Keep keywords/ in sync with the gds2palace and gds2openEMS sources.

  sync_keywords.py signatures <gds2palace_dir> <gds2openems_modules_dir>
      Rewrites keywords/workflow_signatures.csv from the workflow function definitions
      (parsed with ast, nothing is imported).

  sync_keywords.py check-defaults <gds2palace_dir> <gds2openems_modules_dir>
      Reports keyword-file defaults that don't match any default in the sources
      (get_optional_setting(settings, 'key', default) / settings.get('key', default)).
      Same key, different flow: several defaults can be valid (e.g. 'iterative'), so this
      only reports, it doesn't rewrite.
"""

import ast
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
KEYWORDS = os.path.join(HERE, "..", "keywords")

# Workflow functions whose arguments give loose variables their meaning, and the
# parameter -> keyword names where they differ.
FUNCTIONS = {
    "read_gds": ("util_gds_reader.py", {"purposelist": "purpose", "preprocess": "preprocess_gds"}),
    "read_substrate": ("util_stackup_reader.py", {}),
    "setupSimulation": ("util_simulation_setup.py", {}),
    "runSimulation": ("util_simulation_setup.py", {}),
}


def keyword_names():
    names = set()
    for f in os.listdir(KEYWORDS):
        if f.endswith(".csv") and f != "workflow_signatures.csv":
            with open(os.path.join(KEYWORDS, f), encoding="utf-8") as fh:
                names.update(line.split("\t")[0].strip() for line in fh if line.strip())
    return names


def function_params(path, name):
    tree = ast.parse(open(path, encoding="utf-8").read(), path)
    for node in ast.walk(tree):
        if isinstance(node, ast.FunctionDef) and node.name == name:
            return [a.arg for a in node.args.args]
    return None


def signatures(palace_dir, openems_dir):
    known = keyword_names()
    rows = {}
    for func, (module, rename) in FUNCTIONS.items():
        for src in (palace_dir, openems_dir):
            path = os.path.join(src, module)
            if not os.path.exists(path):
                continue
            params = function_params(path, func)
            if params is None:
                continue
            for index, param in enumerate(params):
                keyword = rename.get(param, param)
                if keyword in known:
                    rows.setdefault((func, index, param), keyword)
    out = os.path.join(KEYWORDS, "workflow_signatures.csv")
    with open(out, "w", encoding="utf-8") as fh:
        for (func, index, param), keyword in sorted(rows.items(), key=lambda r: (r[0][0], r[0][1])):
            fh.write(f"{func}\t{index}\t{param}\t{keyword}\n")
    print(f"wrote {len(rows)} rows to {os.path.normpath(out)}")


def source_defaults(*dirs):
    found = {}
    pat = re.compile(r"""(?:get_optional_setting\s*\(\s*settings\s*,|settings\.get\s*\()\s*['"](\w+)['"]\s*,\s*([^)\n]+?)\s*\)""")
    for d in dirs:
        for f in os.listdir(d):
            if f.endswith(".py"):
                for key, default in pat.findall(open(os.path.join(d, f), encoding="utf-8").read()):
                    found.setdefault(key, set()).add(default.replace(" ", ""))
    return found


def check_defaults(palace_dir, openems_dir):
    found = source_defaults(palace_dir, openems_dir)
    problems = 0
    for f in sorted(os.listdir(KEYWORDS)):
        if not f.endswith(".csv") or f == "workflow_signatures.csv":
            continue
        for line in open(os.path.join(KEYWORDS, f), encoding="utf-8"):
            cols = line.rstrip("\n").split("\t")
            if len(cols) < 4 or not cols[3]:
                continue
            key, default = cols[0], cols[3].replace(" ", "")
            if key in found and default not in found[key]:
                problems += 1
                print(f"{f}: {key} = {cols[3]}, source has {sorted(found[key])}")
    print("defaults ok" if problems == 0 else f"{problems} default(s) differ")
    return problems


if __name__ == "__main__":
    if len(sys.argv) != 4 or sys.argv[1] not in ("signatures", "check-defaults"):
        sys.exit(__doc__)
    if sys.argv[1] == "signatures":
        signatures(sys.argv[2], sys.argv[3])
    else:
        sys.exit(1 if check_defaults(sys.argv[2], sys.argv[3]) else 0)
