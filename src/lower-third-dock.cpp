#include "lower-third-dock.hpp"
#include <obs.h>
#include <obs-frontend-api.h>
#include <obs-data.h>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFrame>
#include <QStringList>
#include <algorithm>
#include <cstring>
#include <vector>
#include <string>

static bool isTextSource(obs_source_t *s)
{
    const char *id = obs_source_get_id(s);
    return id && (!strcmp(id, "text_gdiplus_v2") || !strcmp(id, "text_gdiplus") ||
                  !strcmp(id, "text_ft2_source") || !strcmp(id, "text_ft2_source_v2"));
}

static bool collectItem(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
    auto *out = static_cast<std::vector<std::string> *>(param);
    if (obs_sceneitem_is_group(item)) {
        obs_sceneitem_group_enum_items(item, collectItem, param);
        return true;
    }
    obs_source_t *s = obs_sceneitem_get_source(item);
    if (s && isTextSource(s)) {
        const char *n = obs_source_get_name(s);
        if (n)
            out->emplace_back(n);
    }
    return true;
}

static std::vector<std::string> textSources()
{
    std::vector<std::string> out;
    obs_source_t *cur = obs_frontend_get_current_scene();
    if (!cur)
        return out;

    obs_scene_t *scene = obs_scene_from_source(cur);
    if (scene)
        obs_scene_enum_items(scene, collectItem, &out);
    obs_source_release(cur);

    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

LowerThirdDock::LowerThirdDock(QWidget *p) : QWidget(p)
{
    setMinimumWidth(340);
    buildUi();
    clock.start();
    loadSettings();
    connect(&timer, &QTimer::timeout, this, &LowerThirdDock::animationTick);
    refreshSources();
}

LowerThirdDock::~LowerThirdDock()
{
    timer.stop();
    saveSettings();
}

void LowerThirdDock::setupLayer(Layer &l, const QString &, const QString &txt, int sz)
{
    l.source = new QComboBox(this);
    l.text = new QLineEdit(txt, this);
    l.size = new QSpinBox(this);
    l.size->setRange(1, 500);
    l.size->setValue(sz);
}

void LowerThirdDock::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    auto *title = new QLabel("<b>DJ LOWER THIRD</b>");
    title->setAlignment(Qt::AlignCenter);
    root->addWidget(title);

    setupLayer(l1, "Principal", "DJ PEPITO", 48);
    setupLayer(l2, "Secundario", "@djpepito", 28);
    setupLayer(l3, "Terciario", "BUENOS AIRES", 22);

    auto *layers = new QGroupBox("Textos");
    auto *ll = new QVBoxLayout(layers);
    ll->setContentsMargins(6, 8, 6, 6);
    ll->setSpacing(5);

    for (Layer *l : {&l1, &l2, &l3}) {
        const char *label = l == &l1 ? "Principal" : l == &l2 ? "Secundario" : "Terciario";
        auto *box = new QGroupBox(label);
        auto *f = new QFormLayout(box);
        f->addRow("Fuente:", l->source);
        f->addRow("Texto:", l->text);
        f->addRow("Tamaño:", l->size);
        ll->addWidget(box);
    }
    root->addWidget(layers);

    showBtn = new QPushButton("▶ MOSTRAR AHORA");
    startBtn = new QPushButton("▶ INICIAR CICLO");
    stopBtn = new QPushButton("■ DETENER CICLO");
    for (auto *b : {showBtn, startBtn, stopBtn})
        b->setMinimumHeight(34);

    auto *buttons1 = new QHBoxLayout();
    buttons1->addWidget(showBtn);
    buttons1->addWidget(startBtn);
    root->addLayout(buttons1);
    root->addWidget(stopBtn);

    auto *refresh = new QPushButton("↻ Actualizar fuentes");
    root->addWidget(refresh);

    technicalToggle = new QToolButton(this);
    technicalToggle->setText("⚙ Ajustes técnicos");
    technicalToggle->setCheckable(true);
    technicalToggle->setChecked(false);
    technicalToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    technicalToggle->setArrowType(Qt::RightArrow);
    technicalToggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    root->addWidget(technicalToggle);

    technicalPanel = new QWidget(this);
    auto *technicalLayout = new QVBoxLayout(technicalPanel);
    technicalLayout->setContentsMargins(4, 2, 4, 4);

    auto makeSpin = [&](double v) {
        auto *s = new QDoubleSpinBox(technicalPanel);
        s->setRange(0, 3600);
        s->setDecimals(2);
        s->setSingleStep(.05);
        s->setValue(v);
        s->setSuffix(" s");
        return s;
    };

    visible = makeSpin(13.0);
    interval = makeSpin(10.0);
    fadeIn = makeSpin(.30);
    stagger = makeSpin(.15);
    fadeOut = makeSpin(.40);

    auto *timing = new QGroupBox("Tiempos");
    auto *tf = new QFormLayout(timing);
    tf->addRow("Duración visible:", visible);
    tf->addRow("Intervalo:", interval);
    tf->addRow("Fade-in:", fadeIn);
    tf->addRow("Separación:", stagger);
    tf->addRow("Fade-out:", fadeOut);
    technicalLayout->addWidget(timing);

    technicalPanel->setVisible(false);
    root->addWidget(technicalPanel);
    root->addStretch();

    connect(refresh, &QPushButton::clicked, this, &LowerThirdDock::refreshSources);
    connect(showBtn, &QPushButton::clicked, this, &LowerThirdDock::showNow);
    connect(startBtn, &QPushButton::clicked, this, &LowerThirdDock::startCycle);
    connect(stopBtn, &QPushButton::clicked, this, &LowerThirdDock::stopCycle);
    connect(technicalToggle, &QToolButton::toggled, this, [this](bool open) {
        technicalPanel->setVisible(open);
        technicalToggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });

    for (Layer *l : {&l1, &l2, &l3}) {
        connect(l->text, &QLineEdit::editingFinished, this, [this]() { saveSettings(); });
        connect(l->size, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { saveSettings(); });
        connect(l->source, &QComboBox::currentTextChanged, this, [this](const QString &) { saveSettings(); });
    }
    for (auto *s : {visible, interval, fadeIn, stagger, fadeOut})
        connect(s, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { saveSettings(); });
}

