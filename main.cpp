#include <QApplication>
#include <QMainWindow>

#include "MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QMainWindow *w = createMainWindow();
    w->show();
    return app.exec();
}
