/*
*  Copyright 2017  Smith AR <audoban@openmailbox.org>
*                  Michail Vourlakos <mvourlakos@gmail.com>
*
*  This file is part of Latte-Dock
*
*  Latte-Dock is free software; you can redistribute it and/or
*  modify it under the terms of the GNU General Public License as
*  published by the Free Software Foundation; either version 2 of
*  the License, or (at your option) any later version.
*
*  Latte-Dock is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "layout.h"

#include "screenpool.h"

#include <QDir>
#include <QFile>
#include <KSharedConfig>

#include <KActivities/Consumer>

namespace Latte {

const QString Layout::MultipleLayoutsName = ".multiple-layouts_hidden";

Layout::Layout(QObject *parent, QString layoutFile, QString assignedName)
    : QObject(parent)
{
    qDebug() << "Layout file to create object: " << layoutFile << " with name: " << assignedName;

    if (QFile(layoutFile).exists()) {
        if (assignedName.isEmpty()) {
            assignedName =  layoutName(layoutFile);
        }

        //!this order is important because setFile initializes also the m_layoutGroup
        setFile(layoutFile);
        setName(assignedName);
        loadConfig();
        init();
    }
}

Layout::~Layout()
{
    if (!m_layoutFile.isEmpty()) {
        m_layoutGroup.sync();
    }
}

void Layout::syncToLayoutFile()
{
    if (!m_corona) {
        return;
    }

    KSharedConfigPtr filePtr = KSharedConfig::openConfig(m_layoutFile);

    KConfigGroup oldContainments = KConfigGroup(filePtr, "Containments");
    oldContainments.deleteGroup();
    oldContainments.sync();

    qDebug() << " LAYOUT :: " << m_layoutName << " is syncing its original file.";

    foreach (auto containment, m_containments) {
        containment->config().writeEntry("layoutId", "");
        KConfigGroup newGroup = oldContainments.group(QString::number(containment->id()));
        containment->config().copyTo(&newGroup);
    }

    oldContainments.sync();
}

void Layout::unloadContainments()
{
    if (!m_corona) {
        return;
    }

    qDebug() << "Layout - " + name() + " unload: containments ... size ::: " << m_containments.size()
             << " ,dockViews in memory ::: " << m_dockViews.size()
             << " ,hidden dockViews in memory :::  " << m_waitingDockViews.size();

    foreach (auto view, m_dockViews) {
        view->disconnectSensitiveSignals();
    }

    foreach (auto view, m_waitingDockViews) {
        view->disconnectSensitiveSignals();
    }

    m_unloadedContainmentsIds.clear();

    QList<Plasma::Containment *> systrays;

    //!identify systrays and unload them first
    foreach (auto containment, m_containments) {
        if (Plasma::Applet *parentApplet = qobject_cast<Plasma::Applet *>(containment->parent())) {
            systrays.append(containment);
        }
    }

    while (!systrays.isEmpty()) {
        Plasma::Containment *systray = systrays.at(0);
        m_unloadedContainmentsIds << QString::number(systray->id());
        systrays.removeFirst();
        m_containments.removeAll(systray);
        delete systray;
    }

    while (!m_containments.isEmpty()) {
        Plasma::Containment *containment = m_containments.at(0);
        m_unloadedContainmentsIds << QString::number(containment->id());
        m_containments.removeFirst();
        delete containment;
    }
}

void Layout::unloadDockViews()
{
    if (!m_corona) {
        return;
    }

    qDebug() << "Layout - " + name() + " unload: dockViews ... size: " << m_dockViews.size();

    qDeleteAll(m_dockViews);
    qDeleteAll(m_waitingDockViews);
    m_dockViews.clear();
    m_waitingDockViews.clear();
}

void Layout::init()
{
    connect(this, &Layout::activitiesChanged, this, &Layout::saveConfig);
    connect(this, &Layout::versionChanged, this, &Layout::saveConfig);
    connect(this, &Layout::colorChanged, this, &Layout::saveConfig);
    connect(this, &Layout::showInMenuChanged, this, &Layout::saveConfig);
    connect(this, &Layout::launchersChanged, this, &Layout::saveConfig);
}

void Layout::initToCorona(DockCorona *corona)
{
    m_corona = corona;

    foreach (auto containment, m_corona->containments()) {
        if (m_corona->layoutManager()->memoryUsage() == Dock::SingleLayout) {
            addContainment(containment);
        } else if (m_corona->layoutManager()->memoryUsage() == Dock::MultipleLayouts) {
            QString layoutId = containment->config().readEntry("layoutId", QString());

            if (!layoutId.isEmpty() && (layoutId == m_layoutName)) {
                addContainment(containment);
            }
        }
    }

    qDebug() << "Layout ::::: " << name() << " added contaiments ::: " << m_containments.size();

    updateLastUsedActivity();

    connect(m_corona, &Plasma::Corona::containmentAdded, this, &Layout::addContainment);

    connect(m_corona->m_activityConsumer, &KActivities::Consumer::currentActivityChanged,
            this, &Layout::updateLastUsedActivity);
}

int Layout::version() const
{
    return m_version;
}

void Layout::setVersion(int ver)
{
    if (m_version == ver) {
        return;
    }

    m_version = ver;

    emit versionChanged();
}

bool Layout::showInMenu() const
{
    return m_showInMenu;
}

void Layout::setShowInMenu(bool show)
{
    if (m_showInMenu == show) {
        return;
    }

    m_showInMenu = show;
    emit showInMenuChanged();
}

QString Layout::name() const
{
    return m_layoutName;
}

void Layout::setName(QString name)
{
    if (m_layoutName == name) {
        return;
    }

    qDebug() << "Layout name:" << name;

    m_layoutName = name;

    emit nameChanged();
}

void Layout::renameLayout(QString newName)
{
    if (m_layoutFile != Importer::layoutFilePath(newName)) {
        setFile(Importer::layoutFilePath(newName));
    }

    if (m_layoutName != newName) {
        setName(newName);
    }

    //! thus this is a linked file
    if (m_corona) {
        foreach (auto containment, m_containments) {
            containment->config().writeEntry("layoutId", m_layoutName);
        }
    }
}

QString Layout::color() const
{
    return m_color;
}

void Layout::setColor(QString color)
{
    if (m_color == color) {
        return;
    }

    m_color = color;
    emit colorChanged();
}


QString Layout::file() const
{
    return m_layoutFile;
}

void Layout::setFile(QString file)
{
    if (m_layoutFile == file) {
        return;
    }

    qDebug() << "Layout file:" << file;

    m_layoutFile = file;

    KSharedConfigPtr filePtr = KSharedConfig::openConfig(m_layoutFile);
    m_layoutGroup = KConfigGroup(filePtr, "LayoutSettings");

    emit fileChanged();
}

QStringList Layout::launchers() const
{
    return m_launchers;
}

void Layout::setLaunchers(QStringList launcherList)
{
    if (m_launchers == launcherList)
        return;

    m_launchers = launcherList;

    emit launchersChanged();
}

QStringList Layout::activities() const
{
    return m_activities;
}

void Layout::setActivities(QStringList activities)
{
    if (m_activities == activities) {
        return;
    }

    m_activities = activities;

    emit activitiesChanged();
}

QStringList Layout::unloadedContainmentsIds()
{
    return m_unloadedContainmentsIds;
}

bool Layout::isActiveLayout() const
{
    if (!m_corona) {
        return false;
    }

    Layout *activeLayout = m_corona->layoutManager()->activeLayout(m_layoutName);

    if (activeLayout) {
        return true;
    } else {
        return false;
    }
}

bool Layout::isOriginalLayout() const
{
    return m_layoutName != MultipleLayoutsName;
}

bool Layout::layoutIsBroken() const
{
    if (m_layoutFile.isEmpty() || !QFile(m_layoutFile).exists()) {
        return false;
    }

    QStringList ids;
    QStringList conts;
    QStringList applets;

    KSharedConfigPtr lFile = KSharedConfig::openConfig(m_layoutFile);


    if (!m_corona) {
        KConfigGroup containmentsEntries = KConfigGroup(lFile, "Containments");
        ids << containmentsEntries.groupList();
        conts << ids;

        foreach (auto cId, containmentsEntries.groupList()) {
            auto appletsEntries = containmentsEntries.group(cId).group("Applets");

            ids << appletsEntries.groupList();
            applets << appletsEntries.groupList();
        }
    } else {
        foreach (auto containment, m_containments) {
            ids << QString::number(containment->id());
            conts << ids;

            foreach (auto applet, containment->applets()) {
                ids << QString::number(applet->id());
                applets << QString::number(applet->id());
            }
        }
    }

    QSet<QString> idsSet = QSet<QString>::fromList(ids);

    /* a different way to count duplicates
    QMap<QString, int> countOfStrings;

    for (int i = 0; i < ids.count(); i++) {
        countOfStrings[ids[i]]++;
    }*/

    if (idsSet.count() != ids.count()) {
        qDebug() << "   ----   ERROR - BROKEN LAYOUT :: " << m_layoutName << " ----";

        if (!m_corona) {
            qDebug() << "   ---- file : " << m_layoutFile;
        } else {
            qDebug() << "   ---- in multiple layouts hidden file : " << Importer::layoutFilePath(Layout::MultipleLayoutsName);
        }

        qDebug() << "Contaiments :: " << conts;
        qDebug() << "Applets :: " << applets;

        foreach (QString c, conts) {
            if (applets.contains(c)) {
                qDebug() << "Error: Same applet and containment id found ::: " << c;
            }
        }

        for (int i = 0; i < ids.count(); ++i) {
            for (int j = i + 1; j < ids.count(); ++j) {
                if (ids[i] == ids[j]) {
                    qDebug() << "Error: Applets with same id ::: " << ids[i];
                }
            }
        }

        qDebug() << "  -- - -- - -- - -- - - -- - - - - -- - - - - ";

        if (!m_corona) {
            KConfigGroup containmentsEntries = KConfigGroup(lFile, "Containments");

            foreach (auto cId, containmentsEntries.groupList()) {
                auto appletsEntries = containmentsEntries.group(cId).group("Applets");

                qDebug() << " CONTAINMENT : " << cId << " APPLETS : " << appletsEntries.groupList();
            }
        } else {
            foreach (auto containment, m_containments) {
                QStringList appletsIds;

                foreach (auto applet, containment->applets()) {
                    appletsIds << QString::number(applet->id());
                }

                qDebug() << " CONTAINMENT : " << containment->id() << " APPLETS : " << appletsIds.join(",");
            }
        }

        return true;
    }

    return false;
}


