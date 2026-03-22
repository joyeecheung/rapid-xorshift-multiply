#!/usr/bin/env python3
"""Generate hash distribution scatter plot SVGs
for the seeded-integer-hash-v8-hashdos blog post.

Usage:
       python distribution-graph.py
"""

import os

import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

asset_dir = os.path.join(os.path.dirname(__file__), 'assets')
BITS = 24
N = 1 << BITS
KSHIFT = 12
KMASK = 0xFFFFFF

DEFAULT_SECRETS = [0x2d358dccaa6c78a5, 0x8bb84b93962eacc9, 0x4b33a62ed433d4a3]

def derive_multiplier(secret):
    return (secret & KMASK) | 1

M1 = derive_multiplier(DEFAULT_SECRETS[0])
M2 = derive_multiplier(DEFAULT_SECRETS[1])
M3 = derive_multiplier(DEFAULT_SECRETS[2])


def identity(x):
    return x


def mul_add(x):
    x = np.asarray(x, dtype=np.uint32)
    return ((x * np.uint32(M1) + np.uint32(M2)) & KMASK)


def xsr_mul_xsr_2(x):
    x = np.asarray(x, dtype=np.uint32)
    x ^= (x >> KSHIFT)
    x = (x * M1) & KMASK
    x ^= (x >> KSHIFT)
    x = (x * M2) & KMASK
    x ^= (x >> KSHIFT)
    return x


def xsr_mul_xsr_3(x):
    x = np.asarray(x, dtype=np.uint32)
    x ^= (x >> KSHIFT)
    x = (x * M1) & KMASK
    x ^= (x >> KSHIFT)
    x = (x * M2) & KMASK
    x ^= (x >> KSHIFT)
    x = (x * M3) & KMASK
    x ^= (x >> KSHIFT)
    return x



def plot_distribution_scatter(hash_fns, titles, output_path):
    """Plot 2x2 scatter of (x, hash(x)) for sequential inputs.
    A good hash maps evenly-spaced inputs to uniformly scattered outputs."""
    fig, axes = plt.subplots(2, 2, figsize=(9, 9),
                             constrained_layout=True)

    sample_count = 2000
    sample_x = np.linspace(0, N - 1, sample_count, dtype=np.uint32)

    for ax, fn, title in zip(axes.flat, hash_fns, titles):
        hy = fn(sample_x)
        ax.scatter(sample_x, hy, s=3, alpha=1.0, color='#2563eb',
                   rasterized=True, edgecolors='none')
        ax.set_title(title, fontsize=11, fontweight='bold')
        ax.set_xlabel('input (sequential)', fontsize=9)
        ax.set_ylabel('hash output', fontsize=9)
        ax.set_xlim(0, N)
        ax.set_ylim(0, N)
        ax.set_aspect('equal')
        ax.xaxis.set_major_formatter(plt.FuncFormatter(lambda v, _: f'$2^{{24}}$' if v == N else (f'$2^{{23}}$' if v == N // 2 else f'{int(v):,}')))
        ax.yaxis.set_major_formatter(plt.FuncFormatter(lambda v, _: f'$2^{{24}}$' if v == N else (f'$2^{{23}}$' if v == N // 2 else f'{int(v):,}')))
        ax.set_xticks([0, N // 2, N])
        ax.set_yticks([0, N // 2, N])

    fig.savefig(output_path, format='svg', bbox_inches='tight', dpi=150)
    plt.close(fig)
    print(f"Saved {output_path}")


if __name__ == '__main__':
    os.makedirs(asset_dir, exist_ok=True)

    print(f"Using multipliers: m1=0x{M1:06x}, m2=0x{M2:06x}, m3=0x{M3:06x}")

    print("Generating distribution scatter plots...")
    plot_distribution_scatter(
        [identity, mul_add, xsr_mul_xsr_2, xsr_mul_xsr_3],
        ['Identity', 'multiply-add', '2-round xorshift-multiply', '3-round xorshift-multiply'],
        os.path.join(asset_dir, 'hash-distribution.svg')
    )

    print("Done!")
