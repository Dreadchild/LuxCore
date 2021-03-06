################################################################################
# Copyright 1998-2018 by authors (see AUTHORS.txt)
#
#   This file is part of LuxCoreRender.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
################################################################################

#############################################################################
#############################################################################
#############################################################################
###
### SLG Library
###
#############################################################################
#############################################################################
#############################################################################

#############################################################################
#
# SLG Docs
#
#############################################################################

find_package(Doxygen)

if(DOXYGEN_FOUND)
	message(STATUS "Found Doxygen and generating SLG documentation")

	# Generate doxygen.template
	set(DOXYGEN_SLG_TEMPLATE ${CMAKE_CURRENT_SOURCE_DIR}/../../doxygen/slg.template)
	configure_file(
	  "${DOXYGEN_SLG_TEMPLATE}.in"
	  "${DOXYGEN_SLG_TEMPLATE}"
	  )

	set(DOXYGEN_SLG_INPUT ${CMAKE_CURRENT_BINARY_DIR}/../../doxygen-slg.conf)
	set(DOXYGEN_SLG_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/../../doc/slg)
	set(DOXYGEN_SLG_OUTPUT ${DOXYGEN_OUTPUT_DIR}/slg/html/index.html)

	message(STATUS "Doxygen SLG output: " ${DOXYGEN_SLG_OUTPUT})

	if(DOXYGEN_DOT_FOUND)
			message(STATUS "Found dot")
			set(DOXYGEN_DOT_CONF "HAVE_DOT = YES")
	endif(DOXYGEN_DOT_FOUND)

	add_custom_command(
		OUTPUT ${DOXYGEN_SLG_OUTPUT}
		# Creating custom doxygen-luxrays.conf
		COMMAND mkdir -p ${DOXYGEN_SLG_OUTPUT_DIR}
		COMMAND cp ${DOXYGEN_SLG_TEMPLATE} ${DOXYGEN_SLG_INPUT}
		COMMAND echo "INPUT = " ${CMAKE_CURRENT_SOURCE_DIR}/../../include/slg  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/slg >> ${DOXYGEN_SLG_INPUT}
		COMMAND echo "OUTPUT_DIRECTORY = " ${DOXYGEN_SLG_OUTPUT_DIR} >> ${DOXYGEN_SLG_INPUT}
		COMMAND echo ${DOXYGEN_DOT_CONF} >> ${DOXYGEN_SLG_INPUT}
		# Launch doxygen
		COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_SLG_INPUT}
		DEPENDS ${DOXYGEN_SLG_TEMPLATE}
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../..
	)

	add_custom_target(apidoc_slg DEPENDS ${DOXYGEN_SLG_OUTPUT})
endif(DOXYGEN_FOUND)

###########################################################################

link_directories (${LuxRays_LIB_DIR})

add_definitions(-DCPP_API_EXPORTS ${VISIBILITY_FLAGS}) # for controlling visibility

###########################################################################
#
# BCD files
#
###########################################################################

set(BCD_SRCS
	${LuxRays_SOURCE_DIR}/deps/bcd-1.1/src/core/Chronometer.cpp
	${LuxRays_SOURCE_DIR}/deps/bcd-1.1/src/core/CovarianceMatrix.cpp
	${LuxRays_SOURCE_DIR}/deps/bcd-1.1/src/core/Denoiser.cpp
	${LuxRays_SOURCE_DIR}/deps/bcd-1.1/src/core/DenoisingUnit.cpp
	${LuxRays_SOURCE_DIR}/deps/bcd-1.1/src/core/MultiscaleDenoiser.cpp
	${LuxRays_SOURCE_DIR}/deps/bcd-1.1/src/core/SamplesAccumulator.cpp
	${LuxRays_SOURCE_DIR}/deps/bcd-1.1/src/core/SpikeRemovalFilter.cpp
	${LuxRays_SOURCE_DIR}/deps/bcd-1.1/src/core/Utils.cpp
)

SOURCE_GROUP("Source Files\\BCD Library" FILES ${BCD_SRCS})	

include_directories(${LuxRays_SOURCE_DIR}/deps/eigen-3.3.4)
include_directories(${LuxRays_SOURCE_DIR}/deps/bcd-1.1/include)
include_directories(${LuxRays_SOURCE_DIR}/deps/bcd-1.1/include/bcd/core)

# Use only MPL licensed source code
add_definitions("-DEIGEN_MPL2_ONLY")

add_library(bcd STATIC ${BCD_SRCS})

###########################################################################
#
# OpenVDB files
#
###########################################################################