QString Layout::layoutName(const QString &fileName)
{
    int lastSlash = fileName.lastIndexOf("/");
    QString tempLayoutFile = fileName;
    QString layoutName = tempLayoutFile.remove(0, lastSlash + 1);

    int ext = layoutName.lastIndexOf(".layout.latte");
    layoutName = layoutName.remove(ext, 13);

    return layoutName;
}

void Layout::loadConfig()
{
    m_version = m_layoutGroup.readEntry("version", 2);
    m_color = m_layoutGroup.readEntry("color", QString("blue"));
    m_showInMenu = m_layoutGroup.readEntry("showInMenu", false);
    m_activities = m_layoutGroup.readEntry("activities", QStringList());
    m_launchers = m_layoutGroup.readEntry("launchers", QStringList());

    emit activitiesChanged();
}

void Layout::saveConfig()
{
    qDebug() << "layout is saving... for layout:" << m_layoutName;
    m_layoutGroup.writeEntry("version", m_version);
    m_layoutGroup.writeEntry("showInMenu", m_showInMenu);
    m_layoutGroup.writeEntry("color", m_color);
    m_layoutGroup.writeEntry("launchers", m_launchers);
    m_layoutGroup.writeEntry("activities", m_activities);

    m_layoutGroup.sync();
}

