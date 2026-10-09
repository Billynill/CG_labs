#pragma once

#include <array>
#include <cstdint>

namespace cube {
    struct Vertex {
        float position[3];
        float color[3];
    };

    inline constexpr std::array<Vertex, 8> vertices = {{ //пункт 5 управление цветами
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, 0.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f, 0.0f}},
        {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},

        {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 1.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}},
        {{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 1.0f}},
    }};

    inline constexpr std::array<std::uint32_t, 36> indices = {{
        0, 3, 2,  2, 1, 0, // задняя грань
        4, 5, 6,  6, 7, 4, // передняя грань
        0, 4, 7,  7, 3, 0, // левая грань
        1, 2, 6,  6, 5, 1, // правая грань
        0, 1, 5,  5, 4, 0, // нижняя грань
        3, 7, 6,  6, 2, 3, // верхняя грань
    }};
} // namespace cube;