set(OPENVDB_SRCS
	${LuxRays_SOURCE_DIR}/deps/openvdb-3.1.0/openvdb/Grid.cc
	${LuxRays_SOURCE_DIR}/deps/openvdb-3.1.0/openvdb/Platform.cc
	${LuxRays_SOURCE_DIR}/deps/openvdb-3.1.0/openvdb/openvdb.cc
	${LuxRays_SOURCE_DIR}/deps/openvdb-3.1.0/openvdb/io/Archive.cc
	${LuxRays_SOURCE_DIR}/deps/openvdb-3.1.0/openvdb/io/Compression.cc
	${LuxRays_SOURCE_DIR}/deps/openvdb-3.1.0/openvdb/io/File.cc
	${LuxRays_SOURCE_DIR}/deps/openvdb-3.1.0/openvdb/io/GridDescriptor.cc
	${LuxRays_SOURCE_DIR}/deps/openvdb-3.1.0/openvdb/io/Queue.cc
	${LuxRays_SOURCE_DIR}/deps/openvdb-3.1.0/openvdb/io/Stream.cc
	${LuxRays_SOURCE_DIR}/deps/openvdb-3.1.0/openvdb/io/TempFile.cc
	${LuxRays_SOURCE_DIR}/deps/openvdb-3.1.0/openvdb/math/Maps.cc
	${LuxRays_SOURCE_DIR}/deps/openvdb-3.1.0/openvdb/math/Proximity.cc
	${LuxRays_SOURCE_DIR}/deps/openvdb-3.1.0/openvdb/math/QuantizedUnitVec.cc
	${LuxRays_SOURCE_DIR}/deps/openvdb-3.1.0/openvdb/math/Transform.cc
	${LuxRays_SOURCE_DIR}/deps/openvdb-3.1.0/openvdb/metadata/MetaMap.cc
	${LuxRays_SOURCE_DIR}/deps/openvdb-3.1.0/openvdb/metadata/Metadata.cc
	${LuxRays_SOURCE_DIR}/deps/openvdb-3.1.0/openvdb/util/Formats.cc
	${LuxRays_SOURCE_DIR}/deps/openvdb-3.1.0/openvdb/util/Util.cc
)

SOURCE_GROUP("Source Files\\OpenVDB Library" FILES ${OPENVDB_SRCS})	

include_directories(${LuxRays_SOURCE_DIR}/deps/openvdb-3.1.0)

# Required by OpenVDB to read ABI 3
add_definitions("-DOPENVDB_3_ABI_COMPATIBLE -DOPENVDB_STATICLIB -DOPENVDB_OPENEXR_STATICLIB")

add_library(openvdb STATIC ${OPENVDB_SRCS})

###########################################################################
#
# Kernel files
#
###########################################################################

