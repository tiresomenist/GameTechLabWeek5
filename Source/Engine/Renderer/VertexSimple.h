#pragma once
#include <cstddef>
#include <type_traits>

#include "Core/Math/Vector.h"

struct FVertexSimple
{
	float x = 0.0f, y = 0.0f, z = 0.0f;    // Position
	float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f; // Color
};

struct FVertexTest
{
	float x, y, z;      // Position
	float nx, ny, nz;   // Normal (빛 테스트용)
	float u, v;         // UV (텍스처 테스트용)
};

struct FVertexTexture {
	float x = 0.0f, y = 0.0f, z = 0.0f;
	float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f; // Color
	float u = 0.0f, v = 0.0f;
};


struct FVertexPNCT {
	float x = 0.0f, y = 0.0f, z = 0.0f;				// Position
	float nx = 0.0f, ny = 0.0f, nz = 0.0f;			// Normal Vector
	float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;	//Color
	float u = 0.0f, v = 0.0f;						//UV
};

//형식 체크용 static_assert
static_assert(std::is_standard_layout_v<FVertexPNCT>);
static_assert(sizeof(FVertexPNCT) == 48);
static_assert(offsetof(FVertexPNCT, x) == 0);
static_assert(offsetof(FVertexPNCT, nx) == 12);
static_assert(offsetof(FVertexPNCT, r) == 24);
static_assert(offsetof(FVertexPNCT, u) == 40);

static_assert(std::is_standard_layout_v<FVertexTexture>);
static_assert(sizeof(FVertexTexture) == 36);
static_assert(offsetof(FVertexTexture, x) == 0);
static_assert(offsetof(FVertexTexture, r) == 12);
static_assert(offsetof(FVertexTexture, u) == 28);