//! Containments Actions

void Layout::addContainment(Plasma::Containment *containment)
{
    if (!containment || m_containments.contains(containment)) {
        return;
    }

    bool containmentInLayout{false};

    if (m_corona->layoutManager()->memoryUsage() == Dock::SingleLayout) {
        m_containments.append(containment);
        containmentInLayout = true;
    } else if (m_corona->layoutManager()->memoryUsage() == Dock::MultipleLayouts) {
        QString layoutId = containment->config().readEntry("layoutId", QString());

        if (!layoutId.isEmpty() && (layoutId == m_layoutName)) {
            m_containments.append(containment);
            containmentInLayout = true;
        }
    }

    if (containmentInLayout) {
        addDock(containment);
        connect(containment, &QObject::destroyed, this, &Layout::containmentDestroyed);
    }
}

QHash<const Plasma::Containment *, DockView *> *Layout::dockViews()
{
    return &m_dockViews;
}

QList<Plasma::Containment *> *Layout::containments()
{
    return &m_containments;
}

const QStringList Layout::appliedActivities()
{
    if (!m_corona) {
        return {};
    }

    if (m_corona->layoutManager()->memoryUsage() == Dock::SingleLayout) {
        return {"0"};
    } else if (m_corona->layoutManager()->memoryUsage() == Dock::MultipleLayouts) {
        if (m_activities.isEmpty()) {
            return m_corona->layoutManager()->orphanedActivities();
        } else {
            return m_activities;
        }
    }
}

QString Layout::lastUsedActivityId()
{
    return m_lastUsedActivityId;
}

void Layout::updateLastUsedActivity()
{
    if (!m_corona) {
        return;
    }

    QString currentId = m_corona->activitiesConsumer()->currentActivity();

    if (appliedActivities().contains(currentId)) {
        m_lastUsedActivityId = currentId;
    }
}

void Layout::destroyedChanged(bool destroyed)
{
    if (!m_corona) {
        return;
    }

    qDebug() << "dock containment destroyed changed!!!!";
    Plasma::Containment *sender = qobject_cast<Plasma::Containment *>(QObject::sender());

    if (!sender) {
        return;
    }

    if (destroyed) {
        m_waitingDockViews[sender] = m_dockViews.take(static_cast<Plasma::Containment *>(sender));
    } else {
        m_dockViews[sender] = m_waitingDockViews.take(static_cast<Plasma::Containment *>(sender));
    }

    emit m_corona->docksCountChanged();
    emit m_corona->availableScreenRectChanged();
    emit m_corona->availableScreenRegionChanged();
}

void Layout::containmentDestroyed(QObject *cont)
{
    if (!m_corona) {
        return;
    }

    Plasma::Containment *containment = static_cast<Plasma::Containment *>(cont);

    if (containment) {
        int containmentIndex = m_containments.indexOf(containment);

        if (containmentIndex >= 0) {
            m_containments.removeAt(containmentIndex);
        }

        qDebug() << "Layout " << name() << " :: containment destroyed!!!!";
        auto view = m_dockViews.take(containment);

        if (!view) {
            view = m_waitingDockViews.take(containment);
        }

        if (view) {
            view->disconnectSensitiveSignals();
            view->deleteLater();

            emit m_corona->docksCountChanged();
            emit m_corona->availableScreenRectChanged();
            emit m_corona->availableScreenRegionChanged();
        }
    }
}

