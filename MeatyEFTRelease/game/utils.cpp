#include "../app/includes.h"
#include "headers/utils.h"
#include "../memory/Memory.h"


namespace Utils {

	namespace Text {
		std::string wstring_to_utf8(const std::wstring& str) {
			int utf8_size = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, nullptr, 0, nullptr, nullptr);
			if (utf8_size == 0) {
				return "";
			}

			std::string utf8_conv(utf8_size, '\0');
			WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, &utf8_conv[0], utf8_size, nullptr, nullptr);

			return utf8_conv;
		}

		std::wstring utf8_to_wstring(const std::string& str) {
			int wchar_size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
			if (wchar_size == 0) {
				return L"";
			}

			std::wstring wchar_conv(wchar_size, L'\0');
			MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wchar_conv[0], wchar_size);

			return wchar_conv;
		}

		std::wstring s2ws(const std::string& str) {
			int wchar_size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
			if (wchar_size == 0) {
				return L"";
			}

			std::wstring wchar_conv(wchar_size, L'\0');
			MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wchar_conv[0], wchar_size);

			return wchar_conv;
		}

		std::string ws2s(const std::wstring& wstr) {
			int utf8_size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
			if (utf8_size == 0) {
				return "";
			}

			std::string utf8_conv(utf8_size, '\0');
			WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &utf8_conv[0], utf8_size, nullptr, nullptr);

			return utf8_conv;
		}

		std::wstring convert_cyrillic_to_latin(std::wstring buffer) {
			const wchar_t* lowerCaseCyrillic = L"а б в г д е ё ж з и й к л м н о п   с т у ф х ц ч ш щ ъ ы ь э ю я";
			const wchar_t* upperCaseCyrillic = L"А Б В Г Д Е Ё Ж З И Й К Л М Н О П   С Т У Ф Х Ц Ч Ш Щ Ъ Ы Ь Э Ю Я";
			const wchar_t* lowerCaseLatin = L"a b v g d e yo zh z i y k l m n o p r s t u f kh ts ch sh shch \" y ' e yu ya";
			const wchar_t* upperCaseLatin = L"A B V G D E Yo Zh Z I Y K L M N O P R S T U F Kh Ts Ch Sh Shch \" Y ' E Yu Ya";

			DEFINE_AND_CREATE_LIST(lowerCaseListCyrillic, lowerCaseCyrillic, lowerCaseListCyrillicStream, segment0);
			DEFINE_AND_CREATE_LIST(upperCaseListCyrillic, upperCaseCyrillic, upperCaseListCyrillicStream, segment1);
			DEFINE_AND_CREATE_LIST(lowerCaseListLatin, lowerCaseLatin, lowerCaseListLatinStream, segment2);
			DEFINE_AND_CREATE_LIST(upperCaseListLatin, upperCaseLatin, upperCaseListLatinStream, segment3);

			size_t input_length = buffer.size();
			for (size_t i = 0; i < input_length; i++) {
				wchar_t current_char = buffer[i];
				wchar_t current_text[] = { current_char, 0x0 };
				auto lowercase_search_value = std::find(lowerCaseListCyrillic.begin(), lowerCaseListCyrillic.end(), std::wstring(current_text));
				if (lowercase_search_value != lowerCaseListCyrillic.end()) {
					int index = lowercase_search_value - lowerCaseListCyrillic.begin();
					buffer[i] = lowerCaseListLatin.at(index).at(0);
				}

				auto uppercase_search_value = std::find(upperCaseListCyrillic.begin(), upperCaseListCyrillic.end(), std::wstring(current_text));
				if (uppercase_search_value != upperCaseListCyrillic.end()) {
					int index = uppercase_search_value - upperCaseListCyrillic.begin();
					buffer[i] = upperCaseListLatin.at(index).at(0);
				}
			}
			return buffer;
		}

		std::string random_string(const int len) {
			const std::string alpha_numeric("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890");

			std::default_random_engine generator{ std::random_device{}() };
			std::uniform_int_distribution< std::string::size_type > distribution{ 0, alpha_numeric.size() - 1 };

			std::string str(len, 0);
			for (auto& it : str) {
				it = alpha_numeric[distribution(generator)];
			}

			return str;
		}

		bool containsIgnoreCase(const std::string& haystack, const std::string& needle)
		{
			auto it = std::search(
				haystack.begin(), haystack.end(),
				needle.begin(), needle.end(),
				[](char a, char b)
				{
					return std::tolower(static_cast<unsigned char>(a)) ==
						std::tolower(static_cast<unsigned char>(b));
				});

			return it != haystack.end();
		}
	}

	namespace Player {
		namespace Rotation {
			glm::vec2 correctRotation2d(glm::vec2 rotation) {
				glm::vec2 correctedRotation = {};

				rotation.x -= 90.f;

				if (rotation.x < 0.f)
					rotation.x += 360.f;

				if (rotation.x < 0.f)
					correctedRotation.x = 360.f + rotation.x;
				else
					correctedRotation.x = rotation.x;

				if (rotation.y < 0.f)
					correctedRotation.y = 360.f + rotation.y;
				else
					correctedRotation.y = rotation.y;

				return correctedRotation;
			}
		}
	}
}

