// Do not use intrinsic functions, which allows using `constexpr` on GLM
// functions.
#define GLM_FORCE_PURE 1

#include "nprr.hpp"
#include "parametric_shapes.hpp"

#include "config.hpp"
#include "core/Bonobo.h"
#include "core/FPSCamera.h"
#include "core/helpers.hpp"
#include "core/node.hpp"
#include "core/opengl.hpp"
#include "core/ShaderProgramManager.hpp"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <tinyfiledialogs.h>

#include <array>
#include <clocale>
#include <cstdlib>
#include <stdexcept>
#include <random>

namespace constant
{
	constexpr uint32_t noise_res_x = 1024;
	constexpr uint32_t noise_res_y = 1024;

	constexpr float scale_lengths = 100.0f; // The scene is expressed in centimetres rather than metres, hence the x100.
}

namespace
{
	template <class E>
	constexpr auto toU(E const &e)
	{
		return static_cast<std::underlying_type_t<E>>(e);
	}

	void fill_noise_data(glm::vec3 *tex_arr, GLsizei framebuffer_width, GLsizei framebuffer_height);

	enum class Objects : uint32_t
	{
		Sphere,
		Cube,
		Face,
		Sponza,
		Count
	};

	enum class Texture : uint32_t
	{
		DepthBuffer = 0u,
		GBufferDiffuse,
		Noise,
		Silhouette,
		Result,
		Count
	};
	using Textures = std::array<GLuint, toU(Texture::Count)>;
	Textures createTextures(GLsizei framebuffer_width, GLsizei framebuffer_height);

	enum class Sampler : uint32_t
	{
		Nearest = 0u,
		Linear,
		Mipmaps,
		Shadow,
		Count
	};
	using Samplers = std::array<GLuint, toU(Sampler::Count)>;
	Samplers createSamplers();

	enum class FBO : uint32_t
	{
		GBuffer = 0u,
		Noise,
		Silhouette,
		Resolve,
		FinalWithDepth,
		Count
	};
	using FBOs = std::array<GLuint, toU(FBO::Count)>;
	FBOs createFramebufferObjects(Textures const &textures);

	enum class ElapsedTimeQuery : uint32_t
	{
		GbufferGeneration = 0u,
		Noise,
		Silhouette,
		Resolve,
		GUI,
		CopyToFramebuffer,
		Count
	};
	using ElapsedTimeQueries = std::array<GLuint, toU(ElapsedTimeQuery::Count)>;
	ElapsedTimeQueries createElapsedTimeQueries();

	enum class UBO : uint32_t
	{
		CameraViewProjTransforms = 0u,
		Count
	};
	using UBOs = std::array<GLuint, toU(UBO::Count)>;
	UBOs createUniformBufferObjects();

	struct ViewProjTransforms
	{
		glm::mat4 view_projection = glm::mat4(1.0f);
		glm::mat4 view_projection_inverse = glm::mat4(1.0f);
	};

	struct GBufferShaderLocations
	{
		GLuint ubo_CameraViewProjTransforms{0u};
		GLuint vertex_model_to_world{0u};
		GLuint normal_model_to_world{0u};
		GLuint camera_position{0u};
		GLuint light_position{0u};
		GLuint diffuse_color{0u};
		GLuint is_sketching{0u};
		GLuint thickness{0u};
	};
	void fillGBufferShaderLocations(GLuint gbuffer_shader, GBufferShaderLocations &locations);

	struct SilhouetteShaderLocations
	{
		GLuint ubo_CameraViewProjTransforms{0u};
		GLuint vertex_model_to_world{0u};
		GLuint normal_model_to_world{0u};
		GLuint light_position{0u};
		GLuint noise_texture{0u};
		GLuint is_sketching{0u};
	};
	void fillSilhouetteShaderLocations(GLuint silhouette_shader, SilhouetteShaderLocations &locations);
} // namespace

edan35::NPRR::NPRR(WindowManager &windowManager) : mCamera(0.5f * glm::half_pi<float>(),
														   static_cast<float>(config::resolution_x) / static_cast<float>(config::resolution_y),
														   0.01f * constant::scale_lengths, 30.0f * constant::scale_lengths),
												   inputHandler(), mWindowManager(windowManager), window(nullptr)
{
	WindowManager::WindowDatum window_datum{inputHandler, mCamera, config::resolution_x, config::resolution_y, 0, 0, 0, 0};

	window = mWindowManager.CreateGLFWWindow("NPRR", window_datum, config::msaa_rate);
	if (window == nullptr)
	{
		throw std::runtime_error("Failed to get a window: aborting!");
	}

	bonobo::init();
}

edan35::NPRR::~NPRR()
{
	bonobo::deinit();
}

