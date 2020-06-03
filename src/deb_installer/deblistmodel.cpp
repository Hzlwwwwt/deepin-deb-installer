/*
 * Copyright (C) 2017 ~ 2018 Deepin Technology Co., Ltd.
 *
 * Author:     sbw <sbw@sbw.so>
 *
 * Maintainer: sbw <sbw@sbw.so>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "deblistmodel.h"
#include "packagesmanager.h"
#include "utils.h"
#include <QApplication>
#include <QDebug>
#include <QFuture>
#include <QFutureWatcher>
#include <QSize>
#include <QtConcurrent>
#include <DDialog>
#include <DSysInfo>
#include <QApt/Backend>
#include <QApt/Package>
using namespace QApt;

bool isDpkgRunning()
{
    QProcess proc;
    proc.start("ps", QStringList() << "-e"
               << "-o"
               << "comm");
    proc.waitForFinished();

    const QString output = proc.readAllStandardOutput();
    for (const auto &item : output.split('\n'))
        if (item == "dpkg")
            return true;

    return false;
}

const QString workerErrorString(const int e)
{
    switch (e) {
    case FetchError:
    case DownloadDisallowedError:
        return QApplication::translate("DebListModel", "Installation failed, please check your network connection");
    case NotFoundError:
        return QApplication::translate("DebListModel",
                                       "Installation failed, please check for updates in Control Center");
    case DiskSpaceError:
        return QApplication::translate("DebListModel", "Installation failed, insufficient disk space");
    }
    return QApplication::translate("DebListModel", "Installation Failed");
}

DebListModel::DebListModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_workerStatus(WorkerPrepare)
    , m_packagesManager(new PackagesManager(this))
{

    connect(this, &DebListModel::workerFinished, this, &DebListModel::upWrongStatusRow);
    connect(m_packagesManager, SIGNAL(DependResult(int, int)), this, SLOT(DealDependResult(int, int)));
}

void DebListModel::DealDependResult(int iAuthRes, int iIndex)
{
    switch (iAuthRes) {
    case DebListModel::CancelAuth:
        m_packageOperateStatus[iIndex] = Prepare;
        break;
    case DebListModel::AuthConfirm:
        break;
    case DebListModel::AuthDependsSuccess:
        //m_packageOperateStatus[iIndex] = Success;
        break;
    case DebListModel::AuthDependsErr:
        //m_packageOperateStatus[iIndex] = Failed;
        break;
    default:
        break;
    }
    emit DependResult(iAuthRes);
}

bool DebListModel::isReady() const
{
    return m_packagesManager->isBackendReady();
}

const QList<DebFile *> DebListModel::preparedPackages() const
{
    return m_packagesManager->m_preparedPackages;
}

QModelIndex DebListModel::first() const
{
    return index(0);
}

int DebListModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);

    return m_packagesManager->m_preparedPackages.size();
}

QVariant DebListModel::data(const QModelIndex &index, int role) const
{
    const int r = index.row();

    const DebFile *deb = m_packagesManager->package(r);

    switch (role) {
    case WorkerIsPrepareRole:
        return isWorkerPrepare();
    case ItemIsCurrentRole:
        return m_currentIdx == index;
    case PackageNameRole:
        return deb->packageName();
    case PackagePathRole:
        return deb->filePath();
    case PackageVersionRole:
        return deb->version();
    case PackageVersionStatusRole:
        return m_packagesManager->packageInstallStatus(r);
    case PackageDependsStatusRole:
        return m_packagesManager->packageDependsStatus(r).status;
    case PackageInstalledVersionRole:
        return m_packagesManager->packageInstalledVersion(r);
    case PackageAvailableDependsListRole:
        return m_packagesManager->packageAvailableDepends(r);
    case PackageReverseDependsListRole:
        return m_packagesManager->packageReverseDependsList(deb->packageName(), deb->architecture());
    case PackageDescriptionRole:
        return Utils::fromSpecialEncoding(deb->shortDescription());
    case PackageFailReasonRole:
        return packageFailedReason(r);
    case PackageOperateStatusRole:
        if (m_packageOperateStatus.contains(r))
            return m_packageOperateStatus[r];
        else
            return Prepare;
    case Qt::SizeHintRole:
        return QSize(0, 48);
    default:
        ;
    }

    return QVariant();
}

void DebListModel::installAll()
{

    Q_ASSERT_X(m_workerStatus == WorkerPrepare, Q_FUNC_INFO, "installer status error");
    if (m_workerStatus != WorkerPrepare) return;

    m_workerStatus = WorkerProcessing;
    m_workerStatus_temp = m_workerStatus;
    m_operatingIndex = 0;
    m_operatingStatusIndex = 0;
    m_InitRowStatus = false;
    //    emit workerStarted();
    // start first

    qDebug() << "size:" << m_packagesManager->m_preparedPackages.size();
    initRowStatus();
    installNextDeb();
}

void DebListModel::uninstallPackage(const int idx)
{
    Q_ASSERT(idx == 0);
    Q_ASSERT_X(m_workerStatus == WorkerPrepare, Q_FUNC_INFO, "uninstall status error");

    m_workerStatus = WorkerProcessing;
    m_workerStatus_temp = m_workerStatus;
    m_operatingIndex = idx;

    DebFile *deb = m_packagesManager->package(m_operatingIndex);

    const QStringList rdepends = m_packagesManager->packageReverseDependsList(deb->packageName(), deb->architecture());
    Backend *b = m_packagesManager->m_backendFuture.result();
    for (const auto &r : rdepends) b->markPackageForRemoval(r);
    b->markPackageForRemoval(deb->packageName() + ':' + deb->architecture());

    // uninstall
    qDebug() << Q_FUNC_INFO << "starting to remove package: " << deb->packageName() << rdepends;

    refreshOperatingPackageStatus(Operating);
    Transaction *trans = b->commitChanges();

    connect(trans, &Transaction::progressChanged, this, &DebListModel::transactionProgressChanged);
    connect(trans, &Transaction::statusDetailsChanged, this, &DebListModel::appendOutputInfo);
    connect(trans, &Transaction::statusChanged, this, &DebListModel::onTransactionStatusChanged);
    connect(trans, &Transaction::errorOccurred, this, &DebListModel::onTransactionErrorOccurred);
    connect(trans, &Transaction::finished, this, &DebListModel::uninstallFinished);
    connect(trans, &Transaction::finished, trans, &Transaction::deleteLater);

    m_currentTransaction = trans;

    trans->run();
}

void DebListModel::removePackage(const int idx)
{
    Q_ASSERT_X(m_workerStatus == WorkerPrepare, Q_FUNC_INFO, "installer status error");

    const int packageCount = this->preparedPackages().size();
    QList<int> listdependInstallMark;
    for (int num = 0; num < packageCount; num++) {
        int dependsStatus = this->index(num).data(DebListModel::PackageDependsStatusRole).toInt();
        if (dependsStatus != DependsOk) {
            QString failStr = this->index(num).data(DebListModel::PackageFailReasonRole).toString();
            if (failStr.contains("deepin-wine"))
                listdependInstallMark.append(num);
        }
    }

    m_packagesManager->removePackage(idx, listdependInstallMark);
}

bool DebListModel::getPackageIsNull()
{
    return m_packagesManager->getPackageIsNull();
}

bool DebListModel::appendPackage(DebFile *package, bool isEmpty)
{
    Q_ASSERT_X(m_workerStatus == WorkerPrepare, Q_FUNC_INFO, "installer status error");

    return m_packagesManager->appendPackage(package, isEmpty);
}

void DebListModel::onTransactionErrorOccurred()
{
    Q_ASSERT_X(m_workerStatus == WorkerProcessing, Q_FUNC_INFO, "installer status error");
    Transaction *trans = static_cast<Transaction *>(sender());

    const QApt::ErrorCode e = trans->error();
    Q_ASSERT(e);

    qWarning() << Q_FUNC_INFO << e << workerErrorString(e);
    qWarning() << trans->errorDetails() << trans->errorString();

    if (trans->isCancellable()) trans->cancel();

    if (e == AuthError) {
        trans->deleteLater();
        QTimer::singleShot(100 * 1, this, &DebListModel::checkBoxStatus);
        qDebug() << "reset env to prepare";

        // reset env
        emit AuthCancel();
        emit lockForAuth(false);
        emit EnableReCancelBtn(false);
        m_workerStatus = WorkerPrepare;
        m_workerStatus_temp = m_workerStatus;
        return;
    }

    // DO NOT install next, this action will finished and will be install next automatic.
    trans->setProperty("exitStatus", QApt::ExitFailed);
}

void DebListModel::onTransactionStatusChanged(TransactionStatus stat)
{
    switch (stat) {
    case TransactionStatus::AuthenticationStatus:
        emit lockForAuth(true);
        break;
    case TransactionStatus::WaitingStatus:
        emit lockForAuth(false);
        break;
    default:
        ;
    }

}

int DebListModel::getInstallFileSize()
{
    return m_packagesManager->m_preparedPackages.size();
}

void DebListModel::reset()
{
    //Q_ASSERT_X(m_workerStatus == WorkerFinished, Q_FUNC_INFO, "worker status error");

    m_workerStatus = WorkerPrepare;
    m_workerStatus_temp = m_workerStatus;
    m_operatingIndex = 0;
    m_operatingStatusIndex = 0;

    m_packageOperateStatus.clear();
    m_packageFailReason.clear();
    m_packagesManager->reset();
}

void DebListModel::reset_filestatus()
{
    m_packageOperateStatus.clear();
    m_packageFailReason.clear();
}

void DebListModel::bumpInstallIndex()
{
    Q_ASSERT_X(m_currentTransaction.isNull(), Q_FUNC_INFO, "previous transaction not finished");

    // install finished
    qDebug() << "m_packageFailReason.size:" << m_packageFailReason.size();
    qDebug() << m_packageFailReason;

    qDebug() << "m_packageOperateStatus:" << m_packageOperateStatus;

    if (++m_operatingIndex == m_packagesManager->m_preparedPackages.size()) {
        qDebug() << "congratulations, install finished !!!";
        DebInstallFinishedFlag = 1;
        m_workerStatus = WorkerFinished;
        m_workerStatus_temp = m_workerStatus;
        emit workerFinished();
        emit workerProgressChanged(100);
        emit transactionProgressChanged(100);

        qDebug() << "m_packageDependsStatus,size" << m_packagesManager->m_packageDependsStatus.size();
        for (int i = 0; i < m_packagesManager->m_packageDependsStatus.size(); i++) {
            qDebug() << "m_packageDependsStatus[" << i << "] = " << m_packagesManager->m_packageDependsStatus[i].status;
        }
//        usleep(1000 * 1000);

        return;
    }
    ++ m_operatingStatusIndex;
//    usleep(1000 * 1000);
    qDebug() << "m_packageDependsStatus,size" << m_packagesManager->m_packageDependsStatus.size();
    for (int i = 0; i < m_packagesManager->m_packageDependsStatus.size(); i++) {
        qDebug() << "m_packageDependsStatus[" << i << "] = " << m_packagesManager->m_packageDependsStatus[i].status;
    }

    qDebug() << "m_packagesManager->m_preparedPackages.size()" << m_packagesManager->m_preparedPackages.size();
    qDebug() << "m_operatingIndex" << m_operatingIndex;

    qDebug() << "m_packageFailReason.size:" << m_packageFailReason.size();
    qDebug() << m_packageFailReason;

    emit onChangeOperateIndex(m_operatingIndex);
    // install next

    installNextDeb();
}

void DebListModel::refreshOperatingPackageStatus(const DebListModel::PackageOperationStatus stat)
{
    m_packageOperateStatus[m_operatingStatusIndex] = stat;  //将失败包的索引和状态修改保存,用于更新

    const QModelIndex idx = index(m_operatingStatusIndex);

    emit dataChanged(idx, idx);
}

QString DebListModel::packageFailedReason(const int idx) const
{
    const auto stat = m_packagesManager->packageDependsStatus(idx);
    if (m_packagesManager->isArchError(idx)) return tr("Unmatched package architecture");
    if (stat.isBreak() || stat.isAuthCancel()) {
        if (!stat.package.isEmpty()) {
//            if (bModifyFailedReason) {
//                if (stat.package == "deepin-wine")
//                    return tr("Installing dependencies: %1").arg(stat.package);
//            }
            return tr("Broken dependencies: %1").arg(stat.package);
        }

        const auto conflict = m_packagesManager->packageConflictStat(idx);
        if (!conflict.is_ok()) return tr("Broken dependencies: %1").arg(conflict.unwrap());
        //            return tr("Conflicts: %1").arg(conflict.unwrap());

        Q_UNREACHABLE();
    }
    Q_ASSERT(m_packageOperateStatus.contains(idx));
    Q_ASSERT(m_packageOperateStatus[idx] == Failed);
    if (!m_packageFailReason.contains(idx))
        qDebug() << "ggy" << m_packageFailReason.size() << idx;
    Q_ASSERT(m_packageFailReason.contains(idx));

    return workerErrorString(m_packageFailReason[idx]);
}

void DebListModel::onTransactionFinished()
{
    Q_ASSERT_X(m_workerStatus == WorkerProcessing, Q_FUNC_INFO, "installer status error");
    Transaction *trans = static_cast<Transaction *>(sender());

    // prevent next signal
    disconnect(trans, &Transaction::finished, this, &DebListModel::onTransactionFinished);

    // report new progress
    int progressValue = static_cast<int>(100. * (m_operatingIndex + 1) / m_packagesManager->m_preparedPackages.size());
    emit workerProgressChanged(progressValue);

    DebFile *deb = m_packagesManager->package(m_operatingIndex);
    qDebug() << "install" << deb->packageName() << "finished with exit status:" << trans->exitStatus();
    QString Sourcefilepath = "/var/lib/dpkg/info";
    QString Targetfilepath = "/tmp/.UOS_Installer_build";
    QString filename = deb->packageName();
    filename = filename.toLower();

    int result = Utils::returnfileIsempty(Sourcefilepath, filename);
    if (result) {
        bool transfer_file_result = Utils::File_transfer(Sourcefilepath, Targetfilepath, filename);
        if (transfer_file_result) {
            bool modify_file_result = Utils::Modify_transferfile(Targetfilepath, filename);
            if (modify_file_result) {
                QString shell_Action = Targetfilepath + "/" + filename + ".postinst";
                system(shell_Action.toStdString().c_str());
            }
        }
    }
    qDebug() << "tans.exitStatus()" << trans->exitStatus();
    if (trans->exitStatus()) {
        qWarning() << trans->error() << trans->errorDetails() << trans->errorString();
        m_packageFailReason[m_operatingStatusIndex] = trans->error();
        refreshOperatingPackageStatus(Failed);
        emit appendOutputInfo(trans->errorString());
    } else if (m_packageOperateStatus.contains(m_operatingStatusIndex) &&
               m_packageOperateStatus[m_operatingStatusIndex] != Failed) {
        refreshOperatingPackageStatus(Success);
        if (m_operatingStatusIndex < m_packagesManager->m_preparedPackages.size() - 1) {
            m_packageOperateStatus[m_operatingStatusIndex + 1] = Waiting;
        }
    }
    //    delete trans;
    trans->deleteLater();
    m_currentTransaction = nullptr;
    bumpInstallIndex();
}

void DebListModel::onDependsInstallTransactionFinished()//依赖安装关系满足
{
    Q_ASSERT_X(m_workerStatus == WorkerProcessing, Q_FUNC_INFO, "installer status error");
    Transaction *trans = static_cast<Transaction *>(sender());

    const auto ret = trans->exitStatus();

    DebFile *deb = m_packagesManager->package(m_operatingIndex);
    qDebug() << "install" << deb->packageName() << "dependencies finished with exit status:" << ret;

    if (ret) qWarning() << trans->error() << trans->errorDetails() << trans->errorString();

    if (ret) {
        // record error
        m_packageFailReason[m_operatingStatusIndex] = trans->error();
        refreshOperatingPackageStatus(Failed);
        emit appendOutputInfo(trans->errorString());
    }

    //    delete trans;
    trans->deleteLater();
    m_currentTransaction = nullptr;

    // check current operate exit status to install or install next
    if (ret)
        bumpInstallIndex();
    else
        installNextDeb();
}

void DebListModel::setEndEnable()
{
    emit EnableReCancelBtn(true);
}

void DebListModel::checkBoxStatus()
{
    QTime initTime = QTime::currentTime();
    Transaction *trans = nullptr;
    auto *const backend = m_packagesManager->m_backendFuture.result();
    trans = backend->commitChanges();

    QTime stopTime = QTime::currentTime();
    int elapsed = initTime.msecsTo(stopTime);
    if (elapsed > 20000) {
        QTimer::singleShot(100 * 1, this, &DebListModel::checkBoxStatus);
        return;
    }

    if (trans != nullptr) {

        if (trans->isCancellable()) {
            trans->cancel();
            QTimer::singleShot(100 * 1, this, &DebListModel::setEndEnable);
        } else {
            QTimer::singleShot(100 * 1, this, &DebListModel::checkBoxStatus);
        }
    } else {
        qDebug() << "Transaction is Nullptr";
    }
}

void DebListModel::installDebs()
{
    DebFile *deb = m_packagesManager->package(m_operatingIndex);
    Q_ASSERT_X(m_workerStatus == WorkerProcessing, Q_FUNC_INFO, "installer status error");
    Q_ASSERT_X(m_currentTransaction.isNull(), Q_FUNC_INFO, "previous transaction not finished");

    if (isDpkgRunning()) {
        qDebug() << "dpkg running, waitting...";
        QTimer::singleShot(1000 * 5, this, &DebListModel::installNextDeb);
        return;
    }

    emit onStartInstall();

    // fetch next deb
    auto *const backend = m_packagesManager->m_backendFuture.result();

    Transaction *trans = nullptr;

    // reset package depends status
    m_packagesManager->resetPackageDependsStatus(m_operatingStatusIndex);

    // check available dependencies
    const auto dependsStat = m_packagesManager->packageDependsStatus(m_operatingStatusIndex);
    if (dependsStat.isBreak()) {
        refreshOperatingPackageStatus(Failed);
        m_packageFailReason.insert(m_operatingStatusIndex, -1);
        bumpInstallIndex();
        return;
    } else if (dependsStat.isAvailable()) {
        Q_ASSERT_X(m_packageOperateStatus[m_operatingStatusIndex], Q_FUNC_INFO,
                   "package operate status error when start install availble dependencies");

        const QStringList availableDepends = m_packagesManager->packageAvailableDepends(m_operatingIndex);
        for (auto const &p : availableDepends) backend->markPackageForInstall(p);

        qDebug() << Q_FUNC_INFO << "install" << deb->packageName() << "dependencies: " << availableDepends;

        trans = backend->commitChanges();
        connect(trans, &Transaction::finished, this, &DebListModel::onDependsInstallTransactionFinished);
    } else {
        qDebug() << Q_FUNC_INFO << "starting to install package: " << deb->packageName();

        trans = backend->installFile(*deb);//触发Qapt授权框和安装线程

        connect(trans, &Transaction::progressChanged, this, &DebListModel::transactionProgressChanged);
        connect(trans, &Transaction::finished, this, &DebListModel::onTransactionFinished);
    }

    // NOTE: DO NOT remove this.
    // see: https://bugs.kde.org/show_bug.cgi?id=382272

//    trans->setLocale(".UTF-8");

    trans->setLocale("zh_CN.UTF-8");

    QString uuid = QUuid::createUuid().toString();
    uuid.remove('{').remove('}').remove('-');
    QString pipe = QDir::tempPath() % QLatin1String("/qapt-sock-") % uuid;

    qDebug() << pipe << pipe;

    trans->setDebconfPipe(pipe);

    connect(trans, &Transaction::statusDetailsChanged, this, &DebListModel::appendOutputInfo);
    connect(trans, &Transaction::statusDetailsChanged, this, &DebListModel::onTransactionOutput);
    connect(trans, &Transaction::statusChanged, this, &DebListModel::onTransactionStatusChanged);
    connect(trans, &Transaction::errorOccurred, this, &DebListModel::onTransactionErrorOccurred);

    m_currentTransaction = trans;
    m_currentTransaction->run();
}

void DebListModel::showNoDigitalErrWindow()
{
    DDialog *Ddialog = new DDialog();
    Ddialog->setModal(true);
    Ddialog->setWindowFlag(Qt::WindowStaysOnTopHint);
    Ddialog->setTitle(tr("Unable to install"));
    Ddialog->setMessage(QString(tr("This package does not have a valid digital signature")));
    Ddialog->setIcon(QIcon(Utils::renderSVG(":/images/warning.svg", QSize(32, 32))));
    Ddialog->addButton(QString(tr("OK")), true, DDialog::ButtonNormal);
    Ddialog->show();
    QPushButton *btnOK = qobject_cast<QPushButton *>(Ddialog->getButton(0));
    connect(Ddialog, &DDialog::aboutToClose, this, [ = ] {
        if (preparedPackages().size() > 1)
        {
            refreshOperatingPackageStatus(VerifyFailed);
            bumpInstallIndex();
            return;
        } else if (preparedPackages().size() == 1)
        {
            exit(0);
        }
    });
    connect(btnOK, &DPushButton::clicked, this, [ = ] {
        qDebug() << "result:" << btnOK->isChecked();
        if (preparedPackages().size() > 1)
        {
            refreshOperatingPackageStatus(VerifyFailed);
            bumpInstallIndex();
            return;
        } else if (preparedPackages().size() == 1)
        {
            exit(0);
        }
    });
}

void DebListModel::installNextDeb()
{
    // add for judge OS Version
    // 个人版专业版 非开模式需要验证签名， 服务器版 没有开发者模式，默认不验证签名， 社区版默认开发者模式，不验证签名。
    bool isVerifyDigital = false;
    switch (Dtk::Core::DSysInfo::deepinType()) {
    case Dtk::Core::DSysInfo::DeepinDesktop:
        isVerifyDigital = false;
        break;
    case Dtk::Core::DSysInfo::DeepinPersonal:
    case Dtk::Core::DSysInfo::DeepinProfessional:
        isVerifyDigital = true;
        break;
    case Dtk::Core::DSysInfo::DeepinServer:
        isVerifyDigital = false;
        break;
    default:
        isVerifyDigital = true;
    }
    qDebug() << "DeepinType:" << Dtk::Core::DSysInfo::deepinType();
    qDebug() << "Whether to verify the digital signature：" << isVerifyDigital;

    if (isVerifyDigital) {// 当前系统是个人版或者专业版，非开模式下需要验证签名。
        QDBusInterface Installer("com.deepin.deepinid", "/com/deepin/deepinid", "com.deepin.deepinid");
        bool deviceMode = Installer.property("DeviceUnlocked").toBool();// 判断当前是否处于开发者模式
        qDebug() << "QDBusResult" << deviceMode;
        DebFile *deb = m_packagesManager->package(m_operatingIndex);
        bool digitalSigntual = Utils::Digital_Verify(deb->filePath()); //判断是否有数字签名
        if (!deviceMode && !digitalSigntual) { //非开发者模式且数字签名验证失败
            showNoDigitalErrWindow();
        } else {// 是开发者模式或者有数字签名。
            installDebs();
        }
    } else // 当前系统是服务器版或者社区版， 不需要验证数字签名。
        installDebs();
}

void DebListModel::onTransactionOutput()
{
    Q_ASSERT_X(m_workerStatus == WorkerProcessing, Q_FUNC_INFO, "installer status error");
    Transaction *trans = static_cast<Transaction *>(sender());
    Q_ASSERT(trans == m_currentTransaction.data());
    qDebug() << "local:" << m_currentTransaction->locale();

    refreshOperatingPackageStatus(Operating);

    disconnect(trans, &Transaction::statusDetailsChanged, this, &DebListModel::onTransactionOutput);
}

void DebListModel::uninstallFinished()
{
    Q_ASSERT_X(m_workerStatus == WorkerProcessing, Q_FUNC_INFO, "installer status error");

    qDebug() << Q_FUNC_INFO;

    m_workerStatus = WorkerFinished;
    m_workerStatus_temp = m_workerStatus;
    refreshOperatingPackageStatus(Success);
    m_packageOperateStatus[m_operatingIndex] = Success;

    emit workerFinished();
}

void DebListModel::setCurrentIndex(const QModelIndex &idx)
{
    if (m_currentIdx == idx) return;

    const QModelIndex index = m_currentIdx;
    m_currentIdx = idx;

    emit dataChanged(index, index);
    emit dataChanged(m_currentIdx, m_currentIdx);
}

void DebListModel::initPrepareStatus()
{
    qDebug() << "m_packageOperateStatus" << m_packageOperateStatus;
    for (int i = 0; i < m_packagesManager->m_preparedPackages.size(); i++) {
        m_packageOperateStatus.insert(i, Prepare);
    }
    qDebug() << "after m_packageOperateStatus" << m_packageOperateStatus;
}

void DebListModel::initDependsStatus(int index)
{
    const int packageCount = this->preparedPackages().size();
    if (index >= packageCount)
        return;
    for (int num = index; num < packageCount; num++)
        this->index(num).data(DebListModel::PackageDependsStatusRole);
}

void DebListModel::initRowStatus()
{
    for (int i = 0; i < m_packagesManager->m_preparedPackages.size(); i++) {
        m_operatingStatusIndex = i;
        refreshOperatingPackageStatus(Waiting);
    }
    m_operatingStatusIndex = 0;

    m_InitRowStatus = true;
}

void DebListModel::upWrongStatusRow()
{
    if (m_packagesManager->m_preparedPackages.size() == 1)
        return;

    QList<int> listWrongIndex;
    int iIndex = 0;

    //find wrong index
    QMapIterator<int, int> iteratorpackageOperateStatus(m_packageOperateStatus);
    QList<int> listpackageOperateStatus;
    while (iteratorpackageOperateStatus.hasNext()) {
        iteratorpackageOperateStatus.next();
        if (iteratorpackageOperateStatus.value() == Failed) {
            listWrongIndex.insert(iIndex, iteratorpackageOperateStatus.key());
            listpackageOperateStatus.insert(iIndex++, iteratorpackageOperateStatus.value());
        } else if (iteratorpackageOperateStatus.value() == Success)
            listpackageOperateStatus.append(iteratorpackageOperateStatus.value());
        else
            return;
    }
    if (listWrongIndex.size() == 0)
        return;

    //change  m_preparedPackages, m_packageOperateStatus sort.
    QList<QApt::DebFile *> listTempDebFile;
    QList<QByteArray> listTempPreparedMd5;
    iIndex = 0;
    for (int i = 0; i < m_packagesManager->m_preparedPackages.size(); i++) {
        m_packageOperateStatus[i] = listpackageOperateStatus[i];
        if (listWrongIndex.contains(i)) {
            listTempDebFile.insert(iIndex++, m_packagesManager->m_preparedPackages[i]);
            listTempPreparedMd5.insert(iIndex++, m_packagesManager->m_preparedMd5[i]);
        } else {
            listTempDebFile.append(m_packagesManager->m_preparedPackages[i]);
            listTempPreparedMd5.append(m_packagesManager->m_preparedMd5[i]);
        }
    }
    m_packagesManager->m_preparedPackages = listTempDebFile;
    m_packagesManager->m_preparedMd5 = listTempPreparedMd5;

    //change  m_packageFailReason sort.
    QMap<int, int> mappackageFailReason;
    QMapIterator<int, int> IteratorpackageFailReason(m_packageFailReason);
    while (IteratorpackageFailReason.hasNext()) {
        IteratorpackageFailReason.next();
        int iIndexTemp = listWrongIndex.indexOf(IteratorpackageFailReason.key());
        mappackageFailReason[iIndexTemp] = IteratorpackageFailReason.value();
    }
    m_packageFailReason.clear();
    m_packageFailReason = mappackageFailReason;

    //change  m_packageInstallStatus sort.
    QMapIterator<int, int> MapIteratorpackageInstallStatus(m_packagesManager->m_packageInstallStatus);
    QList<int> listpackageInstallStatus;
    iIndex = 0;
    while (MapIteratorpackageInstallStatus.hasNext()) {
        MapIteratorpackageInstallStatus.next();
        if (listWrongIndex.contains(MapIteratorpackageInstallStatus.key()))
            listpackageInstallStatus.insert(iIndex++, MapIteratorpackageInstallStatus.value());
        else
            listpackageInstallStatus.append(MapIteratorpackageInstallStatus.value());
    }
    for (int i = 0; i < listpackageInstallStatus.size(); i++)
        m_packagesManager->m_packageInstallStatus[i] = listpackageInstallStatus[i];

    //change  m_packageDependsStatus sort.
    QMapIterator<int, PackagesManagerDependsStatus::PackageDependsStatus> MapIteratorpackageDependsStatus(m_packagesManager->m_packageDependsStatus);
    QList<PackagesManagerDependsStatus::PackageDependsStatus> listpackageDependsStatus;
    iIndex = 0;
    while (MapIteratorpackageDependsStatus.hasNext()) {
        MapIteratorpackageDependsStatus.next();
        if (listWrongIndex.contains(MapIteratorpackageDependsStatus.key()))
            listpackageDependsStatus.insert(iIndex++, MapIteratorpackageDependsStatus.value());
        else
            listpackageDependsStatus.append(MapIteratorpackageDependsStatus.value());
    }
    for (int i = 0; i < listpackageDependsStatus.size(); i++)
        m_packagesManager->m_packageDependsStatus[i] = listpackageDependsStatus[i];

    //update view
    const QModelIndex idxStart = index(0);
    const QModelIndex idxEnd = index(m_packageOperateStatus.size() - 1);
    emit dataChanged(idxStart, idxEnd);

    //update scroll
    emit onChangeOperateIndex(-1);
}
