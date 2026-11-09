from pathlib import Path

import numpy as np
from PIL import Image

SIZE = 256
OUTPUT = Path(__file__).resolve().parent.parent / "textures"
rng = np.random.default_rng(seed=42)


def perlin(size: int, period: int, seed: int) -> np.ndarray:
    local_rng = np.random.default_rng(seed)
    angles = local_rng.uniform(0, 2 * np.pi, (period + 1, period + 1))
    gradients = np.stack([np.cos(angles), np.sin(angles)], axis=-1)
    gradients[-1, :] = gradients[0, :]
    gradients[:, -1] = gradients[:, 0]

    coords = np.linspace(0, period, size, endpoint=False)
    x, y = np.meshgrid(coords, coords)
    x0, y0 = x.astype(int), y.astype(int)
    fx, fy = x - x0, y - y0

    def dot_grid(ix, iy, dx, dy):
        g = gradients[iy, ix]
        return g[..., 0] * dx + g[..., 1] * dy

    n00 = dot_grid(x0, y0, fx, fy)
    n10 = dot_grid(x0 + 1, y0, fx - 1, fy)
    n01 = dot_grid(x0, y0 + 1, fx, fy - 1)
    n11 = dot_grid(x0 + 1, y0 + 1, fx - 1, fy - 1)

    def fade(t):
        return t * t * t * (t * (t * 6 - 15) + 10)

    u, v = fade(fx), fade(fy)
    return (n00 * (1 - u) + n10 * u) * (1 - v) + (n01 * (1 - u) + n11 * u) * v


def fbm(size: int, base_period: int, octaves: int, seed: int) -> np.ndarray:
    result = np.zeros((size, size))
    amplitude, total = 1.0, 0.0
    for octave in range(octaves):
        result += amplitude * perlin(size, base_period * 2 ** octave, seed + octave)
        total += amplitude
        amplitude *= 0.5
    result /= total
    return np.clip(result * 0.5 + 0.5, 0.0, 1.0)


def save(name: str, image: np.ndarray) -> None:
    data = np.clip(image * 255.0, 0, 255).astype(np.uint8)
    Image.fromarray(data).save(OUTPUT / name)
    print(f"  {name}")


def colorize(mask: np.ndarray, color) -> np.ndarray:
    return mask[..., None] * np.asarray(color, dtype=float)


def bricks() -> None:
    brick_w, brick_h, mortar = 64, 32, 4
    y, x = np.mgrid[0:SIZE, 0:SIZE]
    row = y // brick_h
    shifted_x = x + (row % 2) * (brick_w // 2)
    column = shifted_x // brick_w
    is_mortar = ((y % brick_h) < mortar) | ((shifted_x % brick_w) < mortar)

    brick_ids = row * 16 + column % (SIZE // brick_w)
    tint = rng.uniform(0.75, 1.1, brick_ids.max() + 1)[brick_ids]
    noise = fbm(SIZE, 8, 4, seed=1)

    brick_color = colorize(tint * (0.75 + 0.35 * noise), (0.62, 0.25, 0.18))
    mortar_color = colorize(0.6 + 0.2 * noise, (0.75, 0.72, 0.68))
    save("bricks_albedo.png", np.where(is_mortar[..., None], mortar_color, brick_color))
    save("bricks_specular.png", np.where(is_mortar, 0.0, 0.15 + 0.25 * noise))


def tiles() -> None:
    tile, grout = 64, 3
    y, x = np.mgrid[0:SIZE, 0:SIZE]
    is_grout = ((x % tile) < grout) | ((y % tile) < grout)
    checker = ((x // tile + y // tile) % 2).astype(float)
    noise = fbm(SIZE, 4, 4, seed=10)

    tile_color = np.where(checker[..., None] > 0.5,
                          colorize(0.85 + 0.15 * noise, (0.92, 0.9, 0.84)),
                          colorize(0.85 + 0.15 * noise, (0.45, 0.55, 0.65)))
    grout_color = colorize(0.25 + 0.1 * noise, (1.0, 1.0, 1.0))
    save("tiles_albedo.png", np.where(is_grout[..., None], grout_color, tile_color))
    save("tiles_specular.png", np.where(is_grout, 0.0, 0.9))


def lava() -> None:
    rock = fbm(SIZE, 4, 5, seed=20)
    ridges = 1.0 - np.abs(perlin(SIZE, 4, 30) + 0.5 * perlin(SIZE, 8, 31))
    cracks = np.clip((ridges - 0.82) / 0.18, 0.0, 1.0) ** 2

    albedo = colorize(0.15 + 0.25 * rock, (0.55, 0.5, 0.48)) * (1.0 - cracks[..., None] * 0.8)
    emissive = colorize(cracks, (1.0, 0.45, 0.08)) + colorize(cracks ** 3, (0.0, 0.4, 0.3))
    save("lava_albedo.png", albedo)
    save("lava_specular.png", 0.1 + 0.3 * rock * (1.0 - cracks))
    save("lava_emissive.png", emissive)


def scifi_panel() -> None:
    panel, gap = 64, 4
    y, x = np.mgrid[0:SIZE, 0:SIZE]
    is_gap = ((x % panel) < gap) | ((y % panel) < gap)
    scratches = fbm(SIZE, 16, 3, seed=40)

    cx, cy = (x % panel) - panel / 2, (y % panel) - panel / 2
    led = np.exp(-(cx ** 2 + cy ** 2) / 18.0)
    glow_lines = is_gap.astype(float) * (0.6 + 0.4 * fbm(SIZE, 4, 2, seed=41))

    metal = colorize(0.35 + 0.15 * scratches, (0.75, 0.78, 0.82))
    albedo = np.where(is_gap[..., None], colorize(np.full((SIZE, SIZE), 0.05), (1, 1, 1)), metal)
    save("scifi_albedo.png", albedo)
    save("scifi_specular.png", np.where(is_gap, 0.0, 0.5 + 0.5 * scratches))
    save("scifi_emissive.png", colorize(glow_lines, (0.1, 0.8, 1.0)) + colorize(led, (1.0, 0.2, 0.6)))


def checker() -> None:
    cell = 32
    y, x = np.mgrid[0:SIZE, 0:SIZE]
    mask = ((x // cell + y // cell) % 2).astype(float)
    save("checker_albedo.png",
         colorize(mask, (0.95, 0.85, 0.3)) + colorize(1.0 - mask, (0.15, 0.25, 0.6)))


if __name__ == "__main__":
    OUTPUT.mkdir(exist_ok=True)
    print(f"Generating textures into {OUTPUT}")
    bricks()
    tiles()
    lava()
    scifi_panel()
    checker()
