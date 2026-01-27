#!/usr/bin/env python3
"""
JSON to HDF5 Converter for ProjectMobius Simulation Data

Converts simulation JSON files to HDF5 format for more efficient loading
in Unreal Engine.

Usage:
    python json_to_hdf5_converter.py input.json output.h5
    python json_to_hdf5_converter.py input.json  # outputs to input.h5

HDF5 Schema:
    /metadata (Group with attributes)
        - duration (float)
        - sampling_rate (float)
        - max_num_entities (int)
        - is_si (bool)
        - is_deg (bool)

    /entities (Dataset - Compound Type)
        - id (int32)
        - name (variable-length string)
        - sim_time_s (float32)
        - max_speed (float32)
        - m_plane (variable-length string)
        - map (int32)

    /simulation/timesteps (Dataset - float32 array)
    /simulation/samples (Dataset - Compound Type)
        - timestep_idx (int32)
        - entity_id (int32)
        - position_x (float32)
        - position_y (float32)
        - position_z (float32)
        - rotation (float32)
        - speed (float32)
        - mode (variable-length string)

MIT License - Copyright (c) 2025 ProjectMobius contributors
"""

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional

import h5py
import numpy as np


def validate_json_structure(data: Dict[str, Any]) -> List[str]:
    """
    Validate the JSON structure matches expected simulation format.
    Returns a list of validation errors (empty if valid).
    """
    errors = []

    # Check required top-level keys
    required_keys = ['entities', 'simulation', 'metadata']
    for key in required_keys:
        if key not in data:
            errors.append(f"Missing required top-level key: '{key}'")

    if errors:
        return errors

    # Validate entities array
    if not isinstance(data['entities'], list):
        errors.append("'entities' must be an array")
    elif len(data['entities']) > 0:
        entity = data['entities'][0]
        entity_fields = ['id', 'name', 'simTimeS', 'max_speed', 'm_plane', 'map']
        for field in entity_fields:
            if field not in entity:
                errors.append(f"Entity missing required field: '{field}'")

    # Validate simulation array
    if not isinstance(data['simulation'], list):
        errors.append("'simulation' must be an array")
    elif len(data['simulation']) > 0:
        timestep = data['simulation'][0]
        if 'time' not in timestep:
            errors.append("Simulation timestep missing 'time' field")
        if 'samples' not in timestep:
            errors.append("Simulation timestep missing 'samples' field")
        elif isinstance(timestep['samples'], list) and len(timestep['samples']) > 0:
            sample = timestep['samples'][0]
            sample_fields = ['entity', 'position', 'rotation', 'speed', 'mode']
            for field in sample_fields:
                if field not in sample:
                    errors.append(f"Sample missing required field: '{field}'")
            if 'position' in sample:
                pos = sample['position']
                for coord in ['x', 'y', 'z']:
                    if coord not in pos:
                        errors.append(f"Position missing coordinate: '{coord}'")

    # Validate metadata
    if not isinstance(data['metadata'], dict):
        errors.append("'metadata' must be an object")
    else:
        meta = data['metadata']
        meta_fields = ['duration', 'sampling_rate', 'max_num_entities', 'isSI', 'isDeg']
        for field in meta_fields:
            if field not in meta:
                errors.append(f"Metadata missing required field: '{field}'")

    return errors


