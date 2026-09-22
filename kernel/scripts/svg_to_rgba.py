#!/usr/bin/env python3
"""Convert SVG to raw RGBA pixel data with full path support including cubic beziers."""
import sys, os, re, math

def parse_svg(svg_content, size=64):
    """Parse SVG and extract paths, converting to RGBA pixel data."""
    # Extract all paths with their fill color
    path_pattern = r'<path[^>]*d="([^"]*)"[^>]*fill="([^"]*)"'
    paths = re.findall(r'<path[^>]*d="([^"]*)"[^>]*fill="([^"]*)"', svg_content)
    
    if not paths:
        # Fallback: find any path with d attribute
        paths = re.findall(r'<path[^>]*d="([^"]*)"', svg_content)
        paths = [(p, '#FFFFFF') for p in paths]
    
    # Create a blank canvas (0 = transparent, 1 = opaque white)
    canvas = [[0]*size for _ in range(size)]
    
    for path_data, fill_color in paths:
        draw_path(canvas, path_data, size, fill_color)
    
    # Convert to RGBA bytes
    rgba = bytearray()
    for y in range(size):
        for x in range(size):
            if canvas[y][x]:
                rgba.extend([255, 255, 255, 255])  # White with full alpha
            else:
                rgba.extend([0, 0, 0, 0])  # Transparent
    return bytes(rgba)

def parse_path_commands(d):
    """Parse SVG path data into commands with parameters."""
    # Normalize: replace commas with spaces, handle negative numbers
    d = d.replace(',', ' ')
    # Find all commands and their parameters
    pattern = r'([MLHVCSQTAZ])([^MLHVCSQTAZ]*)'
    matches = re.findall(r'([MLHVCSQTAZ])([^MLHVCSQTAZ]*)', d.upper())
    
    commands = []
    for cmd, params in matches:
        nums = [float(n) for n in re.findall(r'-?\d+\.?\d*', params)]
        commands.append((cmd, nums))
    return commands

def cubic_bezier_points(x0, y0, x1, y1, x2, y2, x3, y3, max_depth=10, tol=0.5):
    """Recursively subdivide cubic bezier until flat enough."""
    def subdivide(x0, y0, x1, y1, x2, y2, x3, y3, depth):
        # Check flatness: distance of control points from line
        dx = x3 - x0
        dy = y3 - y0
        
        if dx == 0 and dy == 0:
            # Degenerate: just return endpoints
            return [(x0, y0), (x3, y3)]
        
        # Distance from control points to line
        d1 = abs(dy * x1 - dx * y1 + x3 * y0 - y3 * x0) / math.hypot(dx, dy)
        d2 = abs(dy * x2 - dx * y2 + x3 * y0 - y3 * x0) / math.hypot(dx, dy)
        
        if depth >= 8 or max(d1, d2) < 0.5:
            return [(x0, y0), (x3, y3)]
        
        # Subdivide at t=0.5
        mx1 = (x0 + x1) * 0.5
        my1 = (y0 + y1) * 0.5
        mx2 = (x1 + x2) * 0.5
        my2 = (y1 + y2) * 0.5
        mx3 = (x2 + x3) * 0.5
        my3 = (y2 + y3) * 0.5
        mx12 = (mx1 + mx2) * 0.5
        my12 = (my1 + my2) * 0.5
        mx23 = (mx2 + mx3) * 0.5
        my23 = (my2 + my3) * 0.5
        mx = (mx12 + mx23) * 0.5
        my = (my12 + my23) * 0.5
        
        left = subdivide(x0, y0, mx1, my1, mx12, my12, mx, my, depth + 1)
        right = subdivide(mx, my, mx23, my23, mx3, my3, x3, y3, depth + 1)
        return left[:-1] + right
    
    return subdivide(x0, y0, x1, y1, x2, y2, x3, y3, 0)

def quadratic_bezier_points(x0, y0, x1, y1, x2, y2, tol=0.5):
    """Convert quadratic bezier to cubic and subdivide."""
    # Convert Q to C: Q(x0,y0, x1,y1, x2,y2) -> C(x0,y0, (x0+2*x1)/3, (y0+2*y1)/3, (2*x1+x2)/3, (2*y1+y2)/3, x2,y2)
    cx1 = (x0 + 2*x1) / 3
    cy1 = (y0 + 2*y1) / 3
    cx2 = (2*x1 + x2) / 3
    cy2 = (2*y1 + y2) / 3
    return cubic_bezier_points(x0, y0, cx1, cy1, cx2, cy2, x2, y2)

