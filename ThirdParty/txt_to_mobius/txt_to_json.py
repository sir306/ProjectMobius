"""Parser  of trajectory data from a .txt file into a JSON format for Mobius."""

import math
import json
import pandas as pd
import typer
import matplotlib.pyplot as plt

app = typer.Typer()


def parse_txt_with_pandas(file_path):
    """Parse input file and return DataFrame."""
    fps = 25
    with open(file_path, "r") as file:
        for line in file:
            if line.startswith("#"):
                if "framerate" in line.lower():
                    fps = int(line.split(":")[1].strip().split()[0])

            else:
                break

    df = pd.read_csv(
        file_path, sep="\t", comment="#", names=["id", "frame", "x", "y", "z"]
    )

    return df, fps


def smooth_rotation(df, step_size, time_step, alpha=0.1):
    """
    Smooth rotations using differences with step_size and correct large jumps with a threshold.

    Parameters:
        df: DataFrame containing raw x, y coordinates.
        step_size: Number of frames to use for differences.
        time_step: Time step duration in seconds.
    Returns:
        Updated DataFrame with smoothed rotations.
    """
    df["time"] = df["frame"] * time_step
    df["dx"] = df.groupby("id")["x"].shift(-step_size) - df.groupby("id")["x"].shift(
        step_size
    )
    df["dy"] = df.groupby("id")["y"].shift(-step_size) - df.groupby("id")["y"].shift(
        step_size
    )
    df2 = df.dropna(subset=["dx", "dy"]).copy()

    df2["distance"] = (df2["dx"] ** 2 + df2["dy"] ** 2).pow(0.5)
    df2["speed"] = df2["distance"] / (2 * step_size * time_step)

    df2["raw_rotation"] = df2.apply(
        lambda row: math.degrees(math.atan2(row["dy"], row["dx"]))
        if row["distance"] > 0
        else 0.0,
        axis=1,
    )

    df2["rotation"] = apply_ewma(df2["raw_rotation"], alpha=0.1)

    return df2


def apply_ewma(df_column, alpha=0.3):
    """
    Apply forwards and backwards exponential weighted moving average (EWMA) to a pandas Series.

    Parameters:
        df_column (pd.Series): The input column or time series to be smoothed.
        alpha (float): The smoothing factor. Higher values give more weight to recent values (0 < alpha <= 1).

    Returns:
        pd.Series: The smoothed column after applying forwards and backwards EWMAs.
    """
    # Apply forward EWMA
    fwd_ewma = df_column.ewm(alpha=alpha).mean()

    # Apply backward EWMA (reverse the series, apply EWMA, then reverse back)
    bwd_ewma = df_column[::-1].ewm(alpha=alpha).mean()[::-1]

    # Combine the forward and backward EWMAs
    smoothed_series = (fwd_ewma + bwd_ewma) / 2

    return smoothed_series


def plot_rotation_and_speed(df):
    """Plot rotation and speed for the first 20 agents."""
    ids = df["id"].unique()
    for i in range(20):
        fig, (ax1, ax2) = plt.subplots(ncols=1, nrows=2)
        df_subset = df[df["id"] == ids[i]]
        ax1.plot(
            df_subset["frame"],
            df_subset["raw_rotation"],
            "--",
            color="gray",
            label="Raw Rotation",
        )
        ax1.plot(
            df_subset["frame"],
            df_subset["rotation"],
            color="red",
            alpha=0.7,
            label="Smoothed Rotation",
        )
        ax2.plot(
            df_subset["frame"],
            df_subset["speed"],
            color="red",
            alpha=0.7,
            label="Speed",
        )
        ax1.set_xlabel("frame")
        ax2.set_xlabel("frame")
        ax1.set_ylabel(r"rotation [$\circ$]")
        ax2.set_ylabel("speed [m/s]")
        ax1.set_title(f"Agent {ids[i]}")
        ax1.grid(alpha=0.3)
        ax2.grid(alpha=0.3)
        ax1.legend(loc="upper left", bbox_to_anchor=(1.05, 1))  # Legend outside
        plt.tight_layout()
        figname = f"rotation_{ids[i]}.png"
        print(f"----> {figname}", end="\r")
        fig.savefig(figname, dpi=300)


def convert_df_to_json_optimized(df, fps, default_mode):
    """Convert DataFrame to JSON format for Mobius simulation."""
    time_step = 1 / fps
    step_size = 10
    alpha = 0.1
    df = smooth_rotation(df, step_size, time_step, alpha=alpha)
    max_speeds = df.groupby("id")["speed"].max()
    # ================= debugging rotation
    plot_rotation_and_speed(df)
    # ================= debugging rotation
    # Construct entities block
    entities = [
        {
            "id": int(entity_id) - 1,
            "name": f"Adult {'Male' if int(entity_id) % 2 == 0 else 'Female'} [{int(entity_id) - 1}]",
            "simTimeS": "0.0",
            "max_speed": round(max_speed, 3),
            "m_plane": "F#0",
            "map": 0,
        }
        for entity_id, max_speed in max_speeds.items()
    ]
    print(f"Number of entities: {len(entities)}")

    # Construct simulation block
    simulation = []
    precision = 3
    for time, group in df.groupby("time"):
        samples = group.apply(
            lambda row: {
                "entity": int(row["id"]) - 1,
                "position": {
                    "x": round(row["x"], precision),
                    "y": round(row["y"], precision),
                    "z": round(0 * row["z"], precision),
                },  # TODO: setting z to 0 is a hack to make the visualisator happy!
                "mode": default_mode,
                "rotation": round(row["rotation"], precision),
                "speed": round(row["speed"], precision),
            },
            axis=1,
        ).tolist()
        simulation.append({"time": time, "samples": samples})
        print(f"Time: {time:.2f} s, Samples: {len(samples)}", end="\r")

    # Construct metadata block
    duration = df["time"].max()
    metadata = {
        "duration": duration,
        "used_planes": ["F#0"],
        "distance_maps_used": [
            {
                "index": 0,
                "name": "Default Distance Map",
                "num_users": len(entities),
                "use_ground": 0,
                "ground_elevation": 0.0,
                "people_density_catg_num": 2,
            }
        ],
        "timestamp_geometry": 0,
        "timestamp_people": 0,
        "timestamp_exits_links": 0,
        "model_GUID": "",
        "sampling_rate": round(time_step, 3),
        "max_num_entities": len(entities),
        "isSI": True,
        "isDeg": True,
    }

    return {"entities": entities, "simulation": simulation, "metadata": metadata}


@app.command()
def main(
    file_path: str = typer.Argument(..., help="Path to the trajectory TXT file."),
    default_mode: str = typer.Option(
        "LF#0", help="Default mode for simulation entities."
    ),
    output_file: str = typer.Option(
        "output.json", help="Path to the output JSON file."
    ),
):
    try:
        trajectory_df, fps = parse_txt_with_pandas(file_path)
        json_result = convert_df_to_json_optimized(trajectory_df, fps, default_mode)
        with open(output_file, "w") as outfile:
            json.dump(json_result, outfile, indent=4)
        print(f"JSON file saved to {output_file}")
    except FileNotFoundError:
        print("File not found. Please provide the correct file path.")


if __name__ == "__main__":
    app()