void Layout::addDock(Plasma::Containment *containment, bool forceLoading, int expDockScreen)
{
    qDebug() << "Layout :::: " << m_layoutName << " ::: addDock was called... m_containments :: " << m_containments.size();

    if (!containment || !m_corona || !containment->kPackage().isValid()) {
        qWarning() << "the requested containment plugin can not be located or loaded";
        return;
    }

    auto metadata = containment->kPackage().metadata();

    qDebug() << "step 1...";

    if (metadata.pluginId() != "org.kde.latte.containment")
        return;

    qDebug() << "step 2...";

    for (auto *dock : m_dockViews) {
        if (dock->containment() == containment)
            return;
    }

    qDebug() << "step 3...";

    QScreen *nextScreen{qGuiApp->primaryScreen()};

    bool onPrimary = containment->config().readEntry("onPrimary", true);
    int id = containment->screen();

    if (id == -1 && expDockScreen == -1) {
        id = containment->lastScreen();
    }

    if (expDockScreen > -1) {
        id = expDockScreen;
    }

    qDebug() << "add dock - containment id: " << containment->id() << " ,screen id : " << id << " ,onprimary:" << onPrimary << " ,forceDockLoad:" << forceLoading;

    if (id >= 0 && !onPrimary && !forceLoading) {
        QString connector = m_corona->screenPool()->connector(id);
        qDebug() << "add dock - connector : " << connector;
        bool found{false};

        foreach (auto scr, qGuiApp->screens()) {
            if (scr && scr->name() == connector) {
                found = true;
                nextScreen = scr;
                break;
            }
        }

        if (!found) {
            qDebug() << "adding dock rejected, screen not available ! : " << connector;
            return;
        }
    } else if (onPrimary) {
        if (m_corona->explicitDockOccupyEdge(m_corona->screenPool()->primaryScreenId(), containment->location())) {
            qDebug() << "CORONA ::: adding dock rejected, the edge is occupied by explicit dock ! : " <<  containment->location();
            //we must check that an onPrimary dock should never catch up the same edge on
            //the same screen with an explicit dock
            return;
        }
    }

    qDebug() << "Adding dock for container...";
    qDebug() << "onPrimary: " << onPrimary << "screen!!! :" << nextScreen->name();

    //! it is used to set the correct flag during the creation
    //! of the window... This of course is also used during
    //! recreations of the window between different visibility modes
    auto mode = static_cast<Dock::Visibility>(containment->config().readEntry("visibility", static_cast<int>(Dock::DodgeActive)));
    bool dockWin{true};

    if (mode == Dock::AlwaysVisible || mode == Dock::WindowsGoBelow) {
        dockWin = true;
    } else {
        dockWin = containment->config().readEntry("dockWindowBehavior", true);
    }

    auto dockView = new DockView(m_corona, nextScreen, dockWin);
    dockView->init();
    dockView->setContainment(containment);
    dockView->setManagedLayout(this);

    //! force this special dock case to become primary
    //! even though it isnt
    if (forceLoading) {
        dockView->setOnPrimary(true);
    }

    //  connect(containment, &QObject::destroyed, this, &Layout::containmentDestroyed);
    connect(containment, &Plasma::Applet::destroyedChanged, this, &Layout::destroyedChanged);
    connect(containment, &Plasma::Applet::locationChanged, m_corona, &DockCorona::dockLocationChanged);
    connect(containment, &Plasma::Containment::appletAlternativesRequested
            , m_corona, &DockCorona::showAlternativesForApplet, Qt::QueuedConnection);

    if (m_corona->layoutManager()->memoryUsage() == Dock::MultipleLayouts) {
        connect(containment, &Plasma::Containment::appletCreated, this, &Layout::appletCreated);
    }

    //! Qt 5.9 creates a crash for this in wayland, that is why the check is used
    //! but on the other hand we need this for copy to work correctly and show
    //! the copied dock under X11
    //if (!KWindowSystem::isPlatformWayland()) {
    dockView->show();
    //}

    m_dockViews[containment] = dockView;

    emit m_corona->docksCountChanged();
}

