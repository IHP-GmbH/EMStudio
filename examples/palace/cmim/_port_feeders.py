"""Annotate port_information.json with feeder lengths for combine_extend_snp.py."""

import json
import os


def annotate_port_feeders(sim_path, feeders_um, feeder_er=2.5):
    """Write feeder_length / feeder_er / feeder_z0 into port_information.json.

    feeders_um: {portnumber: length_um} from port footprint to DUT lead edge.
    combine_extend_snp.py cascades a negative TL of this length (* sqrt(er)).
    """
    path = os.path.join(sim_path, 'port_information.json')
    if not os.path.isfile(path):
        print('WARNING: missing', path, '- skip feeder annotation')
        return
    with open(path, 'r', encoding='utf-8') as f:
        data = json.load(f)
    for port in data.get('ports', []):
        n = port.get('portnumber')
        if n not in feeders_um:
            continue
        port['feeder_length'] = float(feeders_um[n])
        port['feeder_er'] = float(feeder_er)
        port['feeder_z0'] = float(port.get('Z0', 50))
        print(f"  port {n}: feeder_length={port['feeder_length']} um, "
              f"feeder_er={port['feeder_er']}, feeder_z0={port['feeder_z0']}")
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(data, f, ensure_ascii=False, indent=4)
    print('Annotated feeders in', path)
