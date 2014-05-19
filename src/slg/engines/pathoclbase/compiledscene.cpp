/***************************************************************************
 * Copyright 1998-2013 by authors (see AUTHORS.txt)                        *
 *                                                                         *
 *   This file is part of LuxRender.                                       *
 *                                                                         *
 * Licensed under the Apache License, Version 2.0 (the "License");         *
 * you may not use this file except in compliance with the License.        *
 * You may obtain a copy of the License at                                 *
 *                                                                         *
 *     http://www.apache.org/licenses/LICENSE-2.0                          *
 *                                                                         *
 * Unless required by applicable law or agreed to in writing, software     *
 * distributed under the License is distributed on an "AS IS" BASIS,       *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.*
 * See the License for the specific language governing permissions and     *
 * limitations under the License.                                          *
 ***************************************************************************/

#if !defined(LUXRAYS_DISABLE_OPENCL)

#include <iosfwd>
#include <limits>

#include <boost/lexical_cast.hpp>

#include "slg/engines/pathoclbase/compiledscene.h"
#include "slg/sdl/blender_texture.h"

using namespace std;
using namespace luxrays;
using namespace slg;
using namespace slg::blender;

CompiledScene::CompiledScene(Scene *scn, Film *flm, const size_t maxMemPageS) {
	scene = scn;
	film = flm;
	maxMemPageSize = (u_int)Min<size_t>(maxMemPageS, 0xffffffffu);

	lightsDistribution = NULL;

	EditActionList editActions;
	editActions.AddAllAction();
	Recompile(editActions);
}

CompiledScene::~CompiledScene() {
	delete[] lightsDistribution;
}

void CompiledScene::CompileCamera() {
	//SLG_LOG("Compile Camera");

	//--------------------------------------------------------------------------
	// Camera definition
	//--------------------------------------------------------------------------

	switch (scene->camera->GetType()) {
		case Camera::PERSPECTIVE: {
			const PerspectiveCamera *perspCamera = (PerspectiveCamera *)scene->camera;
			camera.yon = perspCamera->clipYon;
			camera.hither = perspCamera->clipHither;
			camera.lensRadius = perspCamera->lensRadius;
			camera.focalDistance = perspCamera->focalDistance;

			memcpy(camera.rasterToCamera[0].m.m, perspCamera->GetRasterToCameraMatrix(0).m, 4 * 4 * sizeof(float));
			memcpy(camera.cameraToWorld[0].m.m, perspCamera->GetCameraToWorldMatrix(0).m, 4 * 4 * sizeof(float));

			if (perspCamera->IsHorizontalStereoEnabled()) {
				enableCameraHorizStereo = true;
				enableOculusRiftBarrel = perspCamera->IsOculusRiftBarrelEnabled();

				memcpy(camera.rasterToCamera[1].m.m, perspCamera->GetRasterToCameraMatrix(1).m, 4 * 4 * sizeof(float));
				memcpy(camera.cameraToWorld[1].m.m, perspCamera->GetCameraToWorldMatrix(1).m, 4 * 4 * sizeof(float));		
			} else {
				enableCameraHorizStereo = false;
				enableOculusRiftBarrel = false;
			}
			
			if (perspCamera->IsClippingPlaneEnabled()) {
				enableCameraClippingPlane = true;
				ASSIGN_VECTOR(camera.clippingPlaneCenter, perspCamera->clippingPlaneCenter);
				ASSIGN_VECTOR(camera.clippingPlaneNormal, perspCamera->clippingPlaneNormal);
			} else
				enableCameraClippingPlane = false;
			break;
		}
		default:
			throw std::runtime_error("Unknown camera type in CompiledScene::CompileCamera(): " + boost::lexical_cast<std::string>(scene->camera->GetType()));
	}
						
}

static bool MeshPtrCompare(Mesh *p0, Mesh *p1) {
	return p0 < p1;
}

void CompiledScene::CompileGeometry() {
	SLG_LOG("Compile Geometry");

	const u_int objCount = scene->objDefs.GetSize();

	const double tStart = WallClockTime();

	// Clear vectors
	verts.resize(0);
	normals.resize(0);
	uvs.resize(0);
	cols.resize(0);
	alphas.resize(0);
	tris.resize(0);
	meshDescs.resize(0);

	//--------------------------------------------------------------------------
	// Translate geometry
	//--------------------------------------------------------------------------

	// Not using boost::unordered_map because because the key is an ExtMesh pointer
	map<ExtMesh *, u_int, bool (*)(Mesh *, Mesh *)> definedMeshs(MeshPtrCompare);

	slg::ocl::Mesh newMeshDesc;
	newMeshDesc.vertsOffset = 0;
	newMeshDesc.trisOffset = 0;
	newMeshDesc.normalsOffset = 0;
	newMeshDesc.uvsOffset = 0;
	newMeshDesc.colsOffset = 0;
	newMeshDesc.alphasOffset = 0;
	memcpy(&newMeshDesc.trans.m, &Matrix4x4::MAT_IDENTITY, sizeof(float[4][4]));
	memcpy(&newMeshDesc.trans.mInv, &Matrix4x4::MAT_IDENTITY, sizeof(float[4][4]));

	slg::ocl::Mesh currentMeshDesc;
	for (u_int i = 0; i < objCount; ++i) {
		const ExtMesh *mesh = scene->objDefs.GetSceneObject(i)->GetExtMesh();

		bool isExistingInstance;
		if (mesh->GetType() == TYPE_EXT_TRIANGLE_INSTANCE) {
			// It is an instanced mesh
			ExtInstanceTriangleMesh *imesh = (ExtInstanceTriangleMesh *)mesh;

			// Check if is one of the already defined meshes
			map<ExtMesh *, u_int, bool (*)(Mesh *, Mesh *)>::iterator it = definedMeshs.find(imesh->GetExtTriangleMesh());
			if (it == definedMeshs.end()) {
				// It is a new one
				currentMeshDesc = newMeshDesc;

				newMeshDesc.vertsOffset += mesh->GetTotalVertexCount();
				newMeshDesc.trisOffset += mesh->GetTotalTriangleCount();
				if (mesh->HasNormals())
					newMeshDesc.normalsOffset += mesh->GetTotalVertexCount();
				if (mesh->HasUVs())
					newMeshDesc.uvsOffset += mesh->GetTotalVertexCount();
				if (mesh->HasColors())
					newMeshDesc.colsOffset += mesh->GetTotalVertexCount();
				if (mesh->HasAlphas())
					newMeshDesc.alphasOffset += mesh->GetTotalVertexCount();

				isExistingInstance = false;

				const u_int index = meshDescs.size();
				definedMeshs[imesh->GetExtTriangleMesh()] = index;
			} else {
				currentMeshDesc = meshDescs[it->second];

				isExistingInstance = true;
			}

			// Overwrite the only different fields in an instanced mesh
			memcpy(&currentMeshDesc.trans.m, &imesh->GetTransformation().m, sizeof(float[4][4]));
			memcpy(&currentMeshDesc.trans.mInv, &imesh->GetTransformation().mInv, sizeof(float[4][4]));

			// In order to express normals and vertices in local coordinates
			mesh = imesh->GetExtTriangleMesh();
		} else {
			// It is a not instanced mesh
			currentMeshDesc = newMeshDesc;

			newMeshDesc.vertsOffset += mesh->GetTotalVertexCount();
			newMeshDesc.trisOffset += mesh->GetTotalTriangleCount();
			if (mesh->HasNormals())
				newMeshDesc.normalsOffset += mesh->GetTotalVertexCount();
			if (mesh->HasUVs())
				newMeshDesc.uvsOffset += mesh->GetTotalVertexCount();
			if (mesh->HasColors())
				newMeshDesc.colsOffset += mesh->GetTotalVertexCount();
			if (mesh->HasAlphas())
				newMeshDesc.alphasOffset += mesh->GetTotalVertexCount();

			memcpy(&currentMeshDesc.trans.m, &Matrix4x4::MAT_IDENTITY, sizeof(float[4][4]));
			memcpy(&currentMeshDesc.trans.mInv, &Matrix4x4::MAT_IDENTITY, sizeof(float[4][4]));

			isExistingInstance = false;
		}

		if (!isExistingInstance) {
			//------------------------------------------------------------------
			// Translate mesh normals (expressed in local coordinates)
			//------------------------------------------------------------------

			if (mesh->HasNormals()) {
				for (u_int j = 0; j < mesh->GetTotalVertexCount(); ++j)
					normals.push_back(mesh->GetShadeNormal(j));
			} else
				currentMeshDesc.normalsOffset = NULL_INDEX;

			//------------------------------------------------------------------
			// Translate vertex uvs
			//------------------------------------------------------------------

			if (mesh->HasUVs()) {
				for (u_int j = 0; j < mesh->GetTotalVertexCount(); ++j)
					uvs.push_back(mesh->GetUV(j));
			} else
				currentMeshDesc.uvsOffset = NULL_INDEX;

			//------------------------------------------------------------------
			// Translate vertex colors
			//------------------------------------------------------------------

			if (mesh->HasColors()) {
				for (u_int j = 0; j < mesh->GetTotalVertexCount(); ++j)
					cols.push_back(mesh->GetColor(j));
			} else
				currentMeshDesc.colsOffset = NULL_INDEX;

			//------------------------------------------------------------------
			// Translate vertex alphas
			//------------------------------------------------------------------

			if (mesh->HasAlphas()) {
				for (u_int j = 0; j < mesh->GetTotalVertexCount(); ++j)
					alphas.push_back(mesh->GetAlpha(j));
			} else
				currentMeshDesc.alphasOffset = NULL_INDEX;

			//------------------------------------------------------------------
			// Translate mesh vertices (expressed in local coordinates)
			//------------------------------------------------------------------

			for (u_int j = 0; j < mesh->GetTotalVertexCount(); ++j)
				verts.push_back(mesh->GetVertex(j));

			//------------------------------------------------------------------
			// Translate mesh indices
			//------------------------------------------------------------------

			Triangle *mtris = mesh->GetTriangles();
			for (u_int j = 0; j < mesh->GetTotalTriangleCount(); ++j)
				tris.push_back(mtris[j]);
		}

		meshDescs.push_back(currentMeshDesc);
	}

	worldBSphere = scene->dataSet->GetBSphere();

	const double tEnd = WallClockTime();
	SLG_LOG("Scene geometry compilation time: " << int((tEnd - tStart) * 1000.0) << "ms");
}

static bool IsTexConstant(const Texture *tex) {
	return (dynamic_cast<const ConstFloatTexture *>(tex)) ||
			(dynamic_cast<const ConstFloat3Texture *>(tex));
}

static float GetTexConstantFloatValue(const Texture *tex) {
	// Check if a texture is constant and return the value
	const ConstFloatTexture *cft = dynamic_cast<const ConstFloatTexture *>(tex);
	if (cft)
		return cft->GetValue();
	const ConstFloat3Texture *cf3t = dynamic_cast<const ConstFloat3Texture *>(tex);
	if (cf3t)
		return cf3t->GetColor().Y();

	return numeric_limits<float>::infinity();
}

