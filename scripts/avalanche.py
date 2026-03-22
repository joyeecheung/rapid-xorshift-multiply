"""Step 1: Compute avalanche matrices and save to CSV.
   Step 2: Load CSVs and plot the avalanche matrices.

   Usage:
       python avalanche.py compute          # generate CSVs
       python avalanche.py plot             # plot from CSVs
       python avalanche.py                  # both steps
"""

import sys
import random
import numpy as np
import os


asset_dir = os.path.join(os.path.dirname(__file__), 'assets')
KSHIFT = 12
KMASK = 0xFFFFFF

DEFAULT_SECRETS = [0x2d358dccaa6c78a5, 0x8bb84b93962eacc9, 0x4b33a62ed433d4a3]
M1 = (DEFAULT_SECRETS[0] & KMASK) | 1  # 0x6c78a5
M2 = (DEFAULT_SECRETS[1] & KMASK) | 1  # 0x2eacc9
M3 = (DEFAULT_SECRETS[2] & KMASK) | 1  # 0x33d4a3

def identity(h):
    return h

def mul_add(x):
    x &= KMASK
    return ((x * M1 + M2) & KMASK)

def xsr_mul_xsr_2(x):
    x &= KMASK
    x ^= (x >> KSHIFT)
    x = (x * M1) & KMASK
    x ^= (x >> KSHIFT)
    x = (x * M2) & KMASK
    x ^= (x >> KSHIFT)
    return x

def xsr_mul_xsr_3(x):
    x &= KMASK
    x ^= (x >> KSHIFT)
    x = (x * M1) & KMASK
    x ^= (x >> KSHIFT)
    x = (x * M2) & KMASK
    x ^= (x >> KSHIFT)
    x = (x * M3) & KMASK
    x ^= (x >> KSHIFT)
    return x

def generate_avalanche_matrix(hash_func, bits=32, iterations=100000):
    print(f"Generating matrix for {hash_func.__name__} ({iterations} iterations)...")
    matrix = np.zeros((bits, bits))
    for _ in range(iterations):
        base_input = random.getrandbits(bits)
        base_hash = hash_func(base_input)
        for i in range(bits):
            flipped_input = base_input ^ (1 << i)
            flipped_hash = hash_func(flipped_input)
            changed_bits = base_hash ^ flipped_hash
            for j in range(bits):
                if (changed_bits & (1 << j)) != 0:
                    matrix[i][j] += 1
    return matrix / iterations

# (function, csv_name, bit_width)
HASH_FUNCS = [
    (identity,       "identity",    24),
    (mul_add,        "mul_add",     24),
    (xsr_mul_xsr_2,  "xsr_mul_2r",  24),
    (xsr_mul_xsr_3,  "xsr_mul_3r",  24),
]

def compute(iterations=50000):
    for func, name, bits in HASH_FUNCS:
        matrix = generate_avalanche_matrix(func, bits, iterations)
        path = os.path.join(asset_dir, f"avalanche_{name}.csv")
        np.savetxt(path, matrix, delimiter=",")
        print(f"Saved {path}")

TITLES = [
    "Identity",
    "multiply-add",
    "2-round xorshift-multiply",
    "3-round xorshift-multiply",
]
CSV_FILES = [os.path.join(asset_dir, f"avalanche_{name}.csv") for _, name, _ in HASH_FUNCS]

def plot():
    import matplotlib
    matplotlib.use('Agg')
    import seaborn as sns
    import matplotlib.pyplot as plt
    from matplotlib.colors import LinearSegmentedColormap

    cmap = LinearSegmentedColormap.from_list(
        'avalanche', ['#d32f2f', '#2e7d32', '#d32f2f'])

    fig, axes = plt.subplots(2, 2, figsize=(14, 12), constrained_layout=True)

    for ax, csv_path, title in zip(axes.flat, CSV_FILES, TITLES):
        matrix = np.loadtxt(csv_path, delimiter=",")
        bits = matrix.shape[0]
        sns.heatmap(
            matrix,
            ax=ax,
            cmap=cmap,
            vmin=0.0,
            vmax=1.0,
            linewidths=0.5,
            linecolor='white',
            cbar_kws={'label': 'Probability of Output Bit Flipping'},
        )
        ax.invert_yaxis()
        ax.set_title(title, fontsize=11, fontweight='bold')
        ax.set_xlabel(f'Output Bit Position (0 to {bits - 1})', fontsize=9)
        ax.set_ylabel(f'Flipped Input Bit Position (0 to {bits - 1})', fontsize=9)

    out = os.path.join(asset_dir, 'avalanche-matrix.svg')
    fig.savefig(out, format='svg', bbox_inches='tight', dpi=150)
    plt.close(fig)
    print(f"Saved {out}")

if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "both"
    os.makedirs(asset_dir, exist_ok=True)

    if cmd == "compute":
        compute()
    elif cmd == "plot":
        plot()
    else:
        compute()
        plot()