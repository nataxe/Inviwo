/*********************************************************************************
 *
 * Inviwo - Interactive Visualization Workshop
 * Version 0.6b
 *
 * Copyright (c) 2013-2014 Inviwo Foundation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Main file author: Erik Sund�n
 *
 *********************************************************************************/

#include "lightvolumegl.h"
#include <modules/opengl/volume/volumegl.h>
#include <modules/opengl/glwrap/framebufferobject.h>
#include <modules/opengl/glwrap/shader.h>
#include <modules/opengl/image/layergl.h>
#include <modules/opengl/textureutils.h>
#include <inviwo/core/datastructures/light/pointlight.h>
#include <inviwo/core/datastructures/light/directionallight.h>

namespace inviwo {

ProcessorClassIdentifier(LightVolumeGL, "org.inviwo.LightVolumeGL");
ProcessorDisplayName(LightVolumeGL,  "Light Volume");
ProcessorTags(LightVolumeGL, Tags::GL);
ProcessorCategory(LightVolumeGL, "Illumination");
ProcessorCodeState(LightVolumeGL, CODE_STATE_EXPERIMENTAL);

GLfloat borderColor_[4] = {
    1.f, 1.f, 1.f, 1.f
};

static const int faceAxis_[6] = {
    0, 0, 1, 1, 2, 2
};

static const vec3 faceCenters_[6] = {
    vec3(1.f, 0.5f, 0.5f),
    vec3(0.f, 0.5f, 0.5f),
    vec3(0.5f, 1.f, 0.5f),
    vec3(0.5f, 0.f, 0.5f),
    vec3(0.5f, 0.5f, 1.f),
    vec3(0.5f, 0.5f, 0.f)
};

static const vec3 faceNormals_[6] = {
    vec3(1.f, 0.f, 0.f),
    vec3(-1.f, 0.f, 0.f),
    vec3(0.f, 1.f, 0.f),
    vec3(0.f, -1.f, 0.f),
    vec3(0.f, 0.f, 1.f),
    vec3(0.f, 0.f, -1.f)
};

//Defines permutation axis based on face index for chosen propagation axis.
inline void definePermutationMatrices(mat4& permMat, mat4& permLightMat, int permFaceIndex) {
    permMat = mat4(0.f);
    permMat[3][3] = 1.f;

    //Permutation of x and y
    switch (faceAxis_[permFaceIndex]) {
        case 0:
            permMat[0][2] = 1.f;
            permMat[1][1] = 1.f;
            break;

        case 1:
            permMat[0][0] = 1.f;
            permMat[1][2] = 1.f;
            break;

        case 2:
            permMat[0][0] = 1.f;
            permMat[1][1] = 1.f;
            break;
    }

    permLightMat = permMat;
    //Permutation of z
    float closestAxisZ = (faceNormals_[permFaceIndex])[faceAxis_[permFaceIndex]];
    permMat[2][faceAxis_[permFaceIndex]] = closestAxisZ;
    permLightMat[2][faceAxis_[permFaceIndex]] = glm::abs<float>(closestAxisZ);

    //For reverse axis-aligned
    if (closestAxisZ < 0.f) {
        permMat[3][faceAxis_[permFaceIndex]] = 1.f;
    }
}

LightVolumeGL::LightVolumeGL()
    : Processor(),
      inport_("inport"),
      outport_("outport"),
      lightSource_("lightSource"),
      supportColoredLight_("supportColoredLight", "Support Light Color", false),
      volumeSizeOption_("volumeSizeOption", "Light Volume Size"),
      transferFunction_("transferFunction", "Transfer function", TransferFunction(), &inport_),
      floatPrecision_("floatPrecision", "Float Precision", false),
      propagationShader_(NULL),
      mergeShader_(NULL),
      mergeFBO_(NULL),
      internalVolumesInvalid_(false),
      volumeDimOut_(uvec3(0)),
      lightDir_(vec3(0.f)),
      lightPos_(vec3(0.f)),
      lightColor_(vec4(1.f)),
      calculatedOnes_(false)
{
    addPort(inport_);
    addPort(outport_);
    addPort(lightSource_);
    supportColoredLight_.onChange(this, &LightVolumeGL::supportColoredLightChanged);
    addProperty(supportColoredLight_);
    volumeSizeOption_.addOption("1","Full of incoming volume", 1);
    volumeSizeOption_.addOption("1/2","Half of incoming volume", 2);
    volumeSizeOption_.addOption("1/4","Quarter of incoming volume", 4);
    volumeSizeOption_.setSelectedIndex(1);
    volumeSizeOption_.setCurrentStateAsDefault();
    volumeSizeOption_.onChange(this, &LightVolumeGL::volumeSizeOptionChanged);
    addProperty(volumeSizeOption_);
    addProperty(transferFunction_);
    floatPrecision_.onChange(this, &LightVolumeGL::floatPrecisionChanged);
    addProperty(floatPrecision_);
}

LightVolumeGL::~LightVolumeGL() {
}

void LightVolumeGL::initialize() {
    Processor::initialize();

    propagationShader_ = new Shader("lighting/lightpropagation.vert", "lighting/lightpropagation.geom", "lighting/lightpropagation.frag", true);
    mergeShader_ = new Shader("lighting/lightvolumeblend.vert", "lighting/lightvolumeblend.geom", "lighting/lightvolumeblend.frag", true);

    for (int i=0; i<2; ++i) {
        propParams_[i].fbo = new FrameBufferObject();
    }

    mergeFBO_ = new FrameBufferObject();
    supportColoredLightChanged();
}

void LightVolumeGL::deinitialize() {
    delete propagationShader_;
    propagationShader_ = NULL;
    delete mergeShader_;
    mergeShader_ = NULL;

    for (int i=0; i<2; ++i) {
        delete propParams_[i].fbo;
        propParams_[i].fbo = NULL;
        delete propParams_[i].vol;
        propParams_[i].vol = NULL;
    }

    delete mergeFBO_;
    mergeFBO_ = NULL;
    
    Processor::deinitialize();
}

void LightVolumeGL::propagation3DTextureParameterFunction(Texture*) {
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_BORDER);
    borderColorTextureParameterFunction();
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

void LightVolumeGL::process() {
    bool lightColorChanged = false;

    if (lightSource_.isChanged()) {
        lightColorChanged = lightSourceChanged();
    }

    bool reattach = false;

    if (internalVolumesInvalid_ || lightColorChanged || inport_.isChanged()) {
        reattach = volumeChanged(lightColorChanged);
    }

    VolumeGL* outVolumeGL = outport_.getData()->getEditableRepresentation<VolumeGL>();
    TextureUnit volUnit;
    const VolumeGL* inVolumeGL = inport_.getData()->getRepresentation<VolumeGL>();
    inVolumeGL->bindTexture(volUnit.getEnum());
    TextureUnit transFuncUnit;
    const Layer* tfLayer = transferFunction_.get().getData();
    const LayerGL* transferFunctionGL = tfLayer->getRepresentation<LayerGL>();
    transferFunctionGL->bindTexture(transFuncUnit.getEnum());
    TextureUnit lightVolUnit[2];
    propParams_[0].vol->bindTexture(lightVolUnit[0].getEnum());
    propParams_[1].vol->bindTexture(lightVolUnit[1].getEnum());
    propagationShader_->activate();
    propagationShader_->setUniform("volume_", volUnit.getUnitNumber());
    inVolumeGL->setVolumeUniforms(inport_.getData(), propagationShader_, "volumeParameters_");
    propagationShader_->setUniform("transferFunc_", transFuncUnit.getUnitNumber());
    propagationShader_->setUniform("lightVolumeParameters_.dimensions_", volumeDimOutF_);
    propagationShader_->setUniform("lightVolumeParameters_.dimensionsRCP_", volumeDimOutFRCP_);

    BufferObjectArray* rectArray = util::glEnableImagePlaneRect();

    //Perform propagation passes
    for (int i=0; i<2; ++i) {
        propParams_[i].fbo->activate();
        glViewport(0, 0, volumeDimOut_.x, volumeDimOut_.y);

        if (reattach)
            propParams_[i].fbo->attachColorTexture(propParams_[i].vol->getTexture(), 0);

        propagationShader_->setUniform("lightVolume_", lightVolUnit[i].getUnitNumber());
        propagationShader_->setUniform("permutationMatrix_", propParams_[i].axisPermutation);

        if (lightSource_.getData()->getLightSourceType() == LightSourceType::LIGHT_POINT) {
            propagationShader_->setUniform("pointLight_", true);
            propagationShader_->setUniform("lightPos_", lightPos_);
            propagationShader_->setUniform("permutedLightMatrix_", propParams_[i].axisPermutationLight);
        }
        else {
            propagationShader_->setUniform("pointLight_", false);
            propagationShader_->setUniform("permutedLightDirection_", propParams_[i].permutedLightDirection);
        }

        for (unsigned int z=0; z<volumeDimOut_.z; ++z) {
            glFramebufferTexture3DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_3D, propParams_[i].vol->getTexture()->getID(), 0, z);
            propagationShader_->setUniform("sliceNum_", static_cast<GLint>(z));
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glFlush();
        }

        propParams_[i].fbo->deactivate();
    }

