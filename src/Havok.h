#pragma once

using GetOriginalVerticesFn = void (*)(const RE::hkpConvexVerticesShape* a_this, RE::hkArray<RE::hkVector4>& a_outVertices);

inline REL::Relocation<GetOriginalVerticesFn> hkpConvexVerticesShape_getOriginalVertices{ RELOCATION_ID(64067, 65093) };
