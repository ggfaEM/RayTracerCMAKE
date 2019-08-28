#include "..\..\include\threads\dispatcher.h"

namespace rt
{

int Slice::get_index()
{
	if (idx == length || ++idx == length)
	{
		return -1;
	}
	else
	{
		return idx;
	}
}

void work(Slice& s, 
	std::mutex& pairs_mutex,
	std::vector<glm::vec3>& col,
	const Scene& sc,
	StratifiedSampler2D& sampler,
	pbrt::ProgressReporter& reporter,
	unsigned int array_size,
	const glm::vec2* samplingArray,
	float inv_grid_dim,
	float inv_spp,
	float fov_tan,
	float d,
	void (*func)(std::vector<glm::vec3>& col,
		const Scene& sc,
		StratifiedSampler2D& sampler,
		unsigned int array_size,
		const glm::vec2* samplingArray,
		float inv_grid_dim,
		float inv_spp,
		float fov_tan,
		float d,
		int x,
		int y,
		int x1,
		int y1))
{
	int idx = 0;
	unsigned int h_step;
	unsigned int w_step;

	while (idx != -1)
	{
		// try to access the next free raster
		pairs_mutex.lock();
		idx = s.get_index();
		pairs_mutex.unlock();

		if (idx < 0)
		{
			break;
		}

		assert(idx < s.get_length());
		
		// get step range
		w_step = std::min(s.w_step, s.img_width - s.pairs[idx].first);
		h_step = std::min(s.h_step, s.img_height - s.pairs[idx].second);

		for (unsigned int i = 0; i < h_step; ++i)
		{
			for (unsigned int j = 0; j < w_step; ++j)
			{
				func(col, sc, sampler, array_size, samplingArray, inv_grid_dim, inv_spp,
					fov_tan, d, 
					s.pairs[idx].first + j,	// matrix index
					s.pairs[idx].second + i, // matrix index
					(s.pairs[idx].second + i) * s.img_width, // linear index 
					s.pairs[idx].first + j); // linear index
			}
		}	

		reporter.Update();
	}
}

}