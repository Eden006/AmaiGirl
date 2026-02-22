#pragma once
#include <QMainWindow>

class QEvent;

class AboutWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit AboutWindow(QWidget* parent=nullptr);
    ~AboutWindow() override = default;

protected:
    void changeEvent(QEvent* event) override;
};
