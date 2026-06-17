#include "multi_eca_app.h"
#include <QtWidgets/QApplication>


int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    multi_eca_app window;
    window.show();
          return app.exec();
}
