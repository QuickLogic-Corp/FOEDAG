def main():
    parser = argparse.ArgumentParser(description="Update FPGA layout section in architecture file.")
    parser.add_argument('--arch_file', required=True, help='Path to architecture file (i.e., vpr.xml)')
    parser.add_argument('--device_config',
                        help='Path to the device data config.json holding the layout settings.')
    parser.add_argument('--layout_config',
                        help='Path to a config file. A .json file is read as a device config, a '
                             '.yml file as the legacy layout_config.yml.')
    parser.add_argument('--vpr_stdout_log', help='Path to vpr_stdout.log file used for AUTO mode.')
    parser.add_argument('--custom_layout', help=argparse.SUPPRESS)
