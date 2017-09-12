// ImGui - standalone example application for Glfw + OpenGL 3, using programmable pipeline
// If you are new to ImGui, see examples/README.txt and documentation at the top of imgui.cpp.
#define _CRT_SECURE_NO_WARNINGS 1

#include <imgui.h>
#include "imgui_impl_glfw_gl3.h"
#include <stdio.h>
#include <GL/gl3w.h>    // This example is using gl3w to access OpenGL functions (because it is small). You may use glew/glad/glLoadGen/etc. whatever already works for you.
#include <GLFW/glfw3.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <stdlib.h>
#include <lua.hpp>

static void error_callback(int error, const char* description)
{
	fprintf(stderr, "Error %d: %s\n", error, description);
}

static char *current_file_name;
static bool current_file_changed;
static int current_file_size;
static char *current_file_buffer;

static void drop_callback(GLFWwindow *window, int count, const char **paths)
{
	if (count > 0)
	{
		const char *path = paths[0];
		size_t len = strlen(path);
		char *buf = (char *)malloc(1 + len);
		memcpy(buf, path, 1 + len);
		FILE *f = fopen(buf, "rb");
		if (f == nullptr) return;
		fseek(f, 0, SEEK_END);
		current_file_size = ftell(f);
		fseek(f, 0, SEEK_SET);
		free(current_file_buffer);
		current_file_buffer = (char *)malloc(current_file_size);
		fread(current_file_buffer, 1, current_file_size, f);
		fclose(f);
		free(current_file_name);
		current_file_name = buf;
		current_file_changed = true;
	}
}

void Update();

static GLFWwindow* window;
static bool show_test_window = false;
static bool show_another_window = false;
static ImVec4 clear_color = ImColor(32, 32, 50);

static lua_State *s_lua;

int main(int, char**)
{
	// Setup window
	glfwSetErrorCallback(error_callback);
	if (!glfwInit())
		return 1;
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if __APPLE__
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
	window = glfwCreateWindow(1920, 1080, "3D data view", NULL, NULL);
	glfwSetWindowPos(window, 100, 100);
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1); // Enable vsync
	gl3wInit();

	// Setup ImGui binding
	ImGui_ImplGlfwGL3_Init(window, true);
	glfwSetDropCallback(window, drop_callback);

	// Load Fonts
	// (there is a default font, this is only if you want to change it. see extra_fonts/README.txt for more details)
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->AddFontFromFileTTF("DroidSans.ttf", 16.0f);

	s_lua = luaL_newstate();
	luaL_openlibs(s_lua);

	// Main loop
	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();
		ImGui_ImplGlfwGL3_NewFrame();

		Update();

		// Rendering
		ImGui::Render();
		glfwSwapBuffers(window);
	}

	// Cleanup
	ImGui_ImplGlfwGL3_Shutdown();
	glfwTerminate();

	return 0;
}

static char vertShader[4096];
static char fragShader[4096];
static int luaLastValue;
static const char *luaErrorString = "";

bool LuaInputLine(const char *name, char *buf, int bufSize, int *value)
{
	if (!buf[0]) {
		memcpy(buf, "return 0", 8);
	}
	char *luaLine = buf + 7;
	if (ImGui::InputText(name, luaLine, bufSize - 7))
	{
		int error = luaL_dostring(s_lua, buf);
		if (error) {
			luaErrorString = lua_tostring(s_lua, -1);
			lua_pop(s_lua, 1);
		} else {
			luaErrorString = "";
			*value = luaLastValue = lua_tointeger(s_lua, -1);
			lua_pop(s_lua, 1);
			return true;
		}
	}
	return false;
}

static char vertBufOfsLua[256];
static char vertBufSzLua[256];
static char indexBufOfsLua[256];
static char indexBufSzLua[256];
static int vertBufOfs;
static int vertBufSz;
static int vertBufStride = 12;
static int indexBufOfs;
static int indexBufSz;
static int indexBufFmt = 1;
static bool buffersDirty;
static bool shadersDirty;
static bool shadersInitted;
static bool wireframe;
static int upAxis = 1;
static int vertexCount = 0;
static int indexCount = 0;
static char vertShaderErrorLog[1024];
static char fragShaderErrorLog[1024];
static char programErrorLog[1024];
static GLuint currentVertShader, currentFragShader, currentProgram;
static GLuint currentVertBuf, currentIndexBuf;
static GLuint vao;
static glm::vec3 cameraPosition;
static glm::quat cameraOrient;
static int drawMode = 0;

