#include <cmath>
#ifdef WIN32
#include <SDL.h>
#undef main
#else
#include <SDL2/SDL.h>
#endif

#include <GL/glew.h>

#include <string_view>
#include <stdexcept>
#include <iostream>
#include <chrono>
#include <vector>

std::string to_string(std::string_view str)
{
    return std::string(str.begin(), str.end());
}

void sdl2_fail(std::string_view message)
{
    throw std::runtime_error(to_string(message) + SDL_GetError());
}

void glew_fail(std::string_view message, GLenum error)
{
    throw std::runtime_error(to_string(message) + reinterpret_cast<const char *>(glewGetErrorString(error)));
}

const char vertex_shader_source[] =
R"(#version 330 core

uniform mat4 view;

layout (location = 0) in vec2 in_position;
layout (location = 1) in vec4 in_color;

out vec4 color;

void main()
{
    gl_Position = view * vec4(in_position, 0.0, 1.0);
    color = in_color;
}
)";

const char fragment_shader_source[] =
R"(#version 330 core

in vec4 color;

layout (location = 0) out vec4 out_color;

void main()
{
    out_color = color;
}
)";

const char bezier_vertex_shader_source[] =
R"(#version 330 core

uniform mat4 view;

layout (location = 0) in vec2 in_position;
layout (location = 1) in vec4 in_color;
layout (location = 2) in float in_dist;

out vec4 color;
out float dist;

void main()
{
    gl_Position = view * vec4(in_position, 0.0, 1.0);
    color = in_color;
    dist = in_dist;
}
)";

const char bezier_fragment_shader_source[] =
R"(#version 330 core

in vec4 color;
in float dist;

const float modulo = 40.0;
const float modulo_thrashold = modulo / 2;

layout (location = 0) out vec4 out_color;

void main()
{
    if (mod(dist, modulo) < modulo_thrashold) {
        discard;
    }
    out_color = color;
}
)";

GLuint create_shader(GLenum type, const char * source)
{
    GLuint result = glCreateShader(type);
    glShaderSource(result, 1, &source, nullptr);
    glCompileShader(result);
    GLint status;
    glGetShaderiv(result, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE)
    {
        GLint info_log_length;
        glGetShaderiv(result, GL_INFO_LOG_LENGTH, &info_log_length);
        std::string info_log(info_log_length, '\0');
        glGetShaderInfoLog(result, info_log.size(), nullptr, info_log.data());
        throw std::runtime_error("Shader compilation failed: " + info_log);
    }
    return result;
}

GLuint create_program(GLuint vertex_shader, GLuint fragment_shader)
{
    GLuint result = glCreateProgram();
    glAttachShader(result, vertex_shader);
    glAttachShader(result, fragment_shader);
    glLinkProgram(result);

    GLint status;
    glGetProgramiv(result, GL_LINK_STATUS, &status);
    if (status != GL_TRUE)
    {
        GLint info_log_length;
        glGetProgramiv(result, GL_INFO_LOG_LENGTH, &info_log_length);
        std::string info_log(info_log_length, '\0');
        glGetProgramInfoLog(result, info_log.size(), nullptr, info_log.data());
        throw std::runtime_error("Program linkage failed: " + info_log);
    }

    return result;
}

struct vec2
{
    float x;
    float y;
};

struct vertex
{
    vec2 position;
    std::uint8_t color[4];
};

struct bezier_vertex : public vertex {
    float dist;
};

vec2 bezier(std::vector<vertex> const & vertices, float t)
{
    std::vector<vec2> points(vertices.size());

    for (std::size_t i = 0; i < vertices.size(); ++i)
        points[i] = vertices[i].position;

    // De Casteljau's algorithm
    for (std::size_t k = 0; k + 1 < vertices.size(); ++k) {
        for (std::size_t i = 0; i + k + 1 < vertices.size(); ++i) {
            points[i].x = points[i].x * (1.f - t) + points[i + 1].x * t;
            points[i].y = points[i].y * (1.f - t) + points[i + 1].y * t;
        }
    }
    return points[0];
}

