#pragma once
#include "core/rt.h"
#include "shape/shape.h"
#include <fstream>
#include <sstream>

namespace rt
{
/*
	Read an .obj file and store the vertices and normals into two seperate containers.
	Return true, if everything went succesfully, false if file could not be read.
*/
inline bool loadObjFile(const std::string &file,
	std::vector<float> *vertices,
	std::vector<int> *indices)
{
	std::ifstream ifs{ file };
	std::istringstream iss;
	std::string line;

	char type;
	// values
	std::string v1, v2, v3;

	if (ifs.is_open())
	{
		while (std::getline(ifs, line))
		{
			if (!line.empty())
			{
				iss.str(line);
				iss.clear();

				iss >> type >> v1 >> v2 >> v3;

				if (type == 'v')
				{
					vertices->push_back(std::stof(v1));
					vertices->push_back(std::stof(v2));
					vertices->push_back(std::stof(v3));
				}
				else if (type == 'f')
				{
					indices->push_back(std::stoi(v1) - 1);
					indices->push_back(std::stoi(v2) - 1);
					indices->push_back(std::stoi(v3) - 1);
				}
			}
		}
	}
	else {
		return false;
	}
	return true;
}

/*
	Extract the vertices and face indices stored inside an assimp scene object that imports
	data from a file.
*/
std::vector<TriangleMesh> extractMeshes(const std::string &file)
{
	Assimp::Importer imp;
	const aiScene *a_scene = imp.ReadFile(file, aiProcess_Triangulate);
	
	if (!a_scene || (a_scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE))
	{
		std::cerr << "Assimp ERROR: " << imp.GetErrorString() << std::endl;
		std::exit(1);
	}

	float x, y, z;
	std::vector<TriangleMesh> tr_meshes;

	for (size_t mesh_num = 0; mesh_num < a_scene->mNumMeshes; ++mesh_num)
	{
		tr_meshes.push_back(TriangleMesh());
		for (size_t i = 0; i < a_scene->mMeshes[mesh_num]->mNumVertices; ++i)
		{
			x = (a_scene->mMeshes[mesh_num]->mVertices[i].x);
			y = (a_scene->mMeshes[mesh_num]->mVertices[i].y);
			z = (a_scene->mMeshes[mesh_num]->mVertices[i].z);
			tr_meshes[mesh_num].tr_mesh->push_back(glm::vec3(x, y, z));

			x = a_scene->mMeshes[mesh_num]->mNormals[i].x;
			y = a_scene->mMeshes[mesh_num]->mNormals[i].y;
			z = a_scene->mMeshes[mesh_num]->mNormals[i].z;
			normals.push_back(glm::vec3(x, y, z));
		}
	}

	int j;

	for (size_t mesh_num = 0; mesh_num < a_scene->mNumMeshes; ++mesh_num)
	{
		for (size_t i = 0; i < a_scene->mMeshes[mesh_num]->mNumFaces; ++i)
		{
			for (size_t n = 0; n < a_scene->mMeshes[mesh_num]->mFaces[i].mNumIndices; ++n)
			{
				j = a_scene->mMeshes[mesh_num]->mFaces[i].mIndices[n];
				indices.push_back(j);
			}
		}
	}
	std::cout << "Loading obj file finished." << std::endl;
}

}