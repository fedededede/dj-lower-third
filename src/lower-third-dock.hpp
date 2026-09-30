#pragma once
#include <QWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QTimer>
#include <QElapsedTimer>
#include <QSettings>
#include <QToolButton>

class LowerThirdDock : public QWidget {
    Q_OBJECT
public:
    explicit LowerThirdDock(QWidget *parent=nullptr);
    ~LowerThirdDock() override;

private slots:
    void refreshSources();
    void showNow();
    void startCycle();
    void stopCycle();
    void animationTick();

private:
    struct Layer {
        QComboBox *source{};
        QLineEdit *text{};
        QSpinBox *size{};
    };

    Layer l1, l2, l3;
    QDoubleSpinBox *visible{};
    QDoubleSpinBox *interval{};
    QDoubleSpinBox *fadeIn{};
    QDoubleSpinBox *stagger{};
    QDoubleSpinBox *fadeOut{};
    QPushButton *showBtn{};
    QPushButton *startBtn{};
    QPushButton *stopBtn{};
    QWidget *technicalPanel{};
    QToolButton *technicalToggle{};
    QTimer timer;
    QElapsedTimer clock;
    bool cycle=false;
    bool loading=false;
    enum State { Idle, FadeIn, Visible, FadeOut, Waiting } state=Idle;
    qint64 startMs=0, visibleMs=0, waitMs=0;

    void buildUi();
    void loadSettings();
    void saveSettings() const;
    void setupLayer(Layer&, const QString&, const QString&, int);
    QString sourceName(const Layer&) const;
    void prepare();
    void launch();
    void finishFadeOut();
    static void setItemVisible(const char*, bool);
    static void setOpacity(const char*, int);
    static void setText(const char*, const char*, int);
    double smooth(double) const;
};