void LowerThirdDock::loadSettings()
{
    loading = true;
    QSettings s("DJLowerThird", "DJLowerThirdOBS");
    l1.text->setText(s.value("l1/text", l1.text->text()).toString());
    l2.text->setText(s.value("l2/text", l2.text->text()).toString());
    l3.text->setText(s.value("l3/text", l3.text->text()).toString());
    l1.size->setValue(s.value("l1/size", l1.size->value()).toInt());
    l2.size->setValue(s.value("l2/size", l2.size->value()).toInt());
    l3.size->setValue(s.value("l3/size", l3.size->value()).toInt());
    visible->setValue(s.value("timing/visible", visible->value()).toDouble());
    interval->setValue(s.value("timing/interval", interval->value()).toDouble());
    fadeIn->setValue(s.value("timing/fadeIn", fadeIn->value()).toDouble());
    stagger->setValue(s.value("timing/stagger", stagger->value()).toDouble());
    fadeOut->setValue(s.value("timing/fadeOut", fadeOut->value()).toDouble());
    loading = false;
}

void LowerThirdDock::saveSettings() const
{
    if (loading)
        return;
    QSettings s("DJLowerThird", "DJLowerThirdOBS");
    s.setValue("l1/text", l1.text->text());
    s.setValue("l2/text", l2.text->text());
    s.setValue("l3/text", l3.text->text());
    s.setValue("l1/source", l1.source->currentText());
    s.setValue("l2/source", l2.source->currentText());
    s.setValue("l3/source", l3.source->currentText());
    s.setValue("l1/size", l1.size->value());
    s.setValue("l2/size", l2.size->value());
    s.setValue("l3/size", l3.size->value());
    s.setValue("timing/visible", visible->value());
    s.setValue("timing/interval", interval->value());
    s.setValue("timing/fadeIn", fadeIn->value());
    s.setValue("timing/stagger", stagger->value());
    s.setValue("timing/fadeOut", fadeOut->value());
}

void LowerThirdDock::refreshSources()
{
    loading = true;
    auto names = textSources();
    const QString olds[] = {l1.source->currentText(), l2.source->currentText(), l3.source->currentText()};
    QComboBox *boxes[] = {l1.source, l2.source, l3.source};
    for (int j = 0; j < 3; ++j) {
        boxes[j]->blockSignals(true);
        boxes[j]->clear();
        for (const auto &n : names)
            boxes[j]->addItem(QString::fromStdString(n));
        int i = boxes[j]->findText(olds[j]);
        if (i >= 0)
            boxes[j]->setCurrentIndex(i);
        boxes[j]->blockSignals(false);
    }

    QSettings s("DJLowerThird", "DJLowerThirdOBS");
    const QString keys[] = {"l1/source", "l2/source", "l3/source"};
    for (int j = 0; j < 3; ++j) {
        QString saved = s.value(keys[j]).toString();
        int i = boxes[j]->findText(saved);
        if (i >= 0)
            boxes[j]->setCurrentIndex(i);
    }
    loading = false;
    saveSettings();
}

QString LowerThirdDock::sourceName(const Layer &l) const { return l.source->currentText(); }

