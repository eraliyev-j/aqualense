#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>
#include <QFont>

int main(int argc, char *argv[])
{
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("AquaLens"));
    QApplication::setApplicationName(QStringLiteral("AquaLens"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    QApplication::setStyle(QStringLiteral("Fusion"));

    QFont font = QApplication::font();
    font.setFamilies({QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI"),
                      QStringLiteral("Inter"), QStringLiteral("Noto Sans"), QStringLiteral("DejaVu Sans")});
    font.setPointSizeF(10);
    QApplication::setFont(font);
    QApplication::setWindowIcon(Theme::appIcon());

    MainWindow window;
    window.resize(1440, 900);
    window.show();

    // Optional: open a CSV passed on the command line (e.g. "Open with…" / drag onto the .exe)
    const QStringList args = QApplication::arguments();
    if (args.size() > 1)
        window.openFile(args.at(1));

    return app.exec();
}