set(SLG_KERNELS
	${LuxRays_SOURCE_DIR}/include/slg/cameras/camera_types.cl
	${LuxRays_SOURCE_DIR}/include/slg/cameras/camera_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/engines/pathoclbase/kernels/pathoclbase_datatypes.cl
	${LuxRays_SOURCE_DIR}/include/slg/engines/pathoclbase/kernels/pathoclbase_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/engines/pathoclbase/kernels/pathoclbase_kernels_micro.cl
	${LuxRays_SOURCE_DIR}/include/slg/film/film_types.cl
	${LuxRays_SOURCE_DIR}/include/slg/film/film_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/film/film_mergesamplebuffer_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/film/denoiser/filmdenoiser_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/film/imagepipeline/plugins/plugin_backgroundimg_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/film/imagepipeline/plugins/plugin_bloom_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/film/imagepipeline/plugins/plugin_cameraresponse_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/film/imagepipeline/plugins/plugin_coloraberration_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/film/imagepipeline/plugins/plugin_gammacorrection_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/film/imagepipeline/plugins/plugin_gaussianblur3x3_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/film/imagepipeline/plugins/plugin_objectidmask_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/film/imagepipeline/plugins/plugin_vignetting_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/film/imagepipeline/plugins/plugin_premultiplyalpha_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/film/imagepipeline/plugins/tonemaps/tonemap_reduce_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/film/imagepipeline/plugins/tonemaps/tonemap_autolinear_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/film/imagepipeline/plugins/tonemaps/tonemap_linear_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/film/imagepipeline/plugins/tonemaps/tonemap_luxlinear_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/film/imagepipeline/plugins/tonemaps/tonemap_reinhard02_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/film/sampleresult_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/film/filters/filter_types.cl
	${LuxRays_SOURCE_DIR}/include/slg/film/filters/filter_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/imagemap/imagemap_types.cl
	${LuxRays_SOURCE_DIR}/include/slg/imagemap/imagemap_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/lights/light_types.cl
	${LuxRays_SOURCE_DIR}/include/slg/lights/light_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/samplers/sampler_types.cl
	${LuxRays_SOURCE_DIR}/include/slg/samplers/sampler_random_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/samplers/sampler_sobol_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/samplers/sampler_metropolis_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/samplers/sampler_tilepath_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/bsdf/bsdf_types.cl
	${LuxRays_SOURCE_DIR}/include/slg/bsdf/bsdf_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/bsdf/bsdfutils_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/bsdf/hitpoint_types.cl
	${LuxRays_SOURCE_DIR}/include/slg/materials/material_types.cl
	${LuxRays_SOURCE_DIR}/include/slg/materials/materialdefs_funcs_generic.cl
	${LuxRays_SOURCE_DIR}/include/slg/materials/materialdefs_funcs_default.cl
	${LuxRays_SOURCE_DIR}/include/slg/materials/materialdefs_funcs_archglass.cl
	${LuxRays_SOURCE_DIR}/include/slg/materials/materialdefs_funcs_carpaint.cl
	${LuxRays_SOURCE_DIR}/include/slg/materials/materialdefs_funcs_clearvol.cl
	${LuxRays_SOURCE_DIR}/include/slg/materials/materialdefs_funcs_cloth.cl
	${LuxRays_SOURCE_DIR}/include/slg/materials/materialdefs_funcs_glass.cl
	${LuxRays_SOURCE_DIR}/include/slg/materials/materialdefs_funcs_glossy2.cl
	${LuxRays_SOURCE_DIR}/include/slg/materials/materialdefs_funcs_glossytranslucent.cl
	${LuxRays_SOURCE_DIR}/include/slg/materials/materialdefs_funcs_heterogeneousvol.cl
	${LuxRays_SOURCE_DIR}/include/slg/materials/materialdefs_funcs_homogeneousvol.cl
	${LuxRays_SOURCE_DIR}/include/slg/materials/materialdefs_funcs_matte.cl
	${LuxRays_SOURCE_DIR}/include/slg/materials/materialdefs_funcs_matte_translucent.cl
	${LuxRays_SOURCE_DIR}/include/slg/materials/materialdefs_funcs_metal2.cl
	${LuxRays_SOURCE_DIR}/include/slg/materials/materialdefs_funcs_mirror.cl
	${LuxRays_SOURCE_DIR}/include/slg/materials/materialdefs_funcs_null.cl
	${LuxRays_SOURCE_DIR}/include/slg/materials/materialdefs_funcs_roughglass.cl
	${LuxRays_SOURCE_DIR}/include/slg/materials/materialdefs_funcs_roughmatte_translucent.cl
	${LuxRays_SOURCE_DIR}/include/slg/materials/materialdefs_funcs_velvet.cl
	${LuxRays_SOURCE_DIR}/include/slg/materials/materialdefs_template_glossycoating.cl
	${LuxRays_SOURCE_DIR}/include/slg/materials/materialdefs_template_mix.cl
	${LuxRays_SOURCE_DIR}/include/slg/materials/material_main_withoutdynamic.cl
	${LuxRays_SOURCE_DIR}/include/slg/materials/material_main.cl
	${LuxRays_SOURCE_DIR}/include/slg/scene/scene_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/scene/sceneobject_types.cl
	${LuxRays_SOURCE_DIR}/include/slg/textures/mapping/mapping_types.cl
	${LuxRays_SOURCE_DIR}/include/slg/textures/mapping/mapping_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/textures/texture_types.cl
	${LuxRays_SOURCE_DIR}/include/slg/textures/texture_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/textures/texture_blender_noise_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/textures/texture_blender_noise_funcs2.cl
	${LuxRays_SOURCE_DIR}/include/slg/textures/texture_blender_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/textures/texture_bump_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/textures/texture_noise_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/textures/texture_abs_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/textures/texture_bilerp_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/textures/texture_blackbody_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/textures/texture_clamp_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/textures/texture_colordepth_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/textures/texture_densitygrid_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/textures/texture_hsv_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/textures/texture_irregulardata_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/textures/fresnel/texture_fresnelcolor_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/textures/fresnel/texture_fresnelconst_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/utils/varianceclamping_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/volumes/volume_types.cl
	${LuxRays_SOURCE_DIR}/include/slg/volumes/volume_funcs.cl
	${LuxRays_SOURCE_DIR}/include/slg/volumes/volumeinfo_funcs.cl
)