double LowerThirdDock::smooth(double x) const
{
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

void LowerThirdDock::setItemVisible(const char *name, bool v)
{
    obs_source_t *cur = obs_frontend_get_current_scene();
    if (!cur)
        return;
    obs_scene_t *scene = obs_scene_from_source(cur);
    if (scene) {
        obs_sceneitem_t *item = obs_scene_find_source_recursive(scene, name);
        if (item)
            obs_sceneitem_set_visible(item, v);
    }
    obs_source_release(cur);
}

void LowerThirdDock::setOpacity(const char *name, int op)
{
    obs_source_t *s = obs_get_source_by_name(name);
    if (!s)
        return;
    obs_data_t *d = obs_source_get_settings(s);
    if (d) {
        obs_data_set_int(d, "opacity", std::clamp(op, 0, 100));
        obs_source_update(s, d);
        obs_data_release(d);
    }
    obs_source_release(s);
}

void LowerThirdDock::setText(const char *name, const char *txt, int size)
{
    obs_source_t *s = obs_get_source_by_name(name);
    if (!s)
        return;
    obs_data_t *d = obs_source_get_settings(s);
    if (d) {
        obs_data_set_string(d, "text", txt);
        obs_data_t *font = obs_data_get_obj(d, "font");
        if (font) {
            obs_data_set_int(font, "size", size);
            obs_data_set_obj(d, "font", font);
            obs_data_release(font);
        }
        obs_source_update(s, d);
        obs_data_release(d);
    }
    obs_source_release(s);
}

void LowerThirdDock::prepare()
{
    for (Layer *l : {&l1, &l2, &l3}) {
        QString n = sourceName(*l);
        if (n.isEmpty())
            continue;
        QByteArray name = n.toUtf8();
        QByteArray text = l->text->text().toUtf8();
        setText(name.constData(), text.constData(), l->size->value());
        setItemVisible(name.constData(), true);
        setOpacity(name.constData(), 0);
    }
}

void LowerThirdDock::launch()
{
    prepare();
    state = FadeIn;
    startMs = clock.elapsed();
    timer.start(16);
}

void LowerThirdDock::showNow()
{
    cycle = false;
    timer.stop();
    launch();
}

void LowerThirdDock::startCycle()
{
    cycle = true;
    timer.stop();
    launch();
}

void LowerThirdDock::stopCycle()
{
    cycle = false;
    timer.stop();
    state = Idle;
    for (Layer *l : {&l1, &l2, &l3}) {
        QString n = sourceName(*l);
        if (n.isEmpty())
            continue;
        QByteArray name = n.toUtf8();
        setOpacity(name.constData(), 0);
        setItemVisible(name.constData(), false);
    }
}

void LowerThirdDock::finishFadeOut()
{
    for (Layer *l : {&l1, &l2, &l3}) {
        QString n = sourceName(*l);
        if (n.isEmpty())
            continue;
        QByteArray name = n.toUtf8();
        setOpacity(name.constData(), 0);
        setItemVisible(name.constData(), false);
    }
    if (cycle) {
        state = Waiting;
        waitMs = clock.elapsed();
    } else {
        state = Idle;
        timer.stop();
    }
}

void LowerThirdDock::animationTick()
{
    qint64 now = clock.elapsed();

    if (state == FadeIn) {
        const double f = std::max(1.0, fadeIn->value() * 1000.0);
        const double st = std::max(0.0, stagger->value() * 1000.0);
        Layer *layers[3] = {&l1, &l2, &l3};
        bool all = true;
        for (int i = 0; i < 3; ++i) {
            QString n = sourceName(*layers[i]);
            if (n.isEmpty())
                continue;
            qint64 begin = startMs + (qint64)(st * i);
            double p = now < begin ? 0.0 : std::clamp((now - begin) / f, 0.0, 1.0);
            QByteArray name = n.toUtf8();
            setOpacity(name.constData(), (int)(smooth(p) * 100.0));
            if (p < 1.0)
                all = false;
        }
        if (all) {
            state = Visible;
            visibleMs = now;
        }
        return;
    }

    if (state == Visible) {
        if (now - visibleMs >= (qint64)(std::max(0.0, visible->value()) * 1000.0)) {
            state = FadeOut;
            startMs = now;
        }
        return;
    }

    if (state == FadeOut) {
        const double f = std::max(1.0, fadeOut->value() * 1000.0);
        double p = std::clamp((now - startMs) / f, 0.0, 1.0);
        int op = (int)((1.0 - smooth(p)) * 100.0);
        for (Layer *l : {&l1, &l2, &l3}) {
            QString n = sourceName(*l);
            if (!n.isEmpty()) {
                QByteArray name = n.toUtf8();
                setOpacity(name.constData(), op);
            }
        }
        if (p >= 1.0)
            finishFadeOut();
        return;
    }

    if (state == Waiting) {
        if (now - waitMs >= (qint64)(std::max(0.0, interval->value()) * 1000.0))
            launch();
    }
}
