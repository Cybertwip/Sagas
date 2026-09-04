from __future__ import annotations

import array
import ctypes
import math
from typing import Optional

from OpenGL import GL
from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QMatrix4x4, QMouseEvent, QVector3D, QWheelEvent
from PySide6.QtOpenGLWidgets import QOpenGLWidget

from .pose import DrawBatch
from .types import RasterImage, Vec3

VERTEX_SHADER = """
#version 410 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_color;
layout(location = 3) in vec2 a_uv;
uniform mat4 u_mvp;
out vec3 v_normal;
out vec4 v_color;
out vec2 v_uv;
out vec3 v_world;
void main() {
    v_world = a_position;
    v_normal = a_normal;
    v_color = a_color;
    v_uv = a_uv;
    gl_Position = u_mvp * vec4(a_position, 1.0);
}
"""

FRAGMENT_SHADER = """
#version 410 core
in vec3 v_normal;
in vec4 v_color;
in vec2 v_uv;
in vec3 v_world;
uniform sampler2D u_texture;
uniform bool u_use_texture;
uniform bool u_use_lighting;
uniform bool u_wireframe;
uniform bool u_highlight;
uniform ivec2 u_texture_mode;
uniform ivec2 u_texture_mask;
uniform vec2 u_texture_window;
out vec4 fragment;

float n64Coordinate(float normalized, float extent, int mode, int maskBits, float windowSize) {
    float value = normalized * extent;
    if ((mode & 2) != 0) value = clamp(value, 0.0, max(windowSize - 1.0, 0.0));
    float period = maskBits > 0 ? exp2(float(maskBits)) : extent;
    float section = floor(value / period);
    float sampled = mod(value, period);
    if (sampled < 0.0) sampled += period;
    if ((mode & 1) != 0 && (int(section) & 1) != 0) sampled = period - sampled;
    return sampled / extent;
}

void main() {
    vec4 texel = vec4(1.0);
    if (u_use_texture) {
        vec2 extent = vec2(textureSize(u_texture, 0));
        vec2 uv = vec2(
            n64Coordinate(v_uv.x, extent.x, u_texture_mode.x, u_texture_mask.x, u_texture_window.x),
            n64Coordinate(v_uv.y, extent.y, u_texture_mode.y, u_texture_mask.y, u_texture_window.y));
        texel = texture(u_texture, uv);
    }
    vec3 albedo = texel.rgb * v_color.rgb;
    float alpha = texel.a * v_color.a;
    if (alpha < 0.004) discard;
    vec3 color = albedo;
    if (u_use_lighting) {
        vec3 normal = normalize(v_normal);
        vec3 light = normalize(vec3(-0.35, 0.72, 0.60));
        float wrap = abs(dot(normal, light));
        color = albedo * (0.42 + wrap * 0.58);
    }
    if (u_wireframe) color = vec3(0.95, 0.82, 0.35);
    if (u_highlight) color = mix(color, vec3(0.2, 1.0, 0.45), 0.45);
    fragment = vec4(color, alpha);
}
"""


def _compile(kind: int, source: str) -> int:
    shader = GL.glCreateShader(kind)
    GL.glShaderSource(shader, source)
    GL.glCompileShader(shader)
    if not GL.glGetShaderiv(shader, GL.GL_COMPILE_STATUS):
        log = GL.glGetShaderInfoLog(shader)
        GL.glDeleteShader(shader)
        raise RuntimeError(log.decode() if isinstance(log, bytes) else str(log))
    return shader


