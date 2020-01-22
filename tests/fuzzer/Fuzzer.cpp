/*
  This file is part of KDDockWidgets.

  Copyright (C) 2019-2020 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com
  Author: Sérgio Martins <sergio.martins@kdab.com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// We don't care about performance related checks in the tests
// clazy:excludeall=ctor-missing-parent-argument,missing-qobject-macro,range-loop,missing-typeinfo,detaching-member,function-args-by-ref,non-pod-global-static,reserve-candidates,qstring-allocations

#include "Fuzzer.h"
#include "DockRegistry_p.h"
#include "DockWidget.h"
#include "MainWindow.h"

#include <QJsonDocument>

#include <QString>
#include <QTest>

using namespace KDDockWidgets;
using namespace KDDockWidgets::Testing;
using namespace KDDockWidgets::Testing::Operations;

#define OPERATIONS_PER_TEST 200

static MainWindow* createMainWindow(const Fuzzer::MainWindowDescriptor &mwd)
{
    auto mainWindow = new MainWindow(mwd.name, mwd.mainWindowOption);

    mainWindow->setGeometry(mwd.geometry);

    mainWindow->show();
    return mainWindow;
}

static DockWidget* createDockWidget(const Fuzzer::DockWidgetDescriptor &dwd)
{
    auto dockWidget = new DockWidget(dwd.name);

    dockWidget->setWidget(new Testing::HostedWidget(dwd.minSize));

    if (dwd.isFloating)
        dockWidget->setGeometry(dwd.geometry);

    if (dwd.isVisible)
        dockWidget->show();

    return dockWidget;
}

static void createLayout(const Fuzzer::Layout &layout)
{
    for (const Fuzzer::MainWindowDescriptor &mwd : layout.mainWindows) {
        createMainWindow(mwd);
    }

    for (const Fuzzer::DockWidgetDescriptor &dwd : layout.dockWidgets) {
        createDockWidget(dwd);
    }
}

void Fuzzer::runTest(const Test &test, bool skipLastAndPause)
{
    m_currentTest = test;

    if (!DockRegistry::self()->isEmpty())
        qFatal("There's dock widgets and the start runTest");

    createLayout(test.initialLayout);
    int index = 0;

    auto operations = test.operations;
    if (skipLastAndPause)
        operations.removeLast();

    for (const auto &op : operations) {
        index++;
        op->execute();
        if (op->hasParams())
            qDebug() << "Ran" << op->description();
        QTest::qWait(m_operationDelayMS);
        DockRegistry::self()->checkSanityAll();
    }

    if (!skipLastAndPause) {
        for (MainWindowBase *mw : DockRegistry::self()->mainwindows())
            delete mw;

        for (FloatingWindow *fw : DockRegistry::self()->nestedwindows())
            delete fw;

        for (DockWidgetBase *dw : DockRegistry::self()->dockwidgets())
            delete dw;

        if (!DockRegistry::self()->isEmpty())
            qFatal("There's still dock widgets and the end of runTest");
    }
}

Fuzzer::Fuzzer(bool dumpJsonOnFailure, QObject *parent)
    : QObject(parent)
    , m_randomEngine(m_randomDevice())
    , m_dumpJsonOnFailure(dumpJsonOnFailure)
{
    Testing::installFatalMessageHandler();
    Testing::setWarningObserver(this);
}

Fuzzer::Layout Fuzzer::generateRandomLayout()
{
    // for now we only support 1 main window
    Fuzzer::Layout layout;
    Fuzzer::MainWindowDescriptor mainWindow;

    static int count = 0;
    count++;
    mainWindow.name = QStringLiteral("MainWindow-%1").arg(count);

    mainWindow.geometry = randomGeometry();
    mainWindow.mainWindowOption = MainWindowOption_None; // TODO: Maybe test other options
    layout.mainWindows << mainWindow;

    std::uniform_int_distribution<> numDocksDistrib(1, 10); // TODO: Increase
    const int numDockWidgets = numDocksDistrib(m_randomEngine);
    for (int i = 0; i < numDockWidgets; ++i) {
        layout.dockWidgets << generateRandomDockWidget();
    }

    return layout;
}

Fuzzer::DockWidgetDescriptor Fuzzer::generateRandomDockWidget()
{
    Fuzzer::DockWidgetDescriptor dwd;

    static int count = 0;
    count++;
    dwd.name = QStringLiteral("DockWidget-%1").arg(count);
    dwd.isFloating = getRandomBool(35);
    dwd.isVisible = getRandomBool(70);

    std::uniform_int_distribution<> minSizeDistriv(150, 600);

    dwd.minSize.setWidth(minSizeDistriv(m_randomEngine));
    dwd.minSize.setHeight(minSizeDistriv(m_randomEngine));

    QPoint pos = getRandomPos();
    std::uniform_int_distribution<> widthDistrib(dwd.minSize.width() + 50, dwd.minSize.width() + 600);
    std::uniform_int_distribution<> heightDistrib(dwd.minSize.height() + 50, dwd.minSize.height() + 600);
    dwd.geometry = QRect(pos, QSize(widthDistrib(m_randomEngine), heightDistrib(m_randomEngine)));

    return dwd;
}

Fuzzer::DockWidgetDescriptor::List Fuzzer::generateRandomDockWidgets(int num)
{
    Fuzzer::DockWidgetDescriptor::List dockWidgets;
    for (int i = 0; i < num; ++i) {
        dockWidgets << generateRandomDockWidget();
    }
    return dockWidgets;
}

bool Fuzzer::getRandomBool(int truePercentage)
{
    std::uniform_int_distribution<> distrib(1, 100);
    return distrib(m_randomEngine) < truePercentage;
}

Testing::AddDockWidgetParams Fuzzer::getRandomAddDockWidgetParams()
{
    AddDockWidgetParams params;

    if (auto dw = getRandomDockWidget()) {
        params.dockWidgetName = dw->uniqueName();
    } else {
        qWarning() << Q_FUNC_INFO << "No dock widgets exist yet!";
        return {};
    }

    if (auto mw = getRandomMainWindow()) {
        params.mainWindowName = mw->uniqueName();
    } else {
        qWarning() << Q_FUNC_INFO << "No main widgets exist yet!";
        return {};
    }

    if (getRandomBool()) {
        if (auto rt =  getRandomRelativeTo(params.mainWindow(), params.dockWidget())) {
            params.relativeToName = rt->uniqueName();
        }
    }

    params.location = getRandomLocation();
    params.addingOption = AddingOption_None; // TODO: Test the other ones

    return params;
}

MainWindowBase *Fuzzer::getRandomMainWindow()
{
    auto windows = DockRegistry::self()->mainwindows();
    if (windows.isEmpty()) {
        qWarning() << Q_FUNC_INFO << "No MainWindows exist yet!";
        return nullptr;
    }

    return windows.first();
}

DockWidgetBase *Fuzzer::getRandomDockWidget(const DockWidgetBase::List &excluding)
{
    auto docks = DockRegistry::self()->dockwidgets();
    for (auto dw : excluding)
        docks.removeOne(dw);

    if (docks.isEmpty())
        return nullptr;

    std::uniform_int_distribution<> locationDistrib(0, docks.size() - 1);
    return docks[locationDistrib(m_randomEngine)];
}

DockWidgetBase *Fuzzer::getRandomRelativeTo(MainWindowBase *mainWindow, DockWidgetBase *excluding)
{
    auto docks = DockRegistry::self()->dockwidgets();

    DockWidgetBase::List candidates;

    for (DockWidgetBase *dw : docks)  {
        if (dw != excluding && dw->window() == mainWindow)
            candidates << dw;
    }

    if (candidates.isEmpty())
        return nullptr;

    std::uniform_int_distribution<> locationDistrib(0, candidates.size() - 1);
    return candidates[locationDistrib(m_randomEngine)];
}

Location Fuzzer::getRandomLocation()
{
    std::uniform_int_distribution<> locationDistrib(1, 4);
    return Location(locationDistrib(m_randomEngine));
}

QPoint Fuzzer::getRandomPos()
{
    std::uniform_int_distribution<> posDistrib(0, 500);
    const int x = posDistrib(m_randomEngine);
    const int y = posDistrib(m_randomEngine);
    return {x, y};
}

OperationBase::Ptr Fuzzer::getRandomOperation()
{
    std::uniform_int_distribution<> operationDistrib(OperationType_None + 1, OperationType_Count - 1);
    auto operationType = OperationType(operationDistrib(m_randomEngine));

    return OperationBase::newOperation(this, operationType);
}

Fuzzer::Test Fuzzer::generateRandomTest()
{
    Fuzzer::Test test;
    test.initialLayout = generateRandomLayout();

    const int numOperationsPerTest = OPERATIONS_PER_TEST;
    test.operations.reserve(numOperationsPerTest);
    for (int i = 0; i < numOperationsPerTest; ++i)
        test.operations << getRandomOperation();

    return test;
}

Fuzzer::Test::List Fuzzer::generateRandomTests(int num)
{
    Fuzzer::Test::List tests;

    for (int i = 0; i < num; ++i) {
        tests << generateRandomTest();
    }

    return tests;
}

void Fuzzer::fuzz(FuzzerConfig config)
{
    const Fuzzer::Test::List tests = generateRandomTests(config.numTests);
    qDebug().noquote() << "Running" << QString("%1 tests...").arg(tests.size());

    for (const auto &test : tests) {
        runTest(test);
    }
}

void Fuzzer::fuzz(const QStringList &jsonFiles, bool skipLast)
{
    if (jsonFiles.size() > 1 && skipLast) {
        qFatal("Use -d only when passing a single json file");
    }

    for (const QString &jsonFile : jsonFiles)
        fuzz(jsonFile, skipLast);
}

void Fuzzer::fuzz(const QString &jsonFile, bool skipLast)
{
    QFile file(jsonFile);
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        const QVariantMap map = doc.toVariant().toMap();
        Test test = Test::fromVariantMap(this, map);
        runTest(test, skipLast);
    } else {
        qWarning() << Q_FUNC_INFO << "Failed to open file" << jsonFile;
    }
}

QRect Fuzzer::randomGeometry()
{
    std::uniform_int_distribution<> posDistrib(0, 500);
    std::uniform_int_distribution<> sizeDistrib(700, 1500);
    const int width = posDistrib(m_randomEngine);
    const int height = sizeDistrib(m_randomEngine);

    QPoint pos = getRandomPos();

    return QRect(pos, QSize(width, height));
}

void Fuzzer::onFatal()
{
    if (m_dumpJsonOnFailure) {
        // Tests failed! Let's dump
        m_currentTest.dumpToJsonFile("fuzzer_dump.json");
    }
}

void Fuzzer::setDelayBetweenOperations(int delay)
{
    m_operationDelayMS = delay;
}

void Fuzzer::Test::dumpToJsonFile(const QString &filename) const
{
    const QVariantMap map = toVariantMap();
    QJsonDocument jsonDoc = QJsonDocument::fromVariant(map);
    QFile file(filename);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(jsonDoc.toJson());
    } else {
        qDebug() << Q_FUNC_INFO << "Error opening file";
    }
    file.close();
}
