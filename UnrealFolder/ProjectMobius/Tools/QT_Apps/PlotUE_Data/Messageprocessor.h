/**
 * MIT License
 * Copyright (c) 2025 ProjectMobius contributors
 * Nicholas R. Harding and Peter Thompson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 *      The above copyright notice and this permission notice shall be included in
 *      all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

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
    Heartbeat,
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
    { const_hash("updateChartTitle"),   ActionType::UpdateChartTitle },
    { const_hash("heartbeat"),          ActionType::Heartbeat }
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

/**
 * @brief Parses incoming JSON commands and routes them to chart/axis objects.
 *
 * MessageProcessor lives on a worker thread so the GUI thread stays responsive
 * when large messages arrive from Unreal. Built-in actions are dispatched via
 * a constexpr hash table for speed, and additional actions can be registered at
 * runtime with @ref registerHandler.
 */
class MessageProcessor : public QObject {
    Q_OBJECT

public:
    MessageProcessor(ChartTableModel* model, AxisSettings* axis, ChartSettings* settings);

    /**
     * @brief Register a runtime handler for a custom @c action string.
     * @param actionName The JSON @c action to respond to (e.g., "customEvent").
     * @param handler    Functor executed with the parsed object payload.
     */
    void registerHandler(QStringView actionName, std::function<void(const QJsonObject&)> handler);

public slots:
    /**
     * @brief Parse a raw JSON payload and dispatch to built-in or dynamic handlers.
     * @param msg UTF-8 JSON object containing an @c action field.
     */
    void handleMessage(const QString &msg);

private:
    /**
     * @brief Dispatch built-in actions hashed at compile time.
     */
    void handleBuiltIn(ActionType type, const QJsonObject& obj);

    // Registered dynamic handlers
    std::unordered_map<QString, std::function<void(const QJsonObject&)>> m_dynamicHandlers;

    ChartTableModel* m_model;
    AxisSettings*    m_axis;
    ChartSettings*   m_settings;
};

#endif // MESSAGEPROCESSOR_H