void Layout::copyDock(Plasma::Containment *containment)
{
    if (!containment || !m_corona)
        return;

    qDebug() << "copying containment layout";
    //! Settting mutable for create a containment
    m_corona->setImmutability(Plasma::Types::Mutable);

    QString temp1File = QDir::homePath() + "/.config/lattedock.copy1.bak";

    //! WE NEED A WAY TO COPY A CONTAINMENT!!!!
    QFile copyFile(temp1File);

    if (copyFile.exists())
        copyFile.remove();


    KSharedConfigPtr newFile = KSharedConfig::openConfig(temp1File);
    KConfigGroup copied_conts = KConfigGroup(newFile, "Containments");
    KConfigGroup copied_c1 = KConfigGroup(&copied_conts, QString::number(containment->id()));
    KConfigGroup copied_systray;

    // toCopyContainmentIds << QString::number(containment->id());
    //  toCopyAppletIds << containment->config().group("Applets").groupList();
    containment->config().copyTo(&copied_c1);

    //!investigate if there is a systray in the containment to copy also
    int systrayId = -1;
    QString systrayAppletId;
    auto applets = containment->config().group("Applets");

    foreach (auto applet, applets.groupList()) {
        KConfigGroup appletSettings = applets.group(applet).group("Configuration");

        int tSysId = appletSettings.readEntry("SystrayContainmentId", -1);

        if (tSysId != -1) {
            systrayId = tSysId;
            systrayAppletId = applet;
            qDebug() << "systray was found in the containment... ::: " << tSysId;
            break;
        }
    }

    if (systrayId != -1) {
        Plasma::Containment *systray{nullptr};

        foreach (auto containment, m_corona->containments()) {
            if (containment->id() == systrayId) {
                systray = containment;
                break;
            }
        }

        if (systray) {
            copied_systray = KConfigGroup(&copied_conts, QString::number(systray->id()));
            //  toCopyContainmentIds << QString::number(systray->id());
            //  toCopyAppletIds << systray->config().group("Applets").groupList();
            systray->config().copyTo(&copied_systray);
        }
    }

    //! end of systray specific code

    //! update ids to unique ones
    QString temp2File = newUniqueIdsLayoutFromFile(temp1File);


    //! Finally import the configuration
    QList<Plasma::Containment *> importedDocks = importLayoutFile(temp2File);

    Plasma::Containment *newContainment{nullptr};

    if (importedDocks.size() == 1) {
        newContainment = importedDocks[0];
    }

    if (!newContainment || !newContainment->kPackage().isValid()) {
        qWarning() << "the requested containment plugin can not be located or loaded";
        return;
    }

    auto config = newContainment->config();

    //in multi-screen environment the copied dock is moved to alternative screens first
    const auto screens = qGuiApp->screens();
    auto dock = m_dockViews[containment];

    bool setOnExplicitScreen = false;

    int dockScrId = -1;
    int copyScrId = -1;

    if (dock) {
        dockScrId = m_corona->screenPool()->id(dock->currentScreen());
        qDebug() << "COPY DOCK SCREEN ::: " << dockScrId;

        if (dockScrId != -1 && screens.count() > 1) {
            foreach (auto scr, screens) {
                copyScrId = m_corona->screenPool()->id(scr->name());

                //the screen must exist and not be the same with the original dock
                if (copyScrId > -1 && copyScrId != dockScrId) {
                    QList<Plasma::Types::Location> fEdges = m_corona->freeEdges(copyScrId);

                    if (fEdges.contains((Plasma::Types::Location)containment->location())) {
                        ///set this containment to an explicit screen
                        config.writeEntry("onPrimary", false);
                        config.writeEntry("lastScreen", copyScrId);
                        newContainment->setLocation(containment->location());

                        qDebug() << "COPY DOCK SCREEN NEW SCREEN ::: " << copyScrId;

                        setOnExplicitScreen = true;
                        break;
                    }
                }
            }
        }
    }

    if (!setOnExplicitScreen) {
        QList<Plasma::Types::Location> edges = m_corona->freeEdges(newContainment->screen());

        if (edges.count() > 0) {
            newContainment->setLocation(edges.at(0));
        } else {
            newContainment->setLocation(Plasma::Types::BottomEdge);
        }

        config.writeEntry("onPrimary", false);
        config.writeEntry("lastScreen", dockScrId);
    }

    newContainment->config().sync();

    if (setOnExplicitScreen && copyScrId > -1) {
        qDebug() << "Copy Dock in explicit screen ::: " << copyScrId;
        addDock(newContainment, copyScrId);
        newContainment->reactToScreenChange();
    } else {
        qDebug() << "Copy Dock in current screen...";
        addDock(newContainment, dockScrId);
    }
}

void Layout::appletCreated(Plasma::Applet *applet)
{
    //! In Multiple Layout the orphaned systrays must be assigned to layouts
    //! when the user adds them
    KConfigGroup appletSettings = applet->containment()->config().group("Applets").group(QString::number(applet->id())).group("Configuration");

    int systrayId = appletSettings.readEntry("SystrayContainmentId", -1);

    if (systrayId != -1) {
        uint sId = (uint)systrayId;

        foreach (auto containment, m_corona->containments()) {
            if (containment->id() == sId) {
                containment->config().writeEntry("layoutId", m_layoutName);
            }

            addContainment(containment);
        }
    }
}

void Layout::importToCorona()
{
    if (!m_corona) {
        return;
    }

    //! Settting mutable for create a containment
    m_corona->setImmutability(Plasma::Types::Mutable);

    QString temp1File = QDir::homePath() + "/.config/lattedock.copy1.bak";

    //! WE NEED A WAY TO COPY A CONTAINMENT!!!!
    QFile copyFile(temp1File);

    if (copyFile.exists())
        copyFile.remove();

    KSharedConfigPtr filePtr = KSharedConfig::openConfig(m_layoutFile);
    KSharedConfigPtr newFile = KSharedConfig::openConfig(temp1File);
    KConfigGroup copyGroup = KConfigGroup(newFile, "Containments");
    KConfigGroup current_containments = KConfigGroup(filePtr, "Containments");

    current_containments.copyTo(&copyGroup);

    copyGroup.sync();

    //! update ids to unique ones
    QString temp2File = newUniqueIdsLayoutFromFile(temp1File);


    //! Finally import the configuration
    importLayoutFile(temp2File);
}