void edan35::NPRR::run()
{
	auto diffuse_texture = bonobo::loadTexture2D(config::resources_path("textures/Paper_Wrinkled_001_basecolor.jpg"));

	// Load the geometry of Sponza
	auto const cube_geometry = bonobo::loadObjects(config::resources_path("scenes/cube.obj"));
	auto const sphere_geometry = bonobo::loadObjects(config::resources_path("scenes/sphere.obj"));
	auto const face_geometry = bonobo::loadObjects(config::resources_path("scenes/face/face.obj"));
	auto const sponza_geometry = bonobo::loadObjects(config::resources_path("scenes/sponza/sponza.obj"));
	if (sponza_geometry.empty())
	{
		LogError("Failed to load the Sponza model");
		return;
	}

	const std::vector<std::vector<bonobo::mesh_data>> geometry_array = {
		sphere_geometry,
		cube_geometry,
		face_geometry,
		sponza_geometry,
	};
	const char *geometry_names[] = {
		"Sphere",
		"Cube",
		"Face",
		"Sponza",
	};
	const GLuint line_width[] = {
		20u,
		20u,
		12u,
		5u,
	};
	int current_geometry_id = toU(Objects::Sphere);
	auto current_geometry = geometry_array[current_geometry_id];

	//
	// Setup the camera
	//
	mCamera.mWorld.SetTranslate(glm::vec3(0.0f, 1.0f, 1.8f) * constant::scale_lengths);
	mCamera.mMouseSensitivity = 0.003f;
	mCamera.mMovementSpeed = 3.0f * constant::scale_lengths; // 3 m/s => 10.8 km/h.

	int framebuffer_width, framebuffer_height;
	glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);

	//
	// Setup OpenGL objects
	// Look further down in this file to see the implementation of those functions.
	//
	Textures const textures = createTextures(framebuffer_width, framebuffer_height);
	FBOs const fbos = createFramebufferObjects(textures);
	Samplers const samplers = createSamplers();
	ElapsedTimeQueries const elapsed_time_queries = createElapsedTimeQueries();
	UBOs const ubos = createUniformBufferObjects();

	//
	// Load all the shader programs used
	//
	ShaderProgramManager program_manager;
	GLuint fallback_shader = 0u;
	program_manager.CreateAndRegisterProgram("Fallback",
											 {{ShaderType::vertex, "common/fallback.vert"},
											  {ShaderType::fragment, "common/fallback.frag"}},
											 fallback_shader);
	if (fallback_shader == 0u)
	{
		LogError("Failed to load fallback shader");
		return;
	}

	GLuint fill_gbuffer_shader = 0u;
	program_manager.CreateAndRegisterProgram("Fill G-Buffer",
											 {{ShaderType::vertex, "NPR/fill_gbuffer.vert"},
											  {ShaderType::fragment, "NPR/fill_gbuffer.frag"}},
											 fill_gbuffer_shader);
	if (fill_gbuffer_shader == 0u)
	{
		LogError("Failed to load G-buffer filling shader");
		return;
	}
	GBufferShaderLocations fill_gbuffer_shader_locations;
	fillGBufferShaderLocations(fill_gbuffer_shader, fill_gbuffer_shader_locations);

	GLuint silhouette_shader = 0u;
	program_manager.CreateAndRegisterProgram("Fill G-Buffer",
											 {{ShaderType::vertex, "NPR/silhouette.vert"},
											  {ShaderType::fragment, "NPR/silhouette.frag"},
											  {ShaderType::geometry, "NPR/silhouette.geom"}},
											 silhouette_shader);
	if (silhouette_shader == 0u)
	{
		LogError("Failed to load Silhouette shader");
		return;
	}
	SilhouetteShaderLocations fill_silhouette_shader_locations;
	fillSilhouetteShaderLocations(silhouette_shader, fill_silhouette_shader_locations);

	GLuint resolve_sketch_shader = 0u;
	program_manager.CreateAndRegisterProgram("Resolve deferred",
											 {{ShaderType::vertex, "NPR/resolve_sketch.vert"},
											  {ShaderType::fragment, "NPR/resolve_sketch.frag"}},
											 resolve_sketch_shader);
	if (resolve_sketch_shader == 0u)
	{
		LogError("Failed to load deferred resolution shader");
		return;
	}

	auto const set_uniforms = [](GLuint /*program*/) {};

	ViewProjTransforms camera_view_proj_transforms;

	const GLuint debug_texture_id = bonobo::getDebugTextureID();

	auto const bind_texture_with_sampler = [](GLenum target, unsigned int slot, GLuint program, std::string const &name, GLuint texture, GLuint sampler)
	{
		glActiveTexture(GL_TEXTURE0 + slot);
		glBindTexture(target, texture);
		glUniform1i(glGetUniformLocation(program, name.c_str()), static_cast<GLint>(slot));
		glBindSampler(slot, sampler);
	};

	glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
	glClearDepthf(1.0f);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);

	glUseProgram(0u);

	glBindFramebuffer(GL_READ_FRAMEBUFFER, fbos[toU(FBO::Resolve)]);

	bool is_sketching = true;
	float hatching_thickness = 6.0f;
	float light_pos_x = 2.5f;
	float light_pos_y = 3.0f;
	float light_pos_z = 4.0f;

	auto seconds_nb = 0.0f;
	std::array<GLuint64, toU(ElapsedTimeQuery::Count)> pass_elapsed_times;
	auto lastTime = std::chrono::high_resolution_clock::now();
	bool show_textures = false;
	auto polygon_mode = bonobo::polygon_mode_t::fill;

	bool show_logs = false;
	bool show_gui = true;
	bool shader_reload_failed = false;
	bool copy_elapsed_times = true;
	bool first_frame = true;
	bool show_basis = false;
	float basis_thickness_scale = 40.0f;
	float basis_length_scale = 400.0f;

	while (!glfwWindowShouldClose(window))
	{
		auto const nowTime = std::chrono::high_resolution_clock::now();
		auto const deltaTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(nowTime - lastTime);
		lastTime = nowTime;

		auto &io = ImGui::GetIO();
		inputHandler.SetUICapture(io.WantCaptureMouse, io.WantCaptureKeyboard);

		glfwPollEvents();
		inputHandler.Advance();
		mCamera.Update(deltaTimeUs, inputHandler);

		camera_view_proj_transforms.view_projection = mCamera.GetWorldToClipMatrix();
		camera_view_proj_transforms.view_projection_inverse = mCamera.GetClipToWorldMatrix();

		auto const view_projection = camera_view_proj_transforms.view_projection;

		if (inputHandler.GetKeycodeState(GLFW_KEY_R) & JUST_PRESSED)
		{
			shader_reload_failed = !program_manager.ReloadAllPrograms();
			if (shader_reload_failed)
			{
				tinyfd_notifyPopup("Shader Program Reload Error",
								   "An error occurred while reloading shader programs; see the logs for details.\n"
								   "Rendering is suspended until the issue is solved. Once fixed, just reload the shaders again.",
								   "error");
			}
			else
			{
				fillGBufferShaderLocations(fill_gbuffer_shader, fill_gbuffer_shader_locations);
			}
		}

		if (inputHandler.GetKeycodeState(GLFW_KEY_F3) & JUST_RELEASED)
			show_logs = !show_logs;
		if (inputHandler.GetKeycodeState(GLFW_KEY_F2) & JUST_RELEASED)
			show_gui = !show_gui;

		mWindowManager.NewImGuiFrame();

		if (!first_frame && show_gui && copy_elapsed_times)
		{
			// Copy all timings back from the GPU to the CPU.
			for (GLuint i = 0; i < pass_elapsed_times.size(); ++i)
			{
				glGetQueryObjectui64v(elapsed_time_queries[i], GL_QUERY_RESULT, pass_elapsed_times.data() + i);
			}
		}
		glm::vec3 light_position = glm::vec3(light_pos_x, light_pos_y, light_pos_z) * constant::scale_lengths;
		glm::vec3 camera_position = mCamera.mWorld.GetTranslation();
		//
		// Update per-frame changing UBOs.
		//
		glBindBuffer(GL_UNIFORM_BUFFER, ubos[toU(UBO::CameraViewProjTransforms)]);
		glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(camera_view_proj_transforms), &camera_view_proj_transforms);
		glBindBuffer(GL_UNIFORM_BUFFER, 0u);

		if (!shader_reload_failed)
		{
			//
			// Pass1: Render scene into the g-buffer
			//
			utils::opengl::debug::beginDebugGroup("Fill G-buffer");
			glBeginQuery(GL_TIME_ELAPSED, elapsed_time_queries[toU(ElapsedTimeQuery::GbufferGeneration)]);

			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbos[toU(FBO::GBuffer)]);
			glViewport(0, 0, framebuffer_width, framebuffer_height);
			glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

			glUseProgram(fill_gbuffer_shader);
			glUniform3fv(fill_gbuffer_shader_locations.light_position, 1, glm::value_ptr(light_position));
			glUniform3fv(fill_gbuffer_shader_locations.camera_position, 1, glm::value_ptr(camera_position));
			glUniform1f(fill_gbuffer_shader_locations.thickness, hatching_thickness);
			glUniform1i(fill_gbuffer_shader_locations.is_sketching, is_sketching);
			for (std::size_t i = 0; i < current_geometry.size(); ++i)
			{
				auto const &geometry = current_geometry[i];
				auto const &diffuse_color = current_geometry[i].material.diffuse;

				utils::opengl::debug::beginDebugGroup(geometry.name);

				auto const vertex_model_to_world = glm::mat4(1.0f);
				auto const normal_model_to_world = glm::mat4(1.0f);

				glUniformMatrix4fv(fill_gbuffer_shader_locations.vertex_model_to_world, 1, GL_FALSE, glm::value_ptr(vertex_model_to_world));
				glUniformMatrix4fv(fill_gbuffer_shader_locations.normal_model_to_world, 1, GL_FALSE, glm::value_ptr(normal_model_to_world));
				glUniform3fv(fill_gbuffer_shader_locations.diffuse_color, 1, glm::value_ptr(diffuse_color));

				glBindVertexArray(geometry.vao);
				glDrawElements(GL_TRIANGLES_ADJACENCY, geometry.adjacency_nb, GL_UNSIGNED_INT, reinterpret_cast<GLvoid const *>(0x0));

				utils::opengl::debug::endDebugGroup();
			}

			glEndQuery(GL_TIME_ELAPSED);
			utils::opengl::debug::endDebugGroup();

			glBindTexture(GL_TEXTURE_2D, 0);
			glBindVertexArray(0u);
			glUseProgram(0u);

			//
			// Pass 2: Find the silhouette
			//
			light_position = camera_position;
			utils::opengl::debug::beginDebugGroup("Silhouette");
			glBeginQuery(GL_TIME_ELAPSED, elapsed_time_queries[toU(ElapsedTimeQuery::Silhouette)]);

			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbos[toU(FBO::Silhouette)]);
			glViewport(0, 0, framebuffer_width, framebuffer_height);
			glClear(GL_COLOR_BUFFER_BIT);

			glUseProgram(silhouette_shader);
			glUniform3fv(fill_silhouette_shader_locations.light_position, 1, glm::value_ptr(light_position));
			glUniform1i(fill_silhouette_shader_locations.is_sketching, is_sketching);
			for (std::size_t i = 0; i < current_geometry.size(); ++i)
			{
				auto const &geometry = current_geometry[i];

				utils::opengl::debug::beginDebugGroup(geometry.name);

				auto const vertex_model_to_world = glm::mat4(1.0f);
				auto const normal_model_to_world = glm::mat4(1.0f);

				glUniformMatrix4fv(fill_silhouette_shader_locations.vertex_model_to_world, 1, GL_FALSE, glm::value_ptr(vertex_model_to_world));
				glUniformMatrix4fv(fill_silhouette_shader_locations.normal_model_to_world, 1, GL_FALSE, glm::value_ptr(normal_model_to_world));

				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, textures[toU(Texture::Noise)]);
				glUniform1i(fill_silhouette_shader_locations.noise_texture, 0);
				glBindSampler(0u, samplers[toU(Sampler::Nearest)]);

				glBindVertexArray(geometry.vao);
				if (is_sketching)
					glLineWidth(1u);
				else
					glLineWidth(line_width[current_geometry_id]);
				glDrawElements(GL_TRIANGLES_ADJACENCY, geometry.adjacency_nb, GL_UNSIGNED_INT, reinterpret_cast<GLvoid const *>(0x0));

				utils::opengl::debug::endDebugGroup();
			}

			glEndQuery(GL_TIME_ELAPSED);
			utils::opengl::debug::endDebugGroup();
			glBindVertexArray(0u);
			glUseProgram(0u);

			//
			// Pass 3: Compute final image using both the g-buffer and  the light accumulation buffer
			//
			utils::opengl::debug::beginDebugGroup("Resolve");
			glBeginQuery(GL_TIME_ELAPSED, elapsed_time_queries[toU(ElapsedTimeQuery::Resolve)]);

			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbos[toU(FBO::Resolve)]);
			glUseProgram(resolve_sketch_shader);
			glViewport(0, 0, framebuffer_width, framebuffer_height);

			glUniform1i(glGetUniformLocation(resolve_sketch_shader, "is_sketching"), is_sketching);

			bind_texture_with_sampler(GL_TEXTURE_2D, 0, resolve_sketch_shader, "diffuse_texture", textures[toU(Texture::GBufferDiffuse)], samplers[toU(Sampler::Nearest)]);
			bind_texture_with_sampler(GL_TEXTURE_2D, 1, resolve_sketch_shader, "silhouette_texture", textures[toU(Texture::Silhouette)], samplers[toU(Sampler::Nearest)]);
			bonobo::drawFullscreen();

			glBindSampler(1, 0u);
			glBindSampler(0, 0u);
			glUseProgram(0u);

			glEndQuery(GL_TIME_ELAPSED);
			utils::opengl::debug::endDebugGroup();
		}

		if (show_basis)
		{
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbos[toU(FBO::FinalWithDepth)]);
		}

		utils::opengl::debug::beginDebugGroup("Draw GUI");
		glBeginQuery(GL_TIME_ELAPSED, elapsed_time_queries[toU(ElapsedTimeQuery::GUI)]);

		//
		// Display 3D helpers
		//
		if (show_basis)
		{
			bonobo::renderBasis(basis_thickness_scale, basis_length_scale, mCamera.GetWorldToClipMatrix());
			// If the basis were not shown, FBO::Resolve
			// is still bound so there is no need to rebind it.
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbos[toU(FBO::Resolve)]);
		}

		//
		// Output content of the g-buffer as well as of the shadowmap, for debugging purposes
		//
		if (show_textures)
		{
			bonobo::displayTexture({-0.95f, 0.55f}, {-0.55f, 0.95f}, textures[toU(Texture::DepthBuffer)], samplers[toU(Sampler::Linear)], {0, 0, 0, -1}, glm::uvec2(framebuffer_width, framebuffer_height), true, mCamera.mNear, mCamera.mFar);
			bonobo::displayTexture({-0.95f, 0.05f}, {-0.55f, 0.45f}, textures[toU(Texture::GBufferDiffuse)], samplers[toU(Sampler::Linear)], {0, 1, 2, -1}, glm::uvec2(framebuffer_width, framebuffer_height));
			bonobo::displayTexture({-0.95f, -0.45f}, {-0.55f, -0.05f}, textures[toU(Texture::Silhouette)], samplers[toU(Sampler::Linear)], {0, 1, 2, -1}, glm::uvec2(framebuffer_width, framebuffer_height));
			if (is_sketching)
				bonobo::displayTexture({0.55f, -0.95f}, {0.95f, -0.55f}, textures[toU(Texture::Noise)], samplers[toU(Sampler::Linear)], {0, 0, 0, -1}, glm::uvec2(framebuffer_width, framebuffer_height));
			else
				bonobo::displayTexture({-0.95f, -0.45f}, {-0.55f, -0.05f}, textures[toU(Texture::Silhouette)], samplers[toU(Sampler::Linear)], {0, 1, 2, -1}, glm::uvec2(framebuffer_width, framebuffer_height));
		}

		//
		// Reset viewport back to normal
		//
		glViewport(0, 0, framebuffer_width, framebuffer_height);

		bool opened = ImGui::Begin("Render Time", nullptr, ImGuiWindowFlags_None);
		if (opened)
		{
			ImGui::Text("Frame CPU time: %.3f ms", std::chrono::duration<float, std::milli>(deltaTimeUs).count());

			ImGui::Checkbox("Copy elapsed times back to CPU", &copy_elapsed_times);

			if (ImGui::BeginTable("Pass durations", 2, ImGuiTableFlags_SizingFixedFit))
			{
				ImGui::TableSetupColumn("Pass");
				ImGui::TableSetupColumn("GPU time [ms]");
				ImGui::TableHeadersRow();

				ImGui::TableNextColumn();
				ImGui::Text("Gbuffer gen.");
				ImGui::TableNextColumn();
				ImGui::Text("%.3f", pass_elapsed_times[toU(ElapsedTimeQuery::GbufferGeneration)] / 1000000.0f);

				ImGui::TableNextColumn();
				ImGui::Text("Silhouette det.");
				ImGui::TableNextColumn();
				ImGui::Text("%.3f", pass_elapsed_times[toU(ElapsedTimeQuery::Silhouette)] / 1000000.0f);

				ImGui::TableNextColumn();
				ImGui::Text("Resolve");
				ImGui::TableNextColumn();
				ImGui::Text("%.3f", pass_elapsed_times[toU(ElapsedTimeQuery::Resolve)] / 1000000.0f);

				ImGui::TableNextColumn();
				ImGui::Text("GUI");
				ImGui::TableNextColumn();
				ImGui::Text("%.3f", pass_elapsed_times[toU(ElapsedTimeQuery::GUI)] / 1000000.0f);

				ImGui::TableNextColumn();
				ImGui::Text("Copy to framebuffer");
				ImGui::TableNextColumn();
				ImGui::Text("%.3f", pass_elapsed_times[toU(ElapsedTimeQuery::CopyToFramebuffer)] / 1000000.0f);

				ImGui::EndTable();
			}
		}
		ImGui::End();

		opened = ImGui::Begin("Scene Controls", nullptr, ImGuiWindowFlags_None);
		if (opened)
		{
			ImGui::Checkbox("Show textures", &show_textures);
			ImGui::Checkbox("Sketching?", &is_sketching);
			bool changed = ImGui::Combo("Geometry", &current_geometry_id, geometry_names, IM_ARRAYSIZE(geometry_names), 3);
			current_geometry = geometry_array[current_geometry_id];
			ImGui::Separator();
			if (!is_sketching)
			{
				ImGui::SliderFloat("Hatching Thickness", &hatching_thickness, 4.0f, 30.0f);
				ImGui::Separator();
				ImGui::SliderFloat("Light X", &light_pos_x, -50.0f, 50.0f);
				ImGui::SliderFloat("Light Y", &light_pos_y, -50.0f, 50.0f);
				ImGui::SliderFloat("Light Z", &light_pos_z, -50.0f, 50.0f);
			}

			// ImGui::Checkbox("Show basis", &show_basis);
			// ImGui::SliderFloat("Basis thickness scale", &basis_thickness_scale, 0.0f, 100.0f);
			// ImGui::SliderFloat("Basis length scale", &basis_length_scale, 0.0f, 100.0f);
		}
		ImGui::End();

		if (show_logs)
			Log::View::Render();
		mWindowManager.RenderImGuiFrame(show_gui);

		glEndQuery(GL_TIME_ELAPSED);
		utils::opengl::debug::endDebugGroup();

		//
		// Blit the result back to the default framebuffer.
		//
		utils::opengl::debug::beginDebugGroup("Copy to default framebuffer");
		glBeginQuery(GL_TIME_ELAPSED, elapsed_time_queries[toU(ElapsedTimeQuery::CopyToFramebuffer)]);

		// FBO::Resolve has already been bound to GL_READ_FRAMEBUFFER before rendering the first frame,
		// as no other frame buffer gets bound to it.
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0u);
		glBlitFramebuffer(0, 0, framebuffer_width, framebuffer_height, 0, 0, framebuffer_width, framebuffer_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

		glEndQuery(GL_TIME_ELAPSED);
		utils::opengl::debug::endDebugGroup();

		glfwSwapBuffers(window);

		first_frame = false;
	}

	glDeleteBuffers(static_cast<GLsizei>(ubos.size()), ubos.data());
	glDeleteQueries(static_cast<GLsizei>(elapsed_time_queries.size()), elapsed_time_queries.data());
	glDeleteSamplers(static_cast<GLsizei>(samplers.size()), samplers.data());
	glDeleteFramebuffers(static_cast<GLsizei>(fbos.size()), fbos.data());
	glDeleteTextures(static_cast<GLsizei>(textures.size()), textures.data());

	glDeleteProgram(resolve_sketch_shader);
	resolve_sketch_shader = 0u;
	glDeleteProgram(fill_gbuffer_shader);
	fill_gbuffer_shader = 0u;
	glDeleteProgram(fallback_shader);
	fallback_shader = 0u;
}

