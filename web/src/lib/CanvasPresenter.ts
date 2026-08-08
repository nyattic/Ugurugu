// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nyabi (nyattic)

import { toViewport } from "./ViewTransform";
import type { ViewState } from "./ViewTransform";

export interface DirtyRegion {
    x: number;
    y: number;
    width: number;
    height: number;
}

// Two surfaces on purpose. The document surface is a document-resolution 2D
// canvas that stays the authority for pixels: the eyedropper samples it and
// PNG export encodes it, so both are independent of the current zoom. The
// display surface only ever shows that image under the view transform, which
// is what WebGL 2 accelerates; Canvas 2D is the fallback when no context is
// available, exactly as the feasibility report scoped it.
export class CanvasPresenter {
    #surface: HTMLCanvasElement;
    #surfaceContext: CanvasRenderingContext2D;
    #display: HTMLCanvasElement;
    #gl: WebGL2RenderingContext | null = null;
    #displayContext: CanvasRenderingContext2D | null = null;
    #program: WebGLProgram | null = null;
    #texture: WebGLTexture | null = null;
    #rectangleLocation: WebGLUniformLocation | null = null;
    #documentWidth = 0;
    #documentHeight = 0;
    #textureDirty = true;

    constructor(surface: HTMLCanvasElement, display: HTMLCanvasElement) {
        this.#surface = surface;
        const context = surface.getContext("2d", { willReadFrequently: true });
        if (!context) {
            throw new Error("2D canvas context is unavailable");
        }
        this.#surfaceContext = context;
        this.#display = display;
        this.#initializeDisplay();
    }

    get usingWebGL(): boolean {
        return this.#gl !== null;
    }

    get documentSurface(): HTMLCanvasElement {
        return this.#surface;
    }