void CompiledScene::CompileMaterials() {
	CompileTextures();

	const u_int materialsCount = scene->matDefs.GetSize();
	SLG_LOG("Compile " << materialsCount << " Materials");

	//--------------------------------------------------------------------------
	// Translate material definitions
	//--------------------------------------------------------------------------

	const double tStart = WallClockTime();

	usedMaterialTypes.clear();

	mats.resize(materialsCount);
	useBumpMapping = false;

	for (u_int i = 0; i < materialsCount; ++i) {
		Material *m = scene->matDefs.GetMaterial(i);
		slg::ocl::Material *mat = &mats[i];
		//SLG_LOG(" Type: " << m->GetType());

		mat->matID = m->GetID();
		mat->lightID = m->GetLightID();
        mat->bumpSampleDistance = m->GetBumpSampleDistance();

		// Material emission
		const Texture *emitTex = m->GetEmitTexture();
		if (emitTex)
			mat->emitTexIndex = scene->texDefs.GetTextureIndex(emitTex);
		else
			mat->emitTexIndex = NULL_INDEX;
		ASSIGN_SPECTRUM(mat->emittedFactor, m->GetEmittedFactor());
		mat->usePrimitiveArea = m->IsUsingPrimitiveArea();

		// Material bump mapping
		const Texture *bumpTex = m->GetBumpTexture();
		if (bumpTex) {
			mat->bumpTexIndex = scene->texDefs.GetTextureIndex(bumpTex);
			useBumpMapping = true;
		} else
			mat->bumpTexIndex = NULL_INDEX;

		mat->samples = m->GetSamples();
		mat->visibility =
				(m->IsVisibleIndirectDiffuse() ? DIFFUSE : NONE) |
				(m->IsVisibleIndirectGlossy() ? GLOSSY : NONE) |
				(m->IsVisibleIndirectSpecular() ? SPECULAR : NONE);

		// Material volumes
		const Material *interiorVol = m->GetInteriorVolume();
		mat->interiorVolumeIndex = interiorVol ? scene->matDefs.GetMaterialIndex(interiorVol) : NULL_INDEX;
		const Material *exteriorVol = m->GetExteriorVolume();
		mat->exteriorVolumeIndex = exteriorVol ? scene->matDefs.GetMaterialIndex(exteriorVol) : NULL_INDEX;

		// Material specific parameters
		usedMaterialTypes.insert(m->GetType());
		switch (m->GetType()) {
			case MATTE: {
				const MatteMaterial *mm = static_cast<MatteMaterial *>(m);

				mat->type = slg::ocl::MATTE;
				mat->matte.kdTexIndex = scene->texDefs.GetTextureIndex(mm->GetKd());
				break;
			}
			case ROUGHMATTE: {
				const RoughMatteMaterial *mm = static_cast<RoughMatteMaterial *>(m);

				mat->type = slg::ocl::ROUGHMATTE;
				mat->roughmatte.kdTexIndex = scene->texDefs.GetTextureIndex(mm->GetKd());
				mat->roughmatte.sigmaTexIndex = scene->texDefs.GetTextureIndex(mm->GetSigma());
				break;
			}
			case MIRROR: {
				const MirrorMaterial *mm = static_cast<MirrorMaterial *>(m);

				mat->type = slg::ocl::MIRROR;
				mat->mirror.krTexIndex = scene->texDefs.GetTextureIndex(mm->GetKr());
				break;
			}
			case GLASS: {
				const GlassMaterial *gm = static_cast<GlassMaterial *>(m);

				mat->type = slg::ocl::GLASS;
				mat->glass.krTexIndex = scene->texDefs.GetTextureIndex(gm->GetKr());
				mat->glass.ktTexIndex = scene->texDefs.GetTextureIndex(gm->GetKt());
				if (gm->GetExteriorIOR())
					mat->glass.exteriorIorTexIndex = scene->texDefs.GetTextureIndex(gm->GetExteriorIOR());
				else
					throw runtime_error("Volume rendering is not yet supported by OpenCL code");
				if (gm->GetInteriorIOR())
					mat->glass.interiorIorTexIndex = scene->texDefs.GetTextureIndex(gm->GetInteriorIOR());
				else
					throw runtime_error("Volume rendering is not yet supported by OpenCL code");
				break;
			}
			case ARCHGLASS: {
				const ArchGlassMaterial *am = static_cast<ArchGlassMaterial *>(m);

				mat->type = slg::ocl::ARCHGLASS;
				mat->archglass.krTexIndex = scene->texDefs.GetTextureIndex(am->GetKr());
				mat->archglass.ktTexIndex = scene->texDefs.GetTextureIndex(am->GetKt());
				if (am->GetExteriorIOR())
					mat->glass.exteriorIorTexIndex = scene->texDefs.GetTextureIndex(am->GetExteriorIOR());
				else
					throw runtime_error("Volume rendering is not yet supported by OpenCL code");
				if (am->GetInteriorIOR())
					mat->glass.interiorIorTexIndex = scene->texDefs.GetTextureIndex(am->GetInteriorIOR());
				else
					throw runtime_error("Volume rendering is not yet supported by OpenCL code");
				break;
			}
			case MIX: {
				const MixMaterial *mm = static_cast<MixMaterial *>(m);

				mat->type = slg::ocl::MIX;
				mat->mix.matAIndex = scene->matDefs.GetMaterialIndex(mm->GetMaterialA());
				mat->mix.matBIndex = scene->matDefs.GetMaterialIndex(mm->GetMaterialB());
				mat->mix.mixFactorTexIndex = scene->texDefs.GetTextureIndex(mm->GetMixFactor());
				break;
			}
			case NULLMAT: {
				mat->type = slg::ocl::NULLMAT;
				break;
			}
			case MATTETRANSLUCENT: {
				const MatteTranslucentMaterial *mm = static_cast<MatteTranslucentMaterial *>(m);

				mat->type = slg::ocl::MATTETRANSLUCENT;
				mat->matteTranslucent.krTexIndex = scene->texDefs.GetTextureIndex(mm->GetKr());
				mat->matteTranslucent.ktTexIndex = scene->texDefs.GetTextureIndex(mm->GetKt());
				break;
			}
			case GLOSSY2: {
				const Glossy2Material *g2m = static_cast<Glossy2Material *>(m);

				mat->type = slg::ocl::GLOSSY2;
				mat->glossy2.kdTexIndex = scene->texDefs.GetTextureIndex(g2m->GetKd());
				mat->glossy2.ksTexIndex = scene->texDefs.GetTextureIndex(g2m->GetKs());

				const Texture *nuTex = g2m->GetNu();
				const Texture *nvTex = g2m->GetNv();
				mat->glossy2.nuTexIndex = scene->texDefs.GetTextureIndex(nuTex);
				mat->glossy2.nvTexIndex = scene->texDefs.GetTextureIndex(nvTex);
				// Check if it an anisotropic material
				if (IsTexConstant(nuTex) && IsTexConstant(nvTex) &&
						(GetTexConstantFloatValue(nuTex) != GetTexConstantFloatValue(nvTex)))
					usedMaterialTypes.insert(GLOSSY2_ANISOTROPIC);

				const Texture *depthTex = g2m->GetDepth();
				mat->glossy2.kaTexIndex = scene->texDefs.GetTextureIndex(g2m->GetKa());
				mat->glossy2.depthTexIndex = scene->texDefs.GetTextureIndex(depthTex);
				// Check if depth is just 0.0
				if (IsTexConstant(depthTex) && (GetTexConstantFloatValue(depthTex) > 0.f))
					usedMaterialTypes.insert(GLOSSY2_ABSORPTION);

				const Texture *indexTex = g2m->GetIndex();
				mat->glossy2.indexTexIndex = scene->texDefs.GetTextureIndex(indexTex);
				// Check if index is just 0.0
				if (IsTexConstant(depthTex) && (GetTexConstantFloatValue(indexTex) > 0.f))
					usedMaterialTypes.insert(GLOSSY2_INDEX);

				mat->glossy2.multibounce = g2m->IsMultibounce() ? 1 : 0;
				// Check if multibounce is enabled
				if (g2m->IsMultibounce())
					usedMaterialTypes.insert(GLOSSY2_MULTIBOUNCE);
				break;
			}
			case METAL2: {
				const Metal2Material *m2m = static_cast<Metal2Material *>(m);

				mat->type = slg::ocl::METAL2;
				mat->metal2.nTexIndex = scene->texDefs.GetTextureIndex(m2m->GetN());
				mat->metal2.kTexIndex = scene->texDefs.GetTextureIndex(m2m->GetK());

				const Texture *nuTex = m2m->GetNu();
				const Texture *nvTex = m2m->GetNv();
				mat->metal2.nuTexIndex = scene->texDefs.GetTextureIndex(nuTex);
				mat->metal2.nvTexIndex = scene->texDefs.GetTextureIndex(nvTex);
				// Check if it an anisotropic material
				if (IsTexConstant(nuTex) && IsTexConstant(nvTex) &&
						(GetTexConstantFloatValue(nuTex) != GetTexConstantFloatValue(nvTex)))
					usedMaterialTypes.insert(METAL2_ANISOTROPIC);
				break;
			}
			case ROUGHGLASS: {
				const RoughGlassMaterial *rgm = static_cast<RoughGlassMaterial *>(m);

				mat->type = slg::ocl::ROUGHGLASS;
				mat->roughglass.krTexIndex = scene->texDefs.GetTextureIndex(rgm->GetKr());
				mat->roughglass.ktTexIndex = scene->texDefs.GetTextureIndex(rgm->GetKt());
				if (rgm->GetExteriorIOR())
					mat->glass.exteriorIorTexIndex = scene->texDefs.GetTextureIndex(rgm->GetExteriorIOR());
				else
					throw runtime_error("Volume rendering is not yet supported by OpenCL code");
				if (rgm->GetInteriorIOR())
					mat->glass.interiorIorTexIndex = scene->texDefs.GetTextureIndex(rgm->GetInteriorIOR());
				else
					throw runtime_error("Volume rendering is not yet supported by OpenCL code");

				const Texture *nuTex = rgm->GetNu();
				const Texture *nvTex = rgm->GetNv();
				mat->roughglass.nuTexIndex = scene->texDefs.GetTextureIndex(nuTex);
				mat->roughglass.nvTexIndex = scene->texDefs.GetTextureIndex(nvTex);
				// Check if it an anisotropic material
				if (IsTexConstant(nuTex) && IsTexConstant(nvTex) &&
						(GetTexConstantFloatValue(nuTex) != GetTexConstantFloatValue(nvTex)))
					usedMaterialTypes.insert(ROUGHGLASS_ANISOTROPIC);
				break;
			}
			case VELVET: {
				const VelvetMaterial *vm = static_cast<VelvetMaterial *>(m);

				mat->type = slg::ocl::VELVET;
				mat->velvet.kdTexIndex = scene->texDefs.GetTextureIndex(vm->GetKd());
				mat->velvet.p1TexIndex = scene->texDefs.GetTextureIndex(vm->GetP1());
				mat->velvet.p2TexIndex = scene->texDefs.GetTextureIndex(vm->GetP2());
				mat->velvet.p3TexIndex = scene->texDefs.GetTextureIndex(vm->GetP3());
				mat->velvet.thicknessTexIndex = scene->texDefs.GetTextureIndex(vm->GetThickness());
				break;
			}
			case CLOTH: {
				const ClothMaterial *cm = static_cast<ClothMaterial *>(m);

				mat->type = slg::ocl::CLOTH;
				mat->cloth.Preset = cm->GetPreset();
				mat->cloth.Weft_KdIndex = scene->texDefs.GetTextureIndex(cm->GetWeftKd());
				mat->cloth.Weft_KsIndex = scene->texDefs.GetTextureIndex(cm->GetWeftKs());
				mat->cloth.Warp_KdIndex = scene->texDefs.GetTextureIndex(cm->GetWarpKd());
				mat->cloth.Warp_KsIndex = scene->texDefs.GetTextureIndex(cm->GetWarpKs());
				mat->cloth.Repeat_U = cm->GetRepeatU();
				mat->cloth.Repeat_V = cm->GetRepeatV();
				mat->cloth.specularNormalization = cm->GetSpecularNormalization();
				break;
			}
			case CARPAINT: {
				const CarPaintMaterial *cm = static_cast<CarPaintMaterial *>(m);
				mat->type = slg::ocl::CARPAINT;
				mat->carpaint.KdTexIndex = scene->texDefs.GetTextureIndex(cm->Kd);
				mat->carpaint.Ks1TexIndex = scene->texDefs.GetTextureIndex(cm->Ks1);
				mat->carpaint.Ks2TexIndex = scene->texDefs.GetTextureIndex(cm->Ks2);
				mat->carpaint.Ks3TexIndex = scene->texDefs.GetTextureIndex(cm->Ks3);
				mat->carpaint.M1TexIndex = scene->texDefs.GetTextureIndex(cm->M1);
				mat->carpaint.M2TexIndex = scene->texDefs.GetTextureIndex(cm->M2);
				mat->carpaint.M3TexIndex = scene->texDefs.GetTextureIndex(cm->M3);
				mat->carpaint.R1TexIndex = scene->texDefs.GetTextureIndex(cm->R1);
				mat->carpaint.R2TexIndex = scene->texDefs.GetTextureIndex(cm->R2);
				mat->carpaint.R3TexIndex = scene->texDefs.GetTextureIndex(cm->R3);
				mat->carpaint.KaTexIndex = scene->texDefs.GetTextureIndex(cm->Ka);
				mat->carpaint.depthTexIndex = scene->texDefs.GetTextureIndex(cm->depth);
				break;
			}
			//------------------------------------------------------------------
			// Volumes
			//------------------------------------------------------------------
			case CLEAR_VOL:
			case HOMOGENEOUS_VOL:
			case HETEROGENEOUS_VOL: {
				const Volume *v = static_cast<ClearVolume *>(m);
				mat->volume.iorTexIndex = v->GetIORTexture() ?
					scene->texDefs.GetTextureIndex(v->GetIORTexture()) :
					NULL_INDEX;
				mat->volume.volumeEmissionTexIndex = v->GetVolumeEmissionTexture() ?
					scene->texDefs.GetTextureIndex(v->GetVolumeEmissionTexture()) :
					NULL_INDEX;
				mat->volume.volumeLightID = v->GetVolumeLightID();
				mat->volume.priority = v->GetPriority();
	
				switch (m->GetType()) {
					case CLEAR_VOL: {
						const ClearVolume *cv = static_cast<ClearVolume *>(m);
						mat->type = slg::ocl::CLEAR_VOL;
						mat->volume.clear.sigmaATexIndex = scene->texDefs.GetTextureIndex(cv->GetSigmaA());
						break;
					}
					default:
						throw runtime_error("Unknown volume in CompiledScene::CompileMaterials(): " + boost::lexical_cast<string>(m->GetType()));
				}
				break;
			}
			default:
				throw runtime_error("Unknown material in CompiledScene::CompileMaterials(): " + boost::lexical_cast<string>(m->GetType()));
		}
	}

	//--------------------------------------------------------------------------
	// Translate mesh material indices
	//--------------------------------------------------------------------------

	const u_int objCount = scene->objDefs.GetSize();
	meshMats.resize(objCount);
	for (u_int i = 0; i < objCount; ++i) {
		const Material *m = scene->objDefs.GetSceneObject(i)->GetMaterial();
		meshMats[i] = scene->matDefs.GetMaterialIndex(m);
	}

	//--------------------------------------------------------------------------
	// Default scene volume
	//--------------------------------------------------------------------------
	defaultWorldVolumeIndex = scene->defaultWorldVolume ?
		scene->matDefs.GetMaterialIndex(scene->defaultWorldVolume) : NULL_INDEX;

	const double tEnd = WallClockTime();
	SLG_LOG("Material compilation time: " << int((tEnd - tStart) * 1000.0) << "ms");
}