int main()
{
	std::setlocale(LC_ALL, "");

	Bonobo framework;

	try
	{
		edan35::NPRR nprr(framework.GetWindowManager());
		nprr.run();
	}
	catch (std::runtime_error const &e)
	{
		LogError(e.what());
	}
}
namespace
{
	void fill_noise_data(glm::vec3 *tex_arr, GLsizei framebuffer_width, GLsizei framebuffer_height)
	{
		std::random_device rd;
		std::mt19937 gen(rd());
		std::uniform_real_distribution<> dis(0.0, 1.0);

		for (int i = 0; i < framebuffer_width * framebuffer_height; i++)
			tex_arr[i] = glm::vec3(dis(gen));
	}

	Textures createTextures(GLsizei framebuffer_width, GLsizei framebuffer_height)
	{
		Textures textures;
		glGenTextures(static_cast<GLsizei>(textures.size()), textures.data());

		glBindTexture(GL_TEXTURE_2D, textures[toU(Texture::DepthBuffer)]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, framebuffer_width, framebuffer_height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
		utils::opengl::debug::nameObject(GL_TEXTURE, textures[toU(Texture::DepthBuffer)], "Depth buffer");

		glBindTexture(GL_TEXTURE_2D, textures[toU(Texture::GBufferDiffuse)]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, framebuffer_width, framebuffer_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		utils::opengl::debug::nameObject(GL_TEXTURE, textures[toU(Texture::GBufferDiffuse)], "GBuffer diffuse");

		glm::vec3 *noise_data = new glm::vec3[constant::noise_res_x * constant::noise_res_y];
		fill_noise_data(noise_data, constant::noise_res_x, constant::noise_res_y);
		glBindTexture(GL_TEXTURE_2D, textures[toU(Texture::Noise)]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, constant::noise_res_x, constant::noise_res_y, 0, GL_RGBA, GL_UNSIGNED_BYTE, noise_data);
		utils::opengl::debug::nameObject(GL_TEXTURE, textures[toU(Texture::Noise)], "Noise");

		glBindTexture(GL_TEXTURE_2D, textures[toU(Texture::Silhouette)]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, framebuffer_width, framebuffer_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		utils::opengl::debug::nameObject(GL_TEXTURE, textures[toU(Texture::Silhouette)], "Silhouette");

		glBindTexture(GL_TEXTURE_2D, textures[toU(Texture::Result)]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, framebuffer_width, framebuffer_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		utils::opengl::debug::nameObject(GL_TEXTURE, textures[toU(Texture::Result)], "Final result");

		glBindTexture(GL_TEXTURE_2D, 0u);
		delete[] noise_data;

		return textures;
	}

	Samplers createSamplers()
	{
		Samplers samplers;
		glGenSamplers(static_cast<GLsizei>(samplers.size()), samplers.data());

		// For sampling 2-D textures without interpolation.
		glSamplerParameteri(samplers[toU(Sampler::Nearest)], GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glSamplerParameteri(samplers[toU(Sampler::Nearest)], GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		utils::opengl::debug::nameObject(GL_SAMPLER, samplers[toU(Sampler::Nearest)], "Nearest");

		// For sampling 2-D textures without mipmaps.
		glSamplerParameteri(samplers[toU(Sampler::Linear)], GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glSamplerParameteri(samplers[toU(Sampler::Linear)], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		utils::opengl::debug::nameObject(GL_SAMPLER, samplers[toU(Sampler::Linear)], "Linear");

		// For sampling 2-D textures with mipmaps.
		glSamplerParameteri(samplers[toU(Sampler::Mipmaps)], GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glSamplerParameteri(samplers[toU(Sampler::Mipmaps)], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		utils::opengl::debug::nameObject(GL_SAMPLER, samplers[toU(Sampler::Mipmaps)], "Mimaps");

		// For sampling 2-D shadow maps
		glSamplerParameteri(samplers[toU(Sampler::Shadow)], GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glSamplerParameteri(samplers[toU(Sampler::Shadow)], GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glSamplerParameteri(samplers[toU(Sampler::Shadow)], GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
		glSamplerParameteri(samplers[toU(Sampler::Shadow)], GL_TEXTURE_COMPARE_FUNC, GL_LESS);
		utils::opengl::debug::nameObject(GL_SAMPLER, samplers[toU(Sampler::Shadow)], "Shadow");

		return samplers;
	}

	FBOs createFramebufferObjects(Textures const &textures)
	{
		auto const validate_fbo = [](std::string const &fbo_name)
		{
			auto const status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
			if (status == GL_FRAMEBUFFER_COMPLETE)
				return;

			LogError("Framebuffer \"%s\" is not complete: check the logs for additional information.", fbo_name.data());
		};

		FBOs fbos;
		glGenFramebuffers(static_cast<GLsizei>(fbos.size()), fbos.data());

		glBindFramebuffer(GL_FRAMEBUFFER, fbos[toU(FBO::GBuffer)]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures[toU(Texture::GBufferDiffuse)], 0);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, textures[toU(Texture::DepthBuffer)], 0);
		glReadBuffer(GL_NONE);				// Disable reading back from the colour attachments, as unnecessary in this assignment.
		glDrawBuffer(GL_COLOR_ATTACHMENT0); // The fragment shader output at location 0 will be written to colour attachment 0 (i.e. the rendering result texture).
		validate_fbo("GBuffer");
		utils::opengl::debug::nameObject(GL_FRAMEBUFFER, fbos[toU(FBO::GBuffer)], "GBuffer");

		glBindFramebuffer(GL_FRAMEBUFFER, fbos[toU(FBO::Silhouette)]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures[toU(Texture::Silhouette)], 0);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, textures[toU(Texture::DepthBuffer)], 0);
		glReadBuffer(GL_NONE);				// Disable reading back from the colour attachments, as unnecessary in this assignment.
		glDrawBuffer(GL_COLOR_ATTACHMENT0); // The fragment shader output at location 0 will be written to colour attachment 0 (i.e. the rendering result texture).
		validate_fbo("Silhouette");
		utils::opengl::debug::nameObject(GL_FRAMEBUFFER, fbos[toU(FBO::Silhouette)], "Silhouette");

		glBindFramebuffer(GL_FRAMEBUFFER, fbos[toU(FBO::Noise)]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures[toU(Texture::Noise)], 0);
		glReadBuffer(GL_COLOR_ATTACHMENT0); // Colour attachment result 0 (i.e. the rendering result texture) will be blitted to the screen.
		glDrawBuffer(GL_COLOR_ATTACHMENT0); // The fragment shader output at location 0 will be written to colour attachment 0 (i.e. the rendering result texture).
		validate_fbo("Noise");
		utils::opengl::debug::nameObject(GL_FRAMEBUFFER, fbos[toU(FBO::Noise)], "Noise");

		glBindFramebuffer(GL_FRAMEBUFFER, fbos[toU(FBO::Resolve)]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures[toU(Texture::Result)], 0);
		glReadBuffer(GL_COLOR_ATTACHMENT0); // Colour attachment result 0 (i.e. the rendering result texture) will be blitted to the screen.
		glDrawBuffer(GL_COLOR_ATTACHMENT0); // The fragment shader output at location 0 will be written to colour attachment 0 (i.e. the rendering result texture).
		validate_fbo("Resolve");
		utils::opengl::debug::nameObject(GL_FRAMEBUFFER, fbos[toU(FBO::Resolve)], "Resolve");

		glBindFramebuffer(GL_FRAMEBUFFER, fbos[toU(FBO::FinalWithDepth)]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures[toU(Texture::Result)], 0);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, textures[toU(Texture::DepthBuffer)], 0);
		glReadBuffer(GL_NONE);				// Disable reading back from the colour attachments, as unnecessary in this assignment.
		glDrawBuffer(GL_COLOR_ATTACHMENT0); // The fragment shader output at location 0 will be written to colour attachment 0 (i.e. the rendering result texture).
		validate_fbo("Final with depth");
		utils::opengl::debug::nameObject(GL_FRAMEBUFFER, fbos[toU(FBO::FinalWithDepth)], "Cone wireframe");

		glBindFramebuffer(GL_FRAMEBUFFER, 0u);
		return fbos;
	}

	ElapsedTimeQueries createElapsedTimeQueries()
	{
		ElapsedTimeQueries queries;
		glGenQueries(static_cast<GLsizei>(queries.size()), queries.data());

		if (utils::opengl::debug::isSupported())
		{
			// Queries (like any other OpenGL object) need to have been used at least
			// once to ensure their resources have been allocated so we can call
			// `glObjectLabel()` on them.
			auto const register_query = [](GLuint const query)
			{
				glBeginQuery(GL_TIME_ELAPSED, query);
				glEndQuery(GL_TIME_ELAPSED);
			};

			register_query(queries[toU(ElapsedTimeQuery::GbufferGeneration)]);
			utils::opengl::debug::nameObject(GL_QUERY, queries[toU(ElapsedTimeQuery::GbufferGeneration)], "GBuffer generation");

			register_query(queries[toU(ElapsedTimeQuery::Noise)]);
			utils::opengl::debug::nameObject(GL_QUERY, queries[toU(ElapsedTimeQuery::Noise)], "Noise generation");

			register_query(queries[toU(ElapsedTimeQuery::Resolve)]);
			utils::opengl::debug::nameObject(GL_QUERY, queries[toU(ElapsedTimeQuery::Resolve)], "Resolve");

			register_query(queries[toU(ElapsedTimeQuery::GUI)]);
			utils::opengl::debug::nameObject(GL_QUERY, queries[toU(ElapsedTimeQuery::GUI)], "GUI");
		}

		return queries;
	}

	UBOs createUniformBufferObjects()
	{
		UBOs ubos;
		glGenBuffers(static_cast<GLsizei>(ubos.size()), ubos.data());

		glBindBuffer(GL_UNIFORM_BUFFER, ubos[toU(UBO::CameraViewProjTransforms)]);
		glBufferData(GL_UNIFORM_BUFFER, sizeof(ViewProjTransforms), nullptr, GL_STREAM_DRAW);
		glBindBufferBase(GL_UNIFORM_BUFFER, toU(UBO::CameraViewProjTransforms), ubos[toU(UBO::CameraViewProjTransforms)]);
		utils::opengl::debug::nameObject(GL_BUFFER, ubos[toU(UBO::CameraViewProjTransforms)], "Camera view-projection transforms");

		glBindBuffer(GL_UNIFORM_BUFFER, 0u);
		return ubos;
	}

	void fillGBufferShaderLocations(GLuint gbuffer_shader, GBufferShaderLocations &locations)
	{
		locations.ubo_CameraViewProjTransforms = glGetUniformBlockIndex(gbuffer_shader, "CameraViewProjTransforms");
		locations.vertex_model_to_world = glGetUniformLocation(gbuffer_shader, "vertex_model_to_world");
		locations.normal_model_to_world = glGetUniformLocation(gbuffer_shader, "normal_model_to_world");
		locations.camera_position = glGetUniformLocation(gbuffer_shader, "camera_position");
		locations.light_position = glGetUniformLocation(gbuffer_shader, "light_position");
		locations.diffuse_color = glGetUniformLocation(gbuffer_shader, "diffuse_color");
		locations.is_sketching = glGetUniformLocation(gbuffer_shader, "is_sketching");
		locations.thickness = glGetUniformLocation(gbuffer_shader, "thickness");

		glUniformBlockBinding(gbuffer_shader, locations.ubo_CameraViewProjTransforms, toU(UBO::CameraViewProjTransforms));
	}

	void fillSilhouetteShaderLocations(GLuint silhouette_shader, SilhouetteShaderLocations &locations)
	{
		locations.ubo_CameraViewProjTransforms = glGetUniformBlockIndex(silhouette_shader, "CameraViewProjTransforms");
		locations.vertex_model_to_world = glGetUniformLocation(silhouette_shader, "vertex_model_to_world");
		locations.normal_model_to_world = glGetUniformLocation(silhouette_shader, "normal_model_to_world");
		locations.light_position = glGetUniformLocation(silhouette_shader, "light_position");
		locations.noise_texture = glGetUniformLocation(silhouette_shader, "noise_texture");
		locations.is_sketching = glGetUniformLocation(silhouette_shader, "is_sketching");

		glUniformBlockBinding(silhouette_shader, locations.ubo_CameraViewProjTransforms, toU(UBO::CameraViewProjTransforms));
	}

} // namespace
