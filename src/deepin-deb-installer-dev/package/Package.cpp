// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Package.h"

#include <QApt/DebFile>

Package::Package():
    m_pSigntureStatus(new PackageSigntureStatus)
{
    m_index             = -1;
    m_valid             = false;
    m_name              = "";
    m_version           = "";
    m_architecture      = "";
    m_md5               = "";
    m_dependsStatus     = DependsUnknown;
    m_signtureStatus    = SigntureUnknown;
    m_installStatus     = InstallStatusUnknown;
}

Package::Package(QString packagePath):
    m_pSigntureStatus(new PackageSigntureStatus)
{
    m_packagePath           = packagePath;
    QApt::DebFile *pDebInfo = new QApt::DebFile(packagePath);
    if (!pDebInfo || !pDebInfo->isValid()) {
        qWarning() << "Package" << "Package" << "获取包文件失败";
        m_valid = false;
        return;
    }

    m_index                 = -1;
    m_valid                 = pDebInfo->isValid();
    m_name                  = pDebInfo->packageName();
    m_version               = pDebInfo->version();
    m_architecture          = pDebInfo->architecture();
    m_md5                   = pDebInfo->md5Sum();
    m_signtureStatus        = m_pSigntureStatus->checkPackageSignture(packagePath);
    m_dependsStatus         = DependsUnknown;
    m_installStatus         = InstallStatusUnknown;

    delete pDebInfo;
}

Package::Package(int index, QString packagePath):
    m_pSigntureStatus(new PackageSigntureStatus)
{
    QApt::DebFile *pDebInfo = new QApt::DebFile(packagePath);

    if (!pDebInfo || !pDebInfo->isValid()) {
        qWarning() << "Package" << "Package" << "获取包文件失败";
        m_valid   = false;
        return;
    }

    m_packagePath           = packagePath;

    m_index                 = index;
    m_valid                 = pDebInfo->isValid();
    m_name                  = pDebInfo->packageName();
    m_version               = pDebInfo->version();
    m_architecture          = pDebInfo->architecture();
    m_md5                   = pDebInfo->md5Sum();

    m_signtureStatus        = m_pSigntureStatus->checkPackageSignture(packagePath);

    m_dependsStatus         = DependsUnknown;
    m_installStatus         = InstallStatusUnknown;
}

void Package::setPackageReverseDependsList(QStringList reverseDepends)
{
    m_packageReverseDepends = reverseDepends;
}

void Package::setPackageInstallStatus(InstallStatus packageInstallStatus)
{
    m_installStatus = packageInstallStatus;
}

void Package::setPackageIndex(int index)
{
    m_index = index;
}

void Package::setPackagePath(QString packagePath)
{
    m_packagePath = packagePath;
}

void Package::setPackageDependStatus(DependsStatus packageDependStatus)
{
    m_dependsStatus = packageDependStatus;
}

void Package::setPackageAvailableDepends(QStringList depends)
{
    m_packageAvailableDependList.clear();
    m_packageAvailableDependList << depends;
}

int Package::getIndex()
{
    return m_index;
}

bool Package::getValid()
{
    return m_valid;
}

QString Package::getName()
{
    return m_name;
}

QString Package::getPath()
{
    return m_packagePath;
}

QString Package::getVersion()
{
    return m_version;
}

QString Package::getArchitecture()
{
    return m_architecture;
}

QByteArray Package::getMd5()
{
    return m_md5;
}

SigntureStatus Package::getSigntureStatus()
{
    return m_signtureStatus;
}

DependsStatus Package::getDependStatus()
{
    return m_dependsStatus;
}

InstallStatus Package::getInstallStatus()
{
    return m_installStatus;
}

QStringList Package::getPackageAvailableDepends()
{
    return m_packageAvailableDependList;
}

QStringList Package::getPackageReverseDependList()
{
    return m_packageReverseDepends;
}
Package::~Package()
{
    delete m_pSigntureStatus;
}