void CompiledScene::CompileLights() {
	SLG_LOG("Compile Lights");

	//--------------------------------------------------------------------------
	// Translate lights
	//--------------------------------------------------------------------------

	const double tStart = WallClockTime();

	const std::vector<LightSource *> &lightSources = scene->lightDefs.GetLightSources();

	const u_int lightCount = lightSources.size();
	lightDefs.resize(lightCount);
	envLightIndices.clear();
	infiniteLightDistributions.clear();
	hasInfiniteLights = false;
	hasEnvLights = false;

	for (u_int i = 0; i < lightSources.size(); ++i) {
		const LightSource *l = lightSources[i];
		slg::ocl::LightSource *oclLight = &lightDefs[i];
		oclLight->lightSceneIndex = l->lightSceneIndex;
		oclLight->lightID = l->GetID();
		oclLight->samples = l->GetSamples();
		oclLight->visibility = 
				(l->IsVisibleIndirectDiffuse() ? DIFFUSE : NONE) |
				(l->IsVisibleIndirectGlossy() ? GLOSSY : NONE) |
				(l->IsVisibleIndirectSpecular() ? SPECULAR : NONE);

		hasInfiniteLights |= l->IsInfinite();
		hasEnvLights |= l->IsEnvironmental();
			
		switch (l->GetType()) {
			case TYPE_TRIANGLE: {
				const TriangleLight *tl = (const TriangleLight *)l;

				const ExtMesh *mesh = tl->mesh;
				const Triangle *tri = &(mesh->GetTriangles()[tl->triangleIndex]);

				// LightSource data
				oclLight->type = slg::ocl::TYPE_TRIANGLE;

				// TriangleLight data
				ASSIGN_VECTOR(oclLight->triangle.v0, mesh->GetVertex(tri->v[0]));
				ASSIGN_VECTOR(oclLight->triangle.v1, mesh->GetVertex(tri->v[1]));
				ASSIGN_VECTOR(oclLight->triangle.v2, mesh->GetVertex(tri->v[2]));
				if (mesh->HasUVs()) {
					ASSIGN_UV(oclLight->triangle.uv0, mesh->GetUV(tri->v[0]));
					ASSIGN_UV(oclLight->triangle.uv1, mesh->GetUV(tri->v[1]));
					ASSIGN_UV(oclLight->triangle.uv2, mesh->GetUV(tri->v[2]));
				} else {
					const UV zero;
					ASSIGN_UV(oclLight->triangle.uv0, zero);
					ASSIGN_UV(oclLight->triangle.uv1, zero);
					ASSIGN_UV(oclLight->triangle.uv2, zero);
				}
				oclLight->triangle.invArea = 1.f / tl->GetArea();

				oclLight->triangle.materialIndex = scene->matDefs.GetMaterialIndex(tl->lightMaterial);

				const SampleableSphericalFunction *emissionFunc = tl->lightMaterial->GetEmissionFunc();
				if (emissionFunc) {
					oclLight->triangle.avarage = emissionFunc->Average();
					oclLight->triangle.imageMapIndex = scene->imgMapCache.GetImageMapIndex(
							// I use only ImageMapSphericalFunction
							((const ImageMapSphericalFunction *)(emissionFunc->GetFunc()))->GetImageMap());
				} else {
					oclLight->triangle.avarage = 0.f;
					oclLight->triangle.imageMapIndex = NULL_INDEX;
				}
				break;
			}
			case TYPE_IL: {
				const InfiniteLight *il = (const InfiniteLight *)l;

				// LightSource data
				oclLight->type = slg::ocl::TYPE_IL;

				// NotIntersectableLightSource data
				memcpy(&oclLight->notIntersectable.light2World.m, &il->lightToWorld.m, sizeof(float[4][4]));
				memcpy(&oclLight->notIntersectable.light2World.mInv, &il->lightToWorld.mInv, sizeof(float[4][4]));
				ASSIGN_SPECTRUM(oclLight->notIntersectable.gain, il->gain);

				// InfiniteLight data
				CompileTextureMapping2D(&oclLight->notIntersectable.infinite.mapping, &il->mapping);
				oclLight->notIntersectable.infinite.imageMapIndex = scene->imgMapCache.GetImageMapIndex(il->imageMap);

				// Compile the image map Distribution2D
				const Distribution2D *dist;
				il->GetPreprocessedData(&dist);
				u_int distributionSize;
				const float *infiniteLightDistribution = CompileDistribution2D(dist,
						&distributionSize);

				// Copy the Distribution2D data in the right place
				const u_int size = infiniteLightDistributions.size();
				infiniteLightDistributions.resize(size + distributionSize);
				copy(infiniteLightDistribution, infiniteLightDistribution + distributionSize,
						&infiniteLightDistributions[size]);
				delete[] infiniteLightDistribution;

				oclLight->notIntersectable.infinite.distributionOffset = size;
				break;
			}
			case TYPE_IL_SKY: {
				const SkyLight *sl = (const SkyLight *)l;

				// LightSource data
				oclLight->type = slg::ocl::TYPE_IL_SKY;

				// NotIntersectableLightSource data
				memcpy(&oclLight->notIntersectable.light2World.m, &sl->lightToWorld.m, sizeof(float[4][4]));
				memcpy(&oclLight->notIntersectable.light2World.mInv, &sl->lightToWorld.mInv, sizeof(float[4][4]));
				ASSIGN_SPECTRUM(oclLight->notIntersectable.gain, sl->gain);

				// SkyLight data
				sl->GetPreprocessedData(
						NULL,
						&oclLight->notIntersectable.sky.absoluteTheta, &oclLight->notIntersectable.sky.absolutePhi,
						&oclLight->notIntersectable.sky.zenith_Y, &oclLight->notIntersectable.sky.zenith_x,
						&oclLight->notIntersectable.sky.zenith_y, oclLight->notIntersectable.sky.perez_Y,
						oclLight->notIntersectable.sky.perez_x, oclLight->notIntersectable.sky.perez_y);
				break;
			}
			case TYPE_IL_SKY2: {
				const SkyLight2 *sl = (const SkyLight2 *)l;

				// LightSource data
				oclLight->type = slg::ocl::TYPE_IL_SKY2;

				// NotIntersectableLightSource data
				memcpy(&oclLight->notIntersectable.light2World.m, &sl->lightToWorld.m, sizeof(float[4][4]));
				memcpy(&oclLight->notIntersectable.light2World.mInv, &sl->lightToWorld.mInv, sizeof(float[4][4]));
				ASSIGN_SPECTRUM(oclLight->notIntersectable.gain, sl->gain);

				// SkyLight2 data
				sl->GetPreprocessedData(
						&oclLight->notIntersectable.sky2.absoluteSunDir.x,
						oclLight->notIntersectable.sky2.aTerm.c,
						oclLight->notIntersectable.sky2.bTerm.c,
						oclLight->notIntersectable.sky2.cTerm.c,
						oclLight->notIntersectable.sky2.dTerm.c,
						oclLight->notIntersectable.sky2.eTerm.c,
						oclLight->notIntersectable.sky2.fTerm.c,
						oclLight->notIntersectable.sky2.gTerm.c,
						oclLight->notIntersectable.sky2.hTerm.c,
						oclLight->notIntersectable.sky2.iTerm.c,
						oclLight->notIntersectable.sky2.radianceTerm.c);
				break;
			}
			case TYPE_SUN: {
				const SunLight *sl = (const SunLight *)l;

				// LightSource data
				oclLight->type = slg::ocl::TYPE_SUN;

				// NotIntersectableLightSource data
				memcpy(&oclLight->notIntersectable.light2World.m, &sl->lightToWorld.m, sizeof(float[4][4]));
				memcpy(&oclLight->notIntersectable.light2World.mInv, &sl->lightToWorld.mInv, sizeof(float[4][4]));
				ASSIGN_SPECTRUM(oclLight->notIntersectable.gain, sl->gain);

				// SunLight data
				sl->GetPreprocessedData(
						&oclLight->notIntersectable.sun.absoluteDir.x,
						&oclLight->notIntersectable.sun.x.x,
						&oclLight->notIntersectable.sun.y.x,
						NULL, NULL, NULL,
						&oclLight->notIntersectable.sun.cosThetaMax, &oclLight->notIntersectable.sun.sin2ThetaMax);
				ASSIGN_SPECTRUM(oclLight->notIntersectable.sun.color, sl->color);
				oclLight->notIntersectable.sun.turbidity = sl->turbidity;
				oclLight->notIntersectable.sun.relSize= sl->relSize;
				break;
			}
			case TYPE_POINT: {
				const PointLight *pl = (const PointLight *)l;

				// LightSource data
				oclLight->type = slg::ocl::TYPE_POINT;

				// NotIntersectableLightSource data
				memcpy(&oclLight->notIntersectable.light2World.m, &pl->lightToWorld.m, sizeof(float[4][4]));
				memcpy(&oclLight->notIntersectable.light2World.mInv, &pl->lightToWorld.mInv, sizeof(float[4][4]));
				ASSIGN_SPECTRUM(oclLight->notIntersectable.gain, pl->gain);

				// PointLight data
				pl->GetPreprocessedData(
					NULL,
					&oclLight->notIntersectable.point.absolutePos.x,
					oclLight->notIntersectable.point.emittedFactor.c);
				break;
			}
			case TYPE_MAPPOINT: {
				const MapPointLight *mpl = (const MapPointLight *)l;

				// LightSource data
				oclLight->type = slg::ocl::TYPE_MAPPOINT;

				// NotIntersectableLightSource data
				memcpy(&oclLight->notIntersectable.light2World.m, &mpl->lightToWorld.m, sizeof(float[4][4]));
				memcpy(&oclLight->notIntersectable.light2World.mInv, &mpl->lightToWorld.mInv, sizeof(float[4][4]));
				ASSIGN_SPECTRUM(oclLight->notIntersectable.gain, mpl->gain);

				// MapPointLight data
				const SampleableSphericalFunction *funcData;
				mpl->GetPreprocessedData(
					&oclLight->notIntersectable.mapPoint.localPos.x,
					&oclLight->notIntersectable.mapPoint.absolutePos.x,
					oclLight->notIntersectable.mapPoint.emittedFactor.c,
					&funcData);
				oclLight->notIntersectable.mapPoint.avarage = funcData->Average();
				oclLight->notIntersectable.mapPoint.imageMapIndex = scene->imgMapCache.GetImageMapIndex(mpl->imageMap);
				break;
			}
			case TYPE_SPOT: {
				const SpotLight *sl = (const SpotLight *)l;

				// LightSource data
				oclLight->type = slg::ocl::TYPE_SPOT;

				// NotIntersectableLightSource data
				//memcpy(&oclLight->notIntersectable.light2World.m, &sl->lightToWorld.m, sizeof(float[4][4]));
				//memcpy(&oclLight->notIntersectable.light2World.mInv, &sl->lightToWorld.mInv, sizeof(float[4][4]));
				ASSIGN_SPECTRUM(oclLight->notIntersectable.gain, sl->gain);

				// SpotLight data
				const Transform *alignedWorld2Light;
				sl->GetPreprocessedData(
					oclLight->notIntersectable.spot.emittedFactor.c,
					&oclLight->notIntersectable.spot.absolutePos.x,
					&oclLight->notIntersectable.spot.cosTotalWidth,
					&oclLight->notIntersectable.spot.cosFalloffStart,
					&alignedWorld2Light);
				
				memcpy(&oclLight->notIntersectable.light2World.m, &alignedWorld2Light->m, sizeof(float[4][4]));
				memcpy(&oclLight->notIntersectable.light2World.mInv, &alignedWorld2Light->mInv, sizeof(float[4][4]));
				break;
			}
			case TYPE_PROJECTION: {
				const ProjectionLight *pl = (const ProjectionLight *)l;

				// LightSource data
				oclLight->type = slg::ocl::TYPE_PROJECTION;

				// NotIntersectableLightSource data
				//memcpy(&oclLight->notIntersectable.light2World.m, &pl->lightToWorld.m, sizeof(float[4][4]));
				//memcpy(&oclLight->notIntersectable.light2World.mInv, &pl->lightToWorld.mInv, sizeof(float[4][4]));
				ASSIGN_SPECTRUM(oclLight->notIntersectable.gain, pl->gain);

				// ProjectionLight data
				oclLight->notIntersectable.projection.imageMapIndex = scene->imgMapCache.GetImageMapIndex(pl->imageMap);
				const Transform *alignedWorld2Light, *lightProjection;
				pl->GetPreprocessedData(
					oclLight->notIntersectable.projection.emittedFactor.c,
					&(oclLight->notIntersectable.projection.absolutePos.x),
					&(oclLight->notIntersectable.projection.lightNormal.x),
					&oclLight->notIntersectable.projection.screenX0,
					&oclLight->notIntersectable.projection.screenX1,
					&oclLight->notIntersectable.projection.screenY0,
					&oclLight->notIntersectable.projection.screenY1,
					&alignedWorld2Light, &lightProjection);

				memcpy(&oclLight->notIntersectable.light2World.m, &alignedWorld2Light->m, sizeof(float[4][4]));
				memcpy(&oclLight->notIntersectable.light2World.mInv, &alignedWorld2Light->mInv, sizeof(float[4][4]));

				memcpy(&oclLight->notIntersectable.projection.lightProjection.m, &lightProjection->m, sizeof(float[4][4]));
				break;
			}
			case TYPE_IL_CONSTANT: {
				const ConstantInfiniteLight *cil = (const ConstantInfiniteLight *)l;

				// LightSource data
				oclLight->type = slg::ocl::TYPE_IL_CONSTANT;

				// NotIntersectableLightSource data
				memcpy(&oclLight->notIntersectable.light2World.m, &cil->lightToWorld.m, sizeof(float[4][4]));
				memcpy(&oclLight->notIntersectable.light2World.mInv, &cil->lightToWorld.mInv, sizeof(float[4][4]));
				ASSIGN_SPECTRUM(oclLight->notIntersectable.gain, cil->gain);

				// ConstantInfiniteLight data
				ASSIGN_SPECTRUM(oclLight->notIntersectable.constantInfinite.color, cil->color);
				break;
			}
		case TYPE_SHARPDISTANT: {
				const SharpDistantLight *sdl = (const SharpDistantLight *)l;

				// LightSource data
				oclLight->type = slg::ocl::TYPE_SHARPDISTANT;

				// NotIntersectableLightSource data
				memcpy(&oclLight->notIntersectable.light2World.m, &sdl->lightToWorld.m, sizeof(float[4][4]));
				memcpy(&oclLight->notIntersectable.light2World.mInv, &sdl->lightToWorld.mInv, sizeof(float[4][4]));
				ASSIGN_SPECTRUM(oclLight->notIntersectable.gain, sdl->gain);

				// SharpDistantLight data
				ASSIGN_SPECTRUM(oclLight->notIntersectable.sharpDistant.color, sdl->color);
				sdl->GetPreprocessedData(&(oclLight->notIntersectable.sharpDistant.absoluteLightDir.x), NULL, NULL);
				break;
			}
			case TYPE_DISTANT: {
				const DistantLight *dl = (const DistantLight *)l;

				// LightSource data
				oclLight->type = slg::ocl::TYPE_DISTANT;

				// NotIntersectableLightSource data
				memcpy(&oclLight->notIntersectable.light2World.m, &dl->lightToWorld.m, sizeof(float[4][4]));
				memcpy(&oclLight->notIntersectable.light2World.mInv, &dl->lightToWorld.mInv, sizeof(float[4][4]));
				ASSIGN_SPECTRUM(oclLight->notIntersectable.gain, dl->gain);

				// DistantLight data
				ASSIGN_SPECTRUM(oclLight->notIntersectable.sharpDistant.color, dl->color);
				dl->GetPreprocessedData(
					&(oclLight->notIntersectable.distant.absoluteLightDir.x),
					&(oclLight->notIntersectable.distant.x.x),
					&(oclLight->notIntersectable.distant.y.x),
					NULL,
					&(oclLight->notIntersectable.distant.cosThetaMax));
				break;
			}
			case TYPE_LASER: {
				const LaserLight *ll = (const LaserLight *)l;

				// LightSource data
				oclLight->type = slg::ocl::TYPE_LASER;

				// NotIntersectableLightSource data
				memcpy(&oclLight->notIntersectable.light2World.m, &ll->lightToWorld.m, sizeof(float[4][4]));
				memcpy(&oclLight->notIntersectable.light2World.mInv, &ll->lightToWorld.mInv, sizeof(float[4][4]));
				ASSIGN_SPECTRUM(oclLight->notIntersectable.gain, ll->gain);

				// LaserLight data
				ll->GetPreprocessedData(
					oclLight->notIntersectable.laser.emittedFactor.c,
					&oclLight->notIntersectable.laser.absoluteLightPos.x,
					&oclLight->notIntersectable.laser.absoluteLightDir.x,
					NULL, NULL);
				oclLight->notIntersectable.laser.radius = ll->radius;
				break;
			}
			default:
				throw runtime_error("Unknown Light source type in CompiledScene::CompileLights()");
		}
		
		if (l->IsEnvironmental())
			envLightIndices.push_back(i);
	}

	lightTypeCounts = scene->lightDefs.GetLightTypeCounts();
	meshTriLightDefsOffset = scene->lightDefs.GetLightIndexByMeshIndex();

	// Compile LightDistribution
	delete[] lightsDistribution;
	lightsDistribution = CompileDistribution1D(scene->lightDefs.GetLightsDistribution(), &lightsDistributionSize);

	const double tEnd = WallClockTime();
	SLG_LOG("Lights compilation time: " << int((tEnd - tStart) * 1000.0) << "ms");
}

