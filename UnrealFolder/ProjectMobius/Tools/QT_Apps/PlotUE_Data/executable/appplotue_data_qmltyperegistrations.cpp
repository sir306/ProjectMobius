/****************************************************************************
** Generated QML type registration code
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <QtQml/qqml.h>
#include <QtQml/qqmlmoduleregistration.h>

#if __has_include(<AxisSettings.h>)
#  include <AxisSettings.h>
#endif
#if __has_include(<ChartSettings.h>)
#  include <ChartSettings.h>
#endif


#if !defined(QT_STATIC)
#define Q_QMLTYPE_EXPORT Q_DECL_EXPORT
#else
#define Q_QMLTYPE_EXPORT
#endif
Q_QMLTYPE_EXPORT void qml_register_types_PlotUE_Data()
{
    QT_WARNING_PUSH QT_WARNING_DISABLE_DEPRECATED
    qmlRegisterTypesAndRevisions<AxisSettings>("PlotUE_Data", 1);
    qmlRegisterTypesAndRevisions<ChartSettings>("PlotUE_Data", 1);
    QT_WARNING_POP
    qmlRegisterModule("PlotUE_Data", 1, 0);
}

static const QQmlModuleRegistration plotUEDataRegistration("PlotUE_Data", qml_register_types_PlotUE_Data);