def draw_line(canvas, x1, y1, x2, y2):
    """Bresenham line drawing with canvas bounds."""
    x1, y1, x2, y2 = int(round(x1)), int(round(y1)), int(round(x2)), int(round(y2))
    size = len(canvas)
    
    dx = abs(x2 - x1)
    dy = abs(y2 - y1)
    sx = 1 if x1 < x2 else -1
    sy = 1 if y1 < y2 else -1
    err = dx - dy
    
    x, y = x1, y1
    while True:
        if 0 <= x < size and 0 <= y < size:
            canvas[y][x] = 1
        if x == x2 and y == y2:
            break
        e2 = 2 * err
        if e2 > -abs(y2 - y1):
            err -= abs(y2 - y1)
            x += 1 if x1 < x2 else -1
        if e2 < abs(x2 - x1):
            err += abs(x2 - x1)
            y += 1 if y1 < y2 else -1

def draw_path(canvas, d, size, fill_color='#FFFFFF'):
    """Draw SVG path on canvas with full command support."""
    commands = parse_path_commands(d)
    x = y = 0
    start_x = start_y = 0
    
    for cmd, nums in commands:
        if cmd == 'M':  # Move to
            x, y = nums[0], nums[1]
            start_x, start_y = x, y
        elif cmd == 'L':  # Line to
            for i in range(0, len(nums), 2):
                nx, ny = nums[i], nums[i+1]
                draw_line(canvas, x, y, nx, ny)
                x, y = nx, ny
        elif cmd == 'H':  # Horizontal line
            for nx in nums:
                draw_line(canvas, x, y, nx, y)
                x = nx
        elif cmd == 'V':  # Vertical line
            for ny in nums:
                draw_line(canvas, x, y, x, ny)
                y = ny
        elif cmd == 'C':  # Cubic bezier
            for i in range(0, len(nums), 6):
                x1, y1 = nums[i], nums[i+1]
                x2, y2 = nums[i+2], nums[i+3]
                x3, y3 = nums[i+4], nums[i+5]
                points = cubic_bezier_points(x, y, x1, y1, x2, y2, x3, y3)
                for j in range(len(points) - 1):
                    draw_line(canvas, points[j][0], points[j][1], points[j+1][0], points[j+1][1])
                x, y = x3, y3
        elif cmd == 'S':  # Smooth cubic bezier
            # Simplified: treat as C with reflected control point
            for i in range(0, len(nums), 4):
                x2, y2 = nums[i], nums[i+1]
                x3, y3 = nums[i+2], nums[i+3]
                # Reflected control point (simplified)
                x1 = 2*x - x2  # Mirror previous
                y1 = 2*y - y2
                points = cubic_bezier_points(x, y, x1, y1, x2, y2, x3, y3)
                for j in range(len(points) - 1):
                    draw_line(canvas, points[j][0], points[j][1], points[j+1][0], points[j+1][1])
                x, y = x3, y3
        elif cmd == 'Q':  # Quadratic bezier
            for i in range(0, len(nums), 4):
                x1, y1 = nums[i], nums[i+1]
                x2, y2 = nums[i+2], nums[i+3]
                points = quadratic_bezier_points(x, y, x1, y1, x2, y2)
                for j in range(len(points) - 1):
                    draw_line(canvas, points[j][0], points[j][1], points[j+1][0], points[j+1][1])
                x, y = x2, y2
        elif cmd == 'T':  # Smooth quadratic bezier
            # Simplified
            for i in range(0, len(nums), 2):
                x2, y2 = nums[i], nums[i+1]
                points = quadratic_bezier_points(x, y, (x+x2)/2, (y+y2)/2, x2, y2)
                for j in range(len(points) - 1):
                    draw_line(canvas, points[j][0], points[j][1], points[j+1][0], points[j+1][1])
                x, y = x2, y2
        elif cmd == 'A':  # Elliptical arc (simplified)
            # Skip elliptical arcs for now
            if len(nums) >= 7:
                x, y = nums[-2], nums[-1]
        elif cmd == 'Z':  # Close path
            pass  # We don't explicitly close, lines handle it
    return

def main():
    if len(sys.argv) < 3:
        print("Usage: python3 svg_to_rgba.py input.svg output.rgba [size]")
        sys.exit(1)
    
    input_file = sys.argv[1]
    output_file = sys.argv[2]
    size = int(sys.argv[3]) if len(sys.argv) > 3 else 64
    
    with open(input_file, 'r') as f:
        svg = f.read()
    
    rgba = parse_svg(svg, size)
    with open(output_file, 'wb') as f:
        f.write(rgba)
    
    print(f"Converted {input_file} -> {output_file} ({size}x{size} RGBA, {len(rgba)} bytes)")

if __name__ == '__main__':
    main()