void CompiledScene::CompileTextureMapping2D(slg::ocl::TextureMapping2D *mapping, const TextureMapping2D *m) {
	switch (m->GetType()) {
		case UVMAPPING2D: {
			mapping->type = slg::ocl::UVMAPPING2D;
			const UVMapping2D *uvm = static_cast<const UVMapping2D *>(m);
			mapping->uvMapping2D.uScale = uvm->uScale;
			mapping->uvMapping2D.vScale = uvm->vScale;
			mapping->uvMapping2D.uDelta = uvm->uDelta;
			mapping->uvMapping2D.vDelta = uvm->vDelta;
			break;
		}
		default:
			throw runtime_error("Unknown 2D texture mapping in CompiledScene::CompileTextureMapping2D: " + boost::lexical_cast<string>(m->GetType()));
	}
}

void CompiledScene::CompileTextureMapping3D(slg::ocl::TextureMapping3D *mapping, const TextureMapping3D *m) {
	switch (m->GetType()) {
		case UVMAPPING3D: {
			mapping->type = slg::ocl::UVMAPPING3D;
			const UVMapping3D *uvm = static_cast<const UVMapping3D *>(m);
			memcpy(&mapping->worldToLocal.m, &uvm->worldToLocal.m, sizeof(float[4][4]));
			memcpy(&mapping->worldToLocal.mInv, &uvm->worldToLocal.mInv, sizeof(float[4][4]));
			break;
		}
		case GLOBALMAPPING3D: {
			mapping->type = slg::ocl::GLOBALMAPPING3D;
			const GlobalMapping3D *gm = static_cast<const GlobalMapping3D *>(m);
			memcpy(&mapping->worldToLocal.m, &gm->worldToLocal.m, sizeof(float[4][4]));
			memcpy(&mapping->worldToLocal.mInv, &gm->worldToLocal.mInv, sizeof(float[4][4]));
			break;
		}
		default:
			throw runtime_error("Unknown 3D texture mapping in CompiledScene::CompileTextureMapping3D: " + boost::lexical_cast<string>(m->GetType()));
	}
}

float *CompiledScene::CompileDistribution1D(const Distribution1D *dist, u_int *size) {
	const u_int count = dist->GetCount();
	*size = sizeof(u_int) + count * sizeof(float) + (count + 1) * sizeof(float);
	float *compDist = new float[*size];

	*((u_int *)&compDist[0]) = count;
	copy(dist->GetFuncs(), dist->GetFuncs() + count,
			compDist + 1);
	copy(dist->GetCDFs(), dist->GetCDFs() + count + 1,
			compDist + 1 + count);

	return compDist;
}

float *CompiledScene::CompileDistribution2D(const Distribution2D *dist, u_int *size) {
	u_int marginalSize;
	float *marginalDist = CompileDistribution1D(dist->GetMarginalDistribution(),
			&marginalSize);

	u_int condSize;
	vector<float *> condDists;
	for (u_int i = 0; i < dist->GetHeight(); ++i) {
		condDists.push_back(CompileDistribution1D(dist->GetConditionalDistribution(i),
			&condSize));
	}

	*size = 2 * sizeof(u_int) + marginalSize + condDists.size() * condSize;
	float *compDist = new float[*size];

	*((u_int *)&compDist[0]) = dist->GetWidth();
	*((u_int *)&compDist[1]) = dist->GetHeight();

	float *ptr = &compDist[2];
	copy(marginalDist, marginalDist + marginalSize, ptr);
	ptr += marginalSize / 4;
	delete[] marginalDist;

	const u_int condSize4 = condSize / sizeof(float);
	for (u_int i = 0; i < dist->GetHeight(); ++i) {
		copy(condDists[i], condDists[i] + condSize4, ptr);
		ptr += condSize4;
		delete[] condDists[i];
	}

	return compDist;
}

void CompiledScene::CompileTextures() {
	const u_int texturesCount = scene->texDefs.GetSize();
	SLG_LOG("Compile " << texturesCount << " Textures");
	//SLG_LOG("  Texture size: " << sizeof(slg::ocl::Texture));

	//--------------------------------------------------------------------------
	// Translate textures
	//--------------------------------------------------------------------------

	const double tStart = WallClockTime();

	usedTextureTypes.clear();
	texs.resize(texturesCount);

	for (u_int i = 0; i < texturesCount; ++i) {
		Texture *t = scene->texDefs.GetTexture(i);
		slg::ocl::Texture *tex = &texs[i];

		usedTextureTypes.insert(t->GetType());
		switch (t->GetType()) {
			case CONST_FLOAT: {
				ConstFloatTexture *cft = static_cast<ConstFloatTexture *>(t);

				tex->type = slg::ocl::CONST_FLOAT;
				tex->constFloat.value = cft->GetValue();
				break;
			}
			case CONST_FLOAT3: {
				ConstFloat3Texture *cft = static_cast<ConstFloat3Texture *>(t);

				tex->type = slg::ocl::CONST_FLOAT3;
				ASSIGN_SPECTRUM(tex->constFloat3.color, cft->GetColor());
				break;
			}
			case IMAGEMAP: {
				ImageMapTexture *imt = static_cast<ImageMapTexture *>(t);

				tex->type = slg::ocl::IMAGEMAP;
				const ImageMap *im = imt->GetImageMap();
				tex->imageMapTex.gain = imt->GetGain();
				CompileTextureMapping2D(&tex->imageMapTex.mapping, imt->GetTextureMapping());
				tex->imageMapTex.imageMapIndex = scene->imgMapCache.GetImageMapIndex(im);
				break;
			}
			case SCALE_TEX: {
				ScaleTexture *st = static_cast<ScaleTexture *>(t);

				tex->type = slg::ocl::SCALE_TEX;
				const Texture *tex1 = st->GetTexture1();
				tex->scaleTex.tex1Index = scene->texDefs.GetTextureIndex(tex1);

				const Texture *tex2 = st->GetTexture2();
				tex->scaleTex.tex2Index = scene->texDefs.GetTextureIndex(tex2);
				break;
			}
			case FRESNEL_APPROX_N: {
				FresnelApproxNTexture *ft = static_cast<FresnelApproxNTexture *>(t);

				tex->type = slg::ocl::FRESNEL_APPROX_N;
				const Texture *tx = ft->GetTexture();
				tex->fresnelApproxN.texIndex = scene->texDefs.GetTextureIndex(tx);
				break;
			}
			case FRESNEL_APPROX_K: {
				FresnelApproxKTexture *ft = static_cast<FresnelApproxKTexture *>(t);

				tex->type = slg::ocl::FRESNEL_APPROX_K;
				const Texture *tx = ft->GetTexture();
				tex->fresnelApproxK.texIndex = scene->texDefs.GetTextureIndex(tx);
				break;
			}
			case CHECKERBOARD2D: {
				CheckerBoard2DTexture *cb = static_cast<CheckerBoard2DTexture *>(t);

				tex->type = slg::ocl::CHECKERBOARD2D;
				CompileTextureMapping2D(&tex->checkerBoard2D.mapping, cb->GetTextureMapping());
				const Texture *tex1 = cb->GetTexture1();
				tex->checkerBoard2D.tex1Index = scene->texDefs.GetTextureIndex(tex1);

				const Texture *tex2 = cb->GetTexture2();
				tex->checkerBoard2D.tex2Index = scene->texDefs.GetTextureIndex(tex2);
				break;
			}
			case CHECKERBOARD3D: {
				CheckerBoard3DTexture *cb = static_cast<CheckerBoard3DTexture *>(t);

				tex->type = slg::ocl::CHECKERBOARD3D;
				CompileTextureMapping3D(&tex->checkerBoard3D.mapping, cb->GetTextureMapping());
				const Texture *tex1 = cb->GetTexture1();
				tex->checkerBoard3D.tex1Index = scene->texDefs.GetTextureIndex(tex1);

				const Texture *tex2 = cb->GetTexture2();
				tex->checkerBoard3D.tex2Index = scene->texDefs.GetTextureIndex(tex2);
				break;
			}
			case MIX_TEX: {
				MixTexture *mt = static_cast<MixTexture *>(t);

				tex->type = slg::ocl::MIX_TEX;
				const Texture *amount = mt->GetAmountTexture();
				tex->mixTex.amountTexIndex = scene->texDefs.GetTextureIndex(amount);

				const Texture *tex1 = mt->GetTexture1();
				tex->mixTex.tex1Index = scene->texDefs.GetTextureIndex(tex1);
				const Texture *tex2 = mt->GetTexture2();
				tex->mixTex.tex2Index = scene->texDefs.GetTextureIndex(tex2);
				break;
			}
			case FBM_TEX: {
				FBMTexture *ft = static_cast<FBMTexture *>(t);

				tex->type = slg::ocl::FBM_TEX;
				CompileTextureMapping3D(&tex->fbm.mapping, ft->GetTextureMapping());
				tex->fbm.octaves = ft->GetOctaves();
				tex->fbm.omega = ft->GetOmega();
				break;
			}
			case MARBLE: {
				MarbleTexture *mt = static_cast<MarbleTexture *>(t);

				tex->type = slg::ocl::MARBLE;
				CompileTextureMapping3D(&tex->marble.mapping, mt->GetTextureMapping());
				tex->marble.octaves = mt->GetOctaves();
				tex->marble.omega = mt->GetOmega();
				tex->marble.scale = mt->GetScale();
				tex->marble.variation = mt->GetVariation();
				break;
			}
			case DOTS: {
				DotsTexture *dt = static_cast<DotsTexture *>(t);

				tex->type = slg::ocl::DOTS;
				CompileTextureMapping2D(&tex->dots.mapping, dt->GetTextureMapping());
				const Texture *insideTex = dt->GetInsideTex();
				tex->dots.insideIndex = scene->texDefs.GetTextureIndex(insideTex);

				const Texture *outsideTex = dt->GetOutsideTex();
				tex->dots.outsideIndex = scene->texDefs.GetTextureIndex(outsideTex);
				break;
			}
			case BRICK: {
				BrickTexture *bt = static_cast<BrickTexture *>(t);

				tex->type = slg::ocl::BRICK;
				CompileTextureMapping3D(&tex->brick.mapping, bt->GetTextureMapping());
				const Texture *tex1 = bt->GetTexture1();
				tex->brick.tex1Index = scene->texDefs.GetTextureIndex(tex1);
				const Texture *tex2 = bt->GetTexture2();
				tex->brick.tex2Index = scene->texDefs.GetTextureIndex(tex2);
				const Texture *tex3 = bt->GetTexture3();
				tex->brick.tex3Index = scene->texDefs.GetTextureIndex(tex3);

				switch (bt->GetBond()) {
					case FLEMISH:
						tex->brick.bond = slg::ocl::FLEMISH;
						break;
					default:
					case RUNNING:
						tex->brick.bond = slg::ocl::RUNNING;
						break;
					case ENGLISH:
						tex->brick.bond = slg::ocl::ENGLISH;
						break;
					case HERRINGBONE:
						tex->brick.bond = slg::ocl::HERRINGBONE;
						break;
					case BASKET:
						tex->brick.bond = slg::ocl::BASKET;
						break;
					case KETTING:
						tex->brick.bond = slg::ocl::KETTING;
						break; 
				}

				tex->brick.offsetx = bt->GetOffset().x;
				tex->brick.offsety = bt->GetOffset().y;
				tex->brick.offsetz = bt->GetOffset().z;
				tex->brick.brickwidth = bt->GetBrickWidth();
				tex->brick.brickheight = bt->GetBrickHeight();
				tex->brick.brickdepth = bt->GetBrickDepth();
				tex->brick.mortarsize = bt->GetMortarSize();
				tex->brick.proportion = bt->GetProportion();
				tex->brick.invproportion = bt->GetInvProportion();
				tex->brick.run = bt->GetRun();
				tex->brick.mortarwidth = bt->GetMortarWidth();
				tex->brick.mortarheight = bt->GetMortarHeight();
				tex->brick.mortardepth = bt->GetMortarDepth();
				tex->brick.bevelwidth = bt->GetBevelWidth();
				tex->brick.bevelheight = bt->GetBevelHeight();
				tex->brick.beveldepth = bt->GetBevelDepth();
				tex->brick.usebevel = bt->GetUseBevel();
				break;
			}
			case ADD_TEX: {
				ScaleTexture *st = static_cast<ScaleTexture *>(t);

				tex->type = slg::ocl::ADD_TEX;
				const Texture *tex1 = st->GetTexture1();
				tex->addTex.tex1Index = scene->texDefs.GetTextureIndex(tex1);

				const Texture *tex2 = st->GetTexture2();
				tex->addTex.tex2Index = scene->texDefs.GetTextureIndex(tex2);
				break;
			}
			case WINDY: {
				WindyTexture *wt = static_cast<WindyTexture *>(t);

				tex->type = slg::ocl::WINDY;
				CompileTextureMapping3D(&tex->windy.mapping, wt->GetTextureMapping());
				break;
			}
			case WRINKLED: {
				WrinkledTexture *wt = static_cast<WrinkledTexture *>(t);

				tex->type = slg::ocl::WRINKLED;
				CompileTextureMapping3D(&tex->wrinkled.mapping, wt->GetTextureMapping());
				tex->wrinkled.octaves = wt->GetOctaves();
				tex->wrinkled.omega = wt->GetOmega();
				break;
			}
			case BLENDER_CLOUDS: {
				BlenderCloudsTexture *wt = static_cast<BlenderCloudsTexture *>(t);
				tex->type = slg::ocl::BLENDER_CLOUDS;
				CompileTextureMapping3D(&tex->blenderClouds.mapping, wt->GetTextureMapping());				 
				tex->blenderClouds.noisesize = wt->GetNoiseSize();
				tex->blenderClouds.noisedepth = wt->GetNoiseDepth();
				tex->blenderClouds.bright = wt->GetBright();
				tex->blenderClouds.contrast = wt->GetContrast();
				tex->blenderClouds.hard = wt->GetNoiseType();

				break;
			}
			case BLENDER_WOOD: {
				BlenderWoodTexture *wt = static_cast<BlenderWoodTexture *>(t);

				tex->type = slg::ocl::BLENDER_WOOD;
				CompileTextureMapping3D(&tex->blenderWood.mapping, wt->GetTextureMapping());				 
				tex->blenderWood.turbulence = wt->GetTurbulence();
				tex->blenderWood.bright = wt->GetBright();
				tex->blenderWood.contrast = wt->GetContrast();
				tex->blenderWood.hard = wt->GetNoiseType();
				tex->blenderWood.noisesize = wt->GetNoiseSize();
				switch (wt->GetNoiseBasis2()) {
					default:
					case TEX_SIN:
						tex->blenderWood.noisebasis2 = slg::ocl::TEX_SIN;
						break;
					case TEX_SAW:
						tex->blenderWood.noisebasis2 = slg::ocl::TEX_SAW;
						break;
					case TEX_TRI:
						tex->blenderWood.noisebasis2 = slg::ocl::TEX_TRI;
						break;
				}
				
				switch (wt->GetWoodType()) {
					default:
					case BANDS:
						tex->blenderWood.type = slg::ocl::BANDS;
						break;
					case RINGS:
						tex->blenderWood.type = slg::ocl::RINGS;
						break;
					case BANDNOISE:
						tex->blenderWood.type = slg::ocl::BANDNOISE;
						break;
					case RINGNOISE:
						tex->blenderWood.type = slg::ocl::RINGNOISE;
						break;
				}
				break;
			}
			case UV_TEX: {
				UVTexture *uvt = static_cast<UVTexture *>(t);

				tex->type = slg::ocl::UV_TEX;
				CompileTextureMapping2D(&tex->uvTex.mapping, uvt->GetTextureMapping());
				break;
			}
			case BAND_TEX: {
				BandTexture *bt = static_cast<BandTexture *>(t);

				tex->type = slg::ocl::BAND_TEX;
				const Texture *amount = bt->GetAmountTexture();
				tex->band.amountTexIndex = scene->texDefs.GetTextureIndex(amount);

				const vector<float> &offsets = bt->GetOffsets();
				const vector<Spectrum> &values = bt->GetValues();
				if (offsets.size() > BAND_TEX_MAX_SIZE)
					throw runtime_error("BandTexture with more than " + ToString(BAND_TEX_MAX_SIZE) + " are not supported");
				tex->band.size = offsets.size();
				for (u_int i = 0; i < BAND_TEX_MAX_SIZE; ++i) {
					if (i < offsets.size()) {
						tex->band.offsets[i] = offsets[i];
						ASSIGN_SPECTRUM(tex->band.values[i], values[i]);
					} else {
						tex->band.offsets[i] = 1.f;
						tex->band.values[i].c[0] = 0.f;
						tex->band.values[i].c[1] = 0.f;
						tex->band.values[i].c[2] = 0.f;
					}
				}
				break;
			}
			case HITPOINTCOLOR: {
				tex->type = slg::ocl::HITPOINTCOLOR;
				break;
			}
			case HITPOINTALPHA: {
				tex->type = slg::ocl::HITPOINTALPHA;
				break;
			}
			case HITPOINTGREY: {
				HitPointGreyTexture *hpg = static_cast<HitPointGreyTexture *>(t);

				tex->type = slg::ocl::HITPOINTGREY;
				tex->hitPointGrey.channel = hpg->GetChannel();
				break;
			}
            case NORMALMAP_TEX: {
                NormalMapTexture *nmt = static_cast<NormalMapTexture *>(t);

                tex->type = slg::ocl::NORMALMAP_TEX;
                const Texture *normalTex = nmt->GetTexture();
				tex->normalMap.texIndex = scene->texDefs.GetTextureIndex(normalTex);
				break;
            }
			default:
				throw runtime_error("Unknown texture in CompiledScene::CompileTextures(): " + boost::lexical_cast<string>(t->GetType()));
				break;
		}
	}
		
	const double tEnd = WallClockTime();
	SLG_LOG("Textures compilation time: " << int((tEnd - tStart) * 1000.0) << "ms");
}

