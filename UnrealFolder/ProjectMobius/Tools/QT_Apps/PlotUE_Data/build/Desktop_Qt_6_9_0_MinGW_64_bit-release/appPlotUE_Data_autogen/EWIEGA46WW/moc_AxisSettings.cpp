/****************************************************************************
** Meta object code from reading C++ file 'AxisSettings.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../AxisSettings.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'AxisSettings.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.9.0. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN12AxisSettingsE_t {};
} // unnamed namespace

template <> constexpr inline auto AxisSettings::qt_create_metaobjectdata<qt_meta_tag_ZN12AxisSettingsE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxisSettings",
        "QML.Element",
        "auto",
        "xMinChanged",
        "",
        "xMaxChanged",
        "xChartMaxChanged",
        "xTitleChanged",
        "xGridVisibleChanged",
        "yMinChanged",
        "yMaxChanged",
        "yChartMaxChanged",
        "yTitleChanged",
        "yGridVisibleChanged",
        "setXMin",
        "v",
        "setXMax",
        "setXTitle",
        "t",
        "setXGridVisible",
        "b",
        "setYMin",
        "setYMax",
        "setYTitle",
        "setYGridVisible",
        "calcChartXMax",
        "calcChartYMax",
        "GetYMin",
        "GetYMax",
        "xMin",
        "xMax",
        "xChartMax",
        "xTitle",
        "xGridVisible",
        "yMin",
        "yMax",
        "yChartMax",
        "yTitle",
        "yGridVisible"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'xMinChanged'
        QtMocHelpers::SignalData<void(qreal)>(3, 4, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QReal, 4 },
        }}),
        // Signal 'xMaxChanged'
        QtMocHelpers::SignalData<void(qreal)>(5, 4, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QReal, 4 },
        }}),
        // Signal 'xChartMaxChanged'
        QtMocHelpers::SignalData<void()>(6, 4, QMC::AccessPublic, QMetaType::Void),
        // Signal 'xTitleChanged'
        QtMocHelpers::SignalData<void(const QString &)>(7, 4, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 4 },
        }}),
        // Signal 'xGridVisibleChanged'
        QtMocHelpers::SignalData<void(bool)>(8, 4, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 4 },
        }}),
        // Signal 'yMinChanged'
        QtMocHelpers::SignalData<void(qreal)>(9, 4, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QReal, 4 },
        }}),
        // Signal 'yMaxChanged'
        QtMocHelpers::SignalData<void(qreal)>(10, 4, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QReal, 4 },
        }}),
        // Signal 'yChartMaxChanged'
        QtMocHelpers::SignalData<void()>(11, 4, QMC::AccessPublic, QMetaType::Void),
        // Signal 'yTitleChanged'
        QtMocHelpers::SignalData<void(const QString &)>(12, 4, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 4 },
        }}),
        // Signal 'yGridVisibleChanged'
        QtMocHelpers::SignalData<void(bool)>(13, 4, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 4 },
        }}),
        // Slot 'setXMin'
        QtMocHelpers::SlotData<void(qreal)>(14, 4, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QReal, 15 },
        }}),
        // Slot 'setXMax'
        QtMocHelpers::SlotData<void(qreal)>(16, 4, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QReal, 15 },
        }}),
        // Slot 'setXTitle'
        QtMocHelpers::SlotData<void(const QString &)>(17, 4, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 18 },
        }}),
        // Slot 'setXGridVisible'
        QtMocHelpers::SlotData<void(bool)>(19, 4, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 20 },
        }}),
        // Slot 'setYMin'
        QtMocHelpers::SlotData<void(qreal)>(21, 4, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QReal, 15 },
        }}),
        // Slot 'setYMax'
        QtMocHelpers::SlotData<void(qreal)>(22, 4, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QReal, 15 },
        }}),
        // Slot 'setYTitle'
        QtMocHelpers::SlotData<void(const QString &)>(23, 4, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 18 },
        }}),
        // Slot 'setYGridVisible'
        QtMocHelpers::SlotData<void(bool)>(24, 4, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 20 },
        }}),
        // Slot 'calcChartXMax'
        QtMocHelpers::SlotData<void()>(25, 4, QMC::AccessPublic, QMetaType::Void),
        // Slot 'calcChartYMax'
        QtMocHelpers::SlotData<void()>(26, 4, QMC::AccessPublic, QMetaType::Void),
        // Slot 'GetYMin'
        QtMocHelpers::SlotData<qreal()>(27, 4, QMC::AccessPublic, QMetaType::QReal),
        // Slot 'GetYMax'
        QtMocHelpers::SlotData<qreal()>(28, 4, QMC::AccessPublic, QMetaType::QReal),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'xMin'
        QtMocHelpers::PropertyData<qreal>(29, QMetaType::QReal, QMC::DefaultPropertyFlags | QMC::Writable, 0),
        // property 'xMax'
        QtMocHelpers::PropertyData<qreal>(30, QMetaType::QReal, QMC::DefaultPropertyFlags | QMC::Writable, 1),
        // property 'xChartMax'
        QtMocHelpers::PropertyData<qreal>(31, QMetaType::QReal, QMC::DefaultPropertyFlags | QMC::Writable, 2),
        // property 'xTitle'
        QtMocHelpers::PropertyData<QString>(32, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable, 3),
        // property 'xGridVisible'
        QtMocHelpers::PropertyData<bool>(33, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable, 4),
        // property 'yMin'
        QtMocHelpers::PropertyData<qreal>(34, QMetaType::QReal, QMC::DefaultPropertyFlags | QMC::Writable, 5),
        // property 'yMax'
        QtMocHelpers::PropertyData<qreal>(35, QMetaType::QReal, QMC::DefaultPropertyFlags | QMC::Writable, 6),
        // property 'yChartMax'
        QtMocHelpers::PropertyData<qreal>(36, QMetaType::QReal, QMC::DefaultPropertyFlags | QMC::Writable, 7),
        // property 'yTitle'
        QtMocHelpers::PropertyData<QString>(37, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable, 8),
        // property 'yGridVisible'
        QtMocHelpers::PropertyData<bool>(38, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable, 9),
    };
    QtMocHelpers::UintData qt_enums {
    };
    QtMocHelpers::UintData qt_constructors {};
    QtMocHelpers::ClassInfos qt_classinfo({
            {    1,    2 },
    });
    return QtMocHelpers::metaObjectData<AxisSettings, void>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums, qt_constructors, qt_classinfo);
}
Q_CONSTINIT const QMetaObject AxisSettings::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12AxisSettingsE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12AxisSettingsE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN12AxisSettingsE_t>.metaTypes,
    nullptr
} };

void AxisSettings::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxisSettings *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->xMinChanged((*reinterpret_cast< std::add_pointer_t<qreal>>(_a[1]))); break;
        case 1: _t->xMaxChanged((*reinterpret_cast< std::add_pointer_t<qreal>>(_a[1]))); break;
        case 2: _t->xChartMaxChanged(); break;
        case 3: _t->xTitleChanged((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 4: _t->xGridVisibleChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 5: _t->yMinChanged((*reinterpret_cast< std::add_pointer_t<qreal>>(_a[1]))); break;
        case 6: _t->yMaxChanged((*reinterpret_cast< std::add_pointer_t<qreal>>(_a[1]))); break;
        case 7: _t->yChartMaxChanged(); break;
        case 8: _t->yTitleChanged((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 9: _t->yGridVisibleChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 10: _t->setXMin((*reinterpret_cast< std::add_pointer_t<qreal>>(_a[1]))); break;
        case 11: _t->setXMax((*reinterpret_cast< std::add_pointer_t<qreal>>(_a[1]))); break;
        case 12: _t->setXTitle((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 13: _t->setXGridVisible((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 14: _t->setYMin((*reinterpret_cast< std::add_pointer_t<qreal>>(_a[1]))); break;
        case 15: _t->setYMax((*reinterpret_cast< std::add_pointer_t<qreal>>(_a[1]))); break;
        case 16: _t->setYTitle((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 17: _t->setYGridVisible((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 18: _t->calcChartXMax(); break;
        case 19: _t->calcChartYMax(); break;
        case 20: { qreal _r = _t->GetYMin();
            if (_a[0]) *reinterpret_cast< qreal*>(_a[0]) = std::move(_r); }  break;
        case 21: { qreal _r = _t->GetYMax();
            if (_a[0]) *reinterpret_cast< qreal*>(_a[0]) = std::move(_r); }  break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (AxisSettings::*)(qreal )>(_a, &AxisSettings::xMinChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (AxisSettings::*)(qreal )>(_a, &AxisSettings::xMaxChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (AxisSettings::*)()>(_a, &AxisSettings::xChartMaxChanged, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (AxisSettings::*)(const QString & )>(_a, &AxisSettings::xTitleChanged, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (AxisSettings::*)(bool )>(_a, &AxisSettings::xGridVisibleChanged, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (AxisSettings::*)(qreal )>(_a, &AxisSettings::yMinChanged, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (AxisSettings::*)(qreal )>(_a, &AxisSettings::yMaxChanged, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (AxisSettings::*)()>(_a, &AxisSettings::yChartMaxChanged, 7))
            return;
        if (QtMocHelpers::indexOfMethod<void (AxisSettings::*)(const QString & )>(_a, &AxisSettings::yTitleChanged, 8))
            return;
        if (QtMocHelpers::indexOfMethod<void (AxisSettings::*)(bool )>(_a, &AxisSettings::yGridVisibleChanged, 9))
            return;
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<qreal*>(_v) = _t->m_xMin; break;
        case 1: *reinterpret_cast<qreal*>(_v) = _t->m_xMax; break;
        case 2: *reinterpret_cast<qreal*>(_v) = _t->m_xChartMax; break;
        case 3: *reinterpret_cast<QString*>(_v) = _t->m_xTitle; break;
        case 4: *reinterpret_cast<bool*>(_v) = _t->m_xGridVisible; break;
        case 5: *reinterpret_cast<qreal*>(_v) = _t->m_yMin; break;
        case 6: *reinterpret_cast<qreal*>(_v) = _t->m_yMax; break;
        case 7: *reinterpret_cast<qreal*>(_v) = _t->m_yChartMax; break;
        case 8: *reinterpret_cast<QString*>(_v) = _t->m_yTitle; break;
        case 9: *reinterpret_cast<bool*>(_v) = _t->m_yGridVisible; break;
        default: break;
        }
    }
    if (_c == QMetaObject::WriteProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0:
            if (QtMocHelpers::setProperty(_t->m_xMin, *reinterpret_cast<qreal*>(_v)))
                Q_EMIT _t->xMinChanged(_t->m_xMin);
            break;
        case 1:
            if (QtMocHelpers::setProperty(_t->m_xMax, *reinterpret_cast<qreal*>(_v)))
                Q_EMIT _t->xMaxChanged(_t->m_xMax);
            break;
        case 2:
            if (QtMocHelpers::setProperty(_t->m_xChartMax, *reinterpret_cast<qreal*>(_v)))
                Q_EMIT _t->xChartMaxChanged();
            break;
        case 3:
            if (QtMocHelpers::setProperty(_t->m_xTitle, *reinterpret_cast<QString*>(_v)))
                Q_EMIT _t->xTitleChanged(_t->m_xTitle);
            break;
        case 4:
            if (QtMocHelpers::setProperty(_t->m_xGridVisible, *reinterpret_cast<bool*>(_v)))
                Q_EMIT _t->xGridVisibleChanged(_t->m_xGridVisible);
            break;
        case 5:
            if (QtMocHelpers::setProperty(_t->m_yMin, *reinterpret_cast<qreal*>(_v)))
                Q_EMIT _t->yMinChanged(_t->m_yMin);
            break;
        case 6:
            if (QtMocHelpers::setProperty(_t->m_yMax, *reinterpret_cast<qreal*>(_v)))
                Q_EMIT _t->yMaxChanged(_t->m_yMax);
            break;
        case 7:
            if (QtMocHelpers::setProperty(_t->m_yChartMax, *reinterpret_cast<qreal*>(_v)))
                Q_EMIT _t->yChartMaxChanged();
            break;
        case 8:
            if (QtMocHelpers::setProperty(_t->m_yTitle, *reinterpret_cast<QString*>(_v)))
                Q_EMIT _t->yTitleChanged(_t->m_yTitle);
            break;
        case 9:
            if (QtMocHelpers::setProperty(_t->m_yGridVisible, *reinterpret_cast<bool*>(_v)))
                Q_EMIT _t->yGridVisibleChanged(_t->m_yGridVisible);
            break;
        default: break;
        }
    }
}

const QMetaObject *AxisSettings::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxisSettings::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12AxisSettingsE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int AxisSettings::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 22)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 22;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 22)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 22;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 10;
    }
    return _id;
}

// SIGNAL 0
void AxisSettings::xMinChanged(qreal _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void AxisSettings::xMaxChanged(qreal _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void AxisSettings::xChartMaxChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void AxisSettings::xTitleChanged(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}

// SIGNAL 4
void AxisSettings::xGridVisibleChanged(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1);
}

// SIGNAL 5
void AxisSettings::yMinChanged(qreal _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1);
}

// SIGNAL 6
void AxisSettings::yMaxChanged(qreal _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 6, nullptr, _t1);
}

// SIGNAL 7
void AxisSettings::yChartMaxChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 7, nullptr);
}

// SIGNAL 8
void AxisSettings::yTitleChanged(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 8, nullptr, _t1);
}

// SIGNAL 9
void AxisSettings::yGridVisibleChanged(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 9, nullptr, _t1);
}
QT_WARNING_POP
