#!/usr/bin/env python3
"""
Generate 3D pseudo-rendered XBM sprites for all 6 D&D polyhedral dice.

Outputs: dice_sprites.h (C header with XBM byte arrays)

Each die type gets 8 rotation frames (45-degree increments around Y axis)
at 24x24 pixels, 1-bit depth.

Dependencies: numpy, Pillow (no scipy).
"""

import math
import numpy as np
from PIL import Image, ImageDraw

# ---------------------------------------------------------------------------
# Polyhedra definitions: vertices and face index lists
# ---------------------------------------------------------------------------

def make_tetrahedron():
    """d4: regular tetrahedron inscribed in unit sphere."""
    v = np.array([
        [ 1,  1,  1],
        [ 1, -1, -1],
        [-1,  1, -1],
        [-1, -1,  1],
    ], dtype=float)
    v /= np.linalg.norm(v[0])
    faces = [
        [0, 1, 2],
        [0, 2, 3],
        [0, 3, 1],
        [1, 3, 2],
    ]
    return v, faces


def make_cube():
    """d6: unit cube centred at origin."""
    s = 0.7
    v = np.array([
        [-s, -s, -s],
        [ s, -s, -s],
        [ s,  s, -s],
        [-s,  s, -s],
        [-s, -s,  s],
        [ s, -s,  s],
        [ s,  s,  s],
        [-s,  s,  s],
    ], dtype=float)
    faces = [
        [0, 1, 2, 3],  # back
        [4, 7, 6, 5],  # front
        [0, 3, 7, 4],  # left
        [1, 5, 6, 2],  # right
        [3, 2, 6, 7],  # top
        [0, 4, 5, 1],  # bottom
    ]
    return v, faces


def make_octahedron():
    """d8: regular octahedron."""
    v = np.array([
        [ 0,  1,  0],
        [ 0, -1,  0],
        [ 1,  0,  0],
        [-1,  0,  0],
        [ 0,  0,  1],
        [ 0,  0, -1],
    ], dtype=float)
    faces = [
        [0, 4, 2],
        [0, 2, 5],
        [0, 5, 3],
        [0, 3, 4],
        [1, 2, 4],
        [1, 5, 2],
        [1, 3, 5],
        [1, 4, 3],
    ]
    return v, faces


def make_pentagonal_trapezohedron():
    """d10: pentagonal trapezohedron (approx).

    Two poles at top/bottom, two rings of 5 vertices, 10 kite-shaped faces.
    """
    top = np.array([0, 1.2, 0], dtype=float)
    bot = np.array([0, -1.2, 0], dtype=float)

    upper_ring = []
    lower_ring = []
    for i in range(5):
        a = 2 * math.pi * i / 5
        # Upper ring: rotated by half-step offset from lower ring
        upper_ring.append([math.cos(a) * 0.9, 0.4, math.sin(a) * 0.9])
        a2 = 2 * math.pi * (i + 0.5) / 5
        lower_ring.append([math.cos(a2) * 0.9, -0.4, math.sin(a2) * 0.9])

    # Vertices: 0=top, 1-5=upper ring, 6-10=lower ring, 11=bottom
    v = np.array([top] + upper_ring + lower_ring + [bot], dtype=float)

    # 10 kite faces: alternating between top-connected and bottom-connected
    faces = []
    for i in range(5):
        u_cur = 1 + i
        u_nxt = 1 + (i + 1) % 5
        l_cur = 6 + i
        # Upper kite: top, upper[i], lower[i], upper[i+1]
        faces.append([0, u_cur, l_cur, u_nxt])
        # Lower kite: bottom, lower[i], upper[i+1], lower[i+1]
        l_nxt = 6 + (i + 1) % 5
        faces.append([11, l_nxt, u_nxt, l_cur])

    return v, faces