static string ToOCLString(const slg::ocl::Spectrum &v) {
	return "(float3)(" + ToString(v.c[0]) + ", " + ToString(v.c[1]) + ", " + ToString(v.c[2]) + ")";
}

static void AddTextureSource(stringstream &source,  const string &texName, const string &returnType,
		const string &type, const u_int i, const string &texArgs) {
	source << returnType << " Texture_Index" << i << "_Evaluate" << type << "(__global Texture *texture, "
			"__global HitPoint *hitPoint\n"
			"\t\tTEXTURES_PARAM_DECL) {\n"
			"\treturn " << texName << "Texture_ConstEvaluate" << type << "(hitPoint" <<
				((texArgs.length() > 0) ? (", " + texArgs) : "") << ");\n"
			"}\n";
}

static void AddTextureSource(stringstream &source,  const string &texName,
		const u_int i, const string &texArgs) {
	AddTextureSource(source, texName, "float", "Float", i, texArgs);
	AddTextureSource(source, texName, "float3", "Spectrum", i, texArgs);
}

static string AddTextureSourceCall(const string &type, const u_int i) {
	stringstream ss;
	ss << "Texture_Index" << i << "_Evaluate" << type << "(&texs[" << i << "], hitPoint TEXTURES_PARAM)";

	return ss.str();
}

string CompiledScene::GetTexturesEvaluationSourceCode() const {
	// Generate the source code for each texture that reference other textures
	// and constant textures
	stringstream source;

	const u_int texturesCount = texs.size();
	for (u_int i = 0; i < texturesCount; ++i) {
		const slg::ocl::Texture *tex = &texs[i];

		switch (tex->type) {
			case slg::ocl::CONST_FLOAT:
				AddTextureSource(source, "ConstFloat", i, ToString(tex->constFloat.value));
				break;
			case slg::ocl::CONST_FLOAT3:
				AddTextureSource(source, "ConstFloat3", i, ToOCLString(tex->constFloat3.color));
				break;
			case slg::ocl::IMAGEMAP:
				AddTextureSource(source, "ImageMap", i,
						ToString(tex->imageMapTex.gain) + ", " +
						ToString(tex->imageMapTex.imageMapIndex) + ", " +
						"&texture->imageMapTex.mapping"
						" IMAGEMAPS_PARAM");
				break;
			case slg::ocl::SCALE_TEX: {
				AddTextureSource(source, "Scale", "float", "Float", i,
						AddTextureSourceCall("Float", tex->scaleTex.tex1Index) + ", " +
						AddTextureSourceCall("Float", tex->scaleTex.tex2Index));
				AddTextureSource(source, "Scale", "float3", "Spectrum", i, 
						AddTextureSourceCall("Spectrum", tex->scaleTex.tex1Index) + ", " +
						AddTextureSourceCall("Spectrum", tex->scaleTex.tex2Index));
				break;
			}
			case FRESNEL_APPROX_N:
				AddTextureSource(source, "FresnelApproxN", i, ToString(tex->fresnelApproxN.texIndex));
				break;
			case FRESNEL_APPROX_K: 
				AddTextureSource(source, "FresnelApproxK", i, ToString(tex->fresnelApproxK.texIndex));
				break;
			case slg::ocl::MIX_TEX: {
				AddTextureSource(source, "Mix", "float", "Float", i,
						AddTextureSourceCall("Float", tex->mixTex.amountTexIndex) + ", " +
						AddTextureSourceCall("Float", tex->mixTex.tex1Index) + ", " +
						AddTextureSourceCall("Float", tex->mixTex.tex2Index));
				AddTextureSource(source, "Mix", "float3", "Spectrum", i,
						AddTextureSourceCall("Spectrum", tex->mixTex.amountTexIndex) + ", " +
						AddTextureSourceCall("Spectrum", tex->mixTex.tex1Index) + ", " +
						AddTextureSourceCall("Spectrum", tex->mixTex.tex2Index));
				break;
			}
			case slg::ocl::ADD_TEX: {
				AddTextureSource(source, "Add", "float", "Float", i,
						AddTextureSourceCall("Float", tex->addTex.tex1Index) + ", " +
						AddTextureSourceCall("Float", tex->addTex.tex2Index));
				AddTextureSource(source, "Add", "float3", "Spectrum", i, 
						AddTextureSourceCall("Spectrum", tex->addTex.tex1Index) + ", " +
						AddTextureSourceCall("Spectrum", tex->addTex.tex2Index));
				break;
			}
			case slg::ocl::HITPOINTCOLOR:
				AddTextureSource(source, "HitPointColor", i, "");
				break;
			case slg::ocl::HITPOINTALPHA:
				AddTextureSource(source, "HitPointAlpha", i, "");
				break;
			case slg::ocl::HITPOINTGREY:
				AddTextureSource(source, "HitPointGrey", i, "");
				break;
			case slg::ocl::BLENDER_CLOUDS:
				AddTextureSource(source, "BlenderClouds", i,
						ToString(tex->blenderClouds.noisesize) + ", " +
						ToString(tex->blenderClouds.noisedepth) + ", " +
						ToString(tex->blenderClouds.contrast) + ", " +
						ToString(tex->blenderClouds.bright) + ", " +
						"&texture->blenderClouds.mapping");
				break;
			case slg::ocl::BLENDER_WOOD:
				AddTextureSource(source, "BlenderWood", i,
						ToString(tex->blenderWood.type) + ", " +
						ToString(tex->blenderWood.noisebasis2) + ", " +
						ToString(tex->blenderWood.noisesize) + ", " +
						ToString(tex->blenderWood.turbulence) + ", " +
						ToString(tex->blenderWood.contrast) + ", " +
						ToString(tex->blenderWood.bright) + ", " +
						ToString(tex->blenderWood.hard) + ", " +
						"&texture->blenderWood.mapping");
				break;
			case slg::ocl::CHECKERBOARD2D:
				AddTextureSource(source, "CheckerBoard2D", "float", "Float", i,
						AddTextureSourceCall("Float", tex->checkerBoard2D.tex1Index) + ", " +
						AddTextureSourceCall("Float", tex->checkerBoard2D.tex2Index) + ", " +
						"&texture->checkerBoard2D.mapping");
				AddTextureSource(source, "CheckerBoard2D", "float3", "Spectrum", i, 
						AddTextureSourceCall("Spectrum", tex->checkerBoard2D.tex1Index) + ", " +
						AddTextureSourceCall("Spectrum", tex->checkerBoard2D.tex2Index) + ", " +
						"&texture->checkerBoard2D.mapping");
				break;
			case slg::ocl::CHECKERBOARD3D:
				AddTextureSource(source, "CheckerBoard3D", "float", "Float", i,
						AddTextureSourceCall("Float", tex->checkerBoard3D.tex1Index) + ", " +
						AddTextureSourceCall("Float", tex->checkerBoard3D.tex2Index) + ", " +
						"&texture->checkerBoard3D.mapping");
				AddTextureSource(source, "CheckerBoard3D", "float3", "Spectrum", i, 
						AddTextureSourceCall("Spectrum", tex->checkerBoard3D.tex1Index) + ", " +
						AddTextureSourceCall("Spectrum", tex->checkerBoard3D.tex2Index) + ", " +
						"&texture->checkerBoard3D.mapping");
				break;
			case slg::ocl::FBM_TEX:
				AddTextureSource(source, "FBM", i,
						ToString(tex->fbm.omega) + ", " +
						ToString(tex->fbm.octaves) + ", " +
						"&texture->fbm.mapping");
				break;
			case slg::ocl::MARBLE:
				AddTextureSource(source, "Marble", i,
						ToString(tex->marble.scale) + ", " +
						ToString(tex->marble.omega) + ", " +
						ToString(tex->marble.octaves) + ", " +
						ToString(tex->marble.variation) + ", " +
						"&texture->marble.mapping");
				break;
			case slg::ocl::DOTS:
				AddTextureSource(source, "Dots", "float", "Float", i,
						AddTextureSourceCall("Float", tex->dots.insideIndex) + ", " +
						AddTextureSourceCall("Float", tex->dots.outsideIndex) + ", " +
						"&texture->dots.mapping");
				AddTextureSource(source, "Dots", "float3", "Spectrum", i, 
						AddTextureSourceCall("Spectrum", tex->dots.insideIndex) + ", " +
						AddTextureSourceCall("Spectrum", tex->dots.outsideIndex) + ", " +
						"&texture->dots.mapping");
				break;
			case slg::ocl::BRICK:
				AddTextureSource(source, "Brick", "float", "Float", i,
						AddTextureSourceCall("Float", tex->brick.tex1Index) + ", " +
						AddTextureSourceCall("Float", tex->brick.tex2Index) + ", " +
						AddTextureSourceCall("Float", tex->brick.tex3Index) + ", " +
						ToString(tex->brick.bond) + ", " +
						ToString(tex->brick.brickwidth) + ", " +
						ToString(tex->brick.brickheight) + ", " +
						ToString(tex->brick.brickdepth) + ", " +
						ToString(tex->brick.mortarsize) + ", " +
						"(float3)(" + ToString(tex->brick.offsetx) + ", " + ToString(tex->brick.offsetx) + ", " + ToString(tex->brick.offsetx) + "), " +
						ToString(tex->brick.run) + ", " +
						ToString(tex->brick.mortarwidth) + ", " +
						ToString(tex->brick.mortarheight) + ", " +
						ToString(tex->brick.mortardepth) + ", " +
						ToString(tex->brick.proportion) + ", " +
						ToString(tex->brick.invproportion) + ", " +
						"&texture->brick.mapping");
				AddTextureSource(source, "Brick", "float3", "Spectrum", i, 
						AddTextureSourceCall("Spectrum", tex->brick.tex1Index) + ", " +
						AddTextureSourceCall("Spectrum", tex->brick.tex2Index) + ", " +
						AddTextureSourceCall("Spectrum", tex->brick.tex3Index) + ", " +
						ToString(tex->brick.bond) + ", " +
						ToString(tex->brick.brickwidth) + ", " +
						ToString(tex->brick.brickheight) + ", " +
						ToString(tex->brick.brickdepth) + ", " +
						ToString(tex->brick.mortarsize) + ", " +
						"(float3)(" + ToString(tex->brick.offsetx) + ", " + ToString(tex->brick.offsetx) + ", " + ToString(tex->brick.offsetx) + "), " +
						ToString(tex->brick.run) + ", " +
						ToString(tex->brick.mortarwidth) + ", " +
						ToString(tex->brick.mortarheight) + ", " +
						ToString(tex->brick.mortardepth) + ", " +
						ToString(tex->brick.proportion) + ", " +
						ToString(tex->brick.invproportion) + ", " +
						"&texture->brick.mapping");
				break;
			case slg::ocl::WINDY:
				AddTextureSource(source, "Windy", i,
						"&texture->windy.mapping");
				break;
			case slg::ocl::WRINKLED:
				AddTextureSource(source, "Wrinkled", i,
						ToString(tex->marble.omega) + ", " +
						ToString(tex->marble.octaves) + ", " +
						"&texture->wrinkled.mapping");
				break;
			case slg::ocl::UV_TEX:
				AddTextureSource(source, "UV", i,
						"&texture->uvTex.mapping");
				break;
			case slg::ocl::BAND_TEX:
				AddTextureSource(source, "Band", "float", "Float", i,
						ToString(tex->band.size) + ", " +
						"texture->band.offsets, "
						"texture->band.values, " +
						AddTextureSourceCall("Float", tex->band.amountTexIndex));
				AddTextureSource(source, "Band", "float3", "Spectrum", i,
						ToString(tex->band.size) + ", " +
						"texture->band.offsets, "
						"texture->band.values, " +
						AddTextureSourceCall("Spectrum", tex->band.amountTexIndex));
				break;
			case slg::ocl::NORMALMAP_TEX:
				AddTextureSource(source, "NormalMap", i, "");
				break;
			default:
				throw runtime_error("Unknown texture in CompiledScene::GetTexturesEvaluationSourceCode(): " + boost::lexical_cast<string>(tex->type));
				break;
		}
	}

	// Generate the code for evaluating a generic float texture
	source << "float Texture_GetFloatValue(const uint texIndex, __global HitPoint *hitPoint TEXTURES_PARAM_DECL) {\n"
			"\t __global Texture *tex = &texs[texIndex];\n"
			"\tswitch (texIndex) {\n";
	for (u_int i = 0; i < texturesCount; ++i) {
		source << "\t\tcase " << i << ": return Texture_Index" << i << "_EvaluateFloat(tex, hitPoint TEXTURES_PARAM);\n";
	}
	source << "\t\tdefault: return 0.f;\n"
			"\t}\n"
			"}\n";

	// Generate the code for evaluating a generic spectrum texture
	source << "float3 Texture_GetSpectrumValue(const uint texIndex, __global HitPoint *hitPoint TEXTURES_PARAM_DECL) {\n"
			"\t __global Texture *tex = &texs[texIndex];\n"
			"\tswitch (texIndex) {\n";
	for (u_int i = 0; i < texturesCount; ++i) {
		source << "\t\tcase " << i << ": return Texture_Index" << i << "_EvaluateSpectrum(tex, hitPoint TEXTURES_PARAM);\n";
	}
	source << "\t\tdefault: return BLACK;\n"
			"\t}\n"
			"}\n";
	
	return source.str();
}

