#include "lower-third-dock.hpp"
#include <obs.h>
#include <obs-frontend-api.h>
#include <obs-data.h>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMetaObject>
#include <util/config-file.h>
#include <util/bmem.h>
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
    setMinimumWidth(260);
    buildUi();
    clock.start();
    connect(&timer, &QTimer::timeout, this, &LowerThirdDock::animationTick);
    obs_frontend_add_event_callback(&LowerThirdDock::frontendEvent, this);

    // Scenes/profile may not be loaded yet at plugin load time; the
    // FINISHED_LOADING event below repeats this once they are.
    loadProfile();
    switchScene();
}

LowerThirdDock::~LowerThirdDock()
{
    obs_frontend_remove_event_callback(&LowerThirdDock::frontendEvent, this);
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
    // Everything lives inside a scroll area so the dock works at any height.
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget;
    scroll->setWidget(content);
    outer->addWidget(scroll);

    auto *root = new QVBoxLayout(content);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    auto *title = new QLabel("<b>DJ LOWER THIRD</b>");
    title->setAlignment(Qt::AlignCenter);
    root->addWidget(title);

    sceneLabel = new QLabel(this);
    sceneLabel->setAlignment(Qt::AlignCenter);
    sceneLabel->setWordWrap(true);
    root->addWidget(sceneLabel);

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

static const char *kSection = "DJLowerThird";
static const char *kKey = "Scenes";

QString LowerThirdDock::currentSceneName()
{
    obs_source_t *cur = obs_frontend_get_current_scene();
    if (!cur)
        return QString();
    const char *n = obs_source_get_name(cur);
    QString out = n ? QString::fromUtf8(n) : QString();
    obs_source_release(cur);
    return out;
}

QString LowerThirdDock::keyFor(const QString &scene)
{
    if (scene.isEmpty())
        return QString();
    char *col = obs_frontend_get_current_scene_collection();
    QString c = col ? QString::fromUtf8(col) : QString();
    bfree(col);
    return c + "::" + scene;
}

void LowerThirdDock::loadProfile()
{
    allConfigs = QJsonObject();
    config_t *cfg = obs_frontend_get_profile_config();
    if (!cfg)
        return;
    const char *v = config_get_string(cfg, kSection, kKey);
    if (!v || !*v)
        return;
    QJsonDocument d = QJsonDocument::fromJson(QByteArray::fromBase64(QByteArray(v)));
    if (d.isObject())
        allConfigs = d.object();
}

void LowerThirdDock::persist()
{
    config_t *cfg = obs_frontend_get_profile_config();
    if (!cfg)
        return;
    QByteArray json = QJsonDocument(allConfigs).toJson(QJsonDocument::Compact);
    QByteArray b64 = json.toBase64();
    config_set_string(cfg, kSection, kKey, b64.constData());
    config_save_safe(cfg, "tmp", nullptr);
}

// UI -> config of the scene currently shown
void LowerThirdDock::saveSettings()
{
    if (loading || sceneKey.isEmpty())
        return;
    QJsonObject o;
    QJsonArray t, src, z;
    for (Layer *l : {&l1, &l2, &l3}) {
        t.append(l->text->text());
        src.append(l->source->currentText());
        z.append(l->size->value());
    }
    o["t"] = t;
    o["s"] = src;
    o["z"] = z;
    o["vis"] = visible->value();
    o["int"] = interval->value();
    o["fi"] = fadeIn->value();
    o["st"] = stagger->value();
    o["fo"] = fadeOut->value();
    allConfigs[sceneKey] = o;
    persist();
}

void LowerThirdDock::populateCombos(bool keepSelection)
{
    auto names = textSources();
    QComboBox *boxes[] = {l1.source, l2.source, l3.source};
    for (int j = 0; j < 3; ++j) {
        QString old = keepSelection ? boxes[j]->currentText() : QString();
        boxes[j]->clear();
        for (const auto &n : names)
            boxes[j]->addItem(QString::fromStdString(n));
        int i = boxes[j]->findText(old);
        if (i >= 0)
            boxes[j]->setCurrentIndex(i);
    }
}

// config of the current scene -> UI (unconfigured scenes get defaults)
void LowerThirdDock::applyConfig()
{
    loading = true;
    populateCombos(false);

    const QJsonObject o = allConfigs.value(sceneKey).toObject();
    const QJsonArray t = o["t"].toArray(), src = o["s"].toArray(), z = o["z"].toArray();
    const QString defText[3] = {"DJ PEPITO", "@djpepito", "BUENOS AIRES"};
    const int defSize[3] = {48, 28, 22};
    Layer *layers[3] = {&l1, &l2, &l3};

    for (int j = 0; j < 3; ++j) {
        layers[j]->text->setText(j < t.size() ? t[j].toString() : defText[j]);
        layers[j]->size->setValue(j < z.size() ? z[j].toInt(defSize[j]) : defSize[j]);
        int i = layers[j]->source->findText(j < src.size() ? src[j].toString() : QString());
        if (i < 0 && j < layers[j]->source->count())
            i = j;
        if (i >= 0)
            layers[j]->source->setCurrentIndex(i);
    }
    visible->setValue(o.value("vis").toDouble(13.0));
    interval->setValue(o.value("int").toDouble(10.0));
    fadeIn->setValue(o.value("fi").toDouble(.30));
    stagger->setValue(o.value("st").toDouble(.15));
    fadeOut->setValue(o.value("fo").toDouble(.40));

    sceneLabel->setText(sceneName.isEmpty() ? QString("Escena: —")
                                            : QString("Escena: <b>%1</b>").arg(sceneName.toHtmlEscaped()));
    loading = false;
}

void LowerThirdDock::switchScene()
{
    const QString newScene = currentSceneName();
    const QString newKey = keyFor(newScene);

    saveSettings(); // keep edits of the scene we're leaving (no-op if none)

    if (!activeScene.isEmpty() && newScene != activeScene)
        stopCycle(); // animation belongs to the old scene

    sceneName = newScene;
    sceneKey = newKey;
    applyConfig();
}

void LowerThirdDock::frontendEvent(enum obs_frontend_event e, void *data)
{
    auto *self = static_cast<LowerThirdDock *>(data);
    QMetaObject::invokeMethod(self, [self, e]() { self->handleEvent(e); }, Qt::QueuedConnection);
}

void LowerThirdDock::handleEvent(enum obs_frontend_event e)
{
    switch (e) {
    case OBS_FRONTEND_EVENT_FINISHED_LOADING:
    case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
        sceneKey.clear(); // don't write old-profile UI into the new profile
        loadProfile();
        switchScene();
        break;
    case OBS_FRONTEND_EVENT_SCENE_CHANGED:
    case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
        switchScene();
        break;
    default:
        break;
    }
}

void LowerThirdDock::refreshSources()
{
    const bool was = loading;
    loading = true;
    populateCombos(true);
    loading = was;
    saveSettings();
}

QString LowerThirdDock::sourceName(const Layer &l) const { return l.source->currentText(); }

double LowerThirdDock::smooth(double x) const
{
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

void LowerThirdDock::setItemVisible(const char *sceneName_, const char *name, bool v)
{
    if (!sceneName_ || !*sceneName_)
        return;
    obs_source_t *sceneSrc = obs_get_source_by_name(sceneName_);
    if (!sceneSrc)
        return;
    obs_scene_t *scene = obs_scene_from_source(sceneSrc);
    if (scene) {
        obs_sceneitem_t *item = obs_scene_find_source_recursive(scene, name);
        if (item)
            obs_sceneitem_set_visible(item, v);
    }
    obs_source_release(sceneSrc);
}

// Fade a text source. Windows' GDI+ text has an "opacity" setting; the
// FreeType 2 text source (the only text source on macOS/Linux) doesn't, so
// there we fade by changing the alpha byte of its two colours.
void LowerThirdDock::setOpacity(const char *name, int op)
{
    obs_source_t *s = obs_get_source_by_name(name);
    if (!s)
        return;
    op = std::clamp(op, 0, 100);
    const char *id = obs_source_get_id(s);
    const bool ft2 = id && !strncmp(id, "text_ft2_source", 15);

    obs_data_t *d = obs_source_get_settings(s);
    if (d) {
        if (ft2) {
            const uint32_t alpha = (uint32_t)(op * 255 / 100);
            for (const char *key : {"color1", "color2"}) {
                uint32_t c = (uint32_t)obs_data_get_int(d, key);
                if (c == 0)
                    c = 0x00FFFFFF; // unset: white
                c = (alpha << 24) | (c & 0x00FFFFFF);
                obs_data_set_int(d, key, (long long)c);
            }
        } else {
            obs_data_set_int(d, "opacity", op);
        }
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
    const QByteArray scene = activeScene.toUtf8();
    for (Layer *l : {&l1, &l2, &l3}) {
        QString n = sourceName(*l);
        if (n.isEmpty())
            continue;
        QByteArray name = n.toUtf8();
        QByteArray text = l->text->text().toUtf8();
        setText(name.constData(), text.constData(), l->size->value());
        setItemVisible(scene.constData(), name.constData(), true);
        setOpacity(name.constData(), 0);
    }
}

void LowerThirdDock::launch()
{
    activeScene = currentSceneName();
    prepare();
    state = FadeIn;
    startMs = clock.elapsed();
    timer.start(16);
}

void LowerThirdDock::hideAll()
{
    const QByteArray scene = (activeScene.isEmpty() ? currentSceneName() : activeScene).toUtf8();
    for (Layer *l : {&l1, &l2, &l3}) {
        QString n = sourceName(*l);
        if (n.isEmpty())
            continue;
        QByteArray name = n.toUtf8();
        setOpacity(name.constData(), 0);
        setItemVisible(scene.constData(), name.constData(), false);
    }
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
    hideAll();
    activeScene.clear();
}

void LowerThirdDock::finishFadeOut()
{
    hideAll();
    if (cycle) {
        state = Waiting;
        waitMs = clock.elapsed();
    } else {
        state = Idle;
        timer.stop();
        activeScene.clear();
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
