#pragma once

#include <algorithm>
#include <cmath>

#include "math.hpp"

// Камера "от первого лица": позиция + два угла поворота (рысканье и тангаж).
struct Camera {
	math::Vec3 position = { 0.0f, 1.5f, 5.0f };
	float yaw = -90.0f;   // градусы, -90 означает "смотрим вдоль -Z"
	float pitch = -12.0f; // градусы, ограничен, чтобы не перевернуться через голову
	float fov = 60.0f;    // вертикальный угол обзора в градусах
	float near_plane = 0.05f;
	float far_plane = 100.0f;

	math::Vec3 forward() const {
		const float yaw_rad = math::radians(yaw);
		const float pitch_rad = math::radians(pitch);
		return {
			std::cos(pitch_rad) * std::cos(yaw_rad),
			std::sin(pitch_rad),
			std::cos(pitch_rad) * std::sin(yaw_rad),
		};
	}

	math::Vec3 right() const {
		return math::normalize(math::cross(forward(), { 0.0f, 1.0f, 0.0f }));
	}

	void clampAngles() {
		pitch = std::clamp(pitch, -89.0f, 89.0f);
		fov = std::clamp(fov, 10.0f, 120.0f);
	}

	math::Mat4 view() const {
		return math::lookAt(position, position + forward(), { 0.0f, 1.0f, 0.0f });
	}

	math::Mat4 projection(float aspect) const {
		return math::perspective(math::radians(fov), aspect, near_plane, far_plane);
	}
};