static void AddMaterialSource(stringstream &source, 
		const string &matName, const u_int i, const string &funcName1, const string &funcName2,
		const string &returnType, const string &args,  const string &params, const bool hasReturn = true) {
	source <<
			returnType << " Material_Index" << i << "_" << funcName1 << "(" << args << ") {\n"
			"\t" <<
			(hasReturn ? "return " : " ") <<
			matName << "Material_" << funcName2 << "(" << params << ");\n"
			"}\n";
}

static void AddMaterialSource(stringstream &source, 
		const string &matName, const u_int i) {
	AddMaterialSource(source, matName, i, "GetEventTypes", "GetEventTypes", "BSDFEvent",
			"__global Material *material MATERIALS_PARAM_DECL", "");
	AddMaterialSource(source, matName, i, "IsDelta", "IsDelta", "bool",
			"__global Material *material MATERIALS_PARAM_DECL", "");
	AddMaterialSource(source, matName, i, "Evaluate", "Evaluate", "float3",
			"__global Material *material, "
				"__global HitPoint *hitPoint, const float3 lightDir, const float3 eyeDir, "
				"BSDFEvent *event, float *directPdfW "
				"MATERIALS_PARAM_DECL",
			"material, hitPoint, lightDir, eyeDir, event, directPdfW TEXTURES_PARAM");
	AddMaterialSource(source, matName, i, "Sample", "Sample", "float3",
			"__global Material *material, __global HitPoint *hitPoint, "
				"const float3 fixedDir, float3 *sampledDir, "
				"const float u0, const float u1,\n"
				"#if defined(PARAM_HAS_PASSTHROUGH)\n"
				"\tconst float passThroughEvent,\n"
				"#endif\n"
				"\tfloat *pdfW, float *cosSampledDir, BSDFEvent *event, "
				"const BSDFEvent requestedEvent "
				"MATERIALS_PARAM_DECL",
			"material, hitPoint, fixedDir, sampledDir, u0, u1,\n"
				"#if defined(PARAM_HAS_PASSTHROUGH)\n"
				"\t\tpassThroughEvent,\n"
				"#endif\n"
				"\t\tpdfW,  cosSampledDir, event, requestedEvent TEXTURES_PARAM");
	
	source << "#if defined(PARAM_HAS_PASSTHROUGH)\n";
	AddMaterialSource(source, matName, i, "GetPassThroughTransparency", "GetPassThroughTransparency", "float3",
			"__global Material *material, __global HitPoint *hitPoint, "
				"const float3 localFixedDir, const float passThroughEvent "
				"MATERIALS_PARAM_DECL",
			"material, hitPoint, localFixedDir, passThroughEvent TEXTURES_PARAM");
	source << "#endif\n";
}

static void AddMaterialSourceStandardImplGetEmittedRadiance(stringstream &source, const u_int i) {
	AddMaterialSource(source, "", i, "GetEmittedRadiance", "GetEmittedRadianceNoMix", "float3",
			"__global Material *material, __global HitPoint *hitPoint, const float oneOverPrimitiveArea MATERIALS_PARAM_DECL",
			"material, hitPoint TEXTURES_PARAM");
}

static void AddMaterialSourceStandardImplBump(stringstream &source, const u_int i) {
	source << "#if defined(PARAM_HAS_BUMPMAPS)\n";
	AddMaterialSource(source, "", i, "Bump", "BumpNoMix", "void",
			"__global Material *material, __global HitPoint *hitPoint, "
				"const float3 dpdu, const float3 dpdv, "
				"const float3 dndu, const float3 dndv, const float weight "
				"MATERIALS_PARAM_DECL",
			"material, hitPoint, dpdu, dpdv, dndu, dndv, weight TEXTURES_PARAM", false);
	source << "#endif\n";
}

static void AddMaterialSourceStandardImplGetvolume(stringstream &source, const u_int i) {
	source << "#if defined(PARAM_HAS_VOLUMES)\n";
	AddMaterialSource(source, "", i, "GetInteriorVolume", "GetInteriorVolumeNoMix", "uint",
			"__global Material *material, __global HitPoint *hitPoint, "
				"const float passThroughEvent MATERIALS_PARAM_DECL",
			"material");
	AddMaterialSource(source, "", i, "GetExteriorVolume", "GetExteriorVolumeNoMix", "uint",
			"__global Material *material, __global HitPoint *hitPoint, "
				"const float passThroughEvent MATERIALS_PARAM_DECL",
			"material");
	source << "#endif\n";
}

static void AddMaterialSourceSwitch(stringstream &source, 
		const u_int count, const string &funcName, const string &calledFuncName,
		const string &returnType, const string &defaultReturnValue, 
		const string &args,  const string &params, const bool hasReturn = true) {
	source << returnType << " Material_" << funcName << "(" << args << ") { \n"
			"\t__global Material *mat = &mats[index];\n"
			"\tswitch (index) {\n";
	for (u_int i = 0; i < count; ++i) {
		source << "\t\tcase " << i << ":\n";
		source << "\t\t\t" <<
				(hasReturn ? "return " : "") <<
				"Material_Index" << i << "_" << calledFuncName << "(" << params << ");\n";
		if (!hasReturn)
			source << "\t\t\tbreak;\n";
	}
	
	if (hasReturn) {
		source << "\t\tdefault:\n"
				"\t\t\treturn " << defaultReturnValue<< ";\n";
	}

	source << 
			"\t}\n"
			"}\n";
}

