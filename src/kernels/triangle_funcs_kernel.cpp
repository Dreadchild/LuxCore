#include <string>
namespace luxrays { namespace ocl {
std::string KernelSource_triangle_funcs = 
"#line 2 \"triangle_funcs.cl\"\n"
"\n"
"/***************************************************************************\n"
" *   Copyright (C) 1998-2010 by authors (see AUTHORS.txt )                 *\n"
" *                                                                         *\n"
" *   This file is part of LuxRays.                                         *\n"
" *                                                                         *\n"
" *   LuxRays is free software; you can redistribute it and/or modify       *\n"
" *   it under the terms of the GNU General Public License as published by  *\n"
" *   the Free Software Foundation; either version 3 of the License, or     *\n"
" *   (at your option) any later version.                                   *\n"
" *                                                                         *\n"
" *   LuxRays is distributed in the hope that it will be useful,            *\n"
" *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *\n"
" *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *\n"
" *   GNU General Public License for more details.                          *\n"
" *                                                                         *\n"
" *   You should have received a copy of the GNU General Public License     *\n"
" *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *\n"
" *                                                                         *\n"
" *   LuxRays website: http://www.luxrender.net                             *\n"
" ***************************************************************************/\n"
"\n"
"void Triangle_UniformSample(const float u0, const float u1, float *b1, float *b2) {\n"
"	const float su1 = sqrt(u0);\n"
"	*b1 = 1.f - su1;\n"
"	*b2 = u1 * su1;\n"
"}\n"
"\n"
"float3 Triangle_Sample(const float3 p0, const float3 p1, const float3 p2,\n"
"		const float u0, const float u1,\n"
"		float *b0, float *b1, float *b2) {\n"
"		Triangle_UniformSample(u0, u1, b1, b2);\n"
"		*b0 = 1.f - (*b1) - (*b2);\n"
"\n"
"		return (*b0) * p0 + (*b1) * p1 + (*b2) * p2;\n"
"}\n"
"\n"
"float3 Triangle_GetGeometryNormal(const float3 p0, const float3 p1, const float3 p2) {\n"
"	return normalize(cross(p1 - p0, p2 - p0));\n"
"}\n"
"\n"
"float3 Triangle_InterpolateNormal(const float3 n0, const float3 n1, const float3 n2,\n"
"		const float b0, const float b1, const float b2) {\n"
"	return normalize(b0 * n0 + b1 * n1 + b2 * n2);\n"
"}\n"
"\n"
"float2 Triangle_InterpolateUV(const float2 uv0, const float2 uv1, const float2 uv2,\n"
"		const float b0, const float b1, const float b2) {\n"
"	return normalize(b0 * uv0 + b1 * uv1 + b2 * uv2);\n"
"}\n"
; } }
