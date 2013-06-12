#ifndef IVW_EVENTPROPERTYWIDGETQT_H
#define IVW_EVENTPROPERTYWIDGETQT_H

#include <inviwo/qt/widgets/properties/propertywidgetqt.h>
#include <inviwo/core/properties/eventproperty.h>
#include <inviwo/qt/editor/eventpropertymanager.h>
#include <inviwo/qt/widgets/mappingpopup.h>

#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>

namespace inviwo {

class EventPropertyWidgetQt : public PropertyWidgetQt {
    Q_OBJECT;

public:
    EventPropertyWidgetQt(EventProperty* eventproperty);
    void updateFromProperty();
	void setManager(EventPropertyManager* eventPropertyManager) { eventPropertyManager_ = eventPropertyManager; };

private:
    EventProperty* eventproperty_;
    QPushButton* button_;
	EventPropertyManager* eventPropertyManager_;

    void generateWidget();

public slots:
    void clickedSlot();
};

} //namespace

#endif // IVW_EVENTPROPERTYWIDGETQT_H