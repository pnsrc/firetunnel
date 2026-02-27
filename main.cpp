#include <QApplication>
#include <QMainWindow>
#include <QIcon>

#include "MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/assets/logo.png"));
    QMainWindow *w = createMainWindow();
    w->show();
    return app.exec();
}
