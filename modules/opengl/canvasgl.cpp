#include "canvasgl.h"
#include <modules/opengl/glwrap/textureunit.h>
#include <modules/opengl/geometry/meshgl.h>

namespace inviwo {

bool CanvasGL::glewInitialized_ = false;
GLuint CanvasGL::screenAlignedVerticesId_ = 0;
GLuint CanvasGL::screenAlignedTexCoordsId_ = 1;

CanvasGL::CanvasGL(uvec2 dimensions)
    : Canvas(dimensions) {
    imageGL_ = NULL;
    shader_ = NULL;
    noiseShader_ = NULL;
    if(!screenAlignedRect_){
        shared = false;
        Position2dBuffer* vertices_ = new Position2dBuffer();
        Position2dBufferRAM* vertices = vertices_->getEditableRepresentation<Position2dBufferRAM>();
        vertices->add(vec2(-1.0f, -1.0f));
        vertices->add(vec2(1.0f, -1.0f));
        vertices->add(vec2(-1.0f, 1.0f));
        vertices->add(vec2(1.0f, 1.0f));
        TexCoord2dBuffer* texCoords_ = new TexCoord2dBuffer();
        TexCoord2dBufferRAM* texCoords = texCoords_->getEditableRepresentation<TexCoord2dBufferRAM>();
        texCoords->add(vec2(0.0f, 0.0f));
        texCoords->add(vec2(1.0f, 0.0f));
        texCoords->add(vec2(0.0f, 1.0f));
        texCoords->add(vec2(1.0f, 1.0f));
        Mesh* screenAlignedRectMesh = new Mesh(Mesh::TRIANGLES, Mesh::STRIP);
        screenAlignedRectMesh->addAttribute(vertices_);
        screenAlignedRectMesh->addAttribute(texCoords_);
        screenAlignedRect_ = screenAlignedRectMesh;
    }
}

CanvasGL::~CanvasGL() {}

void CanvasGL::initialize() {
    glShadeModel(GL_SMOOTH);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClearDepth(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_COLOR_MATERIAL);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
    LGL_ERROR;
    shader_ = new Shader("img_texturequad.frag");
    LGL_ERROR;
    noiseShader_ = new Shader("img_noise.frag");
    LGL_ERROR;
}

void CanvasGL::initializeGL() {
    if (!glewInitialized_) {
        LogInfo("Initializing GLEW");
        glewInit();
        LGL_ERROR;
        glewInitialized_ = true;
    }
}

void CanvasGL::initializeSquare() {
    const Mesh* screenAlignedRectMesh = dynamic_cast<const Mesh*>(screenAlignedRect_);
    if (screenAlignedRectMesh) {
        screenAlignedVerticesId_ = screenAlignedRectMesh->getAttributes(0)->getRepresentation<BufferGL>()->getId();
        screenAlignedTexCoordsId_ = screenAlignedRectMesh->getAttributes(1)->getRepresentation<BufferGL>()->getId();
        LGL_ERROR;
    }
}

void CanvasGL::deinitialize() {
    delete shader_;
    shader_ = NULL;
    delete noiseShader_;
    noiseShader_ = NULL;
}

void CanvasGL::activate() {}

void CanvasGL::render(const Image* image, ImageLayerType layer){
    if (image) {
        imageGL_ = image->getRepresentation<ImageGL>();
        pickingContainer_->setPickingSource(image);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        switch(layer){
            case COLOR_LAYER:
                renderColor();
                break;
            case DEPTH_LAYER:
                renderDepth();
                break;
            case PICKING_LAYER:
                renderPicking();
                break;
            default:
                renderNoise();
        }
        glDisable(GL_BLEND);
        //Pre-download picking image
        imageGL_->getPickingTexture()->downloadToPBO();
    } else {
        imageGL_ = NULL;
        renderNoise();
    }
}

void CanvasGL::repaint() {
    update();
}

void CanvasGL::resize(uvec2 size) {
    Canvas::resize(size);
    glViewport(0, 0, size[0], size[1]);
}

void CanvasGL::update() {
    Canvas::update();
    if (imageGL_) {
        renderColor();
    } else {
        renderNoise();
    }
}

void CanvasGL::renderColor() {
    TextureUnit textureUnit;
    imageGL_->bindColorTexture(textureUnit.getEnum());
    renderTexture(textureUnit.getUnitNumber());
    imageGL_->unbindColorTexture();
}

void CanvasGL::renderDepth() {
    TextureUnit textureUnit;
    imageGL_->bindDepthTexture(textureUnit.getEnum());
    renderTexture(textureUnit.getUnitNumber());
    imageGL_->unbindDepthTexture();
}

void CanvasGL::renderPicking() {
    TextureUnit textureUnit;
    imageGL_->bindPickingTexture(textureUnit.getEnum());
    renderTexture(textureUnit.getUnitNumber());
    imageGL_->unbindPickingTexture();
}

void CanvasGL::renderNoise() {
    noiseShader_->activate();
    renderImagePlaneRect();
    noiseShader_->deactivate();
}

void CanvasGL::renderTexture(GLint unitNumber) {
    shader_->activate();
    shader_->setUniform("tex_", unitNumber);
    //FIXME: glViewport should not be here, which indicates this context is not active.
    glViewport(0, 0, dimensions_.x, dimensions_.y);
    renderImagePlaneRect();
    shader_->deactivate();
}

} // namespace
