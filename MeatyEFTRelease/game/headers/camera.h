#pragma once
#include <cstdint>
#include <glm/glm.hpp>

struct CameraArray
{
	uint64_t cameras;    // pointer to camera list
	uint64_t minCount;
	uint64_t curCount;   // current number of cameras
	uint64_t maxCount;
};

class Camera {
public:

	static bool initedCamera;
	void getCameraPtrs();
	bool checkIfOpticMatrix();
	void getMatrixPtrs();
	

	void cameraTask();
	void clearCache();

	static uint64_t fpsCamera;
	static uint64_t fpsMatrixAddr;
	static uint64_t opticCamera;
	static uint64_t opticMatrixAddr;
	static float gameFOV;
	static float gameAspect;

	static bool localmpCamera;
	static uint64_t cameraEntity;

	static uint64_t opticCameraMatrix;
	static glm::highp_mat4 g_viewMatrix;
	static glm::highp_mat4 g_viewMatrixOptic;
	static glm::highp_mat4 g_viewMatrixRAW;
	static glm::highp_mat4 g_viewMatrixOpticRAW;

	static uint64_t closestPlayer;
	static float closestPlayerDist;


private:

	bool is_mpcamera();



};

extern Camera camera;