string CompiledScene::GetMaterialsEvaluationSourceCode() const {
	// Generate the source code for each material that reference other materials
	// and constant materials
	stringstream source;

	const u_int materialsCount = mats.size();
	for (u_int i = 0; i < materialsCount; ++i) {
		const slg::ocl::Material *mat = &mats[i];

		switch (mat->type) {
			case slg::ocl::MATTE: {
				AddMaterialSource(source, "Matte", i);
				AddMaterialSourceStandardImplGetEmittedRadiance(source, i);
				AddMaterialSourceStandardImplBump(source, i);
				AddMaterialSourceStandardImplGetvolume(source, i);
				break;
			}
			case slg::ocl::ARCHGLASS: {
				AddMaterialSource(source, "ArchGlass", i);
				AddMaterialSourceStandardImplGetEmittedRadiance(source, i);
				AddMaterialSourceStandardImplBump(source, i);
				AddMaterialSourceStandardImplGetvolume(source, i);
				break;
			}
			case slg::ocl::CARPAINT: {
				AddMaterialSource(source, "Carpaint", i);
				AddMaterialSourceStandardImplGetEmittedRadiance(source, i);
				AddMaterialSourceStandardImplBump(source, i);
				AddMaterialSourceStandardImplGetvolume(source, i);
				break;
			}
			case slg::ocl::CLOTH: {
				AddMaterialSource(source, "Cloth", i);
				AddMaterialSourceStandardImplGetEmittedRadiance(source, i);
				AddMaterialSourceStandardImplBump(source, i);
				AddMaterialSourceStandardImplGetvolume(source, i);
				break;
			}
			case slg::ocl::GLASS: {
				AddMaterialSource(source, "Glass", i);
				AddMaterialSourceStandardImplGetEmittedRadiance(source, i);
				AddMaterialSourceStandardImplBump(source, i);
				AddMaterialSourceStandardImplGetvolume(source, i);
				break;
			}
			case slg::ocl::GLOSSY2: {
				AddMaterialSource(source, "Glossy2", i);
				AddMaterialSourceStandardImplGetEmittedRadiance(source, i);
				AddMaterialSourceStandardImplBump(source, i);
				AddMaterialSourceStandardImplGetvolume(source, i);
				break;
			}
			case slg::ocl::MATTETRANSLUCENT: {
				AddMaterialSource(source, "MatteTranslucent", i);
				AddMaterialSourceStandardImplGetEmittedRadiance(source, i);
				AddMaterialSourceStandardImplBump(source, i);
				AddMaterialSourceStandardImplGetvolume(source, i);
				break;
			}
			case slg::ocl::METAL2: {
				AddMaterialSource(source, "Metal2", i);
				AddMaterialSourceStandardImplGetEmittedRadiance(source, i);
				AddMaterialSourceStandardImplBump(source, i);
				AddMaterialSourceStandardImplGetvolume(source, i);
				break;
			}
			case slg::ocl::MIRROR: {
				AddMaterialSource(source, "Mirror", i);
				AddMaterialSourceStandardImplGetEmittedRadiance(source, i);
				AddMaterialSourceStandardImplBump(source, i);
				AddMaterialSourceStandardImplGetvolume(source, i);
				break;
			}
			case slg::ocl::NULLMAT: {
				AddMaterialSource(source, "Null", i);
				AddMaterialSourceStandardImplGetEmittedRadiance(source, i);
				AddMaterialSourceStandardImplBump(source, i);
				AddMaterialSourceStandardImplGetvolume(source, i);
				break;
			}
			case slg::ocl::ROUGHGLASS: {
				AddMaterialSource(source, "RoughGlass", i);
				AddMaterialSourceStandardImplGetEmittedRadiance(source, i);
				AddMaterialSourceStandardImplBump(source, i);
				AddMaterialSourceStandardImplGetvolume(source, i);
				break;
			}
			case slg::ocl::VELVET: {
				AddMaterialSource(source, "Velvet", i);
				AddMaterialSourceStandardImplGetEmittedRadiance(source, i);
				AddMaterialSourceStandardImplBump(source, i);
				AddMaterialSourceStandardImplGetvolume(source, i);
				break;
			}
			case slg::ocl::CLEAR_VOL: {
				AddMaterialSource(source, "ClearVol", i);
				AddMaterialSourceStandardImplGetEmittedRadiance(source, i);
				AddMaterialSourceStandardImplBump(source, i);
				AddMaterialSourceStandardImplGetvolume(source, i);
				break;
			}
			case slg::ocl::MIX: {
				// MIX material requires ad hoc code

				// Material_IndexN_GetEventTypes()
				source <<
						"BSDFEvent Material_Index" << i << "_GetEventTypes(__global Material *material MATERIALS_PARAM_DECL) {\n"
						"\treturn Material_Index" << mat->mix.matAIndex << "_GetEventTypes(&mats[" << mat->mix.matAIndex << "] MATERIALS_PARAM) |\n"
						"\t\tMaterial_Index" << mat->mix.matBIndex << "_GetEventTypes(&mats[" << mat->mix.matBIndex << "] MATERIALS_PARAM);\n"
						"}\n";

				// Material_IndexN_IsDelta()
				source <<
						"bool Material_Index" << i << "_IsDelta(__global Material *material MATERIALS_PARAM_DECL) {\n"
						"\treturn Material_Index" << mat->mix.matAIndex << "_IsDelta(&mats[" << mat->mix.matAIndex << "] MATERIALS_PARAM) &&\n"
						"\t\tMaterial_Index" << mat->mix.matBIndex << "_IsDelta(&mats[" << mat->mix.matBIndex << "] MATERIALS_PARAM);\n"
						"}\n";
				
				// Material_IndexN_Evaluate()
				source <<
						"float3 Material_Index" << i << "_Evaluate(__global Material *material, __global HitPoint *hitPoint, const float3 lightDir, const float3 eyeDir, BSDFEvent *event, float *directPdfW MATERIALS_PARAM_DECL) {\n"
						"\tfloat3 result = BLACK;\n"
						"\tconst float factor = Texture_GetFloatValue(" << mat->mix.mixFactorTexIndex << ", hitPoint TEXTURES_PARAM);\n"
						"\tconst float weight2 = clamp(factor, 0.f, 1.f);\n"
						"\tconst float weight1 = 1.f - weight2;\n"
						"\tif (directPdfW)\n"
						"\t	*directPdfW = 0.f;\n"
						"\tBSDFEvent eventMatA = NONE;\n"
						"\tif (weight1 > 0.f) {\n"
						"\t	float directPdfWMatA;\n"
						"\t	const float3 matAResult = Material_Index" <<  mat->mix.matAIndex << "_Evaluate(&mats[" <<  mat->mix.matAIndex << "], hitPoint, lightDir, eyeDir, &eventMatA, &directPdfWMatA MATERIALS_PARAM);\n"
						"\t	if (!Spectrum_IsBlack(matAResult)) {\n"
						"\t		result += weight1 * matAResult;\n"
						"\t		if (directPdfW)\n"
						"\t			*directPdfW += weight1 * directPdfWMatA;\n"
						"\t	}\n"
						"\t}\n"
						"\tBSDFEvent eventMatB = NONE;\n"
						"\tif (weight2 > 0.f) {\n"
						"\t	float directPdfWMatB;\n"
						"\t	const float3 matBResult = Material_Index" <<  mat->mix.matBIndex << "_Evaluate(&mats[" <<  mat->mix.matBIndex << "], hitPoint, lightDir, eyeDir, &eventMatB, &directPdfWMatB MATERIALS_PARAM);\n"
						"\t	if (!Spectrum_IsBlack(matBResult)) {\n"
						"\t		result += weight2 * matBResult;\n"
						"\t		if (directPdfW)\n"
						"\t			*directPdfW += weight2 * directPdfWMatB;\n"
						"\t	}\n"
						"\t}\n"
						"\t*event = eventMatA | eventMatB;\n"
						"\treturn result;\n"
						"}\n";

				// Material_IndexN_Sample()
				source <<
						"float3 Material_Index" << i << "_Sample(__global Material *material, __global HitPoint *hitPoint, const float3 fixedDir, float3 *sampledDir, const float u0, const float u1,\n"
						"#if defined(PARAM_HAS_PASSTHROUGH)\n"
						"\t		const float passThroughEvent,\n"
						"#endif\n"
						"\t		float *pdfW, float *cosSampledDir, BSDFEvent *event, const BSDFEvent requestedEvent MATERIALS_PARAM_DECL) {\n"
						"\tconst float factor = Texture_GetFloatValue(" << mat->mix.mixFactorTexIndex << ", hitPoint TEXTURES_PARAM);\n"
						"\tconst float weight2 = clamp(factor, 0.f, 1.f);\n"
						"\tconst float weight1 = 1.f - weight2;\n"
						"\tconst bool sampleMatA = (passThroughEvent < weight1);\n"
						"\tconst float weightFirst = sampleMatA ? weight1 : weight2;\n"
						"\tconst float weightSecond = sampleMatA ? weight2 : weight1;\n"
						"\tconst float passThroughEventFirst = sampleMatA ? (passThroughEvent / weight1) : (passThroughEvent - weight1) / weight2;\n"
						"\t__global Material *matA = &mats[" <<  mat->mix.matAIndex << "];\n"
						"\t__global Material *matB = &mats[" <<  mat->mix.matBIndex << "];\n"
						"\tfloat3 result = sampleMatA ?\n"
						"\t		Material_Index" <<  mat->mix.matAIndex << "_Sample(matA, hitPoint, fixedDir, sampledDir,\n"
						"\t			u0, u1,\n"
						"#if defined(PARAM_HAS_PASSTHROUGH)\n"
						"\t			passThroughEventFirst,\n"
						"#endif\n"
						"\t			pdfW, cosSampledDir, event, requestedEvent MATERIALS_PARAM):\n"
						"\t		Material_Index" <<  mat->mix.matBIndex << "_Sample(matB, hitPoint, fixedDir, sampledDir,\n"
						"\t			u0, u1,\n"
						"#if defined(PARAM_HAS_PASSTHROUGH)\n"
						"\t			passThroughEventFirst,\n"
						"#endif\n"
						"\t			pdfW, cosSampledDir, event, requestedEvent MATERIALS_PARAM);\n"
						"\tif (Spectrum_IsBlack(result))\n"
						"\t	return BLACK;\n"
						"\t*pdfW *= weightFirst;\n"
						"\tresult *= *pdfW;\n"
						"\tBSDFEvent eventSecond;\n"
						"\tfloat pdfWSecond;\n"
						"\tfloat3 evalSecond = sampleMatA ?\n"
						"\t		Material_Index" <<  mat->mix.matBIndex << "_Evaluate(matB, hitPoint, *sampledDir, fixedDir, &eventSecond, &pdfWSecond MATERIALS_PARAM) :\n"
						"\t		Material_Index" <<  mat->mix.matAIndex << "_Evaluate(matA, hitPoint, *sampledDir, fixedDir, &eventSecond, &pdfWSecond MATERIALS_PARAM);\n"
						"\tif (!Spectrum_IsBlack(evalSecond)) {\n"
						"\t	result += weightSecond * evalSecond;\n"
						"\t	*pdfW += weightSecond * pdfWSecond;\n"
						"\t}\n"
						"\treturn result / *pdfW;\n"
						"}\n";
				
				// Material_IndexN_GetEmittedRadiance()
				source <<
						"float3 Material_Index" << i << "_GetEmittedRadiance(__global Material *material, __global HitPoint *hitPoint, const float oneOverPrimitiveArea MATERIALS_PARAM_DECL) {\n"
						"\tfloat3 result = BLACK;\n"
						"\tconst float factor = Texture_GetFloatValue(" << mat->mix.mixFactorTexIndex << ", hitPoint TEXTURES_PARAM);\n"
						"\tconst float weight2 = clamp(factor, 0.f, 1.f);\n"
						"\tconst float weight1 = 1.f - weight2;\n"
						"\tif (weight1 > 0.f)\n"
						"\t	result += weight1 * Material_Index" <<  mat->mix.matAIndex << "_GetEmittedRadiance(&mats[" <<  mat->mix.matAIndex << "], hitPoint, oneOverPrimitiveArea MATERIALS_PARAM);\n"
						"\tif (weight2 > 0.f)\n"
						"\t	result += weight2 * Material_Index" <<  mat->mix.matBIndex << "_GetEmittedRadiance(&mats[" <<  mat->mix.matBIndex << "], hitPoint, oneOverPrimitiveArea MATERIALS_PARAM);\n"
						"\treturn result;\n"
						"}\n";

				// Material_IndexN_GetPassThroughTransparency()
				source << "#if defined(PARAM_HAS_PASSTHROUGH)\n";
				source <<
						"float3 Material_Index" << i << "_GetPassThroughTransparency(__global Material *material, __global HitPoint *hitPoint, const float3 localFixedDir, const float passThroughEvent MATERIALS_PARAM_DECL) {\n"
						"\tconst float factor = Texture_GetFloatValue(" << mat->mix.mixFactorTexIndex << ", hitPoint TEXTURES_PARAM);\n"
						"\tconst float weight2 = clamp(factor, 0.f, 1.f);\n"
						"\tconst float weight1 = 1.f - weight2;\n"
						"\tif (passThroughEvent < weight1)\n"
						"\t	return Material_Index" <<  mat->mix.matAIndex << "_GetPassThroughTransparency(&mats[" <<  mat->mix.matAIndex << "], hitPoint, localFixedDir, passThroughEvent / weight1 MATERIALS_PARAM);\n"
						"\telse\n"
						"\t	return Material_Index" <<  mat->mix.matBIndex << "_GetPassThroughTransparency(&mats[" <<  mat->mix.matBIndex << "], hitPoint, localFixedDir, (passThroughEvent - weight2) / weight2 MATERIALS_PARAM);\n"
						"}\n";
				source << "#endif\n";

				// Material_IndexN_Bump()
				source << "#if defined(PARAM_HAS_BUMPMAPS)\n";
				source <<
						"void Material_Index" << i << "_Bump(__global Material *material, __global HitPoint *hitPoint, const float3 dpdu, const float3 dpdv, const float3 dndu, const float3 dndv, const float weight MATERIALS_PARAM_DECL) {\n"
						"\tif (weight == 0.f)\n"
						"\t	return;\n"
						"\tif (material->bumpTexIndex != NULL_INDEX)\n"
						"\t	Material_BumpNoMix(material, hitPoint, dpdu, dpdv, dndu, dndv, weight TEXTURES_PARAM);\n"
						"\telse {\n"
						"\tconst float factor = Texture_GetFloatValue(" << mat->mix.mixFactorTexIndex << ", hitPoint TEXTURES_PARAM);\n"
						"\tconst float weight2 = clamp(factor, 0.f, 1.f);\n"
						"\tconst float weight1 = 1.f - weight2;\n"
						"\t	Material_Index" << mat->mix.matAIndex << "_Bump(&mats[" <<  mat->mix.matAIndex << "], hitPoint, dpdu, dpdv, dndu, dndv, weight * weight1 MATERIALS_PARAM);\n"
						"\t	Material_Index" << mat->mix.matBIndex << "_Bump(&mats[" <<  mat->mix.matBIndex << "], hitPoint, dpdu, dpdv, dndu, dndv, weight * weight2 MATERIALS_PARAM);\n"
						"\t}\n"
						"}\n";
				source << "#endif\n";

				// Material_IndexN_GetInteriorVolume() and Material_IndexN_GetExteriorVolume()
				source << "#if defined(PARAM_HAS_VOLUMES)\n";
				source <<
						"uint Material_Index" << i << "_GetInteriorVolume(__global Material *material, __global HitPoint *hitPoint, const float passThroughEvent MATERIALS_PARAM_DECL) {\n"
						"\tif (material->interiorVolumeIndex != NULL_INDEX)\n"
						"\t	return material->interiorVolumeIndex;\n"
						"\tconst float factor = Texture_GetFloatValue(" << mat->mix.mixFactorTexIndex << ", hitPoint TEXTURES_PARAM);\n"
						"\tconst float weight2 = clamp(factor, 0.f, 1.f);\n"
						"\tconst float weight1 = 1.f - weight2;\n"
						"\tif (passThroughEvent < weight1)\n"
						"\t	return Material_Index" <<  mat->mix.matAIndex << "_GetInteriorVolume(&mats[" <<  mat->mix.matAIndex << "], hitPoint, passThroughEvent / weight1 MATERIALS_PARAM);\n"
						"\telse\n"
						"\t	return Material_Index" <<  mat->mix.matBIndex << "_GetInteriorVolume(&mats[" <<  mat->mix.matBIndex << "], hitPoint, (passThroughEvent - weight2) / weight2 MATERIALS_PARAM);\n"
						"}\n";
				source <<
						"uint Material_Index" << i << "_GetExteriorVolume(__global Material *material, __global HitPoint *hitPoint, const float passThroughEvent MATERIALS_PARAM_DECL) {\n"
						"\tif (material->interiorVolumeIndex != NULL_INDEX)\n"
						"\t	return material->interiorVolumeIndex;\n"
						"\tconst float factor = Texture_GetFloatValue(" << mat->mix.mixFactorTexIndex << ", hitPoint TEXTURES_PARAM);\n"
						"\tconst float weight2 = clamp(factor, 0.f, 1.f);\n"
						"\tconst float weight1 = 1.f - weight2;\n"
						"\tif (passThroughEvent < weight1)\n"
						"\t	return Material_Index" <<  mat->mix.matAIndex << "_GetExteriorVolume(&mats[" <<  mat->mix.matAIndex << "], hitPoint, passThroughEvent / weight1 MATERIALS_PARAM);\n"
						"\telse\n"
						"\t	return Material_Index" <<  mat->mix.matBIndex << "_GetExteriorVolume(&mats[" <<  mat->mix.matBIndex << "], hitPoint, (passThroughEvent - weight2) / weight2 MATERIALS_PARAM);\n"
						"}\n";
				source << "#endif\n";
				break;
			}
			default:
				throw runtime_error("Unknown material in CompiledScene::GetMaterialsEvaluationSourceCode(): " + boost::lexical_cast<string>(mat->type));
				break;
		}
	}

	// Generate the code for generic Material_GetEventTypes
	AddMaterialSourceSwitch(source, materialsCount, "GetEventTypes", "GetEventTypes", "BSDFEvent", "NONE",
			"const uint index MATERIALS_PARAM_DECL",
			"mat MATERIALS_PARAM");
	
	// Generate the code for generic Material_IsDelta()
	AddMaterialSourceSwitch(source, materialsCount, "IsDelta", "IsDelta", "bool", "true",
			"const uint index MATERIALS_PARAM_DECL",
			"mat MATERIALS_PARAM");

	// Generate the code for generic Material_GetEmittedRadianceWithMix()
	AddMaterialSourceSwitch(source, materialsCount, "GetEmittedRadianceWithMix", "GetEmittedRadiance", "float3", "BLACK",
			"const uint index, __global HitPoint *hitPoint, const float oneOverPrimitiveArea MATERIALS_PARAM_DECL",
			"mat, hitPoint, oneOverPrimitiveArea MATERIALS_PARAM");

	// Generate the code for generic Material_Evaluate()
	AddMaterialSourceSwitch(source, materialsCount, "Evaluate", "Evaluate", "float3", "BLACK",
			"const uint index, "
				"__global HitPoint *hitPoint, const float3 lightDir, const float3 eyeDir, "
				"BSDFEvent *event, float *directPdfW "
				"MATERIALS_PARAM_DECL",
			"mat, hitPoint, lightDir, eyeDir, event, directPdfW MATERIALS_PARAM");

	// Generate the code for generic Material_Sample()
	AddMaterialSourceSwitch(source, materialsCount, "Sample", "Sample", "float3", "BLACK",
			"const uint index, "
				"__global HitPoint *hitPoint, "
				"const float3 fixedDir, float3 *sampledDir, "
				"const float u0, const float u1,\n"
				"#if defined(PARAM_HAS_PASSTHROUGH)\n"
				"\tconst float passThroughEvent,\n"
				"#endif\n"
				"\tfloat *pdfW, float *cosSampledDir, BSDFEvent *event, "
				"const BSDFEvent requestedEvent "
				"MATERIALS_PARAM_DECL",
			"mat, hitPoint, fixedDir, sampledDir, u0, u1,\n"
				"#if defined(PARAM_HAS_PASSTHROUGH)\n"
				"\t\t\tpassThroughEvent,\n"
				"#endif\n"
				"\t\t\tpdfW,  cosSampledDir, event, requestedEvent MATERIALS_PARAM");

	// Generate the code for generic Material_Bump()
	source << "#if defined(PARAM_HAS_BUMPMAPS)\n";
	AddMaterialSourceSwitch(source, materialsCount, "BumpWithMix", "Bump", "void", "",
			"const uint index, __global HitPoint *hitPoint, "
				"const float3 dpdu, const float3 dpdv, "
				"const float3 dndu, const float3 dndv, const float weight "
				"MATERIALS_PARAM_DECL",
			"mat, hitPoint, dpdu, dpdv, dndu, dndv, weight MATERIALS_PARAM", false);
	source << "#endif\n";

	// Generate the code for generic Material_GetPassThroughTransparency()
	source << "#if defined(PARAM_HAS_PASSTHROUGH)\n";
	AddMaterialSourceSwitch(source, materialsCount, "GetPassThroughTransparency", "GetPassThroughTransparency", "float3", "BLACK",
			"const uint index, __global HitPoint *hitPoint, "
				"const float3 localFixedDir, const float oneOverPrimitiveArea MATERIALS_PARAM_DECL",
			"mat, hitPoint, localFixedDir, oneOverPrimitiveArea MATERIALS_PARAM");
	source << "#endif\n";

	// Generate the code for generic Material_GetInteriorVolumeWithMix() and Material_GetExteriorVolumeWithMix()
	source << "#if defined(PARAM_HAS_VOLUMES)\n";
	AddMaterialSourceSwitch(source, materialsCount, "GetInteriorVolumeWithMix", "GetInteriorVolume", "uint", "NULL_INDEX",
			"const uint index, __global HitPoint *hitPoint, const float passThroughEvent MATERIALS_PARAM_DECL",
			"mat, hitPoint, passThroughEvent MATERIALS_PARAM");
	AddMaterialSourceSwitch(source, materialsCount, "GetExteriorVolumeWithMix", "GetExteriorVolume", "uint", "NULL_INDEX",
			"const uint index, __global HitPoint *hitPoint, const float passThroughEvent MATERIALS_PARAM_DECL",
			"mat, hitPoint, passThroughEvent MATERIALS_PARAM");
	source << "#endif\n";

	// This glue is used to have fast path in case the first material is not MIX
	source <<
			"float3 Material_GetEmittedRadiance(const uint matIndex,\n"
			"		__global HitPoint *hitPoint, const float oneOverPrimitiveArea\n"
			"		MATERIALS_PARAM_DECL) {\n"
			"	__global Material *material = &mats[matIndex];\n"
			"	float3 result;\n"
			"#if defined (PARAM_ENABLE_MAT_MIX)\n"
			"	if (material->type == MIX)\n"
			"		result = Material_GetEmittedRadianceWithMix(matIndex, hitPoint, oneOverPrimitiveArea\n"
			"				MATERIALS_PARAM);\n"
			"	else\n"
			"#endif\n"
			"		result = Material_GetEmittedRadianceNoMix(material, hitPoint\n"
			"				TEXTURES_PARAM);\n"
			"	return 	VLOAD3F(material->emittedFactor.c) * (material->usePrimitiveArea ? oneOverPrimitiveArea : 1.f) * result;\n"
			"}\n"
			"#if defined(PARAM_HAS_BUMPMAPS)\n"
			"void Material_Bump(const uint matIndex, __global HitPoint *hitPoint,\n"
			"        const float3 dpdu, const float3 dpdv,\n"
			"        const float3 dndu, const float3 dndv, const float weight\n"
			"        MATERIALS_PARAM_DECL) {\n"
			"	__global Material *material = &mats[matIndex];\n"
			"#if defined (PARAM_ENABLE_MAT_MIX)\n"
			"	if (material->type == MIX)\n"
			"		Material_BumpWithMix(matIndex, hitPoint,\n"
			"                dpdu, dpdv, dndu, dndv, weight\n"
			"                MATERIALS_PARAM);\n"
			"	else\n"
			"#endif\n"
			"		Material_BumpNoMix(material, hitPoint,\n"
			"                dpdu, dpdv, dndu, dndv, weight\n"
			"                TEXTURES_PARAM);\n"
			"}\n"
			"#endif\n"
			"#if defined(PARAM_HAS_VOLUMES)\n"
			"uint Material_GetInteriorVolume(const uint matIndex,\n"
			"		__global HitPoint *hitPoint\n"
			"#if defined(PARAM_HAS_PASSTHROUGH)\n"
			"		, const float passThroughEvent\n"
			"#endif\n"
			"		MATERIALS_PARAM_DECL) {\n"
			"	__global Material *material = &mats[matIndex];\n"
			"#if defined (PARAM_ENABLE_MAT_MIX)\n"
			"	if (material->type == MIX)\n"
			"		return Material_GetInteriorVolumeWithMix(matIndex, hitPoint\n"
			"#if defined(PARAM_HAS_PASSTHROUGH)\n"
			"			, passThroughEvent\n"
			"#endif\n"
			"			MATERIALS_PARAM);\n"
			"	else\n"
			"#endif\n"
			"		return Material_GetInteriorVolumeNoMix(material);\n"
			"}\n"
			"uint Material_GetExteriorVolume(const uint matIndex,\n"
			"		__global HitPoint *hitPoint\n"
			"#if defined(PARAM_HAS_PASSTHROUGH)\n"
			"		, const float passThroughEvent\n"
			"#endif\n"
			"		MATERIALS_PARAM_DECL) {\n"
			"	__global Material *material = &mats[matIndex];\n"
			"#if defined (PARAM_ENABLE_MAT_MIX)\n"
			"	if (material->type == MIX)\n"
			"		return Material_GetExteriorVolumeWithMix(matIndex, hitPoint\n"
			"#if defined(PARAM_HAS_PASSTHROUGH)\n"
			"			, passThroughEvent\n"
			"#endif\n"
			"			MATERIALS_PARAM);\n"
			"	else\n"
			"#endif\n"
			"		return Material_GetExteriorVolumeNoMix(material);\n"
			"}\n"
			"#endif\n";

	return source.str();
}