# Make sure some IDE won't try to compile CL by some built-in rule:
SET_SOURCE_FILES_PROPERTIES(${SLG_KERNELS} PROPERTIES HEADER_FILE_ONLY TRUE)

SOURCE_GROUP("Source Files\\SLG Kernels" FILES ${SLG_KERNELS})

#############################################################################
#
# SLG Core library
#
#############################################################################

# Note: the following order of object linking is important for GCC when building
# a static library (to avoid an undefined symbol reference with the linker)
set(SLG_CORE_SRCS
	${LuxRays_SOURCE_DIR}/src/slg/bsdf/bsdf.cpp
	${LuxRays_SOURCE_DIR}/src/slg/cameras/camera.cpp
	${LuxRays_SOURCE_DIR}/src/slg/cameras/environment.cpp
	${LuxRays_SOURCE_DIR}/src/slg/cameras/orthographic.cpp
	${LuxRays_SOURCE_DIR}/src/slg/cameras/perspective.cpp
	${LuxRays_SOURCE_DIR}/src/slg/cameras/projective.cpp
	${LuxRays_SOURCE_DIR}/src/slg/cameras/stereo.cpp
	${LuxRays_SOURCE_DIR}/src/slg/core/sdl.cpp
	${LuxRays_SOURCE_DIR}/src/slg/core/sphericalfunction/sphericalfunction.cpp
	${LuxRays_SOURCE_DIR}/src/slg/core/sphericalfunction/photometricdataies.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/renderengine.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/cpurenderengine.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/oclrenderengine.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/pathtracer.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/tilerepository.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/tilerepositoryserialize.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/bidircpu/bidircpu.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/bidircpu/bidircputhread.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/bidircpu/bidircpurenderstate.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/bidirvmcpu/bidirvmcpu.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/bidirvmcpu/bidirvmcputhread.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/bidirvmcpu/hashgrid.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/pathoclbase/compiledscene.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/pathoclbase/compilecamera.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/pathoclbase/compilegeometry.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/pathoclbase/compileimagemaps.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/pathoclbase/compilelights.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/pathoclbase/compilematerials.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/pathoclbase/compilesceneobjects.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/pathoclbase/compiletextures.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/pathoclbase/pathoclbase.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/pathoclbase/pathoclbasenativethread.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/pathoclbase/pathoclbaseoclthread.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/pathoclbase/pathoclbaseoclthreadfilm.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/pathoclbase/pathoclbaseoclthreadinit.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/pathoclbase/pathoclbaseoclthreadkernels.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/pathocl/pathocl.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/pathocl/pathoclrenderstate.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/pathocl/pathoclopenclthread.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/pathocl/pathoclnativethread.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/rtpathocl/rtpathocl.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/rtpathocl/rtpathoclthread.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/lightcpu/lightcpu.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/lightcpu/lightcpurenderstate.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/lightcpu/lightcputhread.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/pathcpu/pathcpu.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/pathcpu/pathcpurenderstate.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/pathcpu/pathcputhread.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/rtpathcpu/rtpathcpu.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/rtpathcpu/rtpathcputhread.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/filesaver/filesaver.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/tilepathcpu/tilepathcpu.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/tilepathcpu/tilepathcpurenderstate.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/tilepathcpu/tilepathcputhread.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/tilepathocl/tilepathocl.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/tilepathocl/tilepathoclrenderstate.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/tilepathocl/tilepathnativethread.cpp
	${LuxRays_SOURCE_DIR}/src/slg/engines/tilepathocl/tilepathoclthread.cpp
	${LuxRays_SOURCE_DIR}/src/slg/imagemap/imagemap.cpp
	${LuxRays_SOURCE_DIR}/src/slg/imagemap/imagemapcache.cpp
	${LuxRays_SOURCE_DIR}/src/slg/imagemap/imagemapcacheserialize.cpp
	${LuxRays_SOURCE_DIR}/src/slg/imagemap/imagemapserialize.cpp
	${LuxRays_SOURCE_DIR}/src/slg/lights/constantinfinitelight.cpp
	${LuxRays_SOURCE_DIR}/src/slg/lights/distantlight.cpp
	${LuxRays_SOURCE_DIR}/src/slg/lights/infinitelight.cpp
	${LuxRays_SOURCE_DIR}/src/slg/lights/laserlight.cpp
	${LuxRays_SOURCE_DIR}/src/slg/lights/light.cpp
	${LuxRays_SOURCE_DIR}/src/slg/lights/lightsourcedefs.cpp
	${LuxRays_SOURCE_DIR}/src/slg/lights/mapspherelight.cpp
	${LuxRays_SOURCE_DIR}/src/slg/lights/mappointlight.cpp
	${LuxRays_SOURCE_DIR}/src/slg/lights/pointlight.cpp
	${LuxRays_SOURCE_DIR}/src/slg/lights/projectionlight.cpp
	${LuxRays_SOURCE_DIR}/src/slg/lights/sharpdistantlight.cpp
	${LuxRays_SOURCE_DIR}/src/slg/lights/sky2light.cpp
	${LuxRays_SOURCE_DIR}/src/slg/lights/spherelight.cpp
	${LuxRays_SOURCE_DIR}/src/slg/lights/spotlight.cpp
	${LuxRays_SOURCE_DIR}/src/slg/lights/sunlight.cpp
	${LuxRays_SOURCE_DIR}/src/slg/lights/trianglelight.cpp
	${LuxRays_SOURCE_DIR}/src/slg/lights/strategies/dlscache.cpp
	${LuxRays_SOURCE_DIR}/src/slg/lights/strategies/dlscacheimpl/dlscacheimpl.cpp
	${LuxRays_SOURCE_DIR}/src/slg/lights/strategies/dlscacheimpl/dlscoctree.cpp
	${LuxRays_SOURCE_DIR}/src/slg/lights/strategies/distributionlightstrategy.cpp
	${LuxRays_SOURCE_DIR}/src/slg/lights/strategies/lightstrategy.cpp
	${LuxRays_SOURCE_DIR}/src/slg/lights/strategies/logpower.cpp
	${LuxRays_SOURCE_DIR}/src/slg/lights/strategies/power.cpp
	${LuxRays_SOURCE_DIR}/src/slg/lights/strategies/uniform.cpp
	${LuxRays_SOURCE_DIR}/src/slg/lights/visibility/envlightvisibility.cpp
	${LuxRays_SOURCE_DIR}/src/slg/materials/archglass.cpp
	${LuxRays_SOURCE_DIR}/src/slg/materials/carpaint.cpp
	${LuxRays_SOURCE_DIR}/src/slg/materials/cloth.cpp
	${LuxRays_SOURCE_DIR}/src/slg/materials/glass.cpp
	${LuxRays_SOURCE_DIR}/src/slg/materials/glossy2.cpp
	${LuxRays_SOURCE_DIR}/src/slg/materials/glossycoating.cpp
	${LuxRays_SOURCE_DIR}/src/slg/materials/glossytranslucent.cpp
	${LuxRays_SOURCE_DIR}/src/slg/materials/material.cpp
	${LuxRays_SOURCE_DIR}/src/slg/materials/materialdefs.cpp
	${LuxRays_SOURCE_DIR}/src/slg/materials/matte.cpp
	${LuxRays_SOURCE_DIR}/src/slg/materials/mattetranslucent.cpp
	${LuxRays_SOURCE_DIR}/src/slg/materials/metal2.cpp
	${LuxRays_SOURCE_DIR}/src/slg/materials/mirror.cpp
	${LuxRays_SOURCE_DIR}/src/slg/materials/mixmat.cpp
	${LuxRays_SOURCE_DIR}/src/slg/materials/null.cpp
	${LuxRays_SOURCE_DIR}/src/slg/materials/roughglass.cpp
	${LuxRays_SOURCE_DIR}/src/slg/materials/roughmatte.cpp
	${LuxRays_SOURCE_DIR}/src/slg/materials/roughmattetranslucent.cpp
	${LuxRays_SOURCE_DIR}/src/slg/materials/velvet.cpp
	${LuxRays_SOURCE_DIR}/src/slg/samplers/sampler.cpp
	${LuxRays_SOURCE_DIR}/src/slg/samplers/random.cpp
	${LuxRays_SOURCE_DIR}/src/slg/samplers/rtpathcpusampler.cpp
	${LuxRays_SOURCE_DIR}/src/slg/samplers/tilepathsampler.cpp
	${LuxRays_SOURCE_DIR}/src/slg/samplers/sobol.cpp
	${LuxRays_SOURCE_DIR}/src/slg/samplers/soboldata.cpp
	${LuxRays_SOURCE_DIR}/src/slg/samplers/sobolsequence.cpp
	${LuxRays_SOURCE_DIR}/src/slg/samplers/metropolis.cpp
	${LuxRays_SOURCE_DIR}/src/slg/scene/extmeshcache.cpp
	${LuxRays_SOURCE_DIR}/src/slg/scene/extmeshcacheserialize.cpp
	${LuxRays_SOURCE_DIR}/src/slg/scene/parsecamera.cpp
	${LuxRays_SOURCE_DIR}/src/slg/scene/parselights.cpp
	${LuxRays_SOURCE_DIR}/src/slg/scene/parsematerials.cpp
	${LuxRays_SOURCE_DIR}/src/slg/scene/parseobjects.cpp
	${LuxRays_SOURCE_DIR}/src/slg/scene/parseshapes.cpp
	${LuxRays_SOURCE_DIR}/src/slg/scene/parsetextures.cpp
	${LuxRays_SOURCE_DIR}/src/slg/scene/parsevolumes.cpp
	${LuxRays_SOURCE_DIR}/src/slg/scene/sceneobject.cpp
	${LuxRays_SOURCE_DIR}/src/slg/scene/sceneobjectdefs.cpp
	${LuxRays_SOURCE_DIR}/src/slg/scene/scene.cpp
	${LuxRays_SOURCE_DIR}/src/slg/scene/scenepreprocess.cpp
	${LuxRays_SOURCE_DIR}/src/slg/scene/sceneserialize.cpp
	${LuxRays_SOURCE_DIR}/src/slg/scene/sceneupdate.cpp
	${LuxRays_SOURCE_DIR}/src/slg/shapes/shape.cpp
	${LuxRays_SOURCE_DIR}/src/slg/shapes/meshshape.cpp
	${LuxRays_SOURCE_DIR}/src/slg/shapes/pointiness.cpp
	${LuxRays_SOURCE_DIR}/src/slg/shapes/strands.cpp
	${LuxRays_SOURCE_DIR}/src/slg/slg.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/mapping/mapping.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/abs.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/add.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/band.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/bilerp.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/blackbody.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/blender_noiselib.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/blender_texture.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/brick.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/checkerboard.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/clamp.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/colordepth.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/constfloat.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/constfloat3.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/cloud.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/dots.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/densitygrid.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/fbm.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/fresnelapprox.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/fresnel/fresnelcauchy.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/fresnel/fresnelcolor.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/fresnel/fresnelconst.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/fresnel/fresnelluxpop.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/fresnel/fresnelpreset.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/fresnel/fresnelsopra.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/fresnel/fresneltexture.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/hitpoint.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/hsv.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/imagemaptex.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/irregulardata.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/lampspectrum.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/marble.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/mixtex.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/normalmap.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/scale.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/subtract.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/texture.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/texturedefs.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/windy.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/wrinkled.cpp
	${LuxRays_SOURCE_DIR}/src/slg/textures/uv.cpp
	${LuxRays_SOURCE_DIR}/src/slg/utils/pathdepthinfo.cpp
	${LuxRays_SOURCE_DIR}/src/slg/utils/varianceclamping.cpp
	${LuxRays_SOURCE_DIR}/src/slg/volumes/clear.cpp
	${LuxRays_SOURCE_DIR}/src/slg/volumes/heterogenous.cpp
	${LuxRays_SOURCE_DIR}/src/slg/volumes/homogenous.cpp
	${LuxRays_SOURCE_DIR}/src/slg/volumes/volume.cpp
	${LuxRays_SOURCE_DIR}/src/slg/renderconfig.cpp
	${LuxRays_SOURCE_DIR}/src/slg/rendersession.cpp
	${LuxRays_SOURCE_DIR}/src/slg/renderstate.cpp
)
SOURCE_GROUP("Source Files\\SLG Core Library" FILES ${SLG_CORE_SRCS})	