QString Layout::availableId(QStringList all, QStringList assigned, int base)
{
    bool found = false;

    int i = base;

    while (!found && i < 32000) {
        QString iStr = QString::number(i);

        if (!all.contains(iStr) && !assigned.contains(iStr)) {
            return iStr;
        }

        i++;
    }

    return QString("");
}

QString Layout::newUniqueIdsLayoutFromFile(QString file)
{
    if (!m_corona) {
        return QString();
    }

    QString tempFile = QDir::homePath() + "/.config/lattedock.copy2.bak";

    QFile copyFile(tempFile);

    if (copyFile.exists())
        copyFile.remove();

    //! BEGIN updating the ids in the temp file
    QStringList allIds;
    allIds << m_corona->containmentsIds();
    allIds << m_corona->appletsIds();

    QStringList toInvestigateContainmentIds;
    QStringList toInvestigateAppletIds;
    QStringList toInvestigateSystrayContIds;

    //! first is the systray containment id
    QHash<QString, QString> systrayParentContainmentIds;
    QHash<QString, QString> systrayAppletIds;

    //qDebug() << "Ids:" << allIds;

    //qDebug() << "to copy containments: " << toCopyContainmentIds;
    //qDebug() << "to copy applets: " << toCopyAppletIds;

    QStringList assignedIds;
    QHash<QString, QString> assigned;

    KSharedConfigPtr filePtr = KSharedConfig::openConfig(file);
    KConfigGroup investigate_conts = KConfigGroup(filePtr, "Containments");
    //KConfigGroup copied_c1 = KConfigGroup(&copied_conts, QString::number(containment->id()));

    //! Record the containment and applet ids
    foreach (auto cId, investigate_conts.groupList()) {
        toInvestigateContainmentIds << cId;
        auto appletsEntries = investigate_conts.group(cId).group("Applets");
        toInvestigateAppletIds << appletsEntries.groupList();

        //! investigate for systrays
        foreach (auto appletId, appletsEntries.groupList()) {
            KConfigGroup appletSettings = appletsEntries.group(appletId).group("Configuration");

            int tSysId = appletSettings.readEntry("SystrayContainmentId", -1);

            //! It is a systray !!!
            if (tSysId != -1) {
                QString tSysIdStr = QString::number(tSysId);
                toInvestigateSystrayContIds << tSysIdStr;
                systrayParentContainmentIds[tSysIdStr] = cId;
                systrayAppletIds[tSysIdStr] = appletId;
                qDebug() << "systray was found in the containment...";
            }
        }
    }

    //! Reassign containment and applet ids to unique ones
    foreach (auto contId, toInvestigateContainmentIds) {
        QString newId = availableId(allIds, assignedIds, 12);

        assignedIds << newId;
        assigned[contId] = newId;
    }

    foreach (auto appId, toInvestigateAppletIds) {
        QString newId = availableId(allIds, assignedIds, 40);

        assignedIds << newId;
        assigned[appId] = newId;
    }

    qDebug() << "ALL CORONA IDS ::: " << allIds;
    qDebug() << "FULL ASSIGNMENTS ::: " << assigned;

    foreach (auto cId, toInvestigateContainmentIds) {
        QString value = assigned[cId];

        if (assigned.contains(value)) {
            QString value2 = assigned[value];

            if (cId != assigned[cId] && !value2.isEmpty() && cId == value2) {
                qDebug() << "PROBLEM APPEARED !!!! FOR :::: " << cId << " .. fixed ..";
                assigned[cId] = cId;
                assigned[value] = value;
            }
        }
    }

    foreach (auto aId, toInvestigateAppletIds) {
        QString value = assigned[aId];

        if (assigned.contains(value)) {
            QString value2 = assigned[value];

            if (aId != assigned[aId] && !value2.isEmpty() && aId == value2) {
                qDebug() << "PROBLEM APPEARED !!!! FOR :::: " << aId << " .. fixed ..";
                assigned[aId] = aId;
                assigned[value] = value;
            }
        }
    }

    qDebug() << "FIXED FULL ASSIGNMENTS ::: " << assigned;

    //! update applet ids in their contaiment order and in MultipleLayouts update also the layoutId
    foreach (auto cId, investigate_conts.groupList()) {
        //! Update (appletOrder) and (lockedZoomApplets)
        for (int i = 1; i <= 2; ++i) {
            QString settingStr = (i == 1) ? "appletOrder" : "lockedZoomApplets";
            QString order1 = investigate_conts.group(cId).group("General").readEntry(settingStr, QString());

            if (!order1.isEmpty()) {
                QStringList order1Ids = order1.split(";");
                QStringList fixedOrder1Ids;

                for (int i = 0; i < order1Ids.count(); ++i) {
                    fixedOrder1Ids.append(assigned[order1Ids[i]]);
                }

                QString fixedOrder1 = fixedOrder1Ids.join(";");
                investigate_conts.group(cId).group("General").writeEntry(settingStr, fixedOrder1);
            }
        }

        if (m_corona->layoutManager()->memoryUsage() == Dock::MultipleLayouts) {
            investigate_conts.group(cId).writeEntry("layoutId", m_layoutName);
        }
    }

    //! must update also the systray id in its applet
    foreach (auto systrayId, toInvestigateSystrayContIds) {
        KConfigGroup systrayParentContainment = investigate_conts.group(systrayParentContainmentIds[systrayId]);
        systrayParentContainment.group("Applets").group(systrayAppletIds[systrayId]).group("Configuration").writeEntry("SystrayContainmentId", assigned[systrayId]);
        systrayParentContainment.sync();
    }

    investigate_conts.sync();

    //! Copy To Temp 2 File And Update Correctly The Ids
    KSharedConfigPtr file2Ptr = KSharedConfig::openConfig(tempFile);
    KConfigGroup fixedNewContainmets = KConfigGroup(file2Ptr, "Containments");

    foreach (auto contId, investigate_conts.groupList()) {
        KConfigGroup newContainmentGroup = fixedNewContainmets.group(assigned[contId]);
        investigate_conts.group(contId).copyTo(&newContainmentGroup);

        newContainmentGroup.group("Applets").deleteGroup();

        foreach (auto appId, investigate_conts.group(contId).group("Applets").groupList()) {
            KConfigGroup appletGroup = investigate_conts.group(contId).group("Applets").group(appId);
            KConfigGroup newAppletGroup = fixedNewContainmets.group(assigned[contId]).group("Applets").group(assigned[appId]);
            appletGroup.copyTo(&newAppletGroup);
        }
    }

    fixedNewContainmets.sync();

    return tempFile;
}

