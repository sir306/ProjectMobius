/****************************************************************************
** Meta object code from reading C++ file 'ChartSettings.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../ChartSettings.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'ChartSettings.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN13ChartSettingsE_t {};
} // unnamed namespace

template <> constexpr inline auto ChartSettings::qt_create_metaobjectdata<qt_meta_tag_ZN13ChartSettingsE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ChartSettings",
        "QML.Element",
        "auto",
        "titleChanged",
        "",
        "showAllPointsChanged",
        "currentLiveDataChanged",
        "currentAgentCountChanged",
        "playbarPosChanged",
        "setTitle",
        "t",
        "setShowAllPoints",
        "v",
        "setCurrentTime",
        "setCurrentAgentCount",
        "updateLiveData",
        "time",
        "count",
        "updatePlaybarX",
        "setPlotMetrics",
        "originX",
        "width",
        "min",
        "max",
        "setPlotAreaX",
        "x",
        "setPlotWidth",
        "w",
        "setAxisMin",
        "mn",
        "setAxisMax",
        "mx",
        "title",
        "showAllPoints",
        "statusDisplay",
        "playbarX"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'titleChanged'
        QtMocHelpers::SignalData<void(const QString &)>(3, 4, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 4 },
        }}),
        // Signal 'showAllPointsChanged'
        QtMocHelpers::SignalData<void()>(5, 4, QMC::AccessPublic, QMetaType::Void),
        // Signal 'currentLiveDataChanged'
        QtMocHelpers::SignalData<void()>(6, 4, QMC::AccessPublic, QMetaType::Void),
        // Signal 'currentAgentCountChanged'
        QtMocHelpers::SignalData<void()>(7, 4, QMC::AccessPublic, QMetaType::Void),
        // Signal 'playbarPosChanged'
        QtMocHelpers::SignalData<void()>(8, 4, QMC::AccessPublic, QMetaType::Void),
        // Slot 'setTitle'
        QtMocHelpers::SlotData<void(const QString &)>(9, 4, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 10 },
        }}),
        // Slot 'setShowAllPoints'
        QtMocHelpers::SlotData<void(bool)>(11, 4, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 12 },
        }}),
        // Slot 'setCurrentTime'
        QtMocHelpers::SlotData<void(qreal)>(13, 4, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QReal, 10 },
        }}),
        // Slot 'setCurrentAgentCount'
        QtMocHelpers::SlotData<void(qint32)>(14, 4, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 10 },
        }}),
        // Slot 'updateLiveData'
        QtMocHelpers::SlotData<void(qint32, qint32)>(15, 4, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 16 }, { QMetaType::Int, 17 },
        }}),
        // Slot 'updatePlaybarX'
        QtMocHelpers::SlotData<void()>(18, 4, QMC::AccessPublic, QMetaType::Void),
        // Slot 'setPlotMetrics'
        QtMocHelpers::SlotData<void(qreal, qreal, qreal, qreal)>(19, 4, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QReal, 20 }, { QMetaType::QReal, 21 }, { QMetaType::QReal, 22 }, { QMetaType::QReal, 23 },
        }}),
        // Slot 'setPlotAreaX'
        QtMocHelpers::SlotData<void(qreal)>(24, 4, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QReal, 25 },
        }}),
        // Slot 'setPlotWidth'
        QtMocHelpers::SlotData<void(qreal)>(26, 4, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QReal, 27 },
        }}),
        // Slot 'setAxisMin'
        QtMocHelpers::SlotData<void(qreal)>(28, 4, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QReal, 29 },
        }}),
        // Slot 'setAxisMax'
        QtMocHelpers::SlotData<void(qreal)>(30, 4, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QReal, 31 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'title'
        QtMocHelpers::PropertyData<QString>(32, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable, 0),
        // property 'showAllPoints'
        QtMocHelpers::PropertyData<bool>(33, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 1),
        // property 'statusDisplay'
        QtMocHelpers::PropertyData<QString>(34, QMetaType::QString, QMC::DefaultPropertyFlags, 2),
        // property 'playbarX'
        QtMocHelpers::PropertyData<qreal>(35, QMetaType::QReal, QMC::DefaultPropertyFlags | QMC::Writable, 4),
    };
    QtMocHelpers::UintData qt_enums {
    };
    QtMocHelpers::UintData qt_constructors {};
    QtMocHelpers::ClassInfos qt_classinfo({
            {    1,    2 },
    });
    return QtMocHelpers::metaObjectData<ChartSettings, void>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums, qt_constructors, qt_classinfo);
}
Q_CONSTINIT const QMetaObject ChartSettings::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN13ChartSettingsE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN13ChartSettingsE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN13ChartSettingsE_t>.metaTypes,
    nullptr
} };

void ChartSettings::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<ChartSettings *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->titleChanged((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 1: _t->showAllPointsChanged(); break;
        case 2: _t->currentLiveDataChanged(); break;
        case 3: _t->currentAgentCountChanged(); break;
        case 4: _t->playbarPosChanged(); break;
        case 5: _t->setTitle((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 6: _t->setShowAllPoints((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 7: _t->setCurrentTime((*reinterpret_cast< std::add_pointer_t<qreal>>(_a[1]))); break;
        case 8: _t->setCurrentAgentCount((*reinterpret_cast< std::add_pointer_t<qint32>>(_a[1]))); break;
        case 9: _t->updateLiveData((*reinterpret_cast< std::add_pointer_t<qint32>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<qint32>>(_a[2]))); break;
        case 10: _t->updatePlaybarX(); break;
        case 11: _t->setPlotMetrics((*reinterpret_cast< std::add_pointer_t<qreal>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<qreal>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<qreal>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<qreal>>(_a[4]))); break;
        case 12: _t->setPlotAreaX((*reinterpret_cast< std::add_pointer_t<qreal>>(_a[1]))); break;
        case 13: _t->setPlotWidth((*reinterpret_cast< std::add_pointer_t<qreal>>(_a[1]))); break;
        case 14: _t->setAxisMin((*reinterpret_cast< std::add_pointer_t<qreal>>(_a[1]))); break;
        case 15: _t->setAxisMax((*reinterpret_cast< std::add_pointer_t<qreal>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (ChartSettings::*)(const QString & )>(_a, &ChartSettings::titleChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (ChartSettings::*)()>(_a, &ChartSettings::showAllPointsChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (ChartSettings::*)()>(_a, &ChartSettings::currentLiveDataChanged, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (ChartSettings::*)()>(_a, &ChartSettings::currentAgentCountChanged, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (ChartSettings::*)()>(_a, &ChartSettings::playbarPosChanged, 4))
            return;
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<QString*>(_v) = _t->m_title; break;
        case 1: *reinterpret_cast<bool*>(_v) = _t->showAllPoints(); break;
        case 2: *reinterpret_cast<QString*>(_v) = _t->statusDisplay(); break;
        case 3: *reinterpret_cast<qreal*>(_v) = _t->m_playbarX; break;
        default: break;
        }
    }
    if (_c == QMetaObject::WriteProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0:
            if (QtMocHelpers::setProperty(_t->m_title, *reinterpret_cast<QString*>(_v)))
                Q_EMIT _t->titleChanged(_t->m_title);
            break;
        case 1: _t->setShowAllPoints(*reinterpret_cast<bool*>(_v)); break;
        case 3:
            if (QtMocHelpers::setProperty(_t->m_playbarX, *reinterpret_cast<qreal*>(_v)))
                Q_EMIT _t->playbarPosChanged();
            break;
        default: break;
        }
    }
}

const QMetaObject *ChartSettings::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ChartSettings::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN13ChartSettingsE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int ChartSettings::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 16)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 16;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 16)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 16;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 4;
    }
    return _id;
}

// SIGNAL 0
void ChartSettings::titleChanged(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void ChartSettings::showAllPointsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void ChartSettings::currentLiveDataChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void ChartSettings::currentAgentCountChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void ChartSettings::playbarPosChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}
QT_WARNING_POP
