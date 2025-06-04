#ifndef MESSAGEPROCESSOR_H
#define MESSAGEPROCESSOR_H

#include <QObject>
#include <QStringView>
#include <QJsonObject>
#include <QPointF>
#include <unordered_map>
#include <functional>
#include <string_view>
#include <cstdint>
#include <utility>

enum class ActionType {
    AppendPoint,
    SetData,
    UpdateCurrentTime,
    UpdateAgentCount,
    UpdateLiveData,
    ResetData,
    UpdateAxis,
    UpdateChartTitle,
    BuiltInCount,
    Unknown
};


constexpr uint32_t const_hash(std::string_view str) {
    uint32_t hash = 5381;
    for (char c : str) {
        hash = ((hash << 5) + hash) + static_cast<uint8_t>(c); // hash * 33 + c
    }
    return hash;
}

constexpr std::pair<uint32_t, ActionType> kActionMap[] = {
    { const_hash("appendPoint"),        ActionType::AppendPoint },
    { const_hash("setData"),            ActionType::SetData },
    { const_hash("updateCurrentTime"),  ActionType::UpdateCurrentTime },
    { const_hash("updateAgentCount"),  ActionType::UpdateAgentCount },
    { const_hash("updateLiveData"),  ActionType::UpdateLiveData },
    { const_hash("resetData"),          ActionType::ResetData },
    { const_hash("updateAxis"),         ActionType::UpdateAxis },
    { const_hash("updateChartTitle"),   ActionType::UpdateChartTitle }
};

constexpr ActionType actionFromString(std::string_view str) {
    const uint32_t h = const_hash(str);
    for (const auto& [key, val] : kActionMap) {
        if (key == h)
            return val;
    }
    return ActionType::Unknown;
}

class ChartTableModel;
class AxisSettings;
class ChartSettings;

class MessageProcessor : public QObject {
    Q_OBJECT

public:
    MessageProcessor(ChartTableModel* model, AxisSettings* axis, ChartSettings* settings);

    /// Runtime extension of handler table
    void registerHandler(QStringView actionName, std::function<void(const QJsonObject&)> handler);

public slots:
    void handleMessage(const QString &msg);

private:


    // Static handler dispatch
    void handleBuiltIn(ActionType type, const QJsonObject& obj);

    // Registered dynamic handlers
    std::unordered_map<QString, std::function<void(const QJsonObject&)>> m_dynamicHandlers;

    ChartTableModel* m_model;
    AxisSettings*    m_axis;
    ChartSettings*   m_settings;
};

#endif // MESSAGEPROCESSOR_H
