#include "operatedb.h"
#include "server.h"

#include<QDebug>

#include <QApplication>



int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    operateDB::getInstance().connectSQL();
    Server w;
    //w.show();
    
    return a.exec();
}