QList<Plasma::Containment *> Layout::importLayoutFile(QString file)
{
    KSharedConfigPtr filePtr = KSharedConfig::openConfig(file);
    auto newContainments = m_corona->importLayout(KConfigGroup(filePtr, ""));

    ///Find latte and systray containments
    qDebug() << " imported containments ::: " << newContainments.length();

    QList<Plasma::Containment *> importedDocks;
    //QList<Plasma::Containment *> systrays;

    foreach (auto containment, newContainments) {
        KPluginMetaData meta = containment->kPackage().metadata();

        if (meta.pluginId() == "org.kde.latte.containment") {
            qDebug() << "new latte containment id: " << containment->id();
            importedDocks << containment;
        }
    }

    ///after systrays were found we must update in latte the relevant ids
    /*if (!systrays.isEmpty()) {
        foreach (auto systray, systrays) {
            qDebug() << "systray found with id : " << systray->id();
            Plasma::Applet *parentApplet = qobject_cast<Plasma::Applet *>(systray->parent());

            if (parentApplet) {
                KConfigGroup appletSettings = parentApplet->config().group("Configuration");

                if (appletSettings.hasKey("SystrayContainmentId")) {
                    qDebug() << "!!! updating systray id to : " << systray->id();
                    appletSettings.writeEntry("SystrayContainmentId", systray->id());
                }
            }
        }
    }*/

    return importedDocks;
}

void Layout::recreateDock(Plasma::Containment *containment)
{
    if (!m_corona) {
        return;
    }

    //! give the time to config window to close itself first and then recreate the dock
    //! step:1 remove the dockview
    QTimer::singleShot(350, [this, containment]() {
        auto view = m_dockViews.take(containment);

        if (view) {
            qDebug() << "recreate - step 1: removing dock for containment:" << containment->id();

            //! step:2 add the new dockview
            connect(view, &QObject::destroyed, this, [this, containment]() {
                QTimer::singleShot(250, this, [this, containment]() {
                    if (!m_dockViews.contains(containment)) {
                        qDebug() << "recreate - step 2: adding dock for containment:" << containment->id();
                        addDock(containment);
                    }
                });
            });

            view->deleteLater();

        }
    });
}