    util::glDisableImagePlaneRect(rectArray);

    propagationShader_->deactivate();
    mergeShader_->activate();
    mergeShader_->setUniform("lightVolume_", lightVolUnit[0].getUnitNumber());
    mergeShader_->setUniform("lightVolumeSec_", lightVolUnit[1].getUnitNumber());
    mergeShader_->setUniform("lightVolumeParameters_.dimensions_", volumeDimOutF_);
    mergeShader_->setUniform("lightVolumeParameters_.dimensionsRCP_", volumeDimOutFRCP_);
    mergeShader_->setUniform("permMatInv_", propParams_[0].axisPermutationINV);
    mergeShader_->setUniform("permMatInvSec_", propParams_[1].axisPermutationINV);
    mergeShader_->setUniform("blendingFactor_", blendingFactor_);
    //Perform merge pass
    mergeFBO_->activate();
    glViewport(0, 0, volumeDimOut_.x, volumeDimOut_.y);

    if (reattach)
        mergeFBO_->attachColorTexture(outVolumeGL->getTexture(), 0);

    util::glMultiDrawImagePlaneRect(static_cast<int>(volumeDimOut_.z));
    mergeShader_->deactivate();
    mergeFBO_->deactivate();
}

bool LightVolumeGL::lightSourceChanged() {
    vec3 color = vec3(1.f);
    vec3 directionToCenterOfVolume = vec3(0.f);

    switch (lightSource_.getData()->getLightSourceType())
    {
        case LightSourceType::LIGHT_DIRECTIONAL: {
            if (lightType_ != LightSourceType::LIGHT_DIRECTIONAL) {
                lightType_ = LightSourceType::LIGHT_DIRECTIONAL;
                propagationShader_->getFragmentShaderObject()->removeShaderDefine("POINT_LIGHT");
                propagationShader_->getFragmentShaderObject()->build();
                propagationShader_->link();
            }

            const DirectionalLight* directionLight = dynamic_cast<const DirectionalLight*>(lightSource_.getData());

            if (directionLight) {
                directionToCenterOfVolume = directionLight->getDirection();
                color = directionLight->getIntensity();
            }

            break;
        }

        case LightSourceType::LIGHT_POINT: {
            if (lightType_ != LightSourceType::LIGHT_POINT) {
                lightType_ = LightSourceType::LIGHT_POINT;
                propagationShader_->getFragmentShaderObject()->addShaderDefine("POINT_LIGHT");
                propagationShader_->getFragmentShaderObject()->build();
                propagationShader_->link();
            }

            const PointLight* pointLight = dynamic_cast<const PointLight*>(lightSource_.getData());

            if (pointLight) {
                mat4 toWorld = inport_.getData()->getWorldTransform();
                vec4 volumeCenterWorldW = toWorld * vec4(0.f, 0.f, 0.f, 1.f);
                vec3 volumeCenterWorld = volumeCenterWorldW.xyz();
                lightPos_ = pointLight->getPosition();
                directionToCenterOfVolume = glm::normalize(volumeCenterWorld - lightPos_);
                color = pointLight->getIntensity();
            }

            break;
        }

        default:
            LogWarn("Light source not supported, can only handle Directional or Point Light");
            break;
    }

    updatePermuationMatrices(directionToCenterOfVolume, &propParams_[0], &propParams_[1]);
    bool lightColorChanged = false;

    if (color.r != lightColor_.r) {
        lightColor_.r = color.r;
        lightColorChanged = true;
    }

    if (color.g != lightColor_.g) {
        lightColor_.g = color.g;
        lightColorChanged = true;
    }

    if (color.b != lightColor_.b) {
        lightColor_.b = color.b;
        lightColorChanged = true;
    }

    return lightColorChanged;
}

