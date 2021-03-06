#ifndef PINBOX_H
#define PINBOX_H

#include <QComboBox>

class PinBox : public QComboBox
{
    Q_OBJECT

public:
    explicit PinBox(QWidget *parent = 0);
signals:
    void pinChanged();
public slots:
    void setPinItems(const QString& pinGroup);
private slots:
    void indexChanged();
};

#endif // PINBOX_H