add_library(slg-core STATIC ${SLG_CORE_SRCS})

#############################################################################
#
# SLG Kernels library
#
#############################################################################

set(generated_kernels_dir "${GENERATED_DIR}/slg-kernels")
file(MAKE_DIRECTORY ${generated_kernels_dir})

PreprocessOCLKernels(${generated_kernels_dir} slg ${SLG_KERNELS})

set(SLG_KERNEL_SRCS
	${generated_kernels_dir}/sampler_types_kernel.cpp
	${generated_kernels_dir}/sampler_random_funcs_kernel.cpp
	${generated_kernels_dir}/sampler_sobol_funcs_kernel.cpp
	${generated_kernels_dir}/sampler_metropolis_funcs_kernel.cpp
	${generated_kernels_dir}/sampler_tilepath_funcs_kernel.cpp
	${generated_kernels_dir}/film_types_kernel.cpp
	${generated_kernels_dir}/film_funcs_kernel.cpp
	${generated_kernels_dir}/film_mergesamplebuffer_funcs_kernel.cpp
	${generated_kernels_dir}/filmdenoiser_funcs_kernel.cpp
	${generated_kernels_dir}/plugin_backgroundimg_funcs_kernel.cpp
	${generated_kernels_dir}/plugin_bloom_funcs_kernel.cpp
	${generated_kernels_dir}/plugin_cameraresponse_funcs_kernel.cpp
	${generated_kernels_dir}/plugin_coloraberration_funcs_kernel.cpp
	${generated_kernels_dir}/plugin_gammacorrection_funcs_kernel.cpp
	${generated_kernels_dir}/plugin_gaussianblur3x3_funcs_kernel.cpp
	${generated_kernels_dir}/plugin_objectidmask_funcs_kernel.cpp
	${generated_kernels_dir}/plugin_vignetting_funcs_kernel.cpp
	${generated_kernels_dir}/plugin_premultiplyalpha_funcs_kernel.cpp
	${generated_kernels_dir}/tonemap_reduce_funcs_kernel.cpp
	${generated_kernels_dir}/tonemap_autolinear_funcs_kernel.cpp
	${generated_kernels_dir}/tonemap_linear_funcs_kernel.cpp
	${generated_kernels_dir}/tonemap_luxlinear_funcs_kernel.cpp
	${generated_kernels_dir}/tonemap_reinhard02_funcs_kernel.cpp
	${generated_kernels_dir}/sampleresult_funcs_kernel.cpp
	${generated_kernels_dir}/filter_types_kernel.cpp
	${generated_kernels_dir}/filter_funcs_kernel.cpp
	${generated_kernels_dir}/camera_types_kernel.cpp
	${generated_kernels_dir}/camera_funcs_kernel.cpp
	${generated_kernels_dir}/bsdf_types_kernel.cpp
	${generated_kernels_dir}/bsdf_funcs_kernel.cpp
	${generated_kernels_dir}/bsdfutils_funcs_kernel.cpp
	${generated_kernels_dir}/imagemap_types_kernel.cpp
	${generated_kernels_dir}/imagemap_funcs_kernel.cpp
	${generated_kernels_dir}/material_types_kernel.cpp
	${generated_kernels_dir}/materialdefs_funcs_generic_kernel.cpp
	${generated_kernels_dir}/materialdefs_funcs_default_kernel.cpp
	${generated_kernels_dir}/materialdefs_funcs_archglass_kernel.cpp
	${generated_kernels_dir}/materialdefs_funcs_carpaint_kernel.cpp
	${generated_kernels_dir}/materialdefs_funcs_clearvol_kernel.cpp
	${generated_kernels_dir}/materialdefs_funcs_cloth_kernel.cpp
	${generated_kernels_dir}/materialdefs_funcs_glass_kernel.cpp
	${generated_kernels_dir}/materialdefs_funcs_glossy2_kernel.cpp
	${generated_kernels_dir}/materialdefs_funcs_glossytranslucent_kernel.cpp
	${generated_kernels_dir}/materialdefs_funcs_heterogeneousvol_kernel.cpp
	${generated_kernels_dir}/materialdefs_funcs_homogeneousvol_kernel.cpp
	${generated_kernels_dir}/materialdefs_funcs_matte_kernel.cpp
	${generated_kernels_dir}/materialdefs_funcs_matte_translucent_kernel.cpp
	${generated_kernels_dir}/materialdefs_funcs_metal2_kernel.cpp
	${generated_kernels_dir}/materialdefs_funcs_mirror_kernel.cpp
	${generated_kernels_dir}/materialdefs_funcs_null_kernel.cpp
	${generated_kernels_dir}/materialdefs_funcs_roughglass_kernel.cpp
	${generated_kernels_dir}/materialdefs_funcs_roughmatte_translucent_kernel.cpp
	${generated_kernels_dir}/materialdefs_funcs_velvet_kernel.cpp
	${generated_kernels_dir}/materialdefs_template_glossycoating_kernel.cpp
	${generated_kernels_dir}/materialdefs_template_mix_kernel.cpp
	${generated_kernels_dir}/material_main_withoutdynamic_kernel.cpp
	${generated_kernels_dir}/material_main_kernel.cpp
	${generated_kernels_dir}/texture_types_kernel.cpp
	${generated_kernels_dir}/texture_funcs_kernel.cpp
	${generated_kernels_dir}/texture_blender_noise_funcs_kernel.cpp
	${generated_kernels_dir}/texture_blender_noise_funcs2_kernel.cpp
	${generated_kernels_dir}/texture_blender_funcs_kernel.cpp
	${generated_kernels_dir}/texture_bump_funcs_kernel.cpp
	${generated_kernels_dir}/texture_noise_funcs_kernel.cpp
	${generated_kernels_dir}/texture_abs_funcs_kernel.cpp
	${generated_kernels_dir}/texture_bilerp_funcs_kernel.cpp
	${generated_kernels_dir}/texture_blackbody_funcs_kernel.cpp
	${generated_kernels_dir}/texture_clamp_funcs_kernel.cpp
	${generated_kernels_dir}/texture_colordepth_funcs_kernel.cpp
	${generated_kernels_dir}/texture_densitygrid_funcs_kernel.cpp
	${generated_kernels_dir}/texture_fresnelcolor_funcs_kernel.cpp
	${generated_kernels_dir}/texture_fresnelconst_funcs_kernel.cpp
	${generated_kernels_dir}/texture_hsv_funcs_kernel.cpp
	${generated_kernels_dir}/texture_irregulardata_funcs_kernel.cpp
	${generated_kernels_dir}/varianceclamping_funcs_kernel.cpp
	${generated_kernels_dir}/volume_types_kernel.cpp
	${generated_kernels_dir}/volume_funcs_kernel.cpp
	${generated_kernels_dir}/volumeinfo_funcs_kernel.cpp
	${generated_kernels_dir}/light_types_kernel.cpp
	${generated_kernels_dir}/light_funcs_kernel.cpp
	${generated_kernels_dir}/scene_funcs_kernel.cpp
	${generated_kernels_dir}/sceneobject_types_kernel.cpp
	${generated_kernels_dir}/mapping_types_kernel.cpp
	${generated_kernels_dir}/mapping_funcs_kernel.cpp
	${generated_kernels_dir}/hitpoint_types_kernel.cpp
	${generated_kernels_dir}/pathoclbase_datatypes_kernel.cpp
	${generated_kernels_dir}/pathoclbase_funcs_kernel.cpp
	${generated_kernels_dir}/pathoclbase_kernels_micro_kernel.cpp
)

