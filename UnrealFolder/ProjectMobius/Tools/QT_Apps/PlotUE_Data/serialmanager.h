#ifndef SERIALMANAGER_H
#define SERIALMANAGER_H

#include <QObject>

class SerialManager : public QObject
{
    Q_OBJECT
public:
    explicit SerialManager(QObject *parent = nullptr);

signals:
};

#endif // SERIALMANAGER_H