bool LightVolumeGL::volumeChanged(bool lightColorChanged) {
    const Volume* input = inport_.getData();
    glm::uvec3 inDim = input->getDimension();
    glm::uvec3 outDim = uvec3(inDim.x/volumeSizeOption_.get(), inDim.y/volumeSizeOption_.get(), inDim.z/volumeSizeOption_.get());

    if (internalVolumesInvalid_ || (volumeDimOut_ != outDim)) {
        volumeDimOut_ = outDim;
        volumeDimOutF_ = vec3(volumeDimOut_);
        volumeDimOutFRCP_ = vec3(1.0f)/volumeDimOutF_;
        volumeDimInF_ = vec3(inDim);
        volumeDimInFRCP_ = vec3(1.0f)/volumeDimInF_;
        const DataFormatBase* format;

        if (supportColoredLight_.get()){
            if(floatPrecision_.get())
                format = DataVec4FLOAT32::get();
            else
                format = DataVec4UINT8::get();
        }
        else{
            if(floatPrecision_.get())
                format = DataFLOAT32::get();
            else
                format = DataUINT8::get();
        }

        for (int i=0; i<2; ++i) {
            if (!propParams_[i].vol || propParams_[i].vol->getDataFormat() != format) {
                if (propParams_[i].vol)
                    delete propParams_[i].vol;

                propParams_[i].vol = new VolumeGL(volumeDimOut_, format);
                propParams_[i].vol->getTexture()->setTextureParameterFunction(this, &LightVolumeGL::propagation3DTextureParameterFunction);
                propParams_[i].vol->getTexture()->initialize(NULL);
            }
            else {
                propParams_[i].vol->setDimension(volumeDimOut_);
            }
        }

        outport_.setData(NULL);
        VolumeGL* volumeGL = new VolumeGL(volumeDimOut_, format);
        volumeGL->getTexture()->initialize(NULL);
        outport_.setData(new Volume(volumeGL));

        internalVolumesInvalid_ = false;
        return true;
    }
    else if (lightColorChanged) {
        for (int i=0; i<2; ++i) {
            propParams_[i].vol->getTexture()->bind();
            borderColorTextureParameterFunction();
            propParams_[i].vol->getTexture()->unbind();
        }
    }

    return false;
}

void LightVolumeGL::volumeSizeOptionChanged() {
    if (inport_.hasData()) {
        if ((inport_.getData()->getDimension()/uvec3(volumeSizeOption_.get())) != volumeDimOut_) {
            internalVolumesInvalid_ = true;
        }
    }
}

void LightVolumeGL::supportColoredLightChanged() {
    if (propagationShader_) {
        if (supportColoredLight_.get())
            propagationShader_->getFragmentShaderObject()->addShaderDefine("SUPPORT_LIGHT_COLOR");
        else
            propagationShader_->getFragmentShaderObject()->removeShaderDefine("SUPPORT_LIGHT_COLOR");

        propagationShader_->getFragmentShaderObject()->build();
        propagationShader_->link();
    }

    if (outport_.hasData()) {
        int components = outport_.getData()->getDataFormat()->getComponents();

        if ((components < 3 && supportColoredLight_.get()) || (components > 1 && !supportColoredLight_.get()))
            internalVolumesInvalid_ = true;
    }
}

void LightVolumeGL::floatPrecisionChanged() {
    internalVolumesInvalid_ = true;
}

void LightVolumeGL::borderColorTextureParameterFunction() {
    if (supportColoredLight_.get())
        glTexParameterfv(GL_TEXTURE_3D, GL_TEXTURE_BORDER_COLOR, glm::value_ptr(lightColor_));
    else
        glTexParameterfv(GL_TEXTURE_3D, GL_TEXTURE_BORDER_COLOR, borderColor_);
}

void LightVolumeGL::updatePermuationMatrices(const vec3& lightDir, PropagationParameters* closest, PropagationParameters* secondClosest) {
    if (calculatedOnes_ && lightDir==lightDir_)
        return;

    lightDir_ = lightDir;
    vec3 invertedLightPos = -lightDir;
    vec3 invertedLightPosNorm = glm::normalize(invertedLightPos);
    //Calculate closest and second closest axis-aligned face.
    float thisVal = glm::dot(faceNormals_[0], invertedLightPosNorm - faceCenters_[0]);
    float closestVal = thisVal;
    float secondClosestVal = 0.f;
    int closestFace = 0;
    int secondClosestFace = -1;

    for (int i=1; i<6; ++i) {
        thisVal = glm::dot(faceNormals_[i], invertedLightPosNorm - faceCenters_[i]);

        if (thisVal < closestVal) {
            secondClosestVal = closestVal;
            secondClosestFace = closestFace;
            closestVal = thisVal;
            closestFace = i;
        }
        else if (thisVal < secondClosestVal) {
            secondClosestVal = thisVal;
            secondClosestFace = i;
        }
    }

    vec4 tmpLightDir = vec4(lightDir.x, lightDir.y, lightDir.z, 1.f);
    //Perform permutation calculation for closest face
    definePermutationMatrices(closest->axisPermutation, closest->axisPermutationLight, closestFace);
    //LogInfo("1st Axis Permutation: " << closest->axisPermutation);
    closest->axisPermutationINV = glm::inverse(closest->axisPermutation);
    closest->permutedLightDirection = closest->axisPermutationLight * tmpLightDir;
    //Perform permutation calculation for second closest face
    definePermutationMatrices(secondClosest->axisPermutation, secondClosest->axisPermutationLight, secondClosestFace);
    //LogInfo("2nd Axis Permutation: " << secondClosest->axisPermutation);
    secondClosest->axisPermutationINV = glm::inverse(secondClosest->axisPermutation);
    secondClosest->permutedLightDirection = secondClosest->axisPermutationLight * tmpLightDir;
    //Calculate the blending factor
    blendingFactor_ = static_cast<float>(1.f-(2.f*glm::acos<float>(glm::dot(invertedLightPosNorm, -faceNormals_[closestFace]))/M_PI));
    //LogInfo("Blending Factor: " << blendingFactor_);
    calculatedOnes_ = true;
}
 
LightVolumeGL::PropagationParameters::~PropagationParameters() { 
    delete fbo; 
    delete vol;
}

} // namespace