int main() try
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        sdl2_fail("SDL_Init: ");

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

    SDL_Window * window = SDL_CreateWindow("Graphics course practice 3",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        800, 600,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED);

    if (!window)
        sdl2_fail("SDL_CreateWindow: ");

    int width, height;
    SDL_GetWindowSize(window, &width, &height);

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context)
        sdl2_fail("SDL_GL_CreateContext: ");

    SDL_GL_SetSwapInterval(0);

    if (auto result = glewInit(); result != GLEW_NO_ERROR)
        glew_fail("glewInit: ", result);

    if (!GLEW_VERSION_3_3)
        throw std::runtime_error("OpenGL 3.3 is not supported");

    glClearColor(0.3f, 0.3f, 0.3f, 0.f);

    auto vertex_shader = create_shader(GL_VERTEX_SHADER, vertex_shader_source);
    auto fragment_shader = create_shader(GL_FRAGMENT_SHADER, fragment_shader_source);
    auto program = create_program(vertex_shader, fragment_shader);

    auto bezier_vertex_shader = create_shader(GL_VERTEX_SHADER, bezier_vertex_shader_source);
    auto bezier_fragment_shader = create_shader(GL_FRAGMENT_SHADER, bezier_fragment_shader_source);
    auto bezier_program = create_program(bezier_vertex_shader, bezier_fragment_shader);

    GLuint view_location = glGetUniformLocation(program, "view");
    GLuint bezier_view_location = glGetUniformLocation(bezier_program, "view");

    auto last_frame_start = std::chrono::high_resolution_clock::now();

    float time = 0.f;

    std::vector<vertex> bezier_pts;
    std::vector<bezier_vertex> bezier_spline;

    size_t quality = 4;
    
    GLuint vbos[2];
    GLuint& vbo_pts = vbos[0], &vbo_spline = vbos[1];
    glGenBuffers(2, vbos);

    auto update_pts_vbo = [&bezier_pts, &vbo_pts]() {
        glBindBuffer(GL_ARRAY_BUFFER, vbo_pts);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertex) * bezier_pts.size(), bezier_pts.data(), GL_DYNAMIC_COPY);
    };

    auto update_spline_vbo = [&bezier_spline, &vbo_spline]() {
        glBindBuffer(GL_ARRAY_BUFFER, vbo_spline);
        glBufferData(GL_ARRAY_BUFFER, sizeof(bezier_vertex) * bezier_spline.size(), bezier_spline.data(), GL_DYNAMIC_COPY);
    };


    GLuint vaos[2];
    GLuint& vao_pts = vaos[0], &vao_spline = vaos[1];
    glGenVertexArrays(2, vaos);

    auto build_vao = [](GLuint vao, GLuint vbo, bool bezier_spline) {
        static size_t pos_id = 0, col_id = 1, dist_id = 2;
        static size_t pos_size = 2, col_size = 4, dist_size = 1;
        static size_t pos_bytes = sizeof(vertex::position), col_bytes = sizeof(vertex::color);
        size_t struct_size = bezier_spline ? sizeof(bezier_vertex) : sizeof(vertex);

        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBindVertexArray(vao);

        glEnableVertexAttribArray(0);
	    glVertexAttribPointer(pos_id, pos_size, GL_FLOAT, GL_FALSE, struct_size, (void*) (0));
    
	    glEnableVertexAttribArray(1);
	    glVertexAttribPointer(col_id, col_size, GL_UNSIGNED_BYTE, GL_TRUE, struct_size, (void*) (0 + pos_bytes));

        if (bezier_spline) {
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(dist_id, dist_size, GL_FLOAT, GL_FALSE, struct_size, (void*) (0 + pos_bytes + col_bytes));
        }
    };

    build_vao(vao_spline, vbo_spline, true);
    build_vao(vao_pts, vbo_pts, false);

    auto update_bezier = [&bezier_pts, &bezier_spline, &quality, &update_pts_vbo, &update_spline_vbo, &time]() {
        size_t N = bezier_pts.size() * quality;

        bezier_spline.clear();
        bezier_spline.reserve(N);

        for (size_t i = 0; i < N; i++) {
            vec2 bezier_spline_part = bezier(bezier_pts, static_cast<float>(i) / (N - 1));
            float previous_dist = bezier_spline.empty() ? 0 : bezier_spline.back().dist;
            
            float dist = bezier_spline.empty() ? 0 : std::hypot(bezier_spline.back().position.x - bezier_spline_part.x,
                                                                bezier_spline.back().position.y - bezier_spline_part.y) + previous_dist;

            std::cout << dist << std::endl;
            bezier_spline.push_back(
                {
                    bezier_spline_part,
                    {180, 255, 180, 255},
                    dist
                }
            );
        }

        update_pts_vbo();
        update_spline_vbo();
    };

    update_pts_vbo();
    update_spline_vbo();

    glLineWidth(5.f);
    glPointSize(10);

    // ===== Debug ======
    // vertex dbg_v;
    // glGetBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertex), &dbg_v);
    // std::cout << "VBO[0] = { {" 
    //     << dbg_v.position.x << ", " << dbg_v.position.y << "}, {" 
    //         << ((uint32_t) dbg_v.color[0]) << ", " << ((uint32_t) dbg_v.color[1]) << ", " 
    //         << ((uint32_t) dbg_v.color[2]) << ", " << ((uint32_t) dbg_v.color[3]) << "} }" << std::endl;
    // ===================

    bool running = true;
    while (running)
    {
        for (SDL_Event event; SDL_PollEvent(&event);) switch (event.type)
        {
        case SDL_QUIT:
            running = false;
            break;
        case SDL_WINDOWEVENT: switch (event.window.event)
            {
            case SDL_WINDOWEVENT_RESIZED:
                width = event.window.data1;
                height = event.window.data2;
                glViewport(0, 0, width, height);
                break;
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (event.button.button == SDL_BUTTON_LEFT)
            {
                int mouse_x = event.button.x;
                int mouse_y = event.button.y;
                bezier_pts.push_back(
                    {
                        {static_cast<float>(mouse_x), static_cast<float>(mouse_y)},
                        {255, 255, 255, 255},
                    }
                );
                update_bezier();
            }
            else if (event.button.button == SDL_BUTTON_RIGHT)
            {
                if (!bezier_pts.empty()) {
                    bezier_pts.pop_back();
                    update_bezier();
                }
            }
            break;
        case SDL_KEYDOWN:
            if (event.key.keysym.sym == SDLK_LEFT)
            {
                if (quality > 1) {
                    quality--;
                    update_bezier();
                }
            }
            else if (event.key.keysym.sym == SDLK_RIGHT)
            {
                quality++;
                update_bezier();
            }
            break;
        }

        if (!running)
            break;

        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration_cast<std::chrono::duration<float>>(now - last_frame_start).count();
        last_frame_start = now;
        time += dt;

        glClear(GL_COLOR_BUFFER_BIT);

        float view[16] =
        {
            2.f / width, 0.f, 0.f, -1.f,
            0.f, -2.f / height, 0.f, 1.f,
            0.f, 0.f, 1.f, 0.f,
            0.f, 0.f, 0.f, 1.f,
        };

        
        // Pts render
        glUseProgram(program);
        glUniformMatrix4fv(view_location, 1, GL_TRUE, view);

        glBindBuffer(GL_ARRAY_BUFFER, vbo_pts);
        glBindVertexArray(vao_pts);
        glDrawArrays(GL_LINE_STRIP, 0, bezier_pts.size());
        glDrawArrays(GL_POINTS, 0, bezier_pts.size());

        // Spline render
        glUseProgram(bezier_program);
        glUniformMatrix4fv(bezier_view_location, 1, GL_TRUE, view);

        glBindBuffer(GL_ARRAY_BUFFER, vbo_spline);
        glBindVertexArray(vao_spline);
        glDrawArrays(GL_LINE_STRIP, 0, bezier_spline.size());

        SDL_GL_SwapWindow(window);
    }

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
}
catch (std::exception const & e)
{
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
}