void CompiledScene::CompileImageMaps() {
	SLG_LOG("Compile ImageMaps");

	imageMapDescs.resize(0);
	imageMapMemBlocks.resize(0);

	//--------------------------------------------------------------------------
	// Translate image maps
	//--------------------------------------------------------------------------

	const double tStart = WallClockTime();

	vector<const ImageMap *> ims;
	scene->imgMapCache.GetImageMaps(ims);

	imageMapDescs.resize(ims.size());
	for (u_int i = 0; i < ims.size(); ++i) {
		const ImageMap *im = ims[i];
		slg::ocl::ImageMap *imd = &imageMapDescs[i];

		const u_int pixelCount = im->GetWidth() * im->GetHeight();
		const u_int memSize = pixelCount * im->GetChannelCount() * sizeof(float);

		if (memSize > maxMemPageSize)
			throw runtime_error("An image map is too big to fit in a single block of memory");

		bool found = false;
		u_int page;
		for (u_int j = 0; j < imageMapMemBlocks.size(); ++j) {
			// Check if it fits in this page
			if (memSize + imageMapMemBlocks[j].size() * sizeof(float) <= maxMemPageSize) {
				found = true;
				page = j;
				break;
			}
		}

		if (!found) {
			// Check if I can add a new page
			if (imageMapMemBlocks.size() > 8)
				throw runtime_error("More than 8 blocks of memory are required for image maps");

			// Add a new page
			imageMapMemBlocks.push_back(vector<float>());
			page = imageMapMemBlocks.size() - 1;
		}

		imd->width = im->GetWidth();
		imd->height = im->GetHeight();
		imd->channelCount = im->GetChannelCount();
		imd->pageIndex = page;
		imd->pixelsIndex = (u_int)imageMapMemBlocks[page].size();
		imageMapMemBlocks[page].insert(imageMapMemBlocks[page].end(), im->GetPixels(),
				im->GetPixels() + pixelCount * im->GetChannelCount());
	}

	SLG_LOG("Image maps page count: " << imageMapMemBlocks.size());
	for (u_int i = 0; i < imageMapMemBlocks.size(); ++i)
		SLG_LOG(" RGB channel page " << i << " size: " << imageMapMemBlocks[i].size() * sizeof(float) / 1024 << "Kbytes");

	const double tEnd = WallClockTime();
	SLG_LOG("Texture maps compilation time: " << int((tEnd - tStart) * 1000.0) << "ms");
}

void CompiledScene::Recompile(const EditActionList &editActions) {
	if (editActions.Has(CAMERA_EDIT))
		CompileCamera();
	if (editActions.Has(GEOMETRY_EDIT))
		CompileGeometry();
	if (editActions.Has(MATERIALS_EDIT) || editActions.Has(MATERIAL_TYPES_EDIT))
		CompileMaterials();
	if (editActions.Has(LIGHTS_EDIT))
		CompileLights();
	if (editActions.Has(IMAGEMAPS_EDIT))
		CompileImageMaps();
}

bool CompiledScene::IsMaterialCompiled(const MaterialType type) const {
	return (usedMaterialTypes.find(type) != usedMaterialTypes.end());
}

bool CompiledScene::IsTextureCompiled(const TextureType type) const {
	return (usedTextureTypes.find(type) != usedTextureTypes.end());
}

bool CompiledScene::HasBumpMaps() const {
	return useBumpMapping;
}

bool CompiledScene::RequiresPassThrough() const {
	return (IsMaterialCompiled(GLASS) ||
			IsMaterialCompiled(ARCHGLASS) ||
			IsMaterialCompiled(MIX) ||
			IsMaterialCompiled(NULLMAT) ||
			IsMaterialCompiled(MATTETRANSLUCENT) ||
			IsMaterialCompiled(GLOSSY2) ||
			IsMaterialCompiled(ROUGHGLASS) ||
			IsMaterialCompiled(CARPAINT));
}

bool CompiledScene::HasVolumes() const {
	return IsMaterialCompiled(HOMOGENEOUS_VOL) ||
			IsMaterialCompiled(CLEAR_VOL) ||
			IsMaterialCompiled(HETEROGENEOUS_VOL) ||
			// Volume rendering may be required to evaluate the IOR
			IsMaterialCompiled(GLASS) ||
			IsMaterialCompiled(ARCHGLASS) ||
			IsMaterialCompiled(ROUGHGLASS);
}

#endif
