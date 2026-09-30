#pragma once
#include <QWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QTimer>
#include <QElapsedTimer>
#include <QToolButton>
#include <QJsonObject>
#include <QString>
#include <QLabel>
#include <obs-frontend-api.h>

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
    QLabel *sceneLabel{};
    QTimer timer;
    QElapsedTimer clock;
    bool cycle=false;
    bool loading=false;
    enum State { Idle, FadeIn, Visible, FadeOut, Waiting } state=Idle;
    qint64 startMs=0, visibleMs=0, waitMs=0;

    // Per-scene configuration, stored in the active OBS profile.
    QJsonObject allConfigs;   // "<collection>::<scene>" -> config object
    QString sceneName;        // scene whose config is shown in the UI
    QString sceneKey;         // key of that scene in allConfigs
    QString activeScene;      // scene the running animation belongs to

    void buildUi();
    void setupLayer(Layer&, const QString&, const QString&, int);
    QString sourceName(const Layer&) const;

    void loadProfile();
    void persist();
    void saveSettings();          // UI -> config of current scene
    void applyConfig();           // config of current scene -> UI
    void populateCombos(bool keepSelection);
    void switchScene();
    void handleEvent(enum obs_frontend_event);
    static void frontendEvent(enum obs_frontend_event, void*);
    static QString currentSceneName();
    static QString keyFor(const QString &scene);

    void prepare();
    void launch();
    void hideAll();
    void finishFadeOut();
    static void setItemVisible(const char *scene, const char *source, bool);
    static void setOpacity(const char*, int);
    static void setText(const char*, const char*, int);
    double smooth(double) const;
};