class SequenceGLView(QOpenGLWidget):
    picked = Signal(int, int)  # node, material

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.batches: list[DrawBatch] = []
        self.yaw = 0.6
        self.pitch = 0.35
        self.distance = 2800.0
        self.target = Vec3(0, 400, 0)
        self.fov = 35.0
        self.near = 16.0
        self.far = 65536.0
        self.use_lighting = True
        self.use_textures = True
        self.wireframe = False
        self.isolate_node: Optional[int] = None
        self.isolate_material: Optional[int] = None
        self._program = 0
        self._vao = 0
        self._vbo = 0
        self._textures: dict[str, int] = {}
        self._drag = None
        self._vertex_count = 0
        self.setMinimumSize(480, 360)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)

    def set_batches(self, batches: list[DrawBatch], frame_camera: bool = False,
                    eye: Optional[Vec3] = None, at: Optional[Vec3] = None,
                    fov: Optional[float] = None) -> None:
        self.batches = batches
        if frame_camera and batches:
            xs, ys, zs = [], [], []
            for batch in batches:
                for vertex in batch.vertices:
                    xs.append(vertex.x)
                    ys.append(vertex.y)
                    zs.append(vertex.z)
            if xs:
                self.target = Vec3((min(xs) + max(xs)) * 0.5,
                                   (min(ys) + max(ys)) * 0.5,
                                   (min(zs) + max(zs)) * 0.5)
                span = max(max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs), 1.0)
                self.distance = span * 1.8
        if eye and at:
            self.target = at
            dx, dy, dz = eye.x - at.x, eye.y - at.y, eye.z - at.z
            self.distance = max(math.sqrt(dx * dx + dy * dy + dz * dz), 1.0)
            self.yaw = math.atan2(dx, dz)
            hyp = math.sqrt(dx * dx + dz * dz)
            self.pitch = math.atan2(dy, max(hyp, 1.0))
        if fov:
            self.fov = fov
        self._upload()
        self.update()

    def initializeGL(self) -> None:
        GL.glEnable(GL.GL_DEPTH_TEST)
        GL.glEnable(GL.GL_BLEND)
        GL.glBlendFunc(GL.GL_SRC_ALPHA, GL.GL_ONE_MINUS_SRC_ALPHA)
        GL.glClearColor(0.08, 0.09, 0.11, 1.0)
        vertex = _compile(GL.GL_VERTEX_SHADER, VERTEX_SHADER)
        fragment = _compile(GL.GL_FRAGMENT_SHADER, FRAGMENT_SHADER)
        self._program = GL.glCreateProgram()
        GL.glAttachShader(self._program, vertex)
        GL.glAttachShader(self._program, fragment)
        GL.glLinkProgram(self._program)
        GL.glDeleteShader(vertex)
        GL.glDeleteShader(fragment)
        self._vao = GL.glGenVertexArrays(1)
        self._vbo = GL.glGenBuffers(1)
        if self.batches:
            self._upload()

    def _upload(self) -> None:
        if not self._vao:
            return
        self.makeCurrent()
        floats: list[float] = []
        for batch in self.batches:
            if self.isolate_node is not None and batch.node_index != self.isolate_node:
                continue
            if self.isolate_material is not None and batch.material_index != self.isolate_material:
                continue
            for vertex in batch.vertices:
                floats.extend((
                    vertex.x, vertex.y, vertex.z,
                    vertex.nx, vertex.ny, vertex.nz,
                    vertex.r, vertex.g, vertex.b, vertex.a,
                    vertex.u, vertex.v,
                ))
        self._vertex_count = len(floats) // 12
        payload = array.array("f", floats)
        GL.glBindVertexArray(self._vao)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, self._vbo)
        GL.glBufferData(GL.GL_ARRAY_BUFFER, payload.tobytes(), GL.GL_DYNAMIC_DRAW)
        stride = 12 * 4
        GL.glEnableVertexAttribArray(0)
        GL.glVertexAttribPointer(0, 3, GL.GL_FLOAT, GL.GL_FALSE, stride, ctypes.c_void_p(0))
        GL.glEnableVertexAttribArray(1)
        GL.glVertexAttribPointer(1, 3, GL.GL_FLOAT, GL.GL_FALSE, stride, ctypes.c_void_p(12))
        GL.glEnableVertexAttribArray(2)
        GL.glVertexAttribPointer(2, 4, GL.GL_FLOAT, GL.GL_FALSE, stride, ctypes.c_void_p(24))
        GL.glEnableVertexAttribArray(3)
        GL.glVertexAttribPointer(3, 2, GL.GL_FLOAT, GL.GL_FALSE, stride, ctypes.c_void_p(40))
        GL.glBindVertexArray(0)

    def _texture(self, image: RasterImage) -> int:
        handle = self._textures.get(image.key)
        if handle:
            return handle
        handle = GL.glGenTextures(1)
        GL.glBindTexture(GL.GL_TEXTURE_2D, handle)
        GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MIN_FILTER, GL.GL_NEAREST)
        GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MAG_FILTER, GL.GL_NEAREST)
        GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_WRAP_S, GL.GL_REPEAT)
        GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_WRAP_T, GL.GL_REPEAT)
        GL.glPixelStorei(GL.GL_UNPACK_ALIGNMENT, 1)
        GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, GL.GL_RGBA8, image.width, image.height, 0,
                        GL.GL_RGBA, GL.GL_UNSIGNED_BYTE, image.rgba)
        self._textures[image.key] = int(handle)
        return int(handle)

    def paintGL(self) -> None:
        GL.glClear(GL.GL_COLOR_BUFFER_BIT | GL.GL_DEPTH_BUFFER_BIT)
        if not self._program:
            return
        aspect = max(self.width() / max(self.height(), 1), 0.05)
        projection = QMatrix4x4()
        projection.perspective(self.fov, aspect, self.near, self.far)
        eye = QVector3D(
            self.target.x + self.distance * math.sin(self.yaw) * math.cos(self.pitch),
            self.target.y + self.distance * math.sin(self.pitch),
            self.target.z + self.distance * math.cos(self.yaw) * math.cos(self.pitch),
        )
        view = QMatrix4x4()
        view.lookAt(eye, QVector3D(self.target.x, self.target.y, self.target.z), QVector3D(0, 1, 0))
        mvp = projection * view
        GL.glUseProgram(self._program)
        location = GL.glGetUniformLocation(self._program, b"u_mvp")
        values = array.array("f")
        for column in range(4):
            vector = mvp.column(column)
            values.extend((vector.x(), vector.y(), vector.z(), vector.w()))
        # PyOpenGL's array handlers accept bytes/numpy, not array.array.
        GL.glUniformMatrix4fv(location, 1, GL.GL_FALSE, values.tobytes())
        GL.glPolygonMode(GL.GL_FRONT_AND_BACK, GL.GL_LINE if self.wireframe else GL.GL_FILL)
        GL.glBindVertexArray(self._vao)
        first = 0
        for batch in self.batches:
            if self.isolate_node is not None and batch.node_index != self.isolate_node:
                continue
            if self.isolate_material is not None and batch.material_index != self.isolate_material:
                continue
            count = len(batch.vertices)
            if count == 0:
                continue
            use_texture = bool(self.use_textures and batch.texture)
            if use_texture:
                GL.glActiveTexture(GL.GL_TEXTURE0)
                GL.glBindTexture(GL.GL_TEXTURE_2D, self._texture(batch.texture))
            GL.glUniform1i(GL.glGetUniformLocation(self._program, b"u_use_texture"), int(use_texture))
            GL.glUniform1i(GL.glGetUniformLocation(self._program, b"u_use_lighting"), int(self.use_lighting and batch.lit))
            GL.glUniform1i(GL.glGetUniformLocation(self._program, b"u_wireframe"), int(self.wireframe))
            highlight = (self.isolate_material is not None and batch.material_index == self.isolate_material)
            GL.glUniform1i(GL.glGetUniformLocation(self._program, b"u_highlight"), int(highlight))
            GL.glUniform2i(GL.glGetUniformLocation(self._program, b"u_texture_mode"),
                           batch.texture_mode_s, batch.texture_mode_t)
            GL.glUniform2i(GL.glGetUniformLocation(self._program, b"u_texture_mask"),
                           batch.texture_mask_s, batch.texture_mask_t)
            GL.glUniform2f(GL.glGetUniformLocation(self._program, b"u_texture_window"),
                           max(batch.texture_window_s, 1), max(batch.texture_window_t, 1))
            if batch.translucent:
                GL.glDepthMask(GL.GL_FALSE)
            GL.glDrawArrays(GL.GL_TRIANGLES, first, count)
            GL.glDepthMask(GL.GL_TRUE)
            first += count
        GL.glPolygonMode(GL.GL_FRONT_AND_BACK, GL.GL_FILL)
        GL.glBindVertexArray(0)

    def mousePressEvent(self, event: QMouseEvent) -> None:
        self._drag = (event.position().x(), event.position().y(), event.buttons())

    def mouseMoveEvent(self, event: QMouseEvent) -> None:
        if self._drag is None:
            return
        x, y, buttons = self._drag
        dx = event.position().x() - x
        dy = event.position().y() - y
        self._drag = (event.position().x(), event.position().y(), buttons)
        if buttons & Qt.MouseButton.LeftButton:
            self.yaw += dx * 0.005
            self.pitch = max(-1.4, min(1.4, self.pitch + dy * 0.005))
        elif buttons & Qt.MouseButton.RightButton:
            self.target = Vec3(self.target.x - dx * self.distance * 0.001,
                               self.target.y + dy * self.distance * 0.001,
                               self.target.z)
        self.update()

    def mouseReleaseEvent(self, event: QMouseEvent) -> None:
        self._drag = None

    def wheelEvent(self, event: QWheelEvent) -> None:
        steps = event.angleDelta().y() / 120.0
        self.distance = max(40.0, self.distance * (0.9 ** steps))
        self.update()

    def resizeGL(self, w: int, h: int) -> None:
        GL.glViewport(0, 0, w, h)