void Update()
{
	if (!shadersInitted)
	{
		memcpy(vertShader, R"(#version 330
layout (location = 0) in vec3 in_position;

uniform mat4 c_viewProj;

void main()
{
	gl_Position = c_viewProj * vec4(in_position, 1.0f);
}
)", 153);
		memcpy(fragShader, R"(#version 330
out vec4 FragColor;

void main()
{
	FragColor = vec4(1.0f, 1.0f, 1.0f, 1.0f);
}
)", 94);
		shadersInitted = true;
		shadersDirty = true;
	}
	if (!vao)
	{
		glGenVertexArrays(1, &vao);
	}
	// 1. Show a simple window
	// Tip: if we don't call ImGui::Begin()/ImGui::End() the widgets appears in a window automatically called "Debug"
	{
		ImGui::Text("Hello, world!");
		ImGui::ColorEdit3("clear color", (float*)&clear_color);
		if (ImGui::Button("Test Window")) show_test_window ^= 1;
		ImGui::Text("File: %s (0x%08x %d bytes)", current_file_name == nullptr ? "(none)" : current_file_name, current_file_size, current_file_size);
		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
		ImGui::Combo("Up axis", &upAxis, "x-up\0y-up\0z-up\0\0");
		buffersDirty = LuaInputLine("Vertex buffer offset", vertBufOfsLua, 256, &vertBufOfs);
		buffersDirty |= LuaInputLine("Vertex buffer size (bytes)", vertBufSzLua, 256, &vertBufSz);
		buffersDirty |= ImGui::InputInt("Vertex buffer stride (bytes)", &vertBufStride, 4, 12);
		buffersDirty |= LuaInputLine("Index buffer offset", indexBufOfsLua, 256, &indexBufOfs);
		buffersDirty |= LuaInputLine("Index buffer size (bytes)", indexBufSzLua, 256, &indexBufSz);
		buffersDirty |= ImGui::Combo("Index buffer element type", &indexBufFmt, "ubyte\0ushort\0uint\0\0");
		ImGui::Combo("draw mode", &drawMode, "Points\0Lines\0Triangles\0Line Strip\0Line Loop\0Triangle Strip\0Triangle Fan\0\0");
		ImGui::Checkbox("wireframe", &wireframe);
		shadersDirty |= ImGui::InputTextMultiline("Vertex Shader", vertShader, 4096);
		shadersDirty |= ImGui::InputTextMultiline("Fragment Shader", fragShader, 4096);
		GLuint newVertShader = 0;
		GLuint newFragShader = 0;
		GLuint newProgram = 0;
		if (shadersDirty)
		{
			newVertShader = glCreateShader(GL_VERTEX_SHADER);
			char *vertShaders = vertShader;
			glShaderSource(newVertShader, 1, &vertShaders, NULL);
			glCompileShader(newVertShader);
			int success = 0;
			glGetShaderiv(newVertShader, GL_COMPILE_STATUS, &success);
			if (!success)
			{
				glGetShaderInfoLog(newVertShader, 1024, NULL, vertShaderErrorLog);
				glDeleteShader(newVertShader);
				newVertShader = 0;
			}
			else
			{
				vertShaderErrorLog[0] = 0;
			}

			newFragShader = glCreateShader(GL_FRAGMENT_SHADER);
			char *fragShaders = fragShader;
			glShaderSource(newFragShader, 1, &fragShaders, NULL);
			glCompileShader(newFragShader);
			success = 0;
			glGetShaderiv(newFragShader, GL_COMPILE_STATUS, &success);
			if (!success)
			{
				glGetShaderInfoLog(newFragShader, 1024, NULL, fragShaderErrorLog);
				glDeleteShader(newFragShader);
				newFragShader = 0;
			}
			else
			{
				fragShaderErrorLog[0] = 0;
			}

			if (newFragShader && newVertShader)
			{
				newProgram = glCreateProgram();
				glAttachShader(newProgram, newVertShader);
				glAttachShader(newProgram, newFragShader);
				glLinkProgram(newProgram);
				success = 0;
				glGetProgramiv(newProgram, GL_LINK_STATUS, &success);
				if (!success)
				{
					glGetProgramInfoLog(newProgram, 1024, NULL, programErrorLog);
					glDeleteProgram(newProgram);
					glDeleteShader(newFragShader);
					glDeleteShader(newVertShader);
					newProgram = 0;
					newFragShader = 0;
					newVertShader = 0;
				}
				else
				{
					programErrorLog[0] = 0;
					if (currentVertShader) glDeleteShader(currentVertShader);
					if (currentFragShader) glDeleteShader(currentFragShader);
					if (currentProgram) glDeleteProgram(currentProgram);
					currentProgram = newProgram;
					currentVertShader = newVertShader;
					currentFragShader = newFragShader;
				}
			}
			shadersDirty = false;
		}
		ImGui::Text("Lua error: %s / Lua value: %08x %d", luaErrorString, luaLastValue, luaLastValue);
		ImGui::TextWrapped("Vertex shader error:\n%s", vertShaderErrorLog);
		ImGui::TextWrapped("Fragment shader error:\n%s", fragShaderErrorLog);
		ImGui::TextWrapped("Shader program error:\n%s", programErrorLog);
		
		if (buffersDirty)
		{
			glBindVertexArray(vao);
			int actualVertBufOfs = vertBufOfs;
			int actualVertBufSz = vertBufSz;
			if (actualVertBufOfs > current_file_size)
			{
				actualVertBufOfs = current_file_size;
			}
			if (actualVertBufOfs + actualVertBufSz > current_file_size)
			{
				actualVertBufSz = current_file_size - actualVertBufOfs;
			}
			if (currentVertBuf)
			{
				glDeleteBuffers(1, &currentVertBuf);
				currentVertBuf = 0;
			}
			if (actualVertBufSz > 0)
			{
				vertexCount = actualVertBufSz / vertBufStride;
				glGenBuffers(1, &currentVertBuf);
				glBindBuffer(GL_ARRAY_BUFFER, currentVertBuf);
				glBufferData(GL_ARRAY_BUFFER, actualVertBufSz, current_file_buffer + actualVertBufOfs, GL_STATIC_DRAW);
				char *data = current_file_buffer + actualVertBufOfs;
				float min[3] = {1e6f, 1e6f, 1e6f};
				float max[3] = {-1e6f, -1e6f, -1e6f};
				for (int i = 0; i < actualVertBufSz; i += vertBufStride)
				{
					float *v = (float *)(data + i);
					if (v[0] > max[0]) max[0] = v[0];
					if (v[0] < min[0]) min[0] = v[0];
					if (v[1] > max[1]) max[1] = v[1];
					if (v[1] < min[1]) min[1] = v[1];
					if (v[2] > max[2]) max[2] = v[2];
					if (v[2] < min[2]) min[2] = v[2];
				}
				glm::vec3 targetPosition;
				targetPosition.x = (max[0] + min[0]) * 0.5f;
				targetPosition.y = (max[1] + min[1]) * 0.5f;
				targetPosition.z = (max[2] + min[2]) * 0.5f;
				glm::vec3 dist{max[0] - min[0], max[1] - min[1], max[2] - min[2]};
				float targetDistance = glm::length(dist);
				if (targetDistance < 0.5f) targetDistance = 0.5f;
				cameraPosition = targetPosition + (glm::vec3{0.707, 0.707, 0.707} * targetDistance);
				cameraOrient = glm::rotation(glm::vec3{1, 0, 0}, normalize(cameraPosition - targetPosition));
			}
			else
			{
				vertexCount = 0;
			}
			
			int actualIndexBufOfs = indexBufOfs;
			int actualIndexBufSz = indexBufSz;
			if (actualIndexBufOfs > current_file_size)
			{
				actualIndexBufOfs = current_file_size;
			}
			if (actualIndexBufOfs + actualIndexBufSz > current_file_size)
			{
				actualIndexBufSz = current_file_size - actualVertBufOfs;
			}
			if (currentIndexBuf)
			{
				glDeleteBuffers(1, &currentIndexBuf);
				currentIndexBuf = 0;
			}
			if (actualIndexBufSz > 0)
			{
				glGenBuffers(1, &currentIndexBuf);
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, currentIndexBuf);
				glBufferData(GL_ELEMENT_ARRAY_BUFFER, actualIndexBufSz, current_file_buffer + actualIndexBufOfs, GL_STATIC_DRAW);
				int indexElSz = 2;
				if (indexBufFmt == 0) indexElSz = 1;
				else if (indexBufFmt == 2) indexElSz = 4;
				indexCount = actualIndexBufSz / indexElSz;
			}
			else
			{
				indexCount = 0;
			}
		}

		int display_w, display_h;
		glfwGetFramebufferSize(window, &display_w, &display_h);
		glViewport(0, 0, display_w, display_h);
		glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
		glClear(GL_COLOR_BUFFER_BIT);
		glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);

		glm::vec3 up{0, 1, 0};
		if (upAxis == 0) up = glm::vec3{1, 0, 0};
		else if (upAxis == 2) up = glm::vec3{0, 0, 1};
		auto io = ImGui::GetIO();
		glm::vec3 forward = glm::conjugate(cameraOrient) * glm::vec3{1, 0, 0} * cameraOrient;
		// left = up 'cross forward
		glm::vec3 left = glm::cross(up, forward);
		if (!io.WantCaptureMouse)
		{
			if (io.MouseDown[0])
			{
				// positive is to the right and down
				// to the right = clockwise rotation about up
				float rightRot = io.MouseDelta.x / (float)display_w;
				// up = clockwise rotation about up 'cross forward, so negate y
				float upRot = -io.MouseDelta.y / (float)display_h;
				cameraOrient = cameraOrient * glm::angleAxis(rightRot, up) * glm::angleAxis(upRot, left);
			}
		}
		if (!io.WantCaptureKeyboard)
		{
			glm::vec3 motion{};
			float s = 0.1f;
			if (io.KeyCtrl) s *= 10.0f;
			if (io.KeyShift) s *= 10.0f;
			if (io.KeysDown[GLFW_KEY_Q]) motion += s * up;
			if (io.KeysDown[GLFW_KEY_E]) motion -= s * up;
			if (io.KeysDown[GLFW_KEY_A]) motion += s * left;
			if (io.KeysDown[GLFW_KEY_D]) motion -= s * left;
			if (io.KeysDown[GLFW_KEY_W]) motion += s * forward;
			if (io.KeysDown[GLFW_KEY_S]) motion -= s * forward;
			cameraPosition += motion;
		}

		if (currentVertBuf) {
			glUseProgram(currentProgram);
			glEnable(GL_DEPTH);
			glEnable(GL_COLOR);
			glm::mat4 projection = glm::perspective(glm::radians(60.0f), (float)display_w / (float)display_h, 0.1f, 1000.0f);
			glm::vec3 targetPosition = cameraPosition + forward;
			glm::mat4 view = glm::lookAt(cameraPosition, targetPosition, up);
			glm::mat4 viewProj = projection * view;
			glUniformMatrix4fv(glGetUniformLocation(currentProgram, "c_viewProj"), 1, GL_FALSE, &viewProj[0][0]);
			glBindVertexArray(vao);
			glBindBuffer(GL_ARRAY_BUFFER, currentVertBuf);
			glEnableVertexAttribArray(0);
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, vertBufStride, NULL);
			// Points\0Lines\0Triangles\0Line Strip\0Line Loop\0Triangle Strip\0Triangle Fan\0\0
			GLenum mode = GL_POINTS;
			switch (drawMode)
			{
			case 1: mode = GL_LINES; break;
			case 2: mode = GL_TRIANGLES; break;
			case 3: mode = GL_LINE_STRIP; break;
			case 4: mode = GL_LINE_LOOP; break;
			case 5: mode = GL_TRIANGLE_STRIP; break;
			case 6: mode = GL_TRIANGLE_FAN; break;
			}
			if (currentIndexBuf)
			{
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, currentIndexBuf);
				GLenum indexElType = GL_UNSIGNED_SHORT;
				if (indexBufFmt == 0) indexElType = GL_UNSIGNED_BYTE;
				else if (indexBufFmt == 2) indexElType = GL_UNSIGNED_INT;
				glDrawElements(mode, indexCount, indexElType, 0);
			}
			else
			{
				glDrawArrays(mode, 0, vertexCount);
			}
			int error = glGetError();
			if (error == GL_INVALID_OPERATION)
			{
				printf("e");
			}
		}
	}

	// 3. Show the ImGui test window. Most of the sample code is in ImGui::ShowTestWindow()
	if (show_test_window)
	{
		ImGui::SetNextWindowPos(ImVec2(650, 20), ImGuiCond_FirstUseEver);
		ImGui::ShowTestWindow(&show_test_window);
	}
}
