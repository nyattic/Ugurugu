<script lang="ts">
    let {
        color,
        onchange,
    }: {
        color: string;
        onchange: (hex: string) => void;
    } = $props();

    const size = 176;
    const ringGap = 6;
    const tau = Math.PI * 2;

    const outerRadius = size / 2 - 2;
    const ringThickness = Math.max(10, size * 0.11);
    const innerRadius = outerRadius - ringThickness - ringGap;
    const fieldSide = Math.floor(innerRadius * Math.SQRT2) - 2;
    const fieldStart = size / 2 - fieldSide / 2;

    let canvas: HTMLCanvasElement;
    let hue = $state(0);
    let saturation = $state(0);
    let value = $state(0);
    let lastEmitted = "";
    let dragging: "none" | "ring" | "field" = "none";

    function hexToHsv(hex: string) {
        const match = /^#([0-9a-f]{6})$/i.exec(hex);
        if (!match) {
            return null;
        }
        const packed = Number.parseInt(match[1], 16);
        const red = ((packed >> 16) & 0xff) / 255;
        const green = ((packed >> 8) & 0xff) / 255;
        const blue = (packed & 0xff) / 255;
        const max = Math.max(red, green, blue);
        const min = Math.min(red, green, blue);
        const delta = max - min;
        let h = 0;
        if (delta > 0) {
            if (max === red) {
                h = ((green - blue) / delta + 6) % 6;
            } else if (max === green) {
                h = (blue - red) / delta + 2;
            } else {
                h = (red - green) / delta + 4;
            }
            h /= 6;
        }
        return {
            h,
            s: max > 0 ? delta / max : 0,
            v: max,
        };
    }

    function hsvToHex(h: number, s: number, v: number) {
        const sector = h * 6;
        const chroma = v * s;
        const middle = chroma * (1 - Math.abs((sector % 2) - 1));
        const base = v - chroma;
        let red = 0;
        let green = 0;
        let blue = 0;
        if (sector < 1) {
            [red, green, blue] = [chroma, middle, 0];
        } else if (sector < 2) {
            [red, green, blue] = [middle, chroma, 0];
        } else if (sector < 3) {
            [red, green, blue] = [0, chroma, middle];
        } else if (sector < 4) {
            [red, green, blue] = [0, middle, chroma];
        } else if (sector < 5) {
            [red, green, blue] = [middle, 0, chroma];
        } else {
            [red, green, blue] = [chroma, 0, middle];
        }
        const channel = (component: number) =>
            Math.round((component + base) * 255)
                .toString(16)
                .padStart(2, "0");
        return `#${channel(red)}${channel(green)}${channel(blue)}`;
    }

    function draw() {
        const context = canvas?.getContext("2d");
        if (!context) {
            return;
        }
        const scale = window.devicePixelRatio || 1;
        if (canvas.width !== size * scale) {
            canvas.width = size * scale;
            canvas.height = size * scale;
        }
        context.setTransform(scale, 0, 0, scale, 0, 0);
        context.clearRect(0, 0, size, size);

        const center = size / 2;
        // The ring reads counter-clockwise from the right, the way a colour
        // circle is normally drawn; canvas conic gradients run clockwise, so
        // the hue stops are laid down in reverse.
        const ring = context.createConicGradient(0, center, center);
        for (let stop = 0; stop <= 12; stop += 1) {
            ring.addColorStop(stop / 12, `hsl(${360 - stop * 30} 100% 50%)`);
        }
        context.beginPath();
        context.arc(
            center,
            center,
            (outerRadius + innerRadius + ringGap) / 2,
            0,
            tau,
        );
        context.lineWidth = ringThickness;
        context.strokeStyle = ring;
        context.stroke();

        context.fillStyle = `hsl(${hue * 360} 100% 50%)`;
        context.fillRect(fieldStart, fieldStart, fieldSide, fieldSide);
        const white = context.createLinearGradient(
            fieldStart,
            0,
            fieldStart + fieldSide,
            0,
        );
        white.addColorStop(0, "rgba(255 255 255 / 100%)");
        white.addColorStop(1, "rgba(255 255 255 / 0%)");
        context.fillStyle = white;
        context.fillRect(fieldStart, fieldStart, fieldSide, fieldSide);
        const black = context.createLinearGradient(
            0,
            fieldStart,
            0,
            fieldStart + fieldSide,
        );
        black.addColorStop(0, "rgba(0 0 0 / 0%)");
        black.addColorStop(1, "rgba(0 0 0 / 100%)");
        context.fillStyle = black;
        context.fillRect(fieldStart, fieldStart, fieldSide, fieldSide);

        const markerRadius = (outerRadius + innerRadius + ringGap) / 2;
        const ringMarkerX = center + Math.cos(hue * tau) * markerRadius;
        const ringMarkerY = center - Math.sin(hue * tau) * markerRadius;
        const fieldMarkerX = fieldStart + saturation * fieldSide;
        const fieldMarkerY = fieldStart + (1 - value) * fieldSide;
        for (const [x, y] of [
            [ringMarkerX, ringMarkerY],
            [fieldMarkerX, fieldMarkerY],
        ]) {
            context.beginPath();
            context.arc(x, y, 5, 0, tau);
            context.lineWidth = 2;
            context.strokeStyle = "#ffffff";
            context.stroke();
            context.beginPath();
            context.arc(x, y, 6.5, 0, tau);
            context.lineWidth = 1;
            context.strokeStyle = "rgba(0 0 0 / 60%)";
            context.stroke();
        }
    }

    $effect(() => {
        if (color !== lastEmitted) {
            const parsed = hexToHsv(color);
            if (parsed) {
                // A grey has no hue of its own, so keep the ring where the
                // artist left it, matching the desktop wheel.
                if (parsed.s > 0 && parsed.v > 0) {
                    hue = parsed.h;
                }
                saturation = parsed.s;
                value = parsed.v;
            }
            lastEmitted = color;
        }
        draw();
    });

    function emit() {
        const hex = hsvToHex(hue, saturation, value);
        lastEmitted = hex;
        onchange(hex);
    }

    function localPoint(event: PointerEvent) {
        const rect = canvas.getBoundingClientRect();
        return {
            x: ((event.clientX - rect.left) * size) / rect.width,
            y: ((event.clientY - rect.top) * size) / rect.height,
        };
    }

    function applyRing(point: { x: number; y: number }) {
        const dx = point.x - size / 2;
        const dy = point.y - size / 2;
        hue = ((Math.atan2(-dy, dx) / tau) + 1) % 1;
        emit();
    }

    function applyField(point: { x: number; y: number }) {
        saturation = Math.min(
            1,
            Math.max(0, (point.x - fieldStart) / fieldSide),
        );
        value =
            1 - Math.min(1, Math.max(0, (point.y - fieldStart) / fieldSide));
        emit();
    }

    function onPointerDown(event: PointerEvent) {
        if (event.button !== 0) {
            return;
        }
        const point = localPoint(event);
        const dx = point.x - size / 2;
        const dy = point.y - size / 2;
        const distance = Math.hypot(dx, dy);
        const fieldMargin = 6;
        if (
            point.x >= fieldStart - fieldMargin &&
            point.x <= fieldStart + fieldSide + fieldMargin &&
            point.y >= fieldStart - fieldMargin &&
            point.y <= fieldStart + fieldSide + fieldMargin
        ) {
            dragging = "field";
            applyField(point);
        } else if (distance <= outerRadius + 4 && distance > innerRadius) {
            dragging = "ring";
            applyRing(point);
        } else {
            return;
        }
        canvas.setPointerCapture(event.pointerId);
    }

    function onPointerMove(event: PointerEvent) {
        if (dragging === "none") {
            return;
        }
        const point = localPoint(event);
        if (dragging === "ring") {
            applyRing(point);
        } else {
            applyField(point);
        }
    }

    function onPointerUp(event: PointerEvent) {
        if (dragging === "none") {
            return;
        }
        dragging = "none";
        canvas.releasePointerCapture(event.pointerId);
    }
</script>

<canvas
    id="color-wheel"
    bind:this={canvas}
    style={`width: ${size}px; height: ${size}px`}
    onpointerdown={onPointerDown}
    onpointermove={onPointerMove}
    onpointerup={onPointerUp}
    onpointercancel={onPointerUp}
></canvas>

<style>
    canvas {
        touch-action: none;
        cursor: crosshair;
        align-self: center;
    }
</style>
