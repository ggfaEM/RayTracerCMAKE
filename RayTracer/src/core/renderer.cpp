#include "core/renderer.h"
#include "shape/ray.h"
#include "scene/scene.h"
#include "camera/camera.h"
#include "shape/shape.h"
#include "light/light.h"
#include "image/image.h"
#include "samplers/sampler2D.h"
#include "threads/dispatcher.h"
#include "integrators/phong.h"

namespace rt
{
//#define DEBUG_NORMALS

Renderer::Renderer(size_t w, size_t h,
	const std::string& file,
	size_t max_depth) :
	img(new Image(w, h, file)),
	SPP(1),
	GRID_DIM(1),
	NUM_THREADS(1)
{
	if (max_depth < 0)
	{
		MAX_DEPTH = 4;
		LOG(WARNING) << "max_depth < 0, setting MAX_DEPTH to 4";
	}
	else
	{
		this->MAX_DEPTH = max_depth;
	}
}

void Renderer::render_gradient(
	size_t& width_img,
	const size_t& width_stripe,
	size_t& height)
{
	// do not let RAM explode, limit maximum stripe width
	assert(width_stripe < 4e3);

	// set image width and height
	width_img = 256 * width_stripe;
	height = img->get_height();

	img->resize_color_array(256 * width_stripe, height);
	img->colors.resize(256 * width_stripe * height);

	for (int k = 0; k < 256; ++k)
	{
		for (unsigned int i = 0; i < height; ++i)
		{
			for (unsigned int j = 0; j < width_stripe; ++j)
			{
				img->colors[i * width_img + j + k * width_stripe] = glm::dvec3(double(k) / 255.0f);
			}
		}
	}
}

void Renderer::render_with_threads(
	size_t& width,
	size_t& height)
{
	constexpr double fov = glm::radians(30.0);
	double fov_tan = tan(fov / 2);
	double u = 0.0, v = 0.0;
	// distance to view plane
	double foc_len = 0.5 * 1.0 / fov_tan;
	double inv_spp;
	double inv_grid_dim = 1.f / (GRID_DIM * GRID_DIM);

	double crop_min_x = 0.0, crop_max_x = 1.0;
	double crop_min_y = 0.0, crop_max_y = 1.0;

	assert(crop_min_x <= crop_max_x && crop_min_y <= crop_max_y);

	size_t cropped_width[2];
	size_t cropped_height[2];

	crop(crop_min_x, crop_max_x, img->get_width(), cropped_width);
	crop(crop_min_y, crop_max_y, img->get_height(), cropped_height);

	width = cropped_width[1] - cropped_width[0];
	height = cropped_height[1] - cropped_height[0];

	LOG(INFO) << "Image width = " << img->get_width() << "; Image height = " << img->get_height();
	LOG(INFO) << "Cropped width = " << width << "; Cropped height = " << height;

#ifdef BLACK_COLOR_ARRAY_FOR_DEBUGGING
	return col;
#endif

	StratifiedSampler2D sampler{ width, height, GRID_DIM };
	size_t array_size = GRID_DIM * GRID_DIM;
	const glm::dvec2* samplingArray;
	inv_spp = 1.0 / SPP;

	std::unique_ptr<Scene> sc = std::make_unique<TetrahedronScene>(1);
	auto integrator = std::make_unique<PhongIntegrator>();

	// enclose with braces for destructor of ProgressReporter at the end of rendering
	{
		Slice slice(*img, 16, 16);
		std::mutex pairs_mutex;
		std::mutex sampler_mutex;
		std::vector<std::thread> threads_v;

		// launch progress reporter
		int64_t total_tile_pixels = static_cast<int64_t>(slice.dx) * slice.dy;
		pbrt::ProgressReporter reporter(total_tile_pixels, "Rendering:");

		// start rendering with threads
		for (int i = 0; i < NUM_THREADS; ++i)
		{
			threads_v.push_back(std::thread([&]() {
				int idx = 0;
				int h_step;
				int w_step;

				while (idx != -1)
				{
					// try to access the next free image raster
					pairs_mutex.lock();
					idx = slice.get_index();
					pairs_mutex.unlock();

					if (idx < 0)
					{
						break;
					}

					assert(idx < slice.get_length());

					// get step range
					w_step = static_cast<int>(std::min(slice.w_step, slice.img_width - slice.pairs[idx].first));
					h_step = static_cast<int>(std::min(slice.h_step, slice.img_height - slice.pairs[idx].second));

					for (int i = 0; i < h_step; ++i)
					{
						for (int j = 0; j < w_step; ++j)
						{
							sampler_mutex.lock();
							samplingArray = sampler.get2DArray();
							sampler_mutex.unlock();

							for (int s = 0; s < SPP; ++s)
							{
								for (size_t n = 0; n < array_size; ++n)
								{
									 //map pixel coordinates to[-1, 1]x[-1, 1]
									/*double u = (2.0 * (slice.pairs[idx].first + j + samplingArray[n].x) - img->get_width()) / img->get_height();
									double v = (-2.0 * (slice.pairs[idx].second + i + samplingArray[n].y) + img->get_height()) / img->get_height();
									*/
									double u = static_cast<int64_t>(slice.pairs[idx].first) + j - img->get_width()*0.5;
									double v = -(slice.pairs[idx].second + i) + img->get_height()*0.5;
							//		double z = -(img->get_height() * 0.5) / fov_tan;
							
									img->colors[(static_cast<size_t>(slice.pairs[idx].second) + i) * slice.img_width + slice.pairs[idx].first + j] +=
										clamp(integrator->Li(
											sc->cam->getPrimaryRay(u, v, img->get_height() * foc_len * 0.5), *sc.get(), 0));
									//sc->cam->getPrimaryRay(u, v, /*img->get_height()**/foc_len/**0.5*/), *sc.get(), 0));
								}
							}
							img->colors[(static_cast<size_t>(slice.pairs[idx].second) + i) * slice.img_width + slice.pairs[idx].first + j] *=
								inv_grid_dim * inv_spp;
						}
					}
					reporter.Update();
				}
				}));
		}

		for (int i = 0; i < NUM_THREADS; ++i)
		{
			threads_v[i].join();
		}

		reporter.Done();
	}
}

//void Renderer::render_with_threads(
//	size_t& width,
//	size_t& height,
//	double degree)
//{
//	constexpr double fov = glm::radians(30.0);
//	double fov_tan = tan(fov / 2);
//	double u = 0.0, v = 0.0;
//	// distance to view plane
//	double foc_len = 0.5 * 1.0 / fov_tan;
//	double inv_spp;
//	double inv_grid_dim = 1.f / (GRID_DIM * GRID_DIM);
//
//	double crop_min_x = 0.0, crop_max_x = 1.0;
//	double crop_min_y = 0.0, crop_max_y = 1.0;
//
//	assert(crop_min_x <= crop_max_x && crop_min_y <= crop_max_y);
//
//	size_t cropped_width[2];
//	size_t cropped_height[2];
//
//	crop(crop_min_x, crop_max_x, img->get_width(), cropped_width);
//	crop(crop_min_y, crop_max_y, img->get_height(), cropped_height);
//
//	width = cropped_width[1] - cropped_width[0];
//	height = cropped_height[1] - cropped_height[0];
//
//	LOG(INFO) << "Image width = " << img->get_width() << "; Image height = " << img->get_height();
//	LOG(INFO) << "Cropped width = " << width << "; Cropped height = " << height;
//
//#ifdef BLACK_COLOR_ARRAY_FOR_DEBUGGING
//	return col;
//#endif
//
//	StratifiedSampler2D sampler{ width, height, GRID_DIM };
//	size_t array_size = GRID_DIM * GRID_DIM;
//	const glm::dvec2* samplingArray;
//	inv_spp = 1.0 / SPP;
//
//	std::unique_ptr<Scene> sc = std::make_unique<DragonScene>();
//	auto integrator = std::make_unique<PhongIntegrator>();
//
//	// enclose with braces for destructor of ProgressReporter at the end of rendering
//	{
//		Slice slice(*img, 16, 16);
//		std::mutex pairs_mutex;
//		std::mutex sampler_mutex;
//		std::vector<std::thread> threads_v;
//
//		// launch progress reporter
//		pbrt::ProgressReporter reporter(slice.dx * slice.dy, "Rendering:");
//
//		// start rendering with threads
//		for (int i = 0; i < NUM_THREADS; ++i)
//		{
//			threads_v.push_back(std::thread([&]() {
//				int idx = 0;
//				int h_step;
//				int w_step;
//
//				while (idx != -1)
//				{
//					// try to access the next free image raster
//					pairs_mutex.lock();
//					idx = slice.get_index();
//					pairs_mutex.unlock();
//
//					if (idx < 0)
//					{
//						break;
//					}
//
//					assert(idx < slice.get_length());
//
//					// get step range
//					w_step = std::min(slice.w_step, slice.img_width - slice.pairs[idx].first);
//					h_step = std::min(slice.h_step, slice.img_height - slice.pairs[idx].second);
//
//					for (int i = 0; i < h_step; ++i)
//					{
//						for (int j = 0; j < w_step; ++j)
//						{
//							sampler_mutex.lock();
//							samplingArray = sampler.get2DArray();
//							sampler_mutex.unlock();
//
//							for (int s = 0; s < SPP; ++s)
//							{
//								for (size_t n = 0; n < array_size; ++n)
//								{
//									// map pixel coordinates to[-1, 1]x[-1, 1]
//									double u = (2.0 * (slice.pairs[idx].first + j + samplingArray[n].x) - img->get_width()) / img->get_height();
//									double v = (-2.0 * (slice.pairs[idx].second + i + samplingArray[n].y) + img->get_height()) / img->get_height();
//
//									/*double u = (x + samplingArray[idx].x) - WIDTH * 0.5f;
//									double v = -((y + samplingArray[idx].y) - HEIGHT * 0.5f);
//							*/
//									img->colors[(slice.pairs[idx].second + i) * slice.img_width + slice.pairs[idx].first + j] +=
//										clamp(integrator->Li(
//											sc->cam->getPrimaryRay(u, v, foc_len), *sc.get(), 0));
//								}
//							}
//							img->colors[(slice.pairs[idx].second + i) * slice.img_width + slice.pairs[idx].first + j] *=
//								inv_grid_dim * inv_spp;
//						}
//					}
//					reporter.Update();
//				}
//				}));
//		}
//
//		for (int i = 0; i < NUM_THREADS; ++i)
//		{
//			threads_v[i].join();
//		}
//
//		reporter.Done();
//	}
//}

/*
	Short helper function
*/
void Renderer::run(RenderMode mode)
{
	size_t width, height;

	if (mode == RenderMode::THREADS)
	{
		render_with_threads(width, height);
	}
	else if (mode == RenderMode::GRADIENT)
	{
		render_gradient(width, 10, height);
	}
	else if (mode == RenderMode::ANIMATE)
	{
		/*for (int i = 0; i < 90; ++i)
		{
			std::string new_file_name = "picture" + std::to_string(i) + ".ppm";
			render_with_threads(width, height, i);
			img->change_file_name(new_file_name);
			img->write_image_to_file();
			std::fill(img->colors.begin(), img->colors.end(), glm::dvec3(0));
		}*/
	}
	else
	{
		render_with_threads(width, height);
	}

	if (img->get_file_name().empty())
	{
		char buf[200];
		GET_PWD(buf, 200);
		std::cout << buf << std::endl;
		std::string fn = buf;


		LOG(INFO) << "Image will be written to \"" <<
			fn.substr(0, fn.find_last_of("\\/")).append(OS_SLASH).append(
				img->get_file_name());
		img->write_image_to_file();
	}
	else
	{
		//img->append_to_file_name(".ppm");
		img->write_image_to_file();
	}
}

std::vector<glm::dvec3> Renderer::get_colors() const
{
	return img->colors;
}

glm::u64vec2 Renderer::get_image_dim() const
{
	return glm::u64vec2(img->get_width(), img->get_height());
}



} // namespace rt