def make_dodecahedron():
    """d12: regular dodecahedron. 20 vertices, 12 pentagonal faces.

    Vertices derived from cube + golden-ratio rectangles.
    """
    phi = (1 + math.sqrt(5)) / 2
    inv = 1 / phi

    # The 20 vertices of a regular dodecahedron
    verts = []
    # 8 cube vertices (+-1, +-1, +-1)
    for sx in (-1, 1):
        for sy in (-1, 1):
            for sz in (-1, 1):
                verts.append([sx, sy, sz])
    # 4 vertices on each coordinate plane from golden rectangles
    for sx in (-1, 1):
        for sy in (-1, 1):
            verts.append([0, sx * phi, sy * inv])       # yz-plane
    for sx in (-1, 1):
        for sy in (-1, 1):
            verts.append([sy * inv, 0, sx * phi])        # xz-plane
    for sx in (-1, 1):
        for sy in (-1, 1):
            verts.append([sx * phi, sy * inv, 0])        # xy-plane

    v = np.array(verts, dtype=float)
    # Normalise to roughly unit sphere
    v /= np.max(np.linalg.norm(v, axis=1))

    # Hardcoded face indices (12 pentagonal faces).
    # Vertex ordering matches the construction above:
    #  0-7:  cube vertices (---, --+, -+-, -++, +--, +-+, ++-, +++)
    #  8-11: (0, +-phi, +-inv)
    #  12-15: (+-inv, 0, +-phi)
    #  16-19: (+-phi, +-inv, 0)
    faces = [
        # Top face (y positive)
        [2, 8, 3, 11, 6],
        # Bottom face (y negative)
        [0, 4, 9, 5, 1],
        # Front face (z positive)
        [1, 5, 15, 7, 13],
        # Back face (z negative)
        [0, 12, 6, 14, 4],
        # Right face (x positive)
        [4, 14, 7, 15, 5],  # mistake likely — let me recalculate
        # Left face (x negative)
        [0, 1, 13, 3, 12],  # mistake likely — let me recalculate
    ]

    # Actually, let me use the proper algorithmic approach to find faces.
    # For a dodecahedron, each face is a regular pentagon. Each vertex belongs
    # to exactly 3 faces. I'll find faces by finding pentagons among the edges.

    # Build adjacency: vertices connected by edges of length ~2/phi
    from itertools import combinations

    dists = []
    for i, j in combinations(range(20), 2):
        d = np.linalg.norm(v[i] - v[j])
        dists.append(d)
    dists.sort()
    # Edge length is the shortest distance
    edge_len = dists[0]
    tol = edge_len * 0.15

    adj = {i: set() for i in range(20)}
    for i, j in combinations(range(20), 2):
        if abs(np.linalg.norm(v[i] - v[j]) - edge_len) < tol:
            adj[i].add(j)
            adj[j].add(i)

    # Each vertex should have degree 3 in a dodecahedron
    # Find faces: walk around edges. Each face is a 5-cycle.
    found_faces = set()
    final_faces = []

    for start in range(20):
        for n1 in adj[start]:
            for n2 in adj[n1]:
                if n2 == start:
                    continue
                for n3 in adj[n2]:
                    if n3 == start or n3 == n1:
                        continue
                    for n4 in adj[n3]:
                        if n4 == start or n4 == n1 or n4 == n2:
                            continue
                        if start in adj[n4]:
                            # Found a 5-cycle: start, n1, n2, n3, n4
                            face = (start, n1, n2, n3, n4)
                            # Canonical form: smallest rotation
                            canon = min(face[i:] + face[:i] for i in range(5))
                            # Also check reverse
                            rev = tuple(reversed(face))
                            canon_rev = min(rev[i:] + rev[:i] for i in range(5))
                            canon = min(canon, canon_rev)
                            if canon not in found_faces:
                                found_faces.add(canon)
                                final_faces.append(list(canon))

    # Should have exactly 12 faces
    assert len(final_faces) == 12, f"Expected 12 faces, got {len(final_faces)}"

    # Orient faces consistently (outward normals)
    for idx, face in enumerate(final_faces):
        center = np.mean(v[face], axis=0)
        p0, p1, p2 = v[face[0]], v[face[1]], v[face[2]]
        normal = np.cross(p1 - p0, p2 - p0)
        if np.dot(normal, center) < 0:
            final_faces[idx] = list(reversed(face))

    return v, final_faces