SOURCE_GROUP("Source Files\\SLG Kernels\\Generated" FILES ${SLG_KERNEL_SRCS})

add_library(slg-kernels STATIC ${SLG_KERNELS} ${SLG_KERNEL_SRCS})

#############################################################################
#
# SLG Film library
#
#############################################################################

set(SLG_FILM_SRCS
	${LuxRays_SOURCE_DIR}/src/slg/film/film.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/filmaddsample.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/filmchannels.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/filmimagepipeline.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/filmimagepipelineocl.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/filmoutput.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/filmoutputs.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/filmparse.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/filmsamplesplatter.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/filmserialize.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/filmproperties.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/sampleresult.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/convtest/filmconvtest.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/denoiser/filmdenoiser.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/denoiser/samplesaccumulator.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/filters/filter.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/filters/filterdistribution.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/filters/none.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/filters/box.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/filters/gaussian.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/filters/mitchell.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/filters/mitchellss.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/filters/blackmanharris.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/imagepipeline/imagepipeline.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/imagepipeline/imagepipelineserialization.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/imagepipeline/radiancechannelscale.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/imagepipeline/radiancechannelscaleserialization.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/imagepipeline/plugins/backgroundimg.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/imagepipeline/plugins/bcddenoiser.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/imagepipeline/plugins/bloom.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/imagepipeline/plugins/cameraresponse.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/imagepipeline/plugins/coloraberration.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/imagepipeline/plugins/contourlines.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/imagepipeline/plugins/gammacorrection.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/imagepipeline/plugins/gaussianblur3x3.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/imagepipeline/plugins/mist.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/imagepipeline/plugins/nop.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/imagepipeline/plugins/objectidmask.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/imagepipeline/plugins/outputswitcher.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/imagepipeline/plugins/vignetting.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/imagepipeline/plugins/premultiplyalpha.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/imagepipeline/plugins/tonemaps/autolinear.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/imagepipeline/plugins/tonemaps/linear.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/imagepipeline/plugins/tonemaps/luxlinear.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/imagepipeline/plugins/tonemaps/reinhard02.cpp
	${LuxRays_SOURCE_DIR}/src/slg/film/imagepipeline/plugins/tonemaps/tonemap.cpp
)
SOURCE_GROUP("Source Files\\SLG Film Library" FILES ${SLG_FILM_SRCS})	

add_library(slg-film STATIC ${SLG_FILM_SRCS})

###########################################################################
#
# Predefines
#
###########################################################################

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "../lib")
