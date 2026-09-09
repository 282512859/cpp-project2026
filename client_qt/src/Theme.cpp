#include "client_qt/Theme.h"

namespace client_qt {

namespace {

struct ThemeColors {
    const char* bg;
    const char* surface;
    const char* surfaceAlt;
    const char* border;
    const char* borderStrong;
    const char* text;
    const char* textMuted;
    const char* primary;
    const char* primaryHover;
    const char* primaryPressed;
    const char* primarySoft;
    const char* success;
    const char* successSoft;
    const char* warning;
    const char* warningSoft;
    const char* danger;
    const char* dangerSoft;
    const char* progressTrack;
};

const ThemeColors kLight = {
    "#f6f8fa", "#ffffff", "#eef1f4", "#d8dee4", "#afb8c1",
    "#1f2328", "#57606a", "#2f6fed", "#2861d8", "#2456c0", "#e5efff",
    "#1a7f37", "#dafbe1", "#9a6700", "#fff8c5", "#cf222e", "#ffebe9",
    "#eaeef2",
};

const ThemeColors kDark = {
    "#0d1117", "#161b22", "#21262d", "#30363d", "#57606a",
    "#e6edf3", "#8b949e", "#58a6ff", "#79c0ff", "#4c9bef", "#1f3a5f",
    "#3fb950", "#12261e", "#d29922", "#3a2d12", "#f85149", "#3d1d21",
    "#21262d",
};

QString applyTokens(QString qss, const ThemeColors& c) {
    qss.replace(QStringLiteral("{{bg}}"), QLatin1String(c.bg));
    qss.replace(QStringLiteral("{{surface}}"), QLatin1String(c.surface));
    qss.replace(QStringLiteral("{{surfaceAlt}}"), QLatin1String(c.surfaceAlt));
    qss.replace(QStringLiteral("{{border}}"), QLatin1String(c.border));
    qss.replace(QStringLiteral("{{borderStrong}}"), QLatin1String(c.borderStrong));
    qss.replace(QStringLiteral("{{text}}"), QLatin1String(c.text));
    qss.replace(QStringLiteral("{{textMuted}}"), QLatin1String(c.textMuted));
    qss.replace(QStringLiteral("{{primary}}"), QLatin1String(c.primary));
    qss.replace(QStringLiteral("{{primaryHover}}"), QLatin1String(c.primaryHover));
    qss.replace(QStringLiteral("{{primaryPressed}}"), QLatin1String(c.primaryPressed));
    qss.replace(QStringLiteral("{{primarySoft}}"), QLatin1String(c.primarySoft));
    qss.replace(QStringLiteral("{{success}}"), QLatin1String(c.success));
    qss.replace(QStringLiteral("{{successSoft}}"), QLatin1String(c.successSoft));
    qss.replace(QStringLiteral("{{warning}}"), QLatin1String(c.warning));
    qss.replace(QStringLiteral("{{warningSoft}}"), QLatin1String(c.warningSoft));
    qss.replace(QStringLiteral("{{danger}}"), QLatin1String(c.danger));
    qss.replace(QStringLiteral("{{dangerSoft}}"), QLatin1String(c.dangerSoft));
    qss.replace(QStringLiteral("{{progressTrack}}"), QLatin1String(c.progressTrack));
    return qss;
}

const char* kQssTemplate = R"(
QWidget {
    background: {{bg}};
    color: {{text}};
    font-family: "Segoe UI";
    font-size: 13px;
}
QFrame#surface, QFrame#loginForm, QFrame#sideSurface {
    background: {{surface}};
    border: 1px solid {{border}};
    border-radius: 8px;
}
QFrame#introPanel { background: {{bg}}; border: none; }
QFrame#topBar { background: {{surface}}; border: none; border-bottom: 1px solid {{border}}; }
QLabel#brand { color: {{text}}; font-size: 18px; font-weight: 650; }
QLabel#loginTitle { color: {{text}}; font-size: 26px; font-weight: 650; }
QLabel#pageTitle { color: {{text}}; font-size: 20px; font-weight: 650; }
QLabel#sectionTitle { color: {{text}}; font-size: 14px; font-weight: 650; }
QLabel#eyebrow { color: {{primary}}; font-size: 12px; font-weight: 650; }
QLabel#muted { color: {{textMuted}}; }
QLabel#statusPill {
    background: {{primarySoft}}; color: {{primary}};
    border-radius: 11px; padding: 3px 10px; font-weight: 600;
}
QLabel#statusPill[state="ok"] { background: {{successSoft}}; color: {{success}}; }
QLabel#statusPill[state="warn"] { background: {{warningSoft}}; color: {{warning}}; }
QLabel#statusPill[state="error"] { background: {{dangerSoft}}; color: {{danger}}; }
QLabel#statusPill[state="muted"] { background: {{surfaceAlt}}; color: {{textMuted}}; }
QLabel[state="info"] { color: {{textMuted}}; }
QLabel[state="ok"] { color: {{success}}; }
QLabel[state="error"] { color: {{danger}}; }
QLabel[state="warn"] { color: {{warning}}; }
QLabel#featureIcon {
    background: {{primarySoft}}; color: {{primary}};
    border-radius: 16px; font-size: 16px; font-weight: 700;
}
QLineEdit {
    background: {{surface}}; color: {{text}};
    border: 1px solid {{borderStrong}}; border-radius: 6px;
    padding: 8px 10px; min-height: 20px; selection-background-color: {{primary}};
}
QLineEdit:focus { border: 2px solid {{primary}}; padding: 7px 9px; }
QLineEdit:disabled { color: {{textMuted}}; background: {{surfaceAlt}}; }
QPushButton {
    background: {{primary}}; color: #ffffff; border: 1px solid {{primary}};
    border-radius: 6px; padding: 8px 14px; font-weight: 600;
}
QPushButton:hover { background: {{primaryHover}}; border-color: {{primaryHover}}; }
QPushButton:pressed { background: {{primaryPressed}}; }
QPushButton:disabled { background: {{surfaceAlt}}; color: {{textMuted}}; border-color: {{border}}; }
QPushButton#secondaryButton {
    background: {{surface}}; color: {{text}}; border-color: {{border}};
}
QPushButton#secondaryButton:hover { background: {{surfaceAlt}}; border-color: {{borderStrong}}; }
QToolBar { background: transparent; border: none; spacing: 4px; padding: 0; }
QToolButton {
    background: {{surface}}; color: {{text}}; border: 1px solid {{border}};
    border-radius: 6px; padding: 7px 10px; font-weight: 550;
}
QToolButton:hover { background: {{surfaceAlt}}; border-color: {{borderStrong}}; }
QTreeView {
    background: {{surface}}; color: {{text}};
    alternate-background-color: {{surface}};
    border: 1px solid {{border}}; border-radius: 6px; outline: none;
    show-decoration-selected: 1;
}
QHeaderView::section {
    background: {{surfaceAlt}}; color: {{textMuted}}; border: none;
    border-bottom: 1px solid {{border}}; padding: 9px 10px; font-weight: 600;
}
QTreeView::item { padding: 8px 7px; border-bottom: 1px solid {{surfaceAlt}}; }
QTreeView::item:hover { background: {{surfaceAlt}}; }
QTreeView::item:selected { background: {{primarySoft}}; color: {{text}}; }
QTextEdit, QPlainTextEdit {
    background: {{surfaceAlt}}; color: {{text}};
    border: 1px solid {{border}}; border-radius: 6px; padding: 6px;
}
QProgressBar {
    background: {{progressTrack}}; border: none; border-radius: 3px;
    min-height: 6px; max-height: 6px; color: transparent;
}
QProgressBar::chunk { background: {{primary}}; border-radius: 3px; }
QMenu { background: {{surface}}; color: {{text}}; border: 1px solid {{border}}; padding: 4px; }
QMenu::item { padding: 7px 28px 7px 10px; border-radius: 4px; }
QMenu::item:selected { background: {{primarySoft}}; color: {{text}}; }
QMenu::separator { height: 1px; background: {{border}}; margin: 4px 6px; }
QScrollBar:vertical { background: {{surface}}; width: 12px; border: none; }
QScrollBar::handle:vertical { background: {{borderStrong}}; border-radius: 5px; min-height: 24px; margin: 2px; }
QScrollBar::handle:vertical:hover { background: {{textMuted}}; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QScrollBar:horizontal { background: {{surface}}; height: 12px; border: none; }
QScrollBar::handle:horizontal { background: {{borderStrong}}; border-radius: 5px; min-width: 24px; margin: 2px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QToolTip { background: {{surface}}; color: {{text}}; border: 1px solid {{border}}; padding: 4px 8px; }
)";

} // namespace

QString themeStyleSheet(bool dark) {
    return applyTokens(QString::fromUtf8(kQssTemplate),
                       dark ? kDark : kLight);
}

QString themeToggleLabel(bool dark) {
    // When currently light, the toggle switches to dark, and vice versa.
    return dark ? QStringLiteral("Light") : QStringLiteral("Dark");
}

} // namespace client_qt
