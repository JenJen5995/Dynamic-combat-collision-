#pragma once

#include "RE/H/hkArray.h"
#include "RE/H/hkVector4.h"
#include "RE/H/hkpConvexShape.h"

namespace RE
{
	class hkpConvexVerticesShape : public hkpConvexShape
	{
	public:
		inline static constexpr auto RTTI = RTTI_hkpConvexVerticesShape;
		inline static constexpr auto VTABLE = VTABLE_hkpConvexVerticesShape;

		struct FourVectors
		{
			hkVector4 x;
			hkVector4 y;
			hkVector4 z;
		};

		hkVector4            aabbHalfExtents;
		hkVector4            aabbCenter;
		hkArray<FourVectors> rotatedVertices;
		std::int32_t         numVertices;
		void*                externalObject;
		void*                getFaceNormals;
		hkArray<hkVector4>   planeEquations;
		void*                connectivity;
	};
	static_assert(sizeof(hkpConvexVerticesShape) == 0x90);
}

using GetOriginalVerticesFn = void (*)(const RE::hkpConvexVerticesShape* a_this, RE::hkArray<RE::hkVector4>& a_outVertices);

inline REL::Relocation<GetOriginalVerticesFn> hkpConvexVerticesShape_getOriginalVertices{ RELOCATION_ID(64067, 65093) };
