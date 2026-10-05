from _standin_common import all_simulation_ports, record, simulation_port  # noqa: F401


def create_palace(excite_ports, settings):
    record("create_palace", excite_ports=[p.portnumber for p in excite_ports],
           settings_keys=sorted(settings.keys()))
    return "config.json", "data_dir"
