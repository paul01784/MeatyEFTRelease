#pragma once
#include <cstdint>
#include <list>
#include <string>
#include <random>
#include "../external/glm/glm.hpp"
#include "../UI/globals.h"
#include "../Tarkov/Unity/cameraManager.h"
#include "../Tarkov/GameWorld/MainGame.h"





//func dec
glm::vec3 NEW_DMA_get_bone_position_world(uint64_t matrices, uint32_t index);
glm::vec3 get_transform_position1(ULONG64 pMatrix, ULONG64 index);


#define DEFINE_AND_CREATE_LIST(name, input, streamName, tempVar) \
	std::vector<std::wstring> name; \
	std::wstringstream streamName(input); \
	std::wstring tempVar; \
	while (std::getline(streamName, tempVar, L' ')) \
	{ \
		name.push_back(tempVar); \
	}

#define PI 3.141592653589793
#define M_PI	3.14159265358979323846264338327950288419716939937510

namespace Utils {
	inline auto valid_pointer(uintptr_t pointer) -> bool {
		return (pointer && pointer > 0xFFFFFF && pointer < 0x7FFFFFFFFFFF);
	}

	inline bool isGoodVec3(const glm::vec3& vec) { // Marked as inline
		const float epsilon = 1e-9f; // small threshold to account for floating-point precision
		const float minRange = -10000.0f;
		const float maxRange = 10000.0f;

		bool isZero = (std::abs(vec.x) < epsilon && std::abs(vec.y) < epsilon && std::abs(vec.z) < epsilon);
		bool isInRange = (vec.x >= minRange && vec.x <= maxRange &&
			vec.y >= minRange && vec.y <= maxRange &&
			vec.z >= minRange && vec.z <= maxRange);

		return !isZero && isInRange;
	}


	//returns 0 if same!
	inline int compareUint64(uint64_t a, uint64_t b) { // Marked as inline
		if (a < b) return -1;
		if (a > b) return 1;
		return 0;
	}

	//sub namespace
	namespace Text {
		std::string wstring_to_utf8(const std::wstring& str); // Declaration only
		std::wstring utf8_to_wstring(const std::string& str); // Declaration only
		std::wstring s2ws(const std::string& str); // Declaration only
		std::string ws2s(const std::wstring& wstr); // Declaration only
		std::wstring convert_cyrillic_to_latin(std::wstring buffer); // Declaration only
		std::string random_string(const int len); // Declaration only
		bool containsIgnoreCase(const std::string& haystack, const std::string& needle);
	}

	namespace Player {
		namespace Rotation {
			glm::vec2 correctRotation2d(glm::vec2 rotation); // Declaration only
		}
	}

	namespace transform {
		namespace position {
			glm::vec3 getPositionFromTransform(uint64_t transform);
		}
	}

	namespace Camera {

		inline bool world_to_screen(glm::vec3 world, glm::vec2* screen)
		{
			if (!screen)
				return false;

			const ::CameraManagerSnapshot projection =
				cameraManagerTest.snapshot();

			return projection &&
				::CameraManager::worldToScreen(
					*projection,
					world,
					*screen,
					espGlobals::gameRes.x,
					espGlobals::gameRes.y);
		}
	}
}

static inline bool IsNullOrWhiteSpace(const std::string& s)
{
	if (s.empty())
		return true;

	return std::all_of(s.begin(), s.end(),
		[](unsigned char c) { return std::isspace(c); });
}

static inline std::string Trim(std::string s)
{
	auto notSpace = [](unsigned char c) { return !std::isspace(c); };

	s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
	s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());

	return s;
}

static inline std::string TrimEFT(std::string s)
{
	auto isGarbage = [](unsigned char c)
		{
			return
				c == '\0' ||                     // null padding
				c == 0xA0 ||                     // non-breaking space
				std::isspace(c);                 // ASCII whitespace
		};

	// left trim
	s.erase(s.begin(),
		std::find_if(s.begin(), s.end(),
			[&](unsigned char c) { return !isGarbage(c); }));

	// right trim
	s.erase(
		std::find_if(s.rbegin(), s.rend(),
			[&](unsigned char c) { return !isGarbage(c); }).base(),
		s.end());

	return s;
}