def make_icosahedron():
    """d20: regular icosahedron. 12 vertices, 20 triangular faces."""
    phi = (1 + math.sqrt(5)) / 2
    v = np.array([
        [-1,  phi, 0],
        [ 1,  phi, 0],
        [-1, -phi, 0],
        [ 1, -phi, 0],
        [0, -1,  phi],
        [0,  1,  phi],
        [0, -1, -phi],
        [0,  1, -phi],
        [ phi, 0, -1],
        [ phi, 0,  1],
        [-phi, 0, -1],
        [-phi, 0,  1],
    ], dtype=float)
    v /= np.linalg.norm(v[0])

    faces = [
        [0, 11, 5],
        [0, 5, 1],
        [0, 1, 7],
        [0, 7, 10],
        [0, 10, 11],
        [1, 5, 9],
        [5, 11, 4],
        [11, 10, 2],
        [10, 7, 6],
        [7, 1, 8],
        [3, 9, 4],
        [3, 4, 2],
        [3, 2, 6],
        [3, 6, 8],
        [3, 8, 9],
        [4, 9, 5],
        [2, 4, 11],
        [6, 2, 10],
        [8, 6, 7],
        [9, 8, 1],
    ]
    return v, faces


# ---------------------------------------------------------------------------
# 3D rendering utilities
# ---------------------------------------------------------------------------

def rotation_matrix(angle_y, tilt_x):
    """Combined rotation: tilt around X, then rotate around Y."""
    cy, sy = math.cos(angle_y), math.sin(angle_y)
    cx, sx = math.cos(tilt_x), math.sin(tilt_x)

    # Ry
    ry = np.array([
        [cy,  0, sy],
        [ 0,  1,  0],
        [-sy, 0, cy],
    ])
    # Rx
    rx = np.array([
        [1,  0,   0],
        [0, cx, -sx],
        [0, sx,  cx],
    ])
    return ry @ rx


def project_vertices(vertices, angle_y, tilt_x=0.4, size=24, scale=None):
    """Rotate vertices and do orthographic projection to 2D pixel coords.

    Returns:
        pts_2d: list of (x, y) tuples in pixel space
        rotated: Nx3 array of rotated 3D coordinates
    """
    rot = rotation_matrix(angle_y, tilt_x)
    rotated = (rot @ vertices.T).T

    # Auto-scale to fit in the frame with some padding
    if scale is None:
        max_extent = np.max(np.abs(rotated[:, :2]))
        scale = (size / 2 - 2) / max_extent if max_extent > 0 else 1.0

    cx, cy = size / 2, size / 2
    pts_2d = []
    for pt in rotated:
        px = cx + pt[0] * scale
        py = cy - pt[1] * scale  # Y flipped for screen coords
        pts_2d.append((px, py))

    return pts_2d, rotated


def face_visible(pts_2d, face):
    """Check if a face is front-facing using 2D cross product (screen coords).

    With screen-Y pointing down, a positive cross product means the face
    winds clockwise in screen space, which corresponds to a front-facing
    polygon (normal pointing out of screen).
    """
    if len(face) < 3:
        return False
    p0 = pts_2d[face[0]]
    p1 = pts_2d[face[1]]
    p2 = pts_2d[face[2]]
    # Cross product of edge vectors in 2D
    cross_z = (p1[0] - p0[0]) * (p2[1] - p0[1]) - (p1[1] - p0[1]) * (p2[0] - p0[0])
    return cross_z > 0


def render_frame(vertices, faces, angle_y, tilt=0.4, size=24):
    """Render a single frame of a polyhedron.

    Strategy:
    1. Draw all faces filled black (back-to-front) to create solid silhouette
    2. Draw visible (front-facing) faces filled white with black outline
    This creates depth: back faces appear as solid black mass, front faces
    are white with clean black edges.
    """
    img = Image.new('1', (size, size), 0)  # 0 = white/transparent on Flipper
    draw = ImageDraw.Draw(img)

    pts_2d, rotated = project_vertices(vertices, angle_y, tilt, size)

    # Compute face data with average Z for sorting
    face_data = []
    for face in faces:
        avg_z = np.mean([rotated[i][2] for i in face])
        face_data.append((avg_z, face))

    # Sort by Z: smallest (farthest) first = back-to-front
    face_data.sort(key=lambda x: x[0])

    # Pass 1: Draw all faces as filled black (silhouette)
    for _, face in face_data:
        poly = [pts_2d[i] for i in face]
        if len(poly) >= 3:
            draw.polygon(poly, fill=1, outline=1)

    # Pass 2: Draw front-facing faces as white fill with black outline
    for _, face in face_data:
        if not face_visible(pts_2d, face):
            continue
        poly = [pts_2d[i] for i in face]
        if len(poly) >= 3:
            draw.polygon(poly, fill=0, outline=1)

    return img


def image_to_xbm_bytes(img):
    """Convert a 1-bit PIL Image to XBM byte array (LSB-first).

    XBM format: each row is packed into bytes, LSB first.
    For a 24-pixel-wide image, that's 3 bytes per row.
    Bit value 1 = drawn pixel (black on Flipper).
    """
    width, height = img.size
    bytes_per_row = (width + 7) // 8
    data = []

    for y in range(height):
        for bx in range(bytes_per_row):
            byte = 0
            for bit in range(8):
                x = bx * 8 + bit
                if x < width:
                    pixel = img.getpixel((x, y))
                    if pixel:
                        byte |= (1 << bit)  # LSB-first
            data.append(byte)

    return data


# ---------------------------------------------------------------------------
# Main generation
# ---------------------------------------------------------------------------

DICE_DEFS = [
    ("d4",  make_tetrahedron),
    ("d6",  make_cube),
    ("d8",  make_octahedron),
    ("d10", make_pentagonal_trapezohedron),
    ("d12", make_dodecahedron),
    ("d20", make_icosahedron),
]

NUM_FRAMES = 8
SPRITE_SIZE = 24


def generate_header(output_path):
    """Generate the complete C header file with all dice sprite data."""
    lines = []
    lines.append("/* Auto-generated 3D dice sprites - do not edit manually */")
    lines.append("#pragma once")
    lines.append("")
    lines.append(f"#define DICE_SPRITE_SIZE {SPRITE_SIZE}")
    lines.append(f"#define DICE_SPRITE_FRAMES {NUM_FRAMES}")
    lines.append("")

    all_names = []

    for dice_name, make_fn in DICE_DEFS:
        print(f"Generating {dice_name}...")
        vertices, faces = make_fn()
        arr_name = f"sprite_{dice_name}"
        all_names.append(arr_name)

        bytes_per_frame = (SPRITE_SIZE * ((SPRITE_SIZE + 7) // 8))
        lines.append(f"static const uint8_t {arr_name}[{NUM_FRAMES}][{bytes_per_frame}] = {{")

        for frame_idx in range(NUM_FRAMES):
            angle_y = frame_idx * (2 * math.pi / NUM_FRAMES)
            img = render_frame(vertices, faces, angle_y)
            xbm_bytes = image_to_xbm_bytes(img)

            assert len(xbm_bytes) == bytes_per_frame, \
                f"{dice_name} frame {frame_idx}: expected {bytes_per_frame} bytes, got {len(xbm_bytes)}"

            hex_strs = [f"0x{b:02X}" for b in xbm_bytes]
            # Format: 12 bytes per line for readability
            byte_lines = []
            for i in range(0, len(hex_strs), 12):
                byte_lines.append("        " + ", ".join(hex_strs[i:i+12]))

            comma = "," if frame_idx < NUM_FRAMES - 1 else ""
            lines.append(f"    {{ /* frame {frame_idx} ({math.degrees(angle_y):.0f} deg) */")
            lines.append(",\n".join(byte_lines))
            lines.append(f"    }}{comma}")

        lines.append("};")
        lines.append("")

    # Lookup table indexed by DiceType enum
    lines.append("/* Lookup table indexed by DiceType enum */")
    lines.append(f"static const uint8_t* const dice_sprites[{len(DICE_DEFS)}][{NUM_FRAMES}] = {{")
    for i, name in enumerate(all_names):
        refs = ", ".join(f"{name}[{f}]" for f in range(NUM_FRAMES))
        comma = "," if i < len(all_names) - 1 else ""
        lines.append(f"    {{ {refs} }}{comma}")
    lines.append("};")
    lines.append("")

    header_text = "\n".join(lines)

    with open(output_path, "w") as f:
        f.write(header_text)

    print(f"Wrote {output_path} ({len(header_text)} bytes, {len(lines)} lines)")


if __name__ == "__main__":
    import os
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)
    output_path = os.path.join(project_dir, "dice_sprites.h")
    generate_header(output_path)
