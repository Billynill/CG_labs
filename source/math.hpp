#pragma once

#include <cmath>

// Минимальная математика для лабораторной: векторы и матрицы 4x4.
// Матрицы хранятся по столбцам (column-major), как ожидает GLSL.
namespace math {

constexpr float pi = 3.14159265358979f;

inline float radians(float degrees) { return degrees * pi / 180.0f; }

struct Vec3 {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
};

// Используется в структурах для GPU, где поля выравниваются по 16 байт.
struct Vec4 {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	float w = 0.0f;
};

inline Vec4 toVec4(Vec3 v, float w) { return { v.x, v.y, v.z, w }; }

inline Vec3 operator+(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vec3 operator*(Vec3 v, float s) { return { v.x * s, v.y * s, v.z * s }; }

inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Vec3 cross(Vec3 a, Vec3 b) {
	return {
		a.y * b.z - a.z * b.y,
		a.z * b.x - a.x * b.z,
		a.x * b.y - a.y * b.x,
	};
}

inline float length(Vec3 v) { return std::sqrt(dot(v, v)); }

// Если вектор нулевой, возвращаем запасное направление, чтобы не делить на ноль.
inline Vec3 normalize(Vec3 v, Vec3 fallback = { 0.0f, -1.0f, 0.0f }) {
	const float len = length(v);
	return len > 1e-6f ? v * (1.0f / len) : fallback;
}

// Ортогональный к v единичный вектор (нужен для построения конуса прожектора).
inline Vec3 anyPerpendicular(Vec3 v) {
	const Vec3 helper = std::fabs(v.y) < 0.99f ? Vec3{ 0.0f, 1.0f, 0.0f } : Vec3{ 1.0f, 0.0f, 0.0f };
	return normalize(cross(v, helper));
}

struct Mat4 {
	float m[16]{}; // m[column * 4 + row]

	float& at(int row, int column) { return m[column * 4 + row]; }
	float at(int row, int column) const { return m[column * 4 + row]; }
};

inline Mat4 identity() {
	Mat4 result;
	result.at(0, 0) = result.at(1, 1) = result.at(2, 2) = result.at(3, 3) = 1.0f;
	return result;
}

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
	Mat4 result;
	for (int row = 0; row < 4; ++row) {
		for (int column = 0; column < 4; ++column) {
			float sum = 0.0f;
			for (int k = 0; k < 4; ++k) {
				sum += a.at(row, k) * b.at(k, column);
			}
			result.at(row, column) = sum;
		}
	}
	return result;
}

inline Mat4 translation(Vec3 t) {
	Mat4 result = identity();
	result.at(0, 3) = t.x;
	result.at(1, 3) = t.y;
	result.at(2, 3) = t.z;
	return result;
}

inline Mat4 scale(Vec3 s) {
	Mat4 result = identity();
	result.at(0, 0) = s.x;
	result.at(1, 1) = s.y;
	result.at(2, 2) = s.z;
	return result;
}

inline Mat4 rotationX(float angle) {
	Mat4 result = identity();
	const float c = std::cos(angle), s = std::sin(angle);
	result.at(1, 1) = c; result.at(1, 2) = -s;
	result.at(2, 1) = s; result.at(2, 2) = c;
	return result;
}

inline Mat4 rotationY(float angle) {
	Mat4 result = identity();
	const float c = std::cos(angle), s = std::sin(angle);
	result.at(0, 0) = c;  result.at(0, 2) = s;
	result.at(2, 0) = -s; result.at(2, 2) = c;
	return result;
}

inline Mat4 rotationZ(float angle) {
	Mat4 result = identity();
	const float c = std::cos(angle), s = std::sin(angle);
	result.at(0, 0) = c; result.at(0, 1) = -s;
	result.at(1, 0) = s; result.at(1, 1) = c;
	return result;
}

// Углы в градусах, порядок применения: сначала X, потом Y, потом Z.
inline Mat4 rotationXYZ(Vec3 degrees) {
	return rotationZ(radians(degrees.z)) * rotationY(radians(degrees.y)) * rotationX(radians(degrees.x));
}

// Правосторонняя система координат: камера смотрит вдоль -Z в пространстве вида.
inline Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) {
	const Vec3 forward = normalize(target - eye, { 0.0f, 0.0f, -1.0f });
	const Vec3 right = normalize(cross(forward, up), { 1.0f, 0.0f, 0.0f });
	const Vec3 camera_up = cross(right, forward);

	Mat4 result = identity();
	result.at(0, 0) = right.x;      result.at(0, 1) = right.y;      result.at(0, 2) = right.z;
	result.at(1, 0) = camera_up.x;  result.at(1, 1) = camera_up.y;  result.at(1, 2) = camera_up.z;
	result.at(2, 0) = -forward.x;   result.at(2, 1) = -forward.y;   result.at(2, 2) = -forward.z;
	result.at(0, 3) = -dot(right, eye);
	result.at(1, 3) = -dot(camera_up, eye);
	result.at(2, 3) = dot(forward, eye);
	return result;
}

// Перспектива под Vulkan: глубина в [0, 1], ось Y экрана направлена вниз, поэтому -f.
inline Mat4 perspective(float fov_y, float aspect, float near_plane, float far_plane) {
	const float f = 1.0f / std::tan(fov_y * 0.5f);
	Mat4 result;
	result.at(0, 0) = f / aspect;
	result.at(1, 1) = -f;
	result.at(2, 2) = far_plane / (near_plane - far_plane);
	result.at(2, 3) = near_plane * far_plane / (near_plane - far_plane);
	result.at(3, 2) = -1.0f;
	return result;
}

} // namespace math