def convert_json_to_hdf5(json_path: Path, hdf5_path: Path, verbose: bool = True) -> bool:
    """
    Convert a JSON simulation file to HDF5 format.

    Args:
        json_path: Path to input JSON file
        hdf5_path: Path to output HDF5 file
        verbose: Print progress information

    Returns:
        True if conversion successful, False otherwise
    """
    if verbose:
        print(f"Loading JSON file: {json_path}")

    # Load JSON
    try:
        with open(json_path, 'r', encoding='utf-8') as f:
            data = json.load(f)
    except json.JSONDecodeError as e:
        print(f"Error: Invalid JSON file - {e}", file=sys.stderr)
        return False
    except IOError as e:
        print(f"Error: Cannot read file - {e}", file=sys.stderr)
        return False

    # Validate structure
    errors = validate_json_structure(data)
    if errors:
        print("Validation errors:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return False

    if verbose:
        print("JSON structure validated successfully")

    # Create HDF5 file
    try:
        with h5py.File(hdf5_path, 'w') as hf:
            # === Write Metadata ===
            if verbose:
                print("Writing metadata...")

            meta_grp = hf.create_group('metadata')
            meta = data['metadata']

            meta_grp.attrs['duration'] = float(meta.get('duration', 0.0))
            meta_grp.attrs['sampling_rate'] = float(meta.get('sampling_rate', 0.1))
            meta_grp.attrs['max_num_entities'] = int(meta.get('max_num_entities', 0))
            meta_grp.attrs['is_si'] = bool(meta.get('isSI', True))
            meta_grp.attrs['is_deg'] = bool(meta.get('isDeg', True))

            # Store additional metadata fields as needed
            if 'used_planes' in meta:
                # Store as variable-length string array
                dt = h5py.special_dtype(vlen=str)
                meta_grp.create_dataset('used_planes',
                                        data=np.array(meta['used_planes'], dtype=object),
                                        dtype=dt)

            # === Write Entities ===
            if verbose:
                print(f"Writing {len(data['entities'])} entities...")

            entities = data['entities']
            num_entities = len(entities)

            # Create compound dtype for entities
            # Use fixed-size strings for better C compatibility
            # Find max string lengths
            max_name_len = max(len(ent['name'].encode('utf-8')) for ent in entities) + 1
            max_plane_len = max(len(ent['m_plane'].encode('utf-8')) for ent in entities) + 1

            entity_dtype = np.dtype([
                ('id', np.int32),
                ('name', f'S{max_name_len}'),  # Fixed-size byte string
                ('sim_time_s', np.float32),
                ('max_speed', np.float32),
                ('m_plane', f'S{max_plane_len}'),  # Fixed-size byte string
                ('map', np.int32)
            ])

            # Build entity data array
            entity_data = np.empty(num_entities, dtype=entity_dtype)
            for i, ent in enumerate(entities):
                entity_data[i]['id'] = int(ent['id'])
                entity_data[i]['name'] = ent['name'].encode('utf-8')  # Encode to bytes for fixed string
                # simTimeS is stored as string in JSON, convert to float
                entity_data[i]['sim_time_s'] = float(ent['simTimeS']) if ent['simTimeS'] else 0.0
                entity_data[i]['max_speed'] = float(ent['max_speed'])
                entity_data[i]['m_plane'] = ent['m_plane'].encode('utf-8')  # Encode to bytes
                entity_data[i]['map'] = int(ent['map'])

            hf.create_dataset('entities', data=entity_data)

            # === Write Simulation Data ===
            if verbose:
                print(f"Writing {len(data['simulation'])} timesteps...")

            sim_grp = hf.create_group('simulation')
            simulation = data['simulation']
            num_timesteps = len(simulation)

            # Write timesteps array
            timesteps = np.array([ts['time'] for ts in simulation], dtype=np.float32)
            sim_grp.create_dataset('timesteps', data=timesteps)

            # Count total samples
            total_samples = sum(len(ts.get('samples', [])) for ts in simulation)

            if verbose:
                print(f"Writing {total_samples} total samples...")

            # Find max mode string length for fixed-size string type
            max_mode_len = 1
            for ts in simulation:
                for sample in ts.get('samples', []):
                    mode_len = len(sample['mode'].encode('utf-8')) + 1
                    if mode_len > max_mode_len:
                        max_mode_len = mode_len

            # Create compound dtype for samples with fixed-size strings
            sample_dtype = np.dtype([
                ('timestep_idx', np.int32),
                ('entity_id', np.int32),
                ('position_x', np.float32),
                ('position_y', np.float32),
                ('position_z', np.float32),
                ('rotation', np.float32),
                ('speed', np.float32),
                ('mode', f'S{max_mode_len}')  # Fixed-size byte string
            ])

            # Build samples array
            sample_data = np.empty(total_samples, dtype=sample_dtype)
            sample_idx = 0

            for ts_idx, ts in enumerate(simulation):
                for sample in ts.get('samples', []):
                    sample_data[sample_idx]['timestep_idx'] = ts_idx
                    sample_data[sample_idx]['entity_id'] = int(sample['entity'])

                    pos = sample['position']
                    sample_data[sample_idx]['position_x'] = float(pos['x'])
                    sample_data[sample_idx]['position_y'] = float(pos['y'])
                    sample_data[sample_idx]['position_z'] = float(pos['z'])

                    sample_data[sample_idx]['rotation'] = float(sample['rotation'])
                    sample_data[sample_idx]['speed'] = float(sample['speed'])
                    sample_data[sample_idx]['mode'] = sample['mode'].encode('utf-8')

                    sample_idx += 1

            # Use chunking and compression for better performance with large files
            if total_samples > 1000:
                chunk_size = min(1000, total_samples)
                sim_grp.create_dataset('samples', data=sample_data,
                                       chunks=(chunk_size,), compression='gzip',
                                       compression_opts=4)
            else:
                sim_grp.create_dataset('samples', data=sample_data)

            # Store sample count per timestep for efficient reading
            samples_per_timestep = np.array([len(ts.get('samples', [])) for ts in simulation],
                                           dtype=np.int32)
            sim_grp.create_dataset('samples_per_timestep', data=samples_per_timestep)

    except IOError as e:
        print(f"Error: Cannot write HDF5 file - {e}", file=sys.stderr)
        return False
    except Exception as e:
        print(f"Error during conversion: {e}", file=sys.stderr)
        return False

    if verbose:
        # Print file size comparison
        json_size = json_path.stat().st_size
        hdf5_size = hdf5_path.stat().st_size
        ratio = hdf5_size / json_size * 100
        print(f"Conversion complete!")
        print(f"  JSON size:  {json_size:,} bytes")
        print(f"  HDF5 size:  {hdf5_size:,} bytes ({ratio:.1f}% of JSON)")
        print(f"  Output: {hdf5_path}")

    return True


def print_hdf5_info(hdf5_path: Path) -> None:
    """Print information about an HDF5 simulation file."""
    print(f"\nHDF5 File Info: {hdf5_path}")
    print("-" * 50)

    with h5py.File(hdf5_path, 'r') as hf:
        # Print metadata
        if 'metadata' in hf:
            meta = hf['metadata']
            print("Metadata:")
            for key, val in meta.attrs.items():
                print(f"  {key}: {val}")

        # Print entities info
        if 'entities' in hf:
            entities = hf['entities']
            print(f"\nEntities: {len(entities)} total")
            if len(entities) > 0:
                print(f"  First entity: id={entities[0]['id']}, name={entities[0]['name']}")

        # Print simulation info
        if 'simulation' in hf:
            sim = hf['simulation']
            if 'timesteps' in sim:
                timesteps = sim['timesteps']
                print(f"\nSimulation:")
                print(f"  Timesteps: {len(timesteps)}")
                print(f"  Time range: {timesteps[0]:.2f} - {timesteps[-1]:.2f}")
            if 'samples' in sim:
                samples = sim['samples']
                print(f"  Total samples: {len(samples)}")


def main():
    parser = argparse.ArgumentParser(
        description='Convert ProjectMobius simulation JSON to HDF5 format',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument('input', type=Path, help='Input JSON file path')
    parser.add_argument('output', type=Path, nargs='?', default=None,
                       help='Output HDF5 file path (default: input with .h5 extension)')
    parser.add_argument('-q', '--quiet', action='store_true',
                       help='Suppress progress output')
    parser.add_argument('-i', '--info', action='store_true',
                       help='Print info about the generated HDF5 file')
    parser.add_argument('-v', '--validate-only', action='store_true',
                       help='Only validate JSON structure, do not convert')

    args = parser.parse_args()

    # Check input file exists
    if not args.input.exists():
        print(f"Error: Input file not found: {args.input}", file=sys.stderr)
        sys.exit(1)

    # Determine output path
    if args.output is None:
        args.output = args.input.with_suffix('.h5')

    # Validate only mode
    if args.validate_only:
        with open(args.input, 'r', encoding='utf-8') as f:
            data = json.load(f)
        errors = validate_json_structure(data)
        if errors:
            print("Validation FAILED:", file=sys.stderr)
            for error in errors:
                print(f"  - {error}", file=sys.stderr)
            sys.exit(1)
        else:
            print("Validation PASSED")
            sys.exit(0)

    # Convert
    success = convert_json_to_hdf5(args.input, args.output, verbose=not args.quiet)

    if success and args.info:
        print_hdf5_info(args.output)

    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()