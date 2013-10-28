#include <inviwo/core/util/canvas.h>
#include <inviwo/core/datastructures/geometry/meshram.h>

namespace inviwo {

EventHandler* eventHandler_();

Geometry* Canvas::screenAlignedRect_ = NULL;

Canvas::Canvas(uvec2 dimensions)
               : dimensions_(dimensions),
                 processorNetworkEvaluator_(0)
{
    shared = true;
    pickingContainer_ = new PickingContainer();

}

Canvas::~Canvas() {
    if(!shared)
        delete screenAlignedRect_;
    delete pickingContainer_;
}

void Canvas::initialize() {
    if(!pickingContainer_)
        pickingContainer_ = new PickingContainer();
}

void Canvas::deinitialize() {}

void Canvas::render(const Image* im) {}

void Canvas::repaint() {}

void Canvas::activate() {}

void Canvas::resize(uvec2 size) {
    uvec2 previousDimensions = dimensions_;
    dimensions_ = size;
    if (processorNetworkEvaluator_) {
        ResizeEvent* resizeEvent = new ResizeEvent(dimensions_);
        resizeEvent->setPreviousSize(previousDimensions);        
        processorNetworkEvaluator_->propagateResizeEvent(this, resizeEvent);
        processorNetworkEvaluator_->evaluate();
    }
}

void Canvas::update() {}

void Canvas::interactionEvent(InteractionEvent* e) {
    processorNetworkEvaluator_->propagateInteractionEvent(this, e);
    processorNetworkEvaluator_->evaluate();
}

void Canvas::mousePressEvent(MouseEvent* e) {
    if(e->button() == MouseEvent::MOUSE_BUTTON_LEFT){
        bool picked = pickingContainer_->performPick(mousePosToPixelCoordinates(e->pos()));
        if(!picked)
            interactionEvent(e);
    }
    else
        interactionEvent(e);
}

void Canvas::mouseReleaseEvent(MouseEvent* e) {
    pickingContainer_->setPickableSelected(false);
    interactionEvent(e);
}

void Canvas::mouseMoveEvent(MouseEvent* e) {
    if(pickingContainer_->isPickableSelected())
        pickingContainer_->movePicked(mousePosToPixelCoordinates(e->pos()));
    else
        interactionEvent(e);
}

void Canvas::keyPressEvent(KeyboardEvent* e) {
	interactionEvent(e);
}

void Canvas::keyReleaseEvent(KeyboardEvent* e) {
	interactionEvent(e);
}

uvec2 Canvas::mousePosToPixelCoordinates(ivec2 mpos){
    uvec2 pos(mpos.x, mpos.y);
    pos.y = dimensions_.y-(pos.y-1);
    return pos;
}

} // namespace