//! the central functions that updates loading/unloading dockviews
//! concerning screen changed (for multi-screen setups mainly)
void Layout::syncDockViewsToScreens()
{
    if (!m_corona) {
        return;
    }

    qDebug() << "screen count changed -+-+ " << qGuiApp->screens().size();

    qDebug() << "adding consideration....";
    qDebug() << "dock view running : " << m_dockViews.count();

    foreach (auto scr, qGuiApp->screens()) {
        qDebug() << "Found screen: " << scr->name();

        foreach (auto cont, m_corona->containments()) {
            int id = cont->screen();

            if (id == -1) {
                id = cont->lastScreen();
            }

            bool onPrimary = cont->config().readEntry("onPrimary", true);
            Plasma::Types::Location location = static_cast<Plasma::Types::Location>((int)cont->config().readEntry("location", (int)Plasma::Types::BottomEdge));

            //! two main situations that a dock must be added when it is not already running
            //! 1. when a dock is primary, not running and the edge for which is associated is free
            //! 2. when a dock in explicit, not running and the associated screen currently exists
            //! e.g. the screen has just been added
            if (((onPrimary && m_corona->freeEdges(qGuiApp->primaryScreen()).contains(location)) || (!onPrimary && (m_corona->screenPool()->connector(id) == scr->name())))
                && (!m_dockViews.contains(cont))) {
                qDebug() << "screen Count signal: view must be added... for:" << scr->name();
                addDock(cont);
            }
        }
    }

    qDebug() << "removing consideration & updating screen for always on primary docks....";

    //! this code tries to find a containment that must not be deleted by
    //! automatic algorithm. Currently the containment with the minimum id
    //! containing tasks plasmoid wins
    int preserveContainmentId{ -1};
    bool dockWithTasksWillBeShown{false};

    //! associate correct values for preserveContainmentId and
    //! dockWithTasksWillBeShown
    foreach (auto view, m_dockViews) {
        bool found{false};

        foreach (auto scr, qGuiApp->screens()) {
            if (scr->name() == view->currentScreen()
                || (view->onPrimary() && scr == qGuiApp->primaryScreen())) {
                found = true;
                break;
            }
        }

        //!check if a tasks dock will be shown (try to prevent its deletion)
        if (found && view->tasksPresent()) {
            dockWithTasksWillBeShown = true;
        }

        if (!found && !view->onPrimary() && (m_dockViews.size() > 1) && m_dockViews.contains(view->containment())
            && !(view->tasksPresent() && m_corona->noDocksWithTasks() == 1)) { //do not delete last dock containing tasks
            if (view->tasksPresent()) {
                if (preserveContainmentId == -1)
                    preserveContainmentId = view->containment()->id();
                else if (view->containment()->id() < preserveContainmentId)
                    preserveContainmentId = view->containment()->id();
            }
        }
    }

    //! check which docks must be deleted e.g. when the corresponding
    //! screen does not exist any more.
    //! The code is smart enough in order
    //! to never delete the last tasks dock and also it makes sure that
    //! the last tasks dock which will exist in the end will be the one
    //! with the lowest containment id
    foreach (auto view, m_dockViews) {
        bool found{false};

        foreach (auto scr, qGuiApp->screens()) {
            if (scr->name() == view->currentScreen()
                || (view->onPrimary() && scr == qGuiApp->primaryScreen())) {
                found = true;
                break;
            }
        }


        //! which explicit docks can be deleted
        if (!found && !view->onPrimary() && (m_dockViews.size() > 1) && m_dockViews.contains(view->containment())
            && !(view->tasksPresent() && m_corona->noDocksWithTasks() == 1)) {
            //do not delete last dock containing tasks
            if (dockWithTasksWillBeShown || preserveContainmentId != view->containment()->id()) {
                qDebug() << "screen Count signal: view must be deleted... for:" << view->currentScreen();
                auto viewToDelete = m_dockViews.take(view->containment());
                viewToDelete->deleteLater();
            }

            //!which primary docks can be deleted
        } else if (view->onPrimary() && !found
                   && !m_corona->freeEdges(qGuiApp->primaryScreen()).contains(view->location())) {
            qDebug() << "screen Count signal: primary view must be deleted... for:" << view->currentScreen();
            auto viewToDelete = m_dockViews.take(view->containment());
            viewToDelete->deleteLater();
        } else {
            //! if the dock will not be deleted its a very good point to reconsider
            //! if the screen in which is running is the correct one
            view->reconsiderScreen();
        }
    }

    qDebug() << "end of screens count change....";
}

void Layout::assignToLayout(DockView *dockView, QList<Plasma::Containment *> containments)
{
    if (!m_corona) {
        return;
    }

    if (dockView) {
        m_dockViews[dockView->containment()] = dockView;
        m_containments << containments;

        foreach (auto containment, containments) {
            containment->config().writeEntry("layoutId", name());

            connect(containment, &QObject::destroyed, this, &Layout::containmentDestroyed);
            connect(containment, &Plasma::Applet::destroyedChanged, this, &Layout::destroyedChanged);
            connect(containment, &Plasma::Containment::appletCreated, this, &Layout::appletCreated);
        }

        dockView->setManagedLayout(this);

        emit m_corona->docksCountChanged();
        emit m_corona->availableScreenRectChanged();
        emit m_corona->availableScreenRegionChanged();
    }
}

QList<Plasma::Containment *> Layout::unassignFromLayout(DockView *dockView)
{
    QList<Plasma::Containment *> containments;

    if (!m_corona) {
        return containments;
    }

    containments << dockView->containment();

    foreach (auto containment, m_containments) {
        Plasma::Applet *parentApplet = qobject_cast<Plasma::Applet *>(containment->parent());

        //! add systrays from that dockView
        if (parentApplet && parentApplet->containment() && parentApplet->containment() == dockView->containment()) {
            containments << containment;
            disconnect(containment, &QObject::destroyed, this, &Layout::containmentDestroyed);
            disconnect(containment, &Plasma::Applet::destroyedChanged, this, &Layout::destroyedChanged);
            disconnect(containment, &Plasma::Containment::appletCreated, this, &Layout::appletCreated);
        }
    }

    foreach (auto containment, containments) {
        m_containments.removeAll(containment);
    }

    if (containments.size() > 0) {
        m_dockViews.remove(dockView->containment());
    }

    return containments;
}

}