    #initializeDisplay() {
        const gl = this.#display.getContext("webgl2", {
            alpha: true,
            antialias: false,
            depth: false,
            stencil: false,
            premultipliedAlpha: false,
        });
        if (!gl) {
            this.#displayContext = this.#display.getContext("2d");
            return;
        }
        const program = createProgram(gl);
        if (!program) {
            this.#displayContext = this.#display.getContext("2d");
            return;
        }
        this.#gl = gl;
        this.#program = program;
        this.#texture = gl.createTexture();
        this.#rectangleLocation = gl.getUniformLocation(program, "uRectangle");
        gl.bindTexture(gl.TEXTURE_2D, this.#texture);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        // Bilinear both ways, because that is what the desktop shows. Its GPU
        // blit samples Nearest, but only after ImageResampler has already
        // resampled the frame to the display size with bilinear taps, so
        // magnifying with NEAREST here would be a visible regression.
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
        // The engine hands out straight (unpremultiplied) RGBA, and the
        // context above was created the same way, so no unpack conversion.
        gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
        gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
        // A context loss leaves every object above invalid; falling back to 2D
        // keeps the shell usable instead of freezing on a blank canvas.
        this.#display.addEventListener("webglcontextlost", (event) => {
            event.preventDefault();
            this.#gl = null;
            this.#program = null;
            this.#texture = null;
            this.#displayContext = this.#display.getContext("2d");
        });
    }

    resizeDocument(width: number, height: number) {
        this.#documentWidth = width;
        this.#documentHeight = height;
        this.#surface.width = width;
        this.#surface.height = height;
        this.#surfaceContext.clearRect(0, 0, width, height);
        this.#textureDirty = true;
    }

    resizeDisplay(width: number, height: number, devicePixelRatio: number) {
        const pixelWidth = Math.max(1, Math.round(width * devicePixelRatio));
        const pixelHeight = Math.max(1, Math.round(height * devicePixelRatio));
        if (
            this.#display.width === pixelWidth &&
            this.#display.height === pixelHeight
        ) {
            return;
        }
        this.#display.width = pixelWidth;
        this.#display.height = pixelHeight;
    }

    writeRegion(region: DirtyRegion, pixels: Uint8ClampedArray<ArrayBuffer>) {
        if (region.width <= 0 || region.height <= 0) {
            return;
        }
        this.#surfaceContext.putImageData(
            new ImageData(pixels, region.width, region.height),
            region.x,
            region.y,
        );
        const gl = this.#gl;
        if (!gl || !this.#texture) {
            return;
        }
        gl.bindTexture(gl.TEXTURE_2D, this.#texture);
        if (this.#textureDirty) {
            gl.texImage2D(
                gl.TEXTURE_2D,
                0,
                gl.RGBA,
                this.#documentWidth,
                this.#documentHeight,
                0,
                gl.RGBA,
                gl.UNSIGNED_BYTE,
                null,
            );
            this.#textureDirty = false;
            // A fresh texture has no pixels outside the dirty rectangle, so
            // the whole surface has to be uploaded once.
            gl.texSubImage2D(
                gl.TEXTURE_2D,
                0,
                0,
                0,
                this.#documentWidth,
                this.#documentHeight,
                gl.RGBA,
                gl.UNSIGNED_BYTE,
                this.#surfaceContext.getImageData(
                    0,
                    0,
                    this.#documentWidth,
                    this.#documentHeight,
                ).data,
            );
            return;
        }
        gl.texSubImage2D(
            gl.TEXTURE_2D,
            0,
            region.x,
            region.y,
            region.width,
            region.height,
            gl.RGBA,
            gl.UNSIGNED_BYTE,
            pixels,
        );
    }

    draw(view: ViewState) {
        const cssWidth = this.#display.clientWidth || this.#display.width;
        const cssHeight = this.#display.clientHeight || this.#display.height;
        const viewport = { width: cssWidth, height: cssHeight };
        const topLeft = toViewport(view, viewport, 0, 0);
        const width = this.#documentWidth * view.scale;
        const height = this.#documentHeight * view.scale;
        const gl = this.#gl;
        if (!gl || !this.#program) {
            const context = this.#displayContext;
            if (!context) {
                return;
            }
            const ratio = this.#display.width / Math.max(1, cssWidth);
            context.setTransform(ratio, 0, 0, ratio, 0, 0);
            context.clearRect(0, 0, cssWidth, cssHeight);
            context.imageSmoothingEnabled = true;
            context.drawImage(
                this.#surface,
                topLeft.x,
                topLeft.y,
                width,
                height,
            );
            return;
        }
        gl.viewport(0, 0, this.#display.width, this.#display.height);
        gl.clearColor(0, 0, 0, 0);
        gl.clear(gl.COLOR_BUFFER_BIT);
        if (this.#documentWidth <= 0 || this.#documentHeight <= 0) {
            return;
        }
        gl.useProgram(this.#program);
        gl.bindTexture(gl.TEXTURE_2D, this.#texture);
        // Clip space rectangle for the document quad: x and y are the top-left
        // corner, z and w the size, all in [-1, 1] with y already flipped.
        gl.uniform4f(
            this.#rectangleLocation,
            (topLeft.x / cssWidth) * 2 - 1,
            1 - (topLeft.y / cssHeight) * 2,
            (width / cssWidth) * 2,
            (height / cssHeight) * 2,
        );
        gl.enable(gl.BLEND);
        gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
        gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
    }

    readPixel(x: number, y: number): [number, number, number] {
        const pixelX = Math.min(
            this.#documentWidth - 1,
            Math.max(0, Math.floor(x)),
        );
        const pixelY = Math.min(
            this.#documentHeight - 1,
            Math.max(0, Math.floor(y)),
        );
        const [red = 0, green = 0, blue = 0] = this.#surfaceContext.getImageData(
            pixelX,
            pixelY,
            1,
            1,
        ).data;
        return [red, green, blue];
    }

    toBlob(type: string): Promise<Blob | null> {
        return new Promise((resolve) => {
            this.#surface.toBlob(resolve, type);
        });
    }
}

const vertexSource = `#version 300 es
uniform vec4 uRectangle;
out vec2 vTexture;
void main() {
    vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    vTexture = corner;
    gl_Position = vec4(
        uRectangle.x + corner.x * uRectangle.z,
        uRectangle.y - corner.y * uRectangle.w,
        0.0,
        1.0);
}`;

const fragmentSource = `#version 300 es
precision mediump float;
uniform sampler2D uSurface;
in vec2 vTexture;
out vec4 fragment;
void main() {
    fragment = texture(uSurface, vTexture);
}`;

function compile(
    gl: WebGL2RenderingContext,
    type: number,
    source: string,
): WebGLShader | null {
    const shader = gl.createShader(type);
    if (!shader) {
        return null;
    }
    gl.shaderSource(shader, source);
    gl.compileShader(shader);
    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
        gl.deleteShader(shader);
        return null;
    }
    return shader;
}

function createProgram(gl: WebGL2RenderingContext): WebGLProgram | null {
    const vertex = compile(gl, gl.VERTEX_SHADER, vertexSource);
    const fragment = compile(gl, gl.FRAGMENT_SHADER, fragmentSource);
    if (!vertex || !fragment) {
        return null;
    }
    const program = gl.createProgram();
    if (!program) {
        return null;
    }
    gl.attachShader(program, vertex);
    gl.attachShader(program, fragment);
    gl.linkProgram(program);
    gl.deleteShader(vertex);
    gl.deleteShader(fragment);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
        gl.deleteProgram(program);
        return null;
    }
    return program;
}
