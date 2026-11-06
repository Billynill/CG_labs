#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "math.hpp"

namespace cube {

struct Vertex {
	math::Vec3 position;
	math::Vec3 normal;
};

struct Mesh {
	std::vector<Vertex> vertices;
	std::vector<std::uint32_t> indices;
};

// Нормали генерируются процедурно: для каждой грани берём векторное
// произведение двух её рёбер. Угол куба принадлежит трём граням с разными
// нормалями, поэтому вершины дублируются: 6 граней * 4 вершины = 24 вершины.
inline Mesh createMesh() {
	const std::array<math::Vec3, 8> corners = {{
		{ -0.5f, -0.5f, -0.5f }, {  0.5f, -0.5f, -0.5f },
		{  0.5f,  0.5f, -0.5f }, { -0.5f,  0.5f, -0.5f },
		{ -0.5f, -0.5f,  0.5f }, {  0.5f, -0.5f,  0.5f },
		{  0.5f,  0.5f,  0.5f }, { -0.5f,  0.5f,  0.5f },
	}};

	// Вершины каждой грани перечислены против часовой стрелки,
	// если смотреть на грань снаружи, поэтому нормаль смотрит наружу.
	const std::array<std::array<std::uint32_t, 4>, 6> faces = {{
		{{ 0, 3, 2, 1 }}, // задняя  (-Z)
		{{ 4, 5, 6, 7 }}, // передняя (+Z)
		{{ 0, 4, 7, 3 }}, // левая   (-X)
		{{ 1, 2, 6, 5 }}, // правая  (+X)
		{{ 0, 1, 5, 4 }}, // нижняя  (-Y)
		{{ 3, 7, 6, 2 }}, // верхняя (+Y)
	}};

	Mesh mesh;
	for (const auto& face : faces) {
		const math::Vec3 a = corners[face[0]];
		const math::Vec3 b = corners[face[1]];
		const math::Vec3 c = corners[face[2]];
		const math::Vec3 normal = math::normalize(math::cross(b - a, c - a));

		const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
		for (std::uint32_t corner : face) {
			mesh.vertices.push_back({ corners[corner], normal });
		}
		// Две треугольные половинки квадратной грани.
		mesh.indices.insert(mesh.indices.end(), {
			first, first + 1, first + 2,
			first + 2, first + 3, first,
		});
	}
	return mesh;
}

} // namespace cube