glm::vec3 NEW_DMA_get_bone_position_world(uint64_t matrices, uint32_t index)
{
	if (!matrices)
		return glm::vec3();


	if (Utils::valid_pointer(matrices) || index > 0 || matrices != 0)
	{

		glm::vec3 boneLocation = get_transform_position1(matrices, index);

		return boneLocation;
	}
	else
	{
		//error getting bone position, clean up and return null vector
		return glm::vec3();
	}
}

glm::vec3 get_transform_position1(ULONG64 pMatrix, ULONG64 index)
{
	uint64_t matrix_list_base = 0;
	uint64_t dependency_index_table_base = 0;

	auto handlePre = mem.CreateScatterHandle();

	mem.AddScatterReadRequest(handlePre, pMatrix + 0x40, &matrix_list_base, sizeof(matrix_list_base));
	mem.AddScatterReadRequest(handlePre, pMatrix + 0x68, &dependency_index_table_base, sizeof(dependency_index_table_base));

	//target.read(pMatrix + 0x18, &matrix_list_base, sizeof(matrix_list_base));
	//target.read(pMatrix + 0x20, &dependency_index_table_base, sizeof(dependency_index_table_base));

	mem.ExecuteReadScatter(handlePre);
	mem.CloseScatterHandle(handlePre);

	static auto get_dependency_index = [](uint64_t base, int32_t index) {
		index = mem.Read<uint32_t>(base + static_cast<unsigned long long>(index) * 4);
		return index;
		};

	static auto get_matrix_blob = [](VMMDLL_SCATTER_HANDLE handle, uint64_t base, uint64_t offs, float* blob, uint32_t size) {
		mem.AddScatterReadRequest(handle, base + offs, blob, size);
		};

	static auto get_matrix_blob_std = [](uint64_t base, uint64_t offs, float* blob, uint32_t size) {
		mem.Read(base + offs, blob, size);
		};


	int32_t index_relation = get_dependency_index(dependency_index_table_base, index);

	glm::vec3 ret_value;
	{
		float* base_matrix3x4 = (float*)malloc(64),
			* matrix3x4_buffer0 = (float*)((uint64_t)base_matrix3x4 + 16),
			* matrix3x4_buffer1 = (float*)((uint64_t)base_matrix3x4 + 32),
			* matrix3x4_buffer2 = (float*)((uint64_t)base_matrix3x4 + 48);

		get_matrix_blob_std(matrix_list_base, index * 48, base_matrix3x4, 16);

		__m128 xmmword_1410D1340 = { -2.f, 2.f, -2.f, 0.f };
		__m128 xmmword_1410D1350 = { 2.f, -2.f, -2.f, 0.f };
		__m128 xmmword_1410D1360 = { -2.f, -2.f, 2.f, 0.f };

		while (index_relation >= 0) {
			uint32_t matrix_relation_index = 6 * index_relation;

			auto handle2 = mem.CreateScatterHandle();


			get_matrix_blob(handle2, matrix_list_base, 8 * static_cast<uint64_t>(matrix_relation_index), matrix3x4_buffer2, 16);


			get_matrix_blob(handle2, matrix_list_base, 8 * static_cast<uint64_t>(matrix_relation_index) + 32, matrix3x4_buffer0, 16);


			get_matrix_blob(handle2, matrix_list_base, 8 * static_cast<uint64_t>(matrix_relation_index) + 16, matrix3x4_buffer1, 16);

			mem.ExecuteReadScatter(handle2);



			__m128 v_0 = *(__m128*)matrix3x4_buffer2;
			__m128 v_1 = *(__m128*)matrix3x4_buffer0;
			__m128i v9 = *(__m128i*)matrix3x4_buffer1;
			__m128* v3 = (__m128*)base_matrix3x4; // r10@1
			__m128 v10; // xmm9@2
			__m128 v11; // xmm3@2
			__m128 v12; // xmm8@2
			__m128 v13; // xmm4@2
			__m128 v14; // xmm2@2
			__m128 v15; // xmm5@2
			__m128 v16; // xmm6@2
			__m128 v17; // xmm7@2

			v10 = _mm_mul_ps(v_1, *v3);
			v11 = _mm_castsi128_ps(_mm_shuffle_epi32(v9, 0));
			v12 = _mm_castsi128_ps(_mm_shuffle_epi32(v9, 85));
			v13 = _mm_castsi128_ps(_mm_shuffle_epi32(v9, -114));
			v14 = _mm_castsi128_ps(_mm_shuffle_epi32(v9, -37));
			v15 = _mm_castsi128_ps(_mm_shuffle_epi32(v9, -86));
			v16 = _mm_castsi128_ps(_mm_shuffle_epi32(v9, 113));

			v17 = _mm_add_ps(
				_mm_add_ps(
					_mm_add_ps(
						_mm_mul_ps(
							_mm_sub_ps(
								_mm_mul_ps(_mm_mul_ps(v11, (__m128)xmmword_1410D1350), v13),
								_mm_mul_ps(_mm_mul_ps(v12, (__m128)xmmword_1410D1360), v14)),
							_mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(v10), -86))),
						_mm_mul_ps(
							_mm_sub_ps(
								_mm_mul_ps(_mm_mul_ps(v15, (__m128)xmmword_1410D1360), v14),
								_mm_mul_ps(_mm_mul_ps(v11, (__m128)xmmword_1410D1340), v16)),
							_mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(v10), 85)))),
					_mm_add_ps(
						_mm_mul_ps(
							_mm_sub_ps(
								_mm_mul_ps(_mm_mul_ps(v12, (__m128)xmmword_1410D1340), v16),
								_mm_mul_ps(_mm_mul_ps(v15, (__m128)xmmword_1410D1350), v13)),
							_mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(v10), 0))),
						v10)),
				v_0);

			*v3 = v17;

			index_relation = get_dependency_index(dependency_index_table_base, index_relation);

			mem.CloseScatterHandle(handle2);

		}
		ret_value = *(glm::vec3*)base_matrix3x4;
		delete[] base_matrix3x4;
	}

	return ret_value;
}

glm::vec3 Utils::transform::position::getPositionFromTransform(uint64_t transform)
{
	if (transform == 0 || transform == NULL)
		return  glm::vec3();

	uint64_t transform_internal = mem.Read<uint64_t>(transform + 0x10);
	if (transform_internal == 0)
	{
		std::cout << "Error in transform_internal read" << std::endl;
		return  glm::vec3();
	}

	uint64_t matrices = mem.Read<uint64_t>(transform_internal + 0x70);
	if (matrices == 0)
	{
		std::cout << "Error in matrices read" << std::endl;
		return glm::vec3();
	}

	uint32_t index = mem.Read<uint32_t>(transform_internal + 0x78);
	if (!matrices || index < 0)
	{
		std::cout << "Error in index read" << std::endl;
		return  glm::vec3();
	}

	return get_transform_position1(matrices, index);
}
