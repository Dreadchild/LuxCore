#include <string>
namespace slg { namespace ocl {
std::string KernelSource_light_funcs = 
"#line 2 \"light_funcs.cl\"\n"
"\n"
"/***************************************************************************\n"
" * Copyright 1998-2013 by authors (see AUTHORS.txt)                        *\n"
" *                                                                         *\n"
" *   This file is part of LuxRender.                                       *\n"
" *                                                                         *\n"
" * Licensed under the Apache License, Version 2.0 (the \"License\");         *\n"
" * you may not use this file except in compliance with the License.        *\n"
" * You may obtain a copy of the License at                                 *\n"
" *                                                                         *\n"
" *     http://www.apache.org/licenses/LICENSE-2.0                          *\n"
" *                                                                         *\n"
" * Unless required by applicable law or agreed to in writing, software     *\n"
" * distributed under the License is distributed on an \"AS IS\" BASIS,       *\n"
" * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.*\n"
" * See the License for the specific language governing permissions and     *\n"
" * limitations under the License.                                          *\n"
" ***************************************************************************/\n"
"\n"
"//------------------------------------------------------------------------------\n"
"// InfiniteLight\n"
"//------------------------------------------------------------------------------\n"
"\n"
"#if defined(PARAM_HAS_INFINITELIGHT)\n"
"\n"
"float3 InfiniteLight_GetRadiance(__global LightSource *infiniteLight,\n"
"		__global float *infiniteLightDistirbution,\n"
"		const float3 dir, float *directPdfA\n"
"		IMAGEMAPS_PARAM_DECL) {\n"
"	__global ImageMap *imageMap = &imageMapDescs[infiniteLight->notIntersecable.infinite.imageMapIndex];\n"
"	__global float *pixels = ImageMap_GetPixelsAddress(\n"
"			imageMapBuff, imageMap->pageIndex, imageMap->pixelsIndex);\n"
"\n"
"	const float3 localDir = normalize(Transform_InvApplyVector(&infiniteLight->notIntersecable.light2World, -dir));\n"
"	const float2 uv = (float2)(\n"
"		SphericalPhi(localDir) * (1.f / (2.f * M_PI_F)),\n"
"		SphericalTheta(localDir) * M_1_PI_F);\n"
"\n"
"	// TextureMapping2D_Map() is expended here\n"
"	const float2 scale = VLOAD2F(&infiniteLight->notIntersecable.infinite.mapping.uvMapping2D.uScale);\n"
"	const float2 delta = VLOAD2F(&infiniteLight->notIntersecable.infinite.mapping.uvMapping2D.uDelta);\n"
"	const float2 mapUV = uv * scale + delta;\n"
"\n"
"	const float distPdf = Distribution2D_Pdf(infiniteLightDistirbution, mapUV.s0, mapUV.s1);\n"
"	*directPdfA = distPdf / (4.f * M_PI_F);\n"
"\n"
"	return VLOAD3F(&infiniteLight->notIntersecable.gain.r) * ImageMap_GetSpectrum(\n"
"			pixels,\n"
"			imageMap->width, imageMap->height, imageMap->channelCount,\n"
"			mapUV.s0, mapUV.s1);\n"
"}\n"
"\n"
"float3 InfiniteLight_Illuminate(__global LightSource *infiniteLight,\n"
"		__global float *infiniteLightDistirbution,\n"
"		const float worldCenterX, const float worldCenterY, const float worldCenterZ,\n"
"		const float sceneRadius,\n"
"		const float u0, const float u1, const float3 p,\n"
"		float3 *dir, float *distance, float *directPdfW\n"
"		IMAGEMAPS_PARAM_DECL) {\n"
"	float2 sampleUV;\n"
"	float distPdf;\n"
"	Distribution2D_SampleContinuous(infiniteLightDistirbution, u0, u1, &sampleUV, &distPdf);\n"
"\n"
"	const float phi = sampleUV.s0 * 2.f * M_PI_F;\n"
"	const float theta = sampleUV.s1 * M_PI_F;\n"
"	*dir = normalize(Transform_ApplyVector(&infiniteLight->notIntersecable.light2World,\n"
"			SphericalDirection(sin(theta), cos(theta), phi)));\n"
"\n"
"	const float3 worldCenter = (float3)(worldCenterX, worldCenterY, worldCenterZ);\n"
"	const float worldRadius = PARAM_LIGHT_WORLD_RADIUS_SCALE * sceneRadius * 1.01f;\n"
"\n"
"	const float3 toCenter = worldCenter - p;\n"
"	const float centerDistance = dot(toCenter, toCenter);\n"
"	const float approach = dot(toCenter, *dir);\n"
"	*distance = approach + sqrt(max(0.f, worldRadius * worldRadius -\n"
"		centerDistance + approach * approach));\n"
"\n"
"	const float3 emisPoint = p + (*distance) * (*dir);\n"
"	const float3 emisNormal = normalize(worldCenter - emisPoint);\n"
"\n"
"	const float cosAtLight = dot(emisNormal, -(*dir));\n"
"	if (cosAtLight < DEFAULT_COS_EPSILON_STATIC)\n"
"		return BLACK;\n"
"\n"
"	*directPdfW = distPdf / (4.f * M_PI_F);\n"
"\n"
"	// InfiniteLight_GetRadiance is expended here\n"
"	__global ImageMap *imageMap = &imageMapDescs[infiniteLight->notIntersecable.infinite.imageMapIndex];\n"
"	__global float *pixels = ImageMap_GetPixelsAddress(\n"
"			imageMapBuff, imageMap->pageIndex, imageMap->pixelsIndex);\n"
"\n"
"	const float2 uv = (float2)(sampleUV.s0, sampleUV.s1);\n"
"\n"
"	// TextureMapping2D_Map() is expended here\n"
"	const float2 scale = VLOAD2F(&infiniteLight->notIntersecable.infinite.mapping.uvMapping2D.uScale);\n"
"	const float2 delta = VLOAD2F(&infiniteLight->notIntersecable.infinite.mapping.uvMapping2D.uDelta);\n"
"	const float2 mapUV = uv * scale + delta;\n"
"	\n"
"	return VLOAD3F(&infiniteLight->notIntersecable.gain.r) * ImageMap_GetSpectrum(\n"
"			pixels,\n"
"			imageMap->width, imageMap->height, imageMap->channelCount,\n"
"			mapUV.s0, mapUV.s1);\n"
"}\n"
"\n"
"#endif\n"
"\n"
"//------------------------------------------------------------------------------\n"
"// SktLight\n"
"//------------------------------------------------------------------------------\n"
"\n"
"#if defined(PARAM_HAS_SKYLIGHT)\n"
"\n"
"float SkyLight_PerezBase(__global float *lam, const float theta, const float gamma) {\n"
"	return (1.f + lam[1] * exp(lam[2] / cos(theta))) *\n"
"		(1.f + lam[3] * exp(lam[4] * gamma)  + lam[5] * cos(gamma) * cos(gamma));\n"
"}\n"
"\n"
"float SkyLight_RiAngleBetween(const float thetav, const float phiv, const float theta, const float phi) {\n"
"	const float cospsi = sin(thetav) * sin(theta) * cos(phi - phiv) + cos(thetav) * cos(theta);\n"
"	if (cospsi >= 1.f)\n"
"		return 0.f;\n"
"	if (cospsi <= -1.f)\n"
"		return M_PI_F;\n"
"	return acos(cospsi);\n"
"}\n"
"\n"
"float3 SkyLight_ChromaticityToSpectrum(float Y, float x, float y) {\n"
"	float X, Z;\n"
"	\n"
"	if (y != 0.f)\n"
"		X = (x / y) * Y;\n"
"	else\n"
"		X = 0.f;\n"
"	\n"
"	if (y != 0.f && Y != 0.f)\n"
"		Z = (1.f - x - y) / y * Y;\n"
"	else\n"
"		Z = 0.f;\n"
"\n"
"	// Assuming sRGB (D65 illuminant)\n"
"	return (float3)(3.2410f * X - 1.5374f * Y - 0.4986f * Z,\n"
"			-0.9692f * X + 1.8760f * Y + 0.0416f * Z,\n"
"			0.0556f * X - 0.2040f * Y + 1.0570f * Z);\n"
"}\n"
"\n"
"float3 SkyLight_GetSkySpectralRadiance(__global LightSource *skyLight,\n"
"		const float theta, const float phi) {\n"
"	// Add bottom half of hemisphere with horizon colour\n"
"	const float theta_fin = fmin(theta, (M_PI_F * .5f) - .001f);\n"
"	const float gamma = SkyLight_RiAngleBetween(theta, phi, \n"
"			skyLight->notIntersecable.sky.thetaS, skyLight->notIntersecable.sky.phiS);\n"
"\n"
"	// Compute xyY values\n"
"	const float x = skyLight->notIntersecable.sky.zenith_x * SkyLight_PerezBase(\n"
"			skyLight->notIntersecable.sky.perez_x, theta_fin, gamma);\n"
"	const float y = skyLight->notIntersecable.sky.zenith_y * SkyLight_PerezBase(\n"
"			skyLight->notIntersecable.sky.perez_y, theta_fin, gamma);\n"
"	const float Y = skyLight->notIntersecable.sky.zenith_Y * SkyLight_PerezBase(\n"
"			skyLight->notIntersecable.sky.perez_Y, theta_fin, gamma);\n"
"\n"
"	return SkyLight_ChromaticityToSpectrum(Y, x, y);\n"
"}\n"
"\n"
"float3 SkyLight_GetRadiance(__global LightSource *skyLight, const float3 dir,\n"
"		float *directPdfA) {\n"
"	*directPdfA = 1.f / (4.f * M_PI_F);\n"
"\n"
"	const float3 localDir = normalize(Transform_InvApplyVector(&skyLight->notIntersecable.light2World, -dir));\n"
"	const float theta = SphericalTheta(localDir);\n"
"	const float phi = SphericalPhi(localDir);\n"
"	const float3 s = SkyLight_GetSkySpectralRadiance(skyLight, theta, phi);\n"
"\n"
"	return VLOAD3F(&skyLight->notIntersecable.gain.r) * s;\n"
"}\n"
"\n"
"float3 SkyLight_Illuminate(__global LightSource *skyLight,\n"
"		const float worldCenterX, const float worldCenterY, const float worldCenterZ,\n"
"		const float sceneRadius,\n"
"		const float u0, const float u1, const float3 p,\n"
"		float3 *dir, float *distance, float *directPdfW) {\n"
"	const float3 worldCenter = (float3)(worldCenterX, worldCenterY, worldCenterZ);\n"
"	const float worldRadius = PARAM_LIGHT_WORLD_RADIUS_SCALE * sceneRadius * 1.01f;\n"
"\n"
"	const float3 localDir = normalize(Transform_ApplyVector(&skyLight->notIntersecable.light2World, -(*dir)));\n"
"	*dir = normalize(Transform_ApplyVector(&skyLight->notIntersecable.light2World,  UniformSampleSphere(u0, u1)));\n"
"\n"
"	const float3 toCenter = worldCenter - p;\n"
"	const float centerDistance = dot(toCenter, toCenter);\n"
"	const float approach = dot(toCenter, *dir);\n"
"	*distance = approach + sqrt(max(0.f, worldRadius * worldRadius -\n"
"		centerDistance + approach * approach));\n"
"\n"
"	const float3 emisPoint = p + (*distance) * (*dir);\n"
"	const float3 emisNormal = normalize(worldCenter - emisPoint);\n"
"\n"
"	const float cosAtLight = dot(emisNormal, -(*dir));\n"
"	if (cosAtLight < DEFAULT_COS_EPSILON_STATIC)\n"
"		return BLACK;\n"
"\n"
"	return SkyLight_GetRadiance(skyLight, -(*dir), directPdfW);\n"
"}\n"
"\n"
"#endif\n"
"\n"
"//------------------------------------------------------------------------------\n"
"// SunLight\n"
"//------------------------------------------------------------------------------\n"
"\n"
"#if defined(PARAM_HAS_SUNLIGHT)\n"
"\n"
"float3 SunLight_Illuminate(__global LightSource *sunLight,\n"
"		const float u0, const float u1,\n"
"		float3 *dir, float *distance, float *directPdfW) {\n"
"	const float cosThetaMax = sunLight->notIntersecable.sun.cosThetaMax;\n"
"	const float3 sunDir = VLOAD3F(&sunLight->notIntersecable.sun.sunDir.x);\n"
"	*dir = UniformSampleCone(u0, u1, cosThetaMax, VLOAD3F(&sunLight->notIntersecable.sun.x.x), VLOAD3F(&sunLight->notIntersecable.sun.y.x), sunDir);\n"
"\n"
"	// Check if the point can be inside the sun cone of light\n"
"	const float cosAtLight = dot(sunDir, *dir);\n"
"	if (cosAtLight <= cosThetaMax)\n"
"		return BLACK;\n"
"\n"
"	*distance = INFINITY;\n"
"	*directPdfW = UniformConePdf(cosThetaMax);\n"
"\n"
"	return VLOAD3F(&sunLight->notIntersecable.sun.sunColor.r);\n"
"}\n"
"\n"
"float3 SunLight_GetRadiance(__global LightSource *sunLight, const float3 dir, float *directPdfA) {\n"
"	const float cosThetaMax = sunLight->notIntersecable.sun.cosThetaMax;\n"
"	const float3 sunDir = VLOAD3F(&sunLight->notIntersecable.sun.sunDir.x);\n"
"\n"
"	if ((cosThetaMax < 1.f) && (dot(-dir, sunDir) > cosThetaMax)) {\n"
"		if (directPdfA)\n"
"			*directPdfA = UniformConePdf(cosThetaMax);\n"
"\n"
"		return VLOAD3F(&sunLight->notIntersecable.sun.sunColor.r);\n"
"	} else\n"
"		return BLACK;\n"
"}\n"
"\n"
"#endif\n"
"\n"
"//------------------------------------------------------------------------------\n"
"// TriangleLight\n"
"//------------------------------------------------------------------------------\n"
"\n"
"#if (PARAM_TRIANGLE_LIGHT_COUNT > 0)\n"
"\n"
"float3 TriangleLight_Illuminate(__global LightSource *triLight,\n"
"		__global HitPoint *tmpHitPoint,\n"
"		const float3 p, const float u0, const float u1,\n"
"#if defined(PARAM_HAS_PASSTHROUGH)\n"
"		const float passThroughEvent,\n"
"#endif\n"
"		float3 *dir, float *distance, float *directPdfW\n"
"		MATERIALS_PARAM_DECL) {\n"
"	const float3 p0 = VLOAD3F(&triLight->triangle.v0.x);\n"
"	const float3 p1 = VLOAD3F(&triLight->triangle.v1.x);\n"
"	const float3 p2 = VLOAD3F(&triLight->triangle.v2.x);\n"
"	float b0, b1, b2;\n"
"	const float3 samplePoint = Triangle_Sample(\n"
"			p0, p1, p2,\n"
"			u0, u1,\n"
"			&b0, &b1, &b2);\n"
"\n"
"	const float3 sampleN = Triangle_GetGeometryNormal(p0, p1, p2); // Light sources are supposed to be flat\n"
"\n"
"	*dir = samplePoint - p;\n"
"	const float distanceSquared = dot(*dir, *dir);;\n"
"	*distance = sqrt(distanceSquared);\n"
"	*dir /= (*distance);\n"
"\n"
"	const float cosAtLight = dot(sampleN, -(*dir));\n"
"	if (cosAtLight < DEFAULT_COS_EPSILON_STATIC)\n"
"		return BLACK;\n"
"\n"
"	float3 emissionColor = WHITE;\n"
"	if (triLight->triangle.imageMapIndex != NULL_INDEX) {\n"
"		// Build the local frame\n"
"		float3 X, Y;\n"
"		CoordinateSystem(sampleN, &X, &Y);\n"
"\n"
"		const float3 localFromLight = ToLocal(X, Y, sampleN, -(*dir));\n"
"\n"
"		// Retrieve the image map information\n"
"		__global ImageMap *imageMap = &imageMapDescs[triLight->triangle.imageMapIndex];\n"
"		__global float *pixels = ImageMap_GetPixelsAddress(\n"
"				imageMapBuff, imageMap->pageIndex, imageMap->pixelsIndex);\n"
"		const float2 uv = (float2)(SphericalPhi(localFromLight) * (1.f / (2.f * M_PI_F)), SphericalTheta(localFromLight) * M_1_PI_F);\n"
"		emissionColor = ImageMap_GetSpectrum(\n"
"			pixels,\n"
"			imageMap->width, imageMap->height, imageMap->channelCount,\n"
"			uv.s0, uv.s1) / triLight->triangle.avarage;\n"
"\n"
"		*directPdfW = triLight->triangle.invArea * distanceSquared ;\n"
"	} else\n"
"		*directPdfW = triLight->triangle.invArea * distanceSquared / cosAtLight;\n"
"\n"
"	const float2 uv0 = VLOAD2F(&triLight->triangle.uv0.u);\n"
"	const float2 uv1 = VLOAD2F(&triLight->triangle.uv1.u);\n"
"	const float2 uv2 = VLOAD2F(&triLight->triangle.uv2.u);\n"
"	const float2 triUV = Triangle_InterpolateUV(uv0, uv1, uv2, b0, b1, b2);\n"
"\n"
"	VSTORE3F(-sampleN, &tmpHitPoint->fixedDir.x);\n"
"	VSTORE3F(samplePoint, &tmpHitPoint->p.x);\n"
"	VSTORE2F(triUV, &tmpHitPoint->uv.u);\n"
"	VSTORE3F(sampleN, &tmpHitPoint->geometryN.x);\n"
"	VSTORE3F(sampleN, &tmpHitPoint->shadeN.x);\n"
"#if defined(PARAM_HAS_PASSTHROUGH)\n"
"	tmpHitPoint->passThroughEvent = passThroughEvent;\n"
"#endif\n"
"\n"
"	return Material_GetEmittedRadiance(&mats[triLight->triangle.materialIndex],\n"
"			tmpHitPoint, triLight->triangle.invArea\n"
"			MATERIALS_PARAM) * emissionColor;\n"
"}\n"
"\n"
"float3 TriangleLight_GetRadiance(__global LightSource *triLight,\n"
"		 __global HitPoint *hitPoint, float *directPdfA\n"
"		MATERIALS_PARAM_DECL) {\n"
"	const float3 dir = VLOAD3F(&hitPoint->fixedDir.x);\n"
"	const float3 hitPointNormal = VLOAD3F(&hitPoint->geometryN.x);\n"
"	const float cosOutLight = dot(hitPointNormal, dir);\n"
"	if (cosOutLight <= 0.f)\n"
"		return BLACK;\n"
"\n"
"	if (directPdfA)\n"
"		*directPdfA = triLight->triangle.invArea;\n"
"\n"
"	float3 emissionColor = WHITE;\n"
"	if (triLight->triangle.imageMapIndex != NULL_INDEX) {\n"
"		// Build the local frame\n"
"		float3 X, Y;\n"
"		CoordinateSystem(hitPointNormal, &X, &Y);\n"
"\n"
"		const float3 localFromLight = ToLocal(X, Y, hitPointNormal, dir);\n"
"\n"
"		// Retrieve the image map information\n"
"		__global ImageMap *imageMap = &imageMapDescs[triLight->triangle.imageMapIndex];\n"
"		__global float *pixels = ImageMap_GetPixelsAddress(\n"
"				imageMapBuff, imageMap->pageIndex, imageMap->pixelsIndex);\n"
"		const float2 uv = (float2)(SphericalPhi(localFromLight) * (1.f / (2.f * M_PI_F)), SphericalTheta(localFromLight) * M_1_PI_F);\n"
"		emissionColor = ImageMap_GetSpectrum(\n"
"			pixels,\n"
"			imageMap->width, imageMap->height, imageMap->channelCount,\n"
"			uv.s0, uv.s1) / triLight->triangle.avarage;\n"
"	}\n"
"\n"
"	return Material_GetEmittedRadiance(&mats[triLight->triangle.materialIndex],\n"
"			hitPoint, triLight->triangle.invArea\n"
"			MATERIALS_PARAM) * emissionColor;\n"
"}\n"
"\n"
"#endif\n"
"\n"
"//------------------------------------------------------------------------------\n"
"// PointLight\n"
"//------------------------------------------------------------------------------\n"
"\n"
"#if defined(PARAM_HAS_POINTLIGHT)\n"
"\n"
"float3 PointLight_Illuminate(__global LightSource *pointLight,\n"
"		const float3 p,	float3 *dir, float *distance, float *directPdfW) {\n"
"	const float3 toLight = VLOAD3F(&pointLight->notIntersecable.point.absolutePos.x) - p;\n"
"	const float distanceSquared = dot(toLight, toLight);\n"
"	*distance = sqrt(distanceSquared);\n"
"	*dir = toLight / *distance;\n"
"\n"
"	*directPdfW = distanceSquared;\n"
"\n"
"	return VLOAD3F(&pointLight->notIntersecable.point.emittedFactor.r);\n"
"}\n"
"\n"
"#endif\n"
"\n"
"//------------------------------------------------------------------------------\n"
"// MapPointLight\n"
"//------------------------------------------------------------------------------\n"
"\n"
"#if defined(PARAM_HAS_MAPPOINTLIGHT)\n"
"\n"
"float3 MapPointLight_Illuminate(__global LightSource *mapPointLight,\n"
"		const float3 p,	float3 *dir, float *distance, float *directPdfW\n"
"		IMAGEMAPS_PARAM_DECL) {\n"
"	const float3 toLight = VLOAD3F(&mapPointLight->notIntersecable.mapPoint.absolutePos.x) - p;\n"
"	const float distanceSquared = dot(toLight, toLight);\n"
"	*distance = sqrt(distanceSquared);\n"
"	*dir = toLight / *distance;\n"
"\n"
"	*directPdfW = distanceSquared;\n"
"\n"
"	// Retrieve the image map information\n"
"	__global ImageMap *imageMap = &imageMapDescs[mapPointLight->notIntersecable.mapPoint.imageMapIndex];\n"
"	__global float *pixels = ImageMap_GetPixelsAddress(\n"
"			imageMapBuff, imageMap->pageIndex, imageMap->pixelsIndex);\n"
"\n"
"	const float3 localFromLight = normalize(Transform_InvApplyVector(&mapPointLight->notIntersecable.light2World, p) - \n"
"		VLOAD3F(&mapPointLight->notIntersecable.mapPoint.localPos.x));\n"
"	const float2 uv = (float2)(SphericalPhi(localFromLight) * (1.f / (2.f * M_PI_F)), SphericalTheta(localFromLight) * M_1_PI_F);\n"
"	const float3 emissionColor = ImageMap_GetSpectrum(\n"
"			pixels,\n"
"			imageMap->width, imageMap->height, imageMap->channelCount,\n"
"			uv.s0, uv.s1) / mapPointLight->notIntersecable.mapPoint.avarage;\n"
"\n"
"	return VLOAD3F(&mapPointLight->notIntersecable.mapPoint.emittedFactor.r) * emissionColor;\n"
"}\n"
"\n"
"#endif\n"
"\n"
"//------------------------------------------------------------------------------\n"
"// Generic light functions\n"
"//------------------------------------------------------------------------------\n"
"\n"
"float3 EnvLight_GetRadiance(__global LightSource *light, const float3 dir, float *directPdfA\n"
"				LIGHTS_PARAM_DECL) {\n"
"	switch (light->type) {\n"
"#if defined(PARAM_HAS_INFINITELIGHT)\n"
"		case TYPE_IL:\n"
"			return InfiniteLight_GetRadiance(light,\n"
"					&infiniteLightDistribution[light->notIntersecable.infinite.distributionOffset],\n"
"					dir, directPdfA\n"
"					IMAGEMAPS_PARAM);\n"
"#endif\n"
"#if defined(PARAM_HAS_SKYLIGHT)\n"
"		case TYPE_IL_SKY:\n"
"			return SkyLight_GetRadiance(light,\n"
"					dir, directPdfA);\n"
"#endif\n"
"#if defined(PARAM_HAS_SUNLIGHT)\n"
"		case TYPE_SUN:\n"
"			return SunLight_GetRadiance(light,\n"
"					dir, directPdfA);\n"
"#endif\n"
"		default:\n"
"			return BLACK;\n"
"	}\n"
"}\n"
"\n"
"#if (PARAM_TRIANGLE_LIGHT_COUNT > 0)\n"
"float3 IntersecableLight_GetRadiance(__global LightSource *light,\n"
"		 __global HitPoint *hitPoint, float *directPdfA\n"
"		LIGHTS_PARAM_DECL) {\n"
"	return TriangleLight_GetRadiance(light, hitPoint, directPdfA\n"
"			MATERIALS_PARAM);\n"
"}\n"
"#endif\n"
"\n"
"float3 Light_Illuminate(\n"
"		__global LightSource *light,\n"
"		const float3 point,\n"
"		const float u0, const float u1, const float u2,\n"
"#if defined(PARAM_HAS_PASSTHROUGH)\n"
"		const float u3,\n"
"#endif\n"
"#if defined(PARAM_HAS_INFINITELIGHT) || defined(PARAM_HAS_SKYLIGHT)\n"
"		const float worldCenterX,\n"
"		const float worldCenterY,\n"
"		const float worldCenterZ,\n"
"		const float worldRadius,\n"
"#endif\n"
"#if (PARAM_TRIANGLE_LIGHT_COUNT > 0)\n"
"		__global HitPoint *tmpHitPoint,\n"
"#endif\n"
"		float3 *lightRayDir, float *distance, float *directPdfW\n"
"		LIGHTS_PARAM_DECL) {\n"
"	switch (light->type) {\n"
"#if defined(PARAM_HAS_INFINITELIGHT)\n"
"		case TYPE_IL:\n"
"			return InfiniteLight_Illuminate(\n"
"				light,\n"
"				&infiniteLightDistribution[light->notIntersecable.infinite.distributionOffset],\n"
"				worldCenterX, worldCenterY, worldCenterZ, worldRadius,\n"
"				u0, u1,\n"
"				point,\n"
"				lightRayDir, distance, directPdfW\n"
"				IMAGEMAPS_PARAM);\n"
"#endif\n"
"#if defined(PARAM_HAS_SKYLIGHT)\n"
"		case TYPE_IL_SKY:\n"
"			return SkyLight_Illuminate(\n"
"				light,\n"
"				worldCenterX, worldCenterY, worldCenterZ, worldRadius,\n"
"				u0, u1,\n"
"				point,\n"
"				lightRayDir, distance, directPdfW);\n"
"#endif\n"
"#if defined(PARAM_HAS_SUNLIGHT)\n"
"		case TYPE_SUN:\n"
"			return SunLight_Illuminate(\n"
"				light,\n"
"				u0, u1,\n"
"				lightRayDir, distance, directPdfW);\n"
"#endif\n"
"#if (PARAM_TRIANGLE_LIGHT_COUNT > 0)\n"
"		case TYPE_TRIANGLE:\n"
"			return TriangleLight_Illuminate(\n"
"					light,\n"
"					tmpHitPoint,\n"
"					point,\n"
"					u0, u1,\n"
"#if defined(PARAM_HAS_PASSTHROUGH)\n"
"					u3,\n"
"#endif\n"
"					lightRayDir, distance, directPdfW\n"
"					MATERIALS_PARAM);\n"
"#endif\n"
"#if defined(PARAM_HAS_POINTLIGHT)\n"
"		case TYPE_POINT:\n"
"			return PointLight_Illuminate(\n"
"					light, point,\n"
"					lightRayDir, distance, directPdfW);\n"
"#endif\n"
"#if defined(PARAM_HAS_MAPPOINTLIGHT)\n"
"		case TYPE_MAPPOINT:\n"
"			return MapPointLight_Illuminate(\n"
"					light, point,\n"
"					lightRayDir, distance, directPdfW\n"
"					IMAGEMAPS_PARAM);\n"
"#endif\n"
"		default:\n"
"			return BLACK;\n"
"	}\n"
"}\n"
; } }